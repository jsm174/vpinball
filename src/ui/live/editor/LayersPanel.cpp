// license:GPLv3+

#include "core/stdafx.h"
#include "LayersPanel.h"
#include "HeatStore.h"
#include "core/player.h"
#include "parts/pintable.h"
#include "mcp/McpBookmarks.h"

namespace VPX::Layers
{

void RenderPanel(float dpi)
{
   if (g_pplayer == nullptr || g_pplayer->m_ptable == nullptr)
      return;
   PinTable* table = g_pplayer->m_ptable;
   Heat::Store& heat = Heat::Store::For(table);
   MCP::Bookmarks::Store& bookmarks = MCP::Bookmarks::Store::For(table);

   if (heat.Empty() && bookmarks.All().empty())
      return;

   ImGui::SetNextWindowPos(ImVec2(20.f * dpi, 120.f * dpi), ImGuiCond_FirstUseEver);
   ImGui::SetNextWindowBgAlpha(0.95f);
   constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
   if (ImGui::Begin("Layers###Layers", nullptr, flags))
   {
      if (!heat.Empty() && ImGui::CollapsingHeader("Ball heatmap", ImGuiTreeNodeFlags_DefaultOpen))
      {
         ImGui::Checkbox("Show##heat", &heat.m_visible);
         int layer = heat.m_layer == Heat::Layer::Time ? 0 : heat.m_layer == Heat::Layer::Impacts ? 1 : 2;
         if (ImGui::RadioButton("Time spent", &layer, 0))
            heat.m_layer = Heat::Layer::Time;
         ImGui::SameLine();
         if (ImGui::RadioButton("Hits", &layer, 1))
            heat.m_layer = Heat::Layer::Impacts;
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Where the ball lost speed hitting something (sum of the speed lost per cell)");
         ImGui::SameLine();
         if (ImGui::RadioButton("Resting", &layer, 2))
            heat.m_layer = Heat::Layer::Resting;
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Where the ball sat almost still (waiting in the plunger lane, stuck, cradled): kept out of 'Time spent'");
         heat.m_layer = layer == 0 ? Heat::Layer::Time : layer == 1 ? Heat::Layer::Impacts : Heat::Layer::Resting;
         ImGui::TextDisabled("%.1f s moving, %.1f s resting (simulator runs)", heat.Total(Heat::Layer::Time) / 1000.f, heat.Total(Heat::Layer::Resting) / 1000.f);
         if (ImGui::Button("Clear heatmap"))
            heat.Clear();
      }
      if (!bookmarks.All().empty() && ImGui::CollapsingHeader("Play bookmarks (F8)", ImGuiTreeNodeFlags_DefaultOpen))
      {
         // The moments the user marked as wrong while playing: show the path of the balls over the table, or delete them
         int removeId = 0;
         for (const MCP::Bookmarks::Bookmark& listed : bookmarks.All())
         {
            MCP::Bookmarks::Bookmark* b = bookmarks.Find(listed.id);
            if (b == nullptr)
               continue;
            ImGui::PushID(b->id);
            ImGui::Checkbox("##showbm", &b->visible);
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("Show the path of the balls over the table");
            ImGui::SameLine();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(MCP::Bookmarks::BookmarkColor(b->id)), "#%d", b->id);
            ImGui::SameLine();
            const float seconds = b->frames.empty() ? 0.f : static_cast<float>(b->frames.back().t - b->frames.front().t) / 1000.f;
            ImGui::TextDisabled("%.1f s%s%s", seconds, b->note.empty() ? "" : ": ", b->note.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete"))
               removeId = b->id;
            ImGui::PopID();
         }
         if (removeId != 0)
            bookmarks.Remove(removeId);
         if (!bookmarks.All().empty() && ImGui::Button("Clear bookmarks"))
            bookmarks.Clear();
         ImGui::TextDisabled("Press F8 while playing to keep the last 6 seconds");
      }
   }
   ImGui::End();
}

}
