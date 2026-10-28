// license:GPLv3+

#include "core/stdafx.h"
#include "SketchDraw.h"

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace VPX::Sketch
{

ImU32 KindColorU32(MarkKind kind, int alpha)
{
   const uint32_t c = KindColor(kind);
   return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, alpha);
}

namespace
{

bool Valid(const ImVec2& p) { return p.x != FLT_MAX && p.y != FLT_MAX; }

// Screen polyline of a mark, split where a point can not be projected
std::vector<std::vector<ImVec2>> ProjectRuns(const std::vector<Point>& poly, const std::function<ImVec2(Point)>& project)
{
   std::vector<std::vector<ImVec2>> runs;
   std::vector<ImVec2> run;
   for (const Point& p : poly)
   {
      const ImVec2 s = project(p);
      if (Valid(s))
         run.push_back(s);
      else if (!run.empty())
      {
         runs.push_back(std::move(run));
         run.clear();
      }
   }
   if (!run.empty())
      runs.push_back(std::move(run));
   return runs;
}

// Chevrons along a screen polyline showing the direction of travel
void DrawChevrons(ImDrawList* dl, const std::vector<ImVec2>& run, ImU32 col, float dpi)
{
   const float spacing = 70.f * dpi, size = 6.f * dpi;
   float carried = spacing * 0.5f;
   for (size_t i = 0; i + 1 < run.size(); i++)
   {
      const ImVec2 a = run[i], b = run[i + 1];
      const float len = std::hypot(b.x - a.x, b.y - a.y);
      if (len < 1e-3f)
         continue;
      const ImVec2 dir((b.x - a.x) / len, (b.y - a.y) / len), nrm(-dir.y, dir.x);
      float t = spacing - carried;
      for (; t <= len; t += spacing)
      {
         const ImVec2 c(a.x + dir.x * t, a.y + dir.y * t);
         const ImVec2 l(c.x - dir.x * size + nrm.x * size, c.y - dir.y * size + nrm.y * size);
         const ImVec2 r(c.x - dir.x * size - nrm.x * size, c.y - dir.y * size - nrm.y * size);
         dl->AddPolyline(std::array<ImVec2, 3> { l, c, r }.data(), 3, col, ImDrawFlags_None, 2.f * dpi);
      }
      carried = len - (t - spacing);
   }
}

std::string LabelText(const Mark& m)
{
   std::string text = m.id > 0 ? "#" + std::to_string(m.id) : std::string();
   // A request for the assistant is marked with a "?" and names the part it is about
   if (m.request)
      text += text.empty() ? "?" : " ?";
   if (!m.label.empty())
      text += (text.empty() ? std::string() : std::string(" ")) + m.label;
   if (m.request && !m.part.empty())
      text += " [" + m.part + "]";
   if (m.done)
      text += text.empty() ? "[done]" : " [done]";
   return text;
}

// Top left of the label of a mark (FLT_MAX when the mark can not be shown)
ImVec2 LabelAnchor(const Mark& m, const std::function<ImVec2(Point)>& project, float dpi)
{
   const ImVec2 none(FLT_MAX, FLT_MAX);
   if (m.type == MarkType::Text)
   {
      const ImVec2 p = project(m.points[0]);
      return Valid(p) ? ImVec2(p.x + 8.f * dpi, p.y - 8.f * dpi) : none;
   }
   if (m.type == MarkType::Box || m.type == MarkType::Circle)
   {
      Point mn, mx;
      Bounds(m, mn, mx);
      ImVec2 a = m.type == MarkType::Box ? project({ mn.x, mn.y }) : project({ (mn.x + mx.x) * 0.5f, mn.y });
      if (!Valid(a))
         return none;
      a.y -= 22.f * dpi;
      return a;
   }
   // Middle of the longest run
   const auto runs = ProjectRuns(Flatten(m), project);
   const std::vector<ImVec2>* longest = nullptr;
   for (const auto& run : runs)
      if (longest == nullptr || run.size() > longest->size())
         longest = &run;
   if (longest == nullptr || longest->empty())
      return none;
   ImVec2 a = (*longest)[longest->size() / 2];
   a.y -= 22.f * dpi;
   return a;
}

void DrawLabel(ImDrawList* dl, ImVec2 anchor, const std::string& text, ImU32 col, float alpha, float dpi)
{
   const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
   const float pad = 3.f * dpi;
   const ImVec2 p0(anchor.x - pad, anchor.y - pad), p1(anchor.x + ts.x + pad, anchor.y + ts.y + pad);
   dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 24, static_cast<int>(200 * alpha)), 3.f * dpi);
   dl->AddRect(p0, p1, (col & 0x00FFFFFF) | (static_cast<ImU32>(160 * alpha) << 24), 3.f * dpi, 0, 1.f);
   dl->AddText(anchor, col, text.c_str());
}

float DistToSegmentPx(ImVec2 m, ImVec2 a, ImVec2 b)
{
   const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
   const float t = len2 > 0.f ? std::clamp(((m.x - a.x) * dx + (m.y - a.y) * dy) / len2, 0.f, 1.f) : 0.f;
   return std::hypot(m.x - (a.x + t * dx), m.y - (a.y + t * dy));
}

bool InsidePolygon(ImVec2 m, const std::vector<ImVec2>& poly)
{
   bool inside = false;
   for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
      if (((poly[i].y > m.y) != (poly[j].y > m.y)) && (m.x < (poly[j].x - poly[i].x) * (m.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
         inside = !inside;
   return inside;
}

}

void DrawMarks(ImDrawList* dl, const std::vector<Mark>& marks, bool hideDone, const std::function<ImVec2(Point)>& project, float dpi)
{
   for (const Mark& m : marks)
   {
      if (m.done && hideDone)
         continue;
      const float fade = m.done ? 0.35f : 1.f;
      const int a = static_cast<int>(255 * fade);
      const ImU32 col = KindColorU32(m.kind, a);
      const ImU32 outline = IM_COL32(0, 0, 0, static_cast<int>(150 * fade));
      const float w = 2.5f * dpi;

      const bool directional = m.kind == MarkKind::Path || m.kind == MarkKind::Ramp || m.kind == MarkKind::Wireform;

      if (m.type == MarkType::Text)
      {
         const ImVec2 p = project(m.points[0]);
         if (!Valid(p))
            continue;
         dl->AddCircleFilled(p, 5.f * dpi, outline, 16);
         dl->AddCircleFilled(p, 3.5f * dpi, col, 16);
      }
      else if (m.type == MarkType::Box || m.type == MarkType::Circle)
      {
         const auto runs = ProjectRuns(Flatten(m), project);
         for (const auto& run : runs)
            if (run.size() >= 3 && runs.size() == 1)
               dl->AddConvexPolyFilled(run.data(), static_cast<int>(run.size()), (col & 0x00FFFFFF) | (static_cast<ImU32>(45 * fade) << 24));
         for (const auto& run : runs)
         {
            dl->AddPolyline(run.data(), static_cast<int>(run.size()), outline, ImDrawFlags_None, w + 2.f * dpi);
            dl->AddPolyline(run.data(), static_cast<int>(run.size()), col, ImDrawFlags_None, w);
         }
      }
      else
      {
         const auto runs = ProjectRuns(Flatten(m), project);
         for (const auto& run : runs)
         {
            if (run.size() < 2)
               continue;
            dl->AddPolyline(run.data(), static_cast<int>(run.size()), outline, ImDrawFlags_None, w + 2.f * dpi);
            dl->AddPolyline(run.data(), static_cast<int>(run.size()), col, ImDrawFlags_None, w);
            if (directional && m.type != MarkType::Arrow)
               DrawChevrons(dl, run, col, dpi);
         }
         if (m.type == MarkType::Arrow && !runs.empty() && runs.back().size() >= 2)
         {
            const auto& run = runs.back();
            const ImVec2 b = run.back(), pa = run[run.size() - 2];
            const float len = std::hypot(b.x - pa.x, b.y - pa.y);
            if (len > 1e-3f)
            {
               const ImVec2 dir((b.x - pa.x) / len, (b.y - pa.y) / len), nrm(-dir.y, dir.x);
               const float s = 11.f * dpi;
               const ImVec2 l(b.x - dir.x * s + nrm.x * s * 0.5f, b.y - dir.y * s + nrm.y * s * 0.5f);
               const ImVec2 r(b.x - dir.x * s - nrm.x * s * 0.5f, b.y - dir.y * s - nrm.y * s * 0.5f);
               dl->AddTriangleFilled(b, l, r, col);
               dl->AddTriangle(b, l, r, outline, 1.f);
            }
         }
         // Start dot: where the drawing began (the ball's direction for path marks)
         if (!runs.empty() && !runs.front().empty())
         {
            dl->AddCircleFilled(runs.front().front(), 4.f * dpi, outline, 12);
            dl->AddCircleFilled(runs.front().front(), 2.8f * dpi, col, 12);
         }
      }

      const std::string text = LabelText(m);
      if (!text.empty())
      {
         const ImVec2 at = LabelAnchor(m, project, dpi);
         if (Valid(at))
            DrawLabel(dl, at, text, col, fade, dpi);
      }
   }
}

void Draw(ImDrawList* dl, const Store& store, const std::function<ImVec2(Point)>& project, float dpi)
{
   if (store.m_visible)
      DrawMarks(dl, store.Marks(), store.m_hideDone, project, dpi);
}

void DrawHighlight(ImDrawList* dl, const Mark& mark, const std::function<ImVec2(Point)>& project, float dpi, ImU32 color)
{
   for (const auto& run : ProjectRuns(Flatten(mark), project))
   {
      if (run.size() >= 2)
         dl->AddPolyline(run.data(), static_cast<int>(run.size()), color, ImDrawFlags_None, 7.f * dpi);
      else if (run.size() == 1)
         dl->AddCircle(run[0], 9.f * dpi, color, 16, 3.f * dpi);
   }
}

float ScreenDistance(const Mark& m, const std::function<ImVec2(Point)>& project, ImVec2 mouse)
{
   float d = FLT_MAX;
   if (m.type == MarkType::Text)
   {
      const ImVec2 p = project(m.points[0]);
      if (Valid(p))
         d = std::hypot(p.x - mouse.x, p.y - mouse.y);
   }
   else
      for (const auto& run : ProjectRuns(Flatten(m), project))
         for (size_t i = 0; i + 1 < run.size(); i++)
            d = std::min(d, DistToSegmentPx(mouse, run[i], run[i + 1]));
   return d;
}

int HitTest(const Store& store, const std::function<ImVec2(Point)>& project, ImVec2 mouse, float tolerancePx, float dpi)
{
   if (!store.m_visible)
      return 0;
   const auto visible = [&store](const Mark& m) { return !(m.done && store.m_hideDone); };

   // The drawn line (or text pin) first; of overlapping marks the later one (drawn on top) wins
   int best = 0;
   float bestDist = tolerancePx;
   for (const Mark& m : store.Marks())
   {
      if (!visible(m))
         continue;
      const float d = ScreenDistance(m, project, mouse);
      if (d <= bestDist)
      {
         bestDist = d;
         best = m.id;
      }
   }
   if (best != 0)
      return best;

   // Then the label, and the inside of boxes and circles (the smallest one, so an inner shape can be picked)
   float bestArea = FLT_MAX;
   for (const Mark& m : store.Marks())
   {
      if (!visible(m))
         continue;
      const std::string text = LabelText(m);
      if (!text.empty())
      {
         const ImVec2 at = LabelAnchor(m, project, dpi);
         if (Valid(at))
         {
            const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
            const float pad = 4.f * dpi;
            if (mouse.x >= at.x - pad && mouse.x <= at.x + ts.x + pad && mouse.y >= at.y - pad && mouse.y <= at.y + ts.y + pad)
               return m.id;
         }
      }
      if (m.type == MarkType::Box || m.type == MarkType::Circle)
      {
         const auto runs = ProjectRuns(Flatten(m), project);
         if (runs.size() == 1 && runs[0].size() >= 3 && InsidePolygon(mouse, runs[0]))
         {
            Point mn, mx;
            Bounds(m, mn, mx);
            const float area = (mx.x - mn.x) * (mx.y - mn.y);
            if (area <= bestArea)
            {
               bestArea = area;
               best = m.id;
            }
         }
      }
   }
   return best;
}

}
