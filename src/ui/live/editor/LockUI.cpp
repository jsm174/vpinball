// license:GPLv3+

#include "core/stdafx.h"
#include "LockUI.h"
#include "LockStore.h"
#include "core/ieditable.h"
#include "parts/Collection.h"
#include "parts/PartGroup.h"
#include "parts/pintable.h"

#include <cmath>

namespace VPX::Locks
{

void RenderLockMenu(IEditable* part, PinTable* table)
{
   if (part == nullptr || table == nullptr || part->GetName().empty())
      return;
   Store& store = Store::For(table);
   store.Reconcile(table);
   const std::string reason = store.Reason(part, table);
   if (!ImGui::BeginMenu(reason.empty() ? "Lock For Assistant" : "Lock For Assistant (locked)"))
      return;
   const auto toggle = [&](const std::string& label, bool locked, const std::function<void(bool)>& apply, const char* tip)
   {
      if (ImGui::MenuItem(label.c_str(), nullptr, locked))
         apply(!locked);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("%s", tip);
   };
   toggle("Part '" + part->GetName() + "'", store.IsPartNameLocked(part->GetName()), [&store, part](bool lock) { store.SetPart(part->GetName(), lock); },
      "The assistant can not change this part through the MCP server while it is locked");
   for (const PartGroup* g = part->GetPartGroup(); g != nullptr; g = g->GetPartGroup())
   {
      const std::string name = g->GetName();
      toggle("Part group '" + name + "'", store.IsPartNameLocked(name), [&store, name](bool lock) { store.SetPart(name, lock); }, "Locks every part of the group");
   }
   for (CComObject<Collection>* c : table->GetCollections())
      if (std::ranges::find(c->GetParts(), part) != c->GetParts().end())
      {
         const std::string name = c->m_name;
         toggle("Collection '" + name + "'", store.IsCollectionNameLocked(name), [&store, name](bool lock) { store.SetCollection(name, lock); }, "Locks every member of the collection");
      }
   ImGui::EndMenu();
}

void DrawBadges(ImDrawList* dl, const PinTable* table, const std::function<ImVec2(Sketch::Point)>& project, float dpi)
{
   if (table == nullptr)
      return;
   Store& store = Store::For(table);
   if (store.Empty())
      return;
   store.Reconcile(table);
   for (IEditable* part : table->GetParts())
   {
      if (part->GetItemType() == eItemPartGroup || part->GetName().empty())
         continue;
      const std::string reason = store.Reason(part, table);
      if (reason.empty())
         continue;
      const Vertex2D c = part->GetCenter();
      const ImVec2 s = project({ c.x, c.y });
      if (s.x == FLT_MAX)
         continue;
      const bool direct = reason.rfind("the part itself", 0) == 0;
      const ImU32 col = IM_COL32(255, 176, 46, 255), dark = IM_COL32(20, 20, 24, 255);
      if (!direct)
      {
         // Locked through its group or collection: a small dot
         dl->AddCircleFilled(s, 3.5f * dpi, dark, 10);
         dl->AddCircleFilled(s, 2.2f * dpi, col, 10);
         continue;
      }
      // Padlock: a body with a shackle above it
      const float w = 7.f * dpi, h = 6.f * dpi;
      const ImVec2 body0(s.x - w, s.y - h * 0.2f), body1(s.x + w, s.y + h * 1.5f);
      const float r = w * 0.65f;
      const ImVec2 sc(s.x, body0.y);
      std::vector<ImVec2> arc;
      for (int i = 0; i <= 10; i++)
      {
         const float a = 3.14159265f + 3.14159265f * static_cast<float>(i) / 10.f;
         arc.push_back(ImVec2(sc.x + r * std::cos(a), sc.y + r * std::sin(a)));
      }
      dl->AddPolyline(arc.data(), static_cast<int>(arc.size()), dark, ImDrawFlags_None, 4.f * dpi);
      dl->AddPolyline(arc.data(), static_cast<int>(arc.size()), col, ImDrawFlags_None, 2.f * dpi);
      dl->AddRectFilled(ImVec2(body0.x - 1.5f * dpi, body0.y - 1.5f * dpi), ImVec2(body1.x + 1.5f * dpi, body1.y + 1.5f * dpi), dark, 2.f * dpi);
      dl->AddRectFilled(body0, body1, col, 1.5f * dpi);
      dl->AddCircleFilled(ImVec2(s.x, (body0.y + body1.y) * 0.5f), 1.4f * dpi, dark, 8);
   }
}

}
