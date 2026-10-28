// license:GPLv3+

#include "core/stdafx.h"
#include "SketchTool.h"
#include "SketchDraw.h"
#include "LockUI.h"
#include "core/player.h"
#include "core/ieditable.h"
#include "parts/pintable.h"

#include <cmath>
#include <cstdio>

namespace VPX::Sketch
{

namespace
{

float Dist(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }
float DistPx(ImVec2 a, ImVec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Ramer-Douglas-Peucker: keeps the shape of a freehand stroke with few points
void Simplify(const std::vector<Point>& pts, size_t first, size_t last, float tol, std::vector<bool>& keep)
{
   if (last <= first + 1)
      return;
   const Point a = pts[first], b = pts[last];
   const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
   float maxDist = 0.f;
   size_t index = first;
   for (size_t i = first + 1; i < last; i++)
   {
      const float t = len2 > 0.f ? std::clamp(((pts[i].x - a.x) * dx + (pts[i].y - a.y) * dy) / len2, 0.f, 1.f) : 0.f;
      const float d = Dist(pts[i], Point { a.x + t * dx, a.y + t * dy });
      if (d > maxDist)
      {
         maxDist = d;
         index = i;
      }
   }
   if (maxDist > tol)
   {
      keep[index] = true;
      Simplify(pts, first, index, tol, keep);
      Simplify(pts, index, last, tol, keep);
   }
}

// Control points of a mark for the Edit tool. Circle: 0 center (moves it), 1 rim (radius). Box: 0..3 corners, 4 center (moves it). Others: one per stored point
std::vector<Point> HandlePositions(const Mark& m)
{
   const auto& p = m.points;
   if (m.type == MarkType::Box && p.size() == 2)
      return { p[0], { p[1].x, p[0].y }, p[1], { p[0].x, p[1].y }, { (p[0].x + p[1].x) * 0.5f, (p[0].y + p[1].y) * 0.5f } };
   return p;
}

void ApplyHandle(Mark& m, const Mark& orig, int h, Point to)
{
   m.points = orig.points;
   auto& p = m.points;
   if (m.type == MarkType::Circle && p.size() == 2)
   {
      if (h == 0)
      {
         const float dx = to.x - orig.points[0].x, dy = to.y - orig.points[0].y;
         p[0] = to;
         p[1] = { orig.points[1].x + dx, orig.points[1].y + dy };
      }
      else
         p[1] = to;
      return;
   }
   if (m.type == MarkType::Box && p.size() == 2)
   {
      switch (h)
      {
      case 0: p[0] = to; break;
      case 1: p[1].x = to.x; p[0].y = to.y; break;
      case 2: p[1] = to; break;
      case 3: p[0].x = to.x; p[1].y = to.y; break;
      default:
      {
         const float dx = to.x - (orig.points[0].x + orig.points[1].x) * 0.5f, dy = to.y - (orig.points[0].y + orig.points[1].y) * 0.5f;
         p[0] = { orig.points[0].x + dx, orig.points[0].y + dy };
         p[1] = { orig.points[1].x + dx, orig.points[1].y + dy };
         break;
      }
      }
      return;
   }
   if (h >= 0 && h < static_cast<int>(p.size()))
      p[h] = to;
}

constexpr const char* TOOL_NAMES[] = { "Pen", "Arrow", "Arc", "Circle", "Box", "Text", "Erase", "Edit" };
constexpr const char* TOOL_HINTS[] = {
   "Drag to draw a freehand line.",
   "Drag from the tail to the head.",
   "Drag start to end, move the mouse to bend the curve, click to set it. Shift+drag continues the last arc smoothly.",
   "Drag from the center to the rim.",
   "Drag between two opposite corners.",
   "Click to place a note, type it, Enter to confirm.",
   "Click a mark (its line, label or inside) to delete it.",
   "Click a mark to select it: drag its control points to reshape it, drag the mark to move it, double-click or Enter to edit its label, 1-8 to change its kind, Delete to remove it." };

}

bool IsBackdropPart(IEditable* part, const PinTable* table)
{
   if (part == nullptr || table == nullptr)
      return true;
   vector<Vertex3Ds> bounds;
   part->GetBoundingVertices(bounds, nullptr);
   if (bounds.empty())
      return false;
   float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
   for (const Vertex3Ds& v : bounds)
   {
      minX = std::min(minX, v.x);
      maxX = std::max(maxX, v.x);
      minY = std::min(minY, v.y);
      maxY = std::max(maxY, v.y);
   }
   return (maxX - minX) > (table->m_right - table->m_left) * 0.8f && (maxY - minY) > (table->m_bottom - table->m_top) * 0.8f;
}

Tool& Tool::Get()
{
   static Tool tool;
   return tool;
}

void Tool::SetActive(bool active)
{
   if (!active)
   {
      Cancel();
      m_textPending = false;
      m_selected = 0;
      m_hoverId = 0;
   }
   m_active = active;
}

void Tool::Cancel()
{
   // An edit drag in progress goes back to how the mark was
   if (m_drag != Drag::None && m_dragMoved && m_env.store != nullptr)
      m_env.store->Replace(m_dragOrig);
   m_drag = Drag::None;
   m_dragMoved = false;
   m_phase = Phase::Idle;
   m_points.clear();
   m_owner = nullptr;
   m_tangent = false;
}

void Tool::Undoable(const std::string& label, std::vector<Mark> before)
{
   if (!m_env.recordUndo || m_env.store == nullptr)
      return;
   Store* s = m_env.store;
   const auto restore = [s](std::vector<Mark> marks) { return [s, marks]() { s->Restore(marks); }; };
   m_env.recordUndo(restore(std::move(before)), [s, restore]() { return restore(s->Snapshot()); }, label);
}

void Tool::CommitMark(MarkType type, std::vector<Point> points, const std::string& label, bool request, const std::string& part, const nlohmann::json& view)
{
   Mark mark;
   mark.type = type;
   mark.kind = request ? MarkKind::Note : m_kind;
   mark.points = std::move(points);
   mark.label = label;
   mark.author = "user";
   mark.request = request;
   mark.part = part;
   mark.view = view;
   std::vector<Mark> before = m_env.store->Snapshot();
   const int id = m_env.store->Add(std::move(mark));
   Undoable("Sketch add", std::move(before));
   m_chainId = type == MarkType::Arc ? id : 0;
}

void Tool::CommitArc(Point start, Point mid, Point end, bool extend)
{
   const Mark* chain = extend ? m_env.store->Find(m_chainId) : nullptr;
   if (chain == nullptr)
   {
      CommitMark(MarkType::Arc, { start, mid, end });
      return;
   }
   Mark mark = *chain;
   mark.points.push_back(mid);
   mark.points.push_back(end);
   std::vector<Mark> before = m_env.store->Snapshot();
   m_env.store->Replace(mark);
   Undoable("Sketch extend arc", std::move(before));
}

void Tool::Input(const Env& env)
{
   if (!m_active || env.store == nullptr)
      return;
   const ImGuiIO& io = ImGui::GetIO();
   m_snapEnabledPref = env.snapEnabled;
   m_toggleSnap = env.toggleSnap;
   // The point under the mouse, snapped for the shape tools (not the freehand pen, the eraser, or moving a whole mark)
   const Point raw = env.unproject(env.mouse);
   const bool snapping = m_tool != ToolType::Pen && m_tool != ToolType::Eraser && !(m_tool == ToolType::Edit && m_drag == Drag::None && m_phase == Phase::Idle)
      && m_drag != Drag::Move;
   const Point p = snapping ? Snap(env, raw, m_drag != Drag::None ? m_dragOrig.id : 0) : raw;

   // What a click would pick, highlighted by DrawPreview
   if (m_phase == Phase::Idle && (m_tool == ToolType::Eraser || m_tool == ToolType::Edit))
   {
      m_hoverId = env.canStart && !m_textPending ? HitTest(*env.store, env.project, env.mouse, 10.f * env.dpi, env.dpi) : 0;
      m_hoverFrame = ImGui::GetFrameCount();
   }

   // Right click on a mark (any tool): selects it (control points appear) and arms its menu, opened on release
   if (m_phase == Phase::Idle && env.canStart && !m_textPending && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
   {
      const int id = HitTest(*env.store, env.project, env.mouse, 10.f * env.dpi, env.dpi);
      if (id != 0)
      {
         m_selected = id;
         m_ctxId = id;
         m_ctxArmed = true;
         m_ctxPress = env.mouse;
         m_ctxEnv = env;
         m_env = env;
         m_rightFrame = ImGui::GetFrameCount();
      }
   }

   if (m_phase == Phase::Idle)
   {
      if (!env.canStart || m_textPending || io.KeyAlt || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
         return;
      m_env = env;
      m_owner = env.owner;
      env.store->m_visible = true; // Drawing or editing while the marks are hidden ("Show marks" off) would give nothing to see
      // A mark selected with the right click can be reshaped and moved from any drawing tool: its control points and its line
      // take the click before the tool does, a click elsewhere deselects it
      if (m_tool != ToolType::Edit && m_tool != ToolType::Eraser && m_selected != 0)
      {
         if (TryHandleDrag(env))
            return;
         if (const Mark* sel = env.store->Find(m_selected); sel != nullptr && ScreenDistance(*sel, env.project, env.mouse) <= 8.f * env.dpi)
         {
            BeginDrag(Drag::Move, *sel, env);
            return;
         }
         m_selected = 0;
      }
      switch (m_tool)
      {
      case ToolType::Eraser:
      {
         const int id = HitTest(*env.store, env.project, env.mouse, 10.f * env.dpi, env.dpi);
         if (id != 0)
         {
            std::vector<Mark> before = env.store->Snapshot();
            env.store->Remove(id);
            Undoable("Sketch erase", std::move(before));
            m_hoverId = 0;
         }
         m_owner = nullptr;
         return;
      }
      case ToolType::Edit: InputEdit(env, p); return;
      case ToolType::Text:
         m_textPending = true;
         m_labelEditId = 0;
         m_textRequest = false;
         m_textFocus = true;
         m_textPoint = p;
         m_textScreen = env.mouse;
         m_textBuf[0] = 0;
         m_textEnv = env;
         m_owner = nullptr;
         return;
      case ToolType::Pen: m_points = { p }; break;
      case ToolType::Arc:
      {
         m_tangent = false;
         if (io.KeyShift && m_chainId != 0)
            if (const Mark* chain = env.store->Find(m_chainId); chain != nullptr && EndTangent(*chain, m_tangentStart, m_tangentDir))
               m_tangent = true;
         m_points = { m_tangent ? m_tangentStart : p, p };
         break;
      }
      default: m_points = { p, p }; break;
      }
      m_phase = Phase::Dragging;
      m_lastScreen = env.mouse;
      return;
   }

   if (env.owner != m_owner)
      return;
   m_env = env; // fresh projection, the camera may have moved

   if (m_tool == ToolType::Edit || m_drag != Drag::None)
   {
      InputEdit(env, p);
      return;
   }

   if (m_phase == Phase::Dragging)
   {
      if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
      {
         if (m_tool == ToolType::Pen)
         {
            if (DistPx(env.mouse, m_lastScreen) > 4.f * env.dpi)
            {
               m_points.push_back(p);
               m_lastScreen = env.mouse;
            }
         }
         else
            m_points.back() = p;
         return;
      }
      // Released
      const float lenPx = m_tool == ToolType::Pen ? 10.f * env.dpi : DistPx(env.project(m_points.front()), env.project(m_points.back()));
      if (lenPx < 6.f * env.dpi || m_points.size() < 2)
      {
         Cancel();
         return;
      }
      switch (m_tool)
      {
      case ToolType::Pen:
      {
         const float pxToUnits = Dist(env.unproject(env.mouse), env.unproject(ImVec2(env.mouse.x + 1.f, env.mouse.y)));
         std::vector<bool> keep(m_points.size(), false);
         keep.front() = keep.back() = true;
         Simplify(m_points, 0, m_points.size() - 1, 2.f * pxToUnits * env.dpi, keep);
         std::vector<Point> out;
         for (size_t i = 0; i < m_points.size(); i++)
            if (keep[i])
               out.push_back(m_points[i]);
         if (out.size() >= 2)
            CommitMark(MarkType::Stroke, std::move(out));
         Cancel();
         break;
      }
      case ToolType::Arrow:
         CommitMark(MarkType::Arrow, m_points);
         Cancel();
         break;
      case ToolType::Circle:
         CommitMark(MarkType::Circle, m_points);
         Cancel();
         break;
      case ToolType::Box:
         CommitMark(MarkType::Box, m_points);
         Cancel();
         break;
      case ToolType::Arc:
         if (m_tangent)
         {
            Point mid;
            TangentArcMid(m_points[0], m_tangentDir, m_points[1], mid);
            CommitArc(m_points[0], mid, m_points[1], true);
            Cancel();
         }
         else
         {
            m_mid = { (m_points[0].x + m_points[1].x) * 0.5f, (m_points[0].y + m_points[1].y) * 0.5f };
            m_phase = Phase::Bend;
         }
         break;
      default: Cancel(); break;
      }
      return;
   }

   // Bend: the arc goes through the mouse; a click sets it
   m_mid = p;
   if (env.canStart && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
   {
      if (ComputeArc(m_points[0], m_mid, m_points[1]).valid)
      {
         CommitArc(m_points[0], m_mid, m_points[1], false);
         Cancel();
      }
   }
}

void Tool::StartLabelEdit(const Env& env, int id, bool request)
{
   const Mark* mark = env.store->Find(id);
   if (mark == nullptr)
      return;
   m_textPending = true;
   m_textFocus = true;
   m_labelEditId = id;
   m_textRequest = request;
   m_textPart = request ? (mark->part.empty() ? "#" + std::to_string(id) : mark->part) : std::string();
   std::snprintf(m_textBuf, sizeof(m_textBuf), "%s", mark->label.c_str());
   m_textScreen = env.mouse;
   m_textEnv = env;
}

Point Tool::Snap(const Env& env, Point raw, int ignoreMarkId)
{
   if (!env.snapActive || env.store == nullptr)
      return raw;
   Point out = raw;
   bool hit = false;
   // The points of the other marks first: shapes join up
   if (env.store->m_visible)
   {
      float best = 14.f * env.dpi;
      for (const Mark& m : env.store->Marks())
      {
         if (m.id == ignoreMarkId || (m.done && env.store->m_hideDone))
            continue;
         for (const Point& q : HandlePositions(m))
         {
            const ImVec2 s = env.project(q);
            if (s.x == FLT_MAX)
               continue;
            const float d = DistPx(s, env.mouse);
            if (d < best)
            {
               best = d;
               out = q;
               hit = true;
            }
         }
      }
   }
   if (!hit && env.snapToParts)
   {
      Point q = raw;
      if (env.snapToParts(q))
      {
         out = q;
         hit = true;
      }
   }
   if (hit)
   {
      m_snapPoint = out;
      m_snapFrame = ImGui::GetFrameCount();
   }
   return out;
}

void Tool::BeginDrag(Drag drag, const Mark& mark, const Env& env)
{
   m_drag = drag;
   m_dragOrig = mark;
   m_dragStart = env.unproject(env.mouse);
   m_dragBefore = env.store->Snapshot();
   m_dragMoved = false;
   m_phase = Phase::Dragging;
   m_lastScreen = env.mouse;
}

bool Tool::TryHandleDrag(const Env& env)
{
   const Mark* sel = m_selected != 0 ? env.store->Find(m_selected) : nullptr;
   if (sel == nullptr)
      return false;
   const std::vector<Point> handles = HandlePositions(*sel);
   int best = -1;
   float bestDist = 9.f * env.dpi;
   for (size_t i = 0; i < handles.size(); i++)
   {
      const ImVec2 s = env.project(handles[i]);
      if (s.x == FLT_MAX)
         continue;
      const float d = DistPx(s, env.mouse);
      if (d <= bestDist)
      {
         best = static_cast<int>(i);
         bestDist = d;
      }
   }
   if (best < 0)
      return false;
   m_dragHandle = best;
   BeginDrag(Drag::Handle, *sel, env);
   return true;
}

// Edit tool: select a mark, drag its control points to reshape it, drag it to move it, double-click to edit its label.
// Also finishes a handle or move drag started from another tool.
void Tool::InputEdit(const Env& env, Point p)
{
   Store& store = *env.store;
   if (m_phase == Phase::Idle)
   {
      // A click: m_env and m_owner are set
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      {
         const int id = HitTest(store, env.project, env.mouse, 10.f * env.dpi, env.dpi);
         if (id != 0)
         {
            m_selected = id;
            StartLabelEdit(env, id);
         }
         m_owner = nullptr;
         return;
      }
      if (TryHandleDrag(env))
         return;
      const int id = HitTest(store, env.project, env.mouse, 10.f * env.dpi, env.dpi);
      if (const Mark* mark = id != 0 ? store.Find(id) : nullptr; mark != nullptr)
      {
         m_selected = id;
         m_drag = Drag::Move;
         m_dragOrig = *mark;
         m_dragStart = p;
         m_dragBefore = store.Snapshot();
         m_dragMoved = false;
         m_phase = Phase::Dragging;
         m_lastScreen = env.mouse;
         return;
      }
      m_selected = 0;
      m_owner = nullptr;
      return;
   }

   // Dragging
   if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
   {
      if (!m_dragMoved && DistPx(env.mouse, m_lastScreen) > 3.f * env.dpi)
         m_dragMoved = true;
      if (m_dragMoved)
      {
         Mark mark = m_dragOrig;
         if (m_drag == Drag::Handle)
            ApplyHandle(mark, m_dragOrig, m_dragHandle, p);
         else
         {
            const float dx = p.x - m_dragStart.x, dy = p.y - m_dragStart.y;
            for (Point& q : mark.points)
               q = { q.x + dx, q.y + dy };
         }
         store.Replace(mark, false);
      }
      return;
   }
   // Released: one undo step for the whole drag
   const bool moved = m_dragMoved;
   m_drag = Drag::None;
   m_dragMoved = false;
   if (moved)
   {
      if (const Mark* current = store.Find(m_dragOrig.id); current != nullptr)
      {
         const Mark mark = *current;
         store.Replace(mark, true);
      }
      Undoable("Sketch edit", std::move(m_dragBefore));
   }
   Cancel();
}

void Tool::DrawPreview(ImDrawList* drawList, const std::function<ImVec2(Point)>& project, float dpi, const void* owner) const
{
   if (!m_active)
      return;
   // Where the point snapped to
   if (ImGui::GetFrameCount() - m_snapFrame <= 1)
   {
      const ImVec2 s = project(m_snapPoint);
      if (s.x != FLT_MAX)
      {
         const float r = 8.f * dpi;
         drawList->AddCircle(s, r, IM_COL32(20, 20, 24, 255), 20, 4.f * dpi);
         drawList->AddCircle(s, r, IM_COL32(255, 200, 40, 255), 20, 2.f * dpi);
         drawList->AddLine(ImVec2(s.x - r * 0.5f, s.y), ImVec2(s.x + r * 0.5f, s.y), IM_COL32(255, 200, 40, 255), 1.5f * dpi);
         drawList->AddLine(ImVec2(s.x, s.y - r * 0.5f), ImVec2(s.x, s.y + r * 0.5f), IM_COL32(255, 200, 40, 255), 1.5f * dpi);
      }
   }
   // Hover (what a click picks) and selection with its control points, in every pane
   if (m_store != nullptr)
   {
      if ((m_tool == ToolType::Eraser || m_tool == ToolType::Edit) && m_hoverId != 0 && ImGui::GetFrameCount() - m_hoverFrame <= 1 && m_hoverId != m_selected)
         if (const Mark* mark = m_store->Find(m_hoverId))
            DrawHighlight(drawList, *mark, project, dpi, m_tool == ToolType::Eraser ? IM_COL32(255, 60, 60, 120) : IM_COL32(255, 255, 255, 60));
      if (m_selected != 0)
         if (const Mark* mark = m_store->Find(m_selected))
         {
            DrawHighlight(drawList, *mark, project, dpi, IM_COL32(255, 255, 255, 90));
            const std::vector<Point> handles = HandlePositions(*mark);
            for (size_t i = 0; i < handles.size(); i++)
            {
               const ImVec2 s = project(handles[i]);
               if (s.x == FLT_MAX)
                  continue;
               const float r = 5.f * dpi;
               // Arc middle points (odd indices) are round: they bend the curve; the others are square
               const bool round = mark->type == MarkType::Arc && (i % 2) == 1;
               const bool dragged = m_drag == Drag::Handle && m_dragHandle == static_cast<int>(i);
               const ImU32 fill = dragged ? IM_COL32(255, 210, 60, 255) : IM_COL32(255, 255, 255, 255);
               if (round)
               {
                  drawList->AddCircleFilled(s, r + 1.f * dpi, fill, 16);
                  drawList->AddCircle(s, r + 1.f * dpi, IM_COL32(20, 20, 24, 255), 16, 1.5f * dpi);
               }
               else
               {
                  drawList->AddRectFilled(ImVec2(s.x - r, s.y - r), ImVec2(s.x + r, s.y + r), fill);
                  drawList->AddRect(ImVec2(s.x - r, s.y - r), ImVec2(s.x + r, s.y + r), IM_COL32(20, 20, 24, 255), 0.f, 0, 1.5f * dpi);
               }
            }
         }
   }
   if (m_phase == Phase::Idle || owner != m_owner || m_points.size() < 2 || m_tool == ToolType::Edit)
      return;
   Mark pm;
   pm.id = 0;
   pm.kind = m_kind;
   pm.points = m_points;
   switch (m_tool)
   {
   case ToolType::Pen: pm.type = MarkType::Stroke; break;
   case ToolType::Arrow: pm.type = MarkType::Arrow; break;
   case ToolType::Box: pm.type = MarkType::Box; break;
   case ToolType::Circle:
      pm.type = MarkType::Circle;
      pm.label = std::format("r {:.0f}", Dist(m_points[0], m_points[1]));
      break;
   case ToolType::Arc:
   {
      pm.type = MarkType::Stroke;
      Point mid;
      bool arc = false;
      if (m_tangent)
      {
         TangentArcMid(m_points[0], m_tangentDir, m_points[1], mid);
         arc = true;
      }
      else if (m_phase == Phase::Bend)
      {
         mid = m_mid;
         arc = true;
      }
      if (arc)
      {
         const ArcInfo info = ComputeArc(m_points[0], mid, m_points[1]);
         if (info.valid)
         {
            pm.type = MarkType::Arc;
            pm.points = { m_points[0], mid, m_points[1] };
            pm.label = std::format("r {:.0f}", info.radius);
         }
      }
      break;
   }
   default: return;
   }
   DrawMarks(drawList, { pm }, false, project, dpi);
}

void Tool::NotePane(const void* owner, ImVec2 topLeft, bool hovered, Store* store)
{
   if (hovered || m_barOwner == nullptr)
   {
      m_barOwner = owner;
      m_barPos = topLeft;
   }
   if (store != nullptr)
      m_store = store;
}

void Tool::RenderContextMenu(float dpi)
{
   if (m_ctxOpenPending)
   {
      ImGui::OpenPopup("##SketchMarkMenu");
      m_ctxOpenPending = false;
   }
   if (!ImGui::BeginPopup("##SketchMarkMenu"))
      return;
   Store* store = m_ctxEnv.store;
   const Mark* found = store != nullptr ? store->Find(m_ctxId) : nullptr;
   if (found == nullptr)
   {
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
      return;
   }
   const Mark mark = *found; // the store changes under the menu items
   ImGui::TextDisabled("#%d %s", mark.id, mark.label.c_str());
   ImGui::Separator();
   if (ImGui::MenuItem("Edit label..."))
   {
      Env env = m_ctxEnv;
      env.mouse = m_ctxPos;
      StartLabelEdit(env, mark.id);
   }
   if (ImGui::BeginMenu("Kind"))
   {
      for (int i = 0; i < KIND_COUNT; i++)
      {
         const MarkKind kind = static_cast<MarkKind>(i);
         const uint32_t c = KindColor(kind);
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f, 1.f));
         const bool clicked = ImGui::MenuItem(KindName(kind), nullptr, mark.kind == kind);
         ImGui::PopStyleColor();
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", KindDescription(kind));
         if (clicked && mark.kind != kind)
         {
            m_env = m_ctxEnv;
            Mark changed = mark;
            changed.kind = kind;
            std::vector<Mark> before = store->Snapshot();
            store->Replace(changed);
            Undoable("Sketch kind", std::move(before));
         }
      }
      ImGui::EndMenu();
   }
   // Ask Assistant: type what you want, the mark then becomes a request (its text is the label)
   if (ImGui::MenuItem(mark.request ? "Edit request..." : "Ask Assistant about this..."))
   {
      Env env = m_ctxEnv;
      env.mouse = m_ctxPos;
      StartLabelEdit(env, mark.id, true);
   }
   if (mark.request && ImGui::MenuItem("Not a request"))
   {
      m_env = m_ctxEnv;
      Mark changed = mark;
      changed.request = false;
      std::vector<Mark> before = store->Snapshot();
      store->Replace(changed);
      Undoable("Sketch unask", std::move(before));
   }
   if (ImGui::MenuItem(mark.done ? "Reopen" : "Mark done"))
   {
      m_env = m_ctxEnv;
      Mark changed = mark;
      changed.done = !mark.done;
      std::vector<Mark> before = store->Snapshot();
      store->Replace(changed);
      Undoable(changed.done ? "Sketch done" : "Sketch reopen", std::move(before));
   }
   ImGui::Separator();
   if (ImGui::MenuItem("Delete"))
   {
      m_env = m_ctxEnv;
      std::vector<Mark> before = store->Snapshot();
      store->Remove(mark.id);
      Undoable("Sketch delete", std::move(before));
      if (m_selected == mark.id)
         m_selected = 0;
   }
   (void)dpi;
   ImGui::EndPopup();
}

void Tool::RenderBar(float dpi)
{
   if (!m_active)
   {
      RenderAskUI(dpi); // Ask Assistant works without the sketch tool
      return;
   }
   const ImGuiIO& io = ImGui::GetIO();

   // The selected mark (Edit tool) may be gone (deleted, cleared, undone)
   const Mark* selected = (m_selected != 0 && m_store != nullptr) ? m_store->Find(m_selected) : nullptr;
   if (selected == nullptr)
      m_selected = 0;

   // Keys: 1-8 pick the kind (and change the kind of the selected mark), Delete / Enter act on the selected mark,
   // Esc cancels the gesture, the text, the selection, or leaves the tool
   if (!io.WantTextInput && !m_textPending && !io.KeyCtrl && !io.KeyAlt)
   {
      for (int i = 0; i < KIND_COUNT; i++)
         if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false))
         {
            m_kind = static_cast<MarkKind>(i);
            // In the Edit tool the number keys also recolour the selection (elsewhere the right click menu has the kinds)
            if (m_tool == ToolType::Edit && selected != nullptr && m_phase == Phase::Idle && selected->kind != m_kind && m_env.store != nullptr)
            {
               Mark mark = *selected;
               mark.kind = m_kind;
               std::vector<Mark> before = m_env.store->Snapshot();
               m_env.store->Replace(mark);
               Undoable("Sketch kind", std::move(before));
            }
         }
      if (selected != nullptr && m_phase == Phase::Idle && m_env.store != nullptr)
      {
         if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
         {
            std::vector<Mark> before = m_env.store->Snapshot();
            m_env.store->Remove(m_selected);
            Undoable("Sketch delete", std::move(before));
            m_selected = 0;
         }
         else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
         {
            Env env = m_env;
            env.mouse = ImGui::GetMousePos();
            StartLabelEdit(env, m_selected);
         }
      }
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
   {
      if (m_textPending)
      {
         m_textPending = false;
         m_labelEditId = 0;
         m_textRequest = false;
      }
      else if (m_phase != Phase::Idle)
         Cancel();
      else if (m_selected != 0)
         m_selected = 0;
      else
      {
         SetActive(false);
         return;
      }
   }

   // Right click menu of a mark: opens on the release, when the mouse did not drag (a drag orbits the 3D view)
   if (m_ctxArmed && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
   {
      m_ctxArmed = false;
      const ImVec2 mouse = ImGui::GetMousePos();
      if (DistPx(mouse, m_ctxPress) < 6.f * dpi)
      {
         m_ctxOpenPending = true;
         m_ctxPos = mouse;
      }
   }
   if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
      m_ctxArmed = false;

   ImGui::SetNextWindowPos(ImVec2(m_barPos.x + 8.f * dpi, m_barPos.y + 8.f * dpi), ImGuiCond_Always);
   ImGui::SetNextWindowBgAlpha(0.92f);
   constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
      | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
   if (ImGui::Begin("##SketchBar", nullptr, flags))
   {
      ImGui::TextUnformatted("Sketch");
      for (int i = 0; i < TOOL_COUNT; i++)
      {
         ImGui::SameLine();
         const bool current = static_cast<int>(m_tool) == i;
         if (current)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
         if (ImGui::Button(TOOL_NAMES[i]))
         {
            if (m_tool != static_cast<ToolType>(i))
            {
               Cancel();
               m_selected = 0;
            }
            m_tool = static_cast<ToolType>(i);
         }
         if (current)
            ImGui::PopStyleColor();
      }
      ImGui::SameLine();
      if (ImGui::Button("X"))
         SetActive(false);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Close the sketch tool [Esc]");

      // Kinds: the colour is the meaning
      for (int i = 0; i < KIND_COUNT; i++)
      {
         if (i > 0)
            ImGui::SameLine();
         const uint32_t c = KindColor(static_cast<MarkKind>(i));
         const ImVec4 col(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f, 1.f);
         ImGui::PushID(i);
         if (ImGui::ColorButton("##kind", col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoPicker, ImVec2(22.f * dpi, 22.f * dpi)))
            m_kind = static_cast<MarkKind>(i);
         const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
         if (static_cast<int>(m_kind) == i)
            ImGui::GetWindowDrawList()->AddRect(ImVec2(mn.x - 2.f, mn.y - 2.f), ImVec2(mx.x + 2.f, mx.y + 2.f), IM_COL32(255, 255, 255, 255), 2.f, 0, 2.f);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d: %s", i + 1, KindDescription(static_cast<MarkKind>(i)));
         ImGui::PopID();
      }
      ImGui::SameLine();
      ImGui::TextUnformatted(KindName(m_kind));

      if (m_store != nullptr)
      {
         ImGui::Checkbox("Show marks", &m_store->m_visible);
         ImGui::SameLine();
         ImGui::Checkbox("Hide done", &m_store->m_hideDone);
      }
      if (m_toggleSnap)
      {
         ImGui::SameLine();
         bool snapOn = m_snapEnabledPref;
         if (ImGui::Checkbox("Snap", &snapOn))
            m_toggleSnap();
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The editor snapping (Shift+Tab, Ctrl inverts it while drawing): to the grid or to the part points, see Snap Options in the Edit menu. Marks also snap to the points of other marks.");
      }
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.f * dpi);
      ImGui::TextWrapped("%s", TOOL_HINTS[static_cast<int>(m_tool)]);
      ImGui::TextWrapped("Right-click a mark (any tool): drag its control points to reshape it, drag its line to move it, menu for label, kind, done, delete.");
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
   }
   ImGui::End();

   RenderContextMenu(dpi);
   RenderAskUI(dpi);
}

void Tool::BeginAsk(const Env& env, Point where, const std::string& part, const nlohmann::json& view, ImVec2 screenPos)
{
   if (env.store == nullptr)
      return;
   m_textPending = true;
   m_textFocus = true;
   m_labelEditId = 0;
   m_textRequest = true;
   m_textPart = part;
   m_textView = view;
   m_textPoint = where;
   m_textScreen = screenPos;
   m_textBuf[0] = 0;
   m_textEnv = env;
}

void Tool::OpenAskMenu(const Env& env, Point where, const std::string& part, const nlohmann::json& view, ImVec2 screenPos)
{
   if (env.store == nullptr)
      return;
   m_askMenuPending = true;
   m_askEnv = env;
   m_askPoint = where;
   m_askPart = part;
   m_askView = view;
   m_askScreen = screenPos;
}

// The Ask Assistant menu and the text popup. Called every frame, whether the sketch tool is active or not
void Tool::RenderAskUI(float dpi)
{
   if (m_askMenuPending)
   {
      ImGui::OpenPopup("##SketchAsk");
      m_askMenuPending = false;
   }
   if (ImGui::BeginPopup("##SketchAsk"))
   {
      const std::string label = m_askPart.empty() ? "Ask Assistant about this spot..." : "Ask Assistant about " + m_askPart + "...";
      if (ImGui::MenuItem(label.c_str()))
         BeginAsk(m_askEnv, m_askPoint, m_askPart, m_askView, m_askScreen);
      // Lock the part for the assistant (the MCP server then refuses to change it)
      if (!m_askPart.empty() && g_pplayer != nullptr)
      {
         IEditable* part = nullptr;
         for (IEditable* candidate : g_pplayer->m_ptable->GetParts())
            if (StrCompareNoCase(candidate->GetName(), m_askPart))
            {
               part = candidate;
               break;
            }
         if (part != nullptr)
         {
            ImGui::Separator();
            Locks::RenderLockMenu(part, g_pplayer->m_ptable);
         }
      }
      ImGui::EndPopup();
   }

   // Esc cancels the popup (while the tool is active its own Esc handling already did)
   if (m_textPending && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
   {
      m_textPending = false;
      m_labelEditId = 0;
      m_textRequest = false;
   }

   if (m_textPending)
   {
      ImGui::SetNextWindowPos(ImVec2(m_textScreen.x + 10.f * dpi, m_textScreen.y + 10.f * dpi), ImGuiCond_Always);
      constexpr ImGuiWindowFlags textFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
      if (ImGui::Begin("##SketchText", nullptr, textFlags))
      {
         if (m_textRequest)
            ImGui::TextDisabled("%s", m_textPart.empty() ? "Ask Assistant about this spot" : ("Ask Assistant about " + m_textPart).c_str());
         if (m_textFocus)
         {
            ImGui::SetKeyboardFocusHere();
            m_textFocus = false;
         }
         ImGui::SetNextItemWidth(240.f * dpi);
         const char* hint = m_labelEditId != 0 ? "Label (empty clears it), Enter to confirm, Esc to cancel"
            : m_textRequest                    ? "What do you want, Enter to send, Esc to cancel"
                                               : "Note, Enter to place, Esc to cancel";
         if (ImGui::InputTextWithHint("##text", hint, m_textBuf, sizeof(m_textBuf), ImGuiInputTextFlags_EnterReturnsTrue))
         {
            m_env = m_textEnv;
            if (m_labelEditId != 0)
            {
               if (const Mark* current = m_env.store->Find(m_labelEditId); current != nullptr)
               {
                  Mark mark = *current;
                  mark.label = m_textBuf;
                  if (m_textRequest)
                     mark.request = true; // Ask Assistant on an existing mark
                  if (mark.label != current->label || mark.request != current->request)
                  {
                     std::vector<Mark> before = m_env.store->Snapshot();
                     m_env.store->Replace(mark);
                     Undoable(m_textRequest ? "Sketch ask" : "Sketch label", std::move(before));
                  }
               }
            }
            else if (m_textBuf[0] != 0)
               CommitMark(MarkType::Text, { m_textPoint }, m_textBuf, m_textRequest, m_textPart, m_textView);
            m_textPending = false;
            m_labelEditId = 0;
            m_textRequest = false;
         }
      }
      ImGui::End();
   }
}

}
