// license:GPLv3+

#include "core/stdafx.h"
#include "ProposalDraw.h"
#include "core/ieditable.h"
#include "LockStore.h"
#include "parts/pintable.h"

#include <cmath>
#include <string>
#include <vector>

namespace VPX::Proposals
{

using json = nlohmann::json;
using Sketch::Point;

namespace
{

constexpr uint32_t PALETTE[] = { 0x2FC4B2, 0xA06BFF, 0xFFB02E, 0x4DA3FF, 0xFF6FA8, 0x8BD43F };

bool Valid(const ImVec2& p) { return p.x != FLT_MAX && p.y != FLT_MAX; }

bool ReadPoints(const json& arr, std::vector<Point>& out)
{
   if (!arr.is_array())
      return false;
   for (const json& e : arr)
   {
      if (e.is_object() && e.contains("x") && e.contains("y") && e["x"].is_number() && e["y"].is_number())
         out.push_back({ e["x"].get<float>(), e["y"].get<float>() });
      else if (e.is_array() && e.size() >= 2 && e[0].is_number() && e[1].is_number())
         out.push_back({ e[0].get<float>(), e[1].get<float>() });
      else
         return false;
   }
   return true;
}

bool IsConvex(const std::vector<Point>& poly)
{
   if (poly.size() < 3)
      return false;
   float sign = 0.f;
   for (size_t i = 0; i < poly.size(); i++)
   {
      const Point &a = poly[i], &b = poly[(i + 1) % poly.size()], &c = poly[(i + 2) % poly.size()];
      const float cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
      if (std::abs(cross) < 1e-4f)
         continue;
      if (sign == 0.f)
         sign = cross;
      else if ((cross > 0.f) != (sign > 0.f))
         return false;
   }
   return true;
}

// The band of the given thickness around a centerline, as one closed outline and as quads to fill
void BuildBand(const std::vector<Point>& c, float thickness, Ghost& g)
{
   if (c.size() < 2)
      return;
   const float half = std::max(thickness, 1.f) * 0.5f;
   std::vector<Point> left, right;
   for (size_t i = 0; i < c.size(); i++)
   {
      // Normal of the point: the average of the normals of the segments around it
      float nx = 0.f, ny = 0.f;
      for (const size_t j : { i == 0 ? size_t(0) : i - 1, i + 1 < c.size() ? i : i - 1 })
      {
         const float dx = c[j + 1].x - c[j].x, dy = c[j + 1].y - c[j].y, len = std::hypot(dx, dy);
         if (len > 1e-6f)
         {
            nx += -dy / len;
            ny += dx / len;
         }
      }
      const float nl = std::hypot(nx, ny);
      if (nl > 1e-6f)
      {
         nx /= nl;
         ny /= nl;
      }
      left.push_back({ c[i].x + nx * half, c[i].y + ny * half });
      right.push_back({ c[i].x - nx * half, c[i].y - ny * half });
   }
   g.outline = left;
   g.outline.insert(g.outline.end(), right.rbegin(), right.rend());
   g.closed = true;
   for (size_t i = 0; i + 1 < c.size(); i++)
      g.fills.push_back({ left[i], left[i + 1], right[i + 1], right[i] });
}

void BuildCircle(Point center, float radius, Ghost& g)
{
   constexpr int N = 28;
   for (int i = 0; i < N; i++)
   {
      const float a = 6.2831853f * static_cast<float>(i) / N;
      g.outline.push_back({ center.x + radius * std::cos(a), center.y + radius * std::sin(a) });
   }
   g.closed = true;
   g.fills.push_back(g.outline);
}

IEditable* FindPart(const PinTable* table, const std::string& name)
{
   if (table == nullptr)
      return nullptr;
   for (IEditable* part : table->GetParts())
      if (StrCompareNoCase(part->GetName(), name))
         return part;
   return nullptr;
}

// Footprint of a part on the playfield
bool PartRect(IEditable* part, Point& mn, Point& mx, Point& center)
{
   if (part == nullptr)
      return false;
   std::vector<Vertex3Ds> bounds;
   part->GetBoundingVertices(bounds, nullptr);
   const Vertex2D c = part->GetCenter();
   center = { c.x, c.y };
   if (bounds.empty())
   {
      mn = { c.x - 15.f, c.y - 15.f };
      mx = { c.x + 15.f, c.y + 15.f };
      return true;
   }
   mn = { FLT_MAX, FLT_MAX };
   mx = { -FLT_MAX, -FLT_MAX };
   for (const Vertex3Ds& v : bounds)
   {
      mn = { std::min(mn.x, v.x), std::min(mn.y, v.y) };
      mx = { std::max(mx.x, v.x), std::max(mx.y, v.y) };
   }
   return true;
}

std::vector<std::string> NamesOf(const json& args)
{
   std::vector<std::string> names;
   if (args.contains("names") && args["names"].is_array())
      for (const json& n : args["names"])
         if (n.is_string())
            names.push_back(n.get<std::string>());
   if (args.contains("name") && args["name"].is_string())
      names.push_back(args["name"].get<std::string>());
   return names;
}

}

std::vector<Ghost> GhostsOf(const Op& op, const PinTable* table)
{
   std::vector<Ghost> ghosts;
   if (op.tool != "vpx_part")
      return ghosts;
   const json& a = op.args;
   const std::string action = a.value("action", std::string());

   if (action == "add")
   {
      const std::string type = a.value("type", std::string()), name = a.value("name", std::string());
      Ghost g;
      g.kind = Ghost::Kind::Add;
      g.label = name.empty() ? type : name + " (" + type + ")";
      std::vector<Point> pts;
      if (a.contains("centerline") && ReadPoints(a["centerline"], pts))
         BuildBand(pts, a.value("thickness", 6.f), g);
      else if (a.contains("points") && ReadPoints(a["points"], pts))
      {
         g.outline = pts;
         g.closed = type != "Ramp";
         if (g.closed && IsConvex(pts))
            g.fills.push_back(pts);
      }
      else if (a.contains("circle") && a["circle"].is_number() && a.contains("x") && a.contains("y"))
         BuildCircle({ a.value("x", 0.f), a.value("y", 0.f) }, a.value("circle", 20.f), g);
      else if (a.contains("x") && a.contains("y") && a["x"].is_number() && a["y"].is_number())
      {
         g.kind = Ghost::Kind::Marker;
         g.outline = { { a.value("x", 0.f), a.value("y", 0.f) } };
      }
      if (!g.outline.empty())
         ghosts.push_back(std::move(g));
   }
   else if (action == "set" || action == "transform" || action == "delete")
   {
      for (const std::string& name : NamesOf(a))
      {
         IEditable* part = FindPart(table, name);
         Point mn, mx, center;
         if (!PartRect(part, mn, mx, center))
            continue;
         Ghost g;
         g.closed = true;
         g.label = name;
         float dx = 0.f, dy = 0.f;
         if (action == "delete")
         {
            g.kind = Ghost::Kind::Remove;
            g.label = "remove " + name;
         }
         else
         {
            g.kind = Ghost::Kind::Change;
            if (action == "transform")
            {
               dx = a.value("dx", 0.f);
               dy = a.value("dy", 0.f);
               if (a.contains("rotate"))
                  g.label += std::format(" rotate {:g}", a.value("rotate", 0.f));
               if (a.contains("scale") || a.contains("scaleX") || a.contains("scaleY"))
                  g.label += " scale";
               if (a.contains("flipX") || a.contains("flipY"))
                  g.label += " flip";
            }
            else if (a.contains("data") && a["data"].is_object())
            {
               const json& data = a["data"];
               if (data.contains("center") && data["center"].is_object() && data["center"].contains("x") && data["center"].contains("y"))
               {
                  dx = data["center"].value("x", center.x) - center.x;
                  dy = data["center"].value("y", center.y) - center.y;
               }
               std::string fields;
               for (auto it = data.begin(); it != data.end() && fields.size() < 40; ++it)
                  fields += (fields.empty() ? "" : ", ") + it.key();
               g.label += ": " + fields;
            }
         }
         g.outline = { { mn.x + dx, mn.y + dy }, { mx.x + dx, mn.y + dy }, { mx.x + dx, mx.y + dy }, { mn.x + dx, mx.y + dy } };
         if (action != "delete" && (std::abs(dx) > 0.5f || std::abs(dy) > 0.5f))
            g.arrow = { center, { center.x + dx, center.y + dy } };
         ghosts.push_back(std::move(g));
      }
   }
   else if (action == "points")
   {
      std::vector<Point> pts;
      if (a.contains("points") && ReadPoints(a["points"], pts) && pts.size() >= 2)
      {
         Ghost g;
         g.kind = Ghost::Kind::Change;
         g.label = a.value("name", std::string()) + " points";
         g.outline = pts;
         g.closed = false;
         ghosts.push_back(std::move(g));
      }
   }
   return ghosts;
}

std::vector<std::string> LockedTargets(const Proposal& proposal, const PinTable* table)
{
   std::vector<std::string> locked;
   Locks::Store& locks = Locks::Store::For(table);
   if (locks.Empty())
      return locked;
   locks.Reconcile(table);
   for (const Op& op : proposal.ops)
   {
      if (op.tool != "vpx_part")
         continue;
      const std::string action = op.args.value("action", std::string());
      if (action != "set" && action != "mesh" && action != "points" && action != "transform" && action != "order" && action != "delete")
         continue;
      for (const std::string& name : NamesOf(op.args))
         if (!locks.Reason(FindPart(table, name), table).empty() && std::ranges::find(locked, name) == locked.end())
            locked.push_back(name);
   }
   return locked;
}

ImU32 ProposalColor(int proposalId, int alpha)
{
   const uint32_t c = PALETTE[static_cast<size_t>(std::max(proposalId - 1, 0)) % std::size(PALETTE)];
   return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, alpha);
}

void DrawGhosts(ImDrawList* dl, const Store& store, const PinTable* table, const std::function<ImVec2(Point)>& project, float dpi)
{
   const auto toScreen = [&project](const std::vector<Point>& pts)
   {
      std::vector<ImVec2> out;
      for (const Point& p : pts)
      {
         const ImVec2 s = project(p);
         if (!Valid(s))
            return std::vector<ImVec2>();
         out.push_back(s);
      }
      return out;
   };
   for (const Proposal& proposal : store.All())
   {
      if (proposal.status != Status::Pending || !proposal.visible)
         continue;
      const ImU32 col = ProposalColor(proposal.id), fill = ProposalColor(proposal.id, 70), soft = ProposalColor(proposal.id, 150);
      const ImU32 dark = IM_COL32(0, 0, 0, 150);
      for (const Op& op : proposal.ops)
         for (const Ghost& g : GhostsOf(op, table))
         {
            ImVec2 labelAt(FLT_MAX, FLT_MAX);
            if (g.kind == Ghost::Kind::Marker)
            {
               const ImVec2 s = project(g.outline[0]);
               if (!Valid(s))
                  continue;
               const float r = 7.f * dpi;
               const ImVec2 d[4] = { { s.x, s.y - r }, { s.x + r, s.y }, { s.x, s.y + r }, { s.x - r, s.y } };
               dl->AddConvexPolyFilled(d, 4, fill);
               dl->AddPolyline(d, 4, col, ImDrawFlags_Closed, 2.f * dpi);
               labelAt = ImVec2(s.x + 10.f * dpi, s.y - 8.f * dpi);
            }
            else
            {
               for (const auto& piece : g.fills)
               {
                  const std::vector<ImVec2> s = toScreen(piece);
                  if (s.size() >= 3)
                     dl->AddConvexPolyFilled(s.data(), static_cast<int>(s.size()), g.kind == Ghost::Kind::Add ? fill : ProposalColor(proposal.id, 35));
               }
               const std::vector<ImVec2> s = toScreen(g.outline);
               if (s.size() < 2)
                  continue;
               const ImDrawFlags flags = g.closed ? ImDrawFlags_Closed : ImDrawFlags_None;
               dl->AddPolyline(s.data(), static_cast<int>(s.size()), dark, flags, 4.5f * dpi);
               dl->AddPolyline(s.data(), static_cast<int>(s.size()), g.kind == Ghost::Kind::Add ? col : soft, flags, 2.5f * dpi);
               if (g.kind == Ghost::Kind::Remove && s.size() == 4)
               {
                  dl->AddLine(s[0], s[2], col, 2.f * dpi);
                  dl->AddLine(s[1], s[3], col, 2.f * dpi);
               }
               labelAt = ImVec2(s[0].x, s[0].y - 22.f * dpi);
               for (const ImVec2& p : s) // the label sits above the top of the ghost
                  if (p.y - 22.f * dpi < labelAt.y)
                     labelAt = ImVec2(p.x, p.y - 22.f * dpi);
            }
            if (g.arrow.size() == 2)
            {
               const ImVec2 a = project(g.arrow[0]), b = project(g.arrow[1]);
               if (Valid(a) && Valid(b))
               {
                  dl->AddLine(a, b, dark, 4.5f * dpi);
                  dl->AddLine(a, b, col, 2.5f * dpi);
                  const float len = std::hypot(b.x - a.x, b.y - a.y);
                  if (len > 1.f)
                  {
                     const ImVec2 dir((b.x - a.x) / len, (b.y - a.y) / len), nrm(-dir.y, dir.x);
                     const float s = 10.f * dpi;
                     dl->AddTriangleFilled(b, ImVec2(b.x - dir.x * s + nrm.x * s * 0.5f, b.y - dir.y * s + nrm.y * s * 0.5f),
                        ImVec2(b.x - dir.x * s - nrm.x * s * 0.5f, b.y - dir.y * s - nrm.y * s * 0.5f), col);
                  }
               }
            }
            if (Valid(labelAt))
            {
               std::string text = proposal.name;
               if (text.size() > 22)
                  text = text.substr(0, 20) + "...";
               text += ": " + g.label;
               const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
               const float pad = 3.f * dpi;
               dl->AddRectFilled(ImVec2(labelAt.x - pad, labelAt.y - pad), ImVec2(labelAt.x + ts.x + pad, labelAt.y + ts.y + pad), IM_COL32(20, 20, 24, 210), 3.f * dpi);
               dl->AddRect(ImVec2(labelAt.x - pad, labelAt.y - pad), ImVec2(labelAt.x + ts.x + pad, labelAt.y + ts.y + pad), soft, 3.f * dpi, 0, 1.f);
               dl->AddText(labelAt, col, text.c_str());
            }
         }
   }
}

}
