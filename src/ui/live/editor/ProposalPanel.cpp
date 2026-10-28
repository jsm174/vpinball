// license:GPLv3+

#include "core/stdafx.h"
#include "ProposalPanel.h"
#include "ProposalDraw.h"
#include "ProposalStore.h"
#include "core/player.h"
#include "mcp/McpProposals.h"
#include "parts/pintable.h"
#include "ui/live/LiveUI.h"

namespace VPX::Proposals
{

void RenderPanel(float dpi)
{
   if (g_pplayer == nullptr || g_pplayer->m_ptable == nullptr)
      return;
   Store& store = Store::For(g_pplayer->m_ptable);
   if (g_pplayer->m_liveUI != nullptr)
      store.Sync(g_pplayer->m_liveUI->m_editorUI.GetUndoLog());
   std::vector<int> pending;
   for (const Proposal& p : store.All())
      if (p.status == Status::Pending)
         pending.push_back(p.id);
   if (pending.empty())
      return;

   const ImGuiIO& io = ImGui::GetIO();
   ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 400.f * dpi, 80.f * dpi), ImGuiCond_FirstUseEver);
   ImGui::SetNextWindowBgAlpha(0.95f);
   constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
   const std::string title = std::format("Assistant proposals ({})###AssistantProposals", pending.size());
   if (ImGui::Begin(title.c_str(), nullptr, flags))
   {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360.f * dpi);
      bool changed = false;
      for (const int id : pending)
      {
         Proposal* proposal = store.Find(id);
         if (proposal == nullptr)
            continue;
         ImGui::PushID(id);
         const ImU32 col = ProposalColor(id);
         const ImVec4 colV = ImGui::ColorConvertU32ToFloat4(col);
         ImGui::Checkbox("##show", &proposal->visible);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show this proposal's ghost on the table");
         ImGui::SameLine();
         ImGui::TextColored(colV, "%s", proposal->name.c_str());
         if (!proposal->group.empty())
         {
            ImGui::SameLine();
            ImGui::TextDisabled("(option of %s)", proposal->group.c_str());
         }
         if (!proposal->description.empty())
            ImGui::TextWrapped("%s", proposal->description.c_str());
         if (const auto lockedParts = LockedTargets(*proposal, g_pplayer->m_ptable); !lockedParts.empty())
         {
            std::string names;
            for (const std::string& n : lockedParts)
               names += (names.empty() ? "" : ", ") + n;
            ImGui::TextColored(ImVec4(1.f, 0.69f, 0.18f, 1.f), "Changes locked parts: %s (accepting overrides the lock)", names.c_str());
         }
         if (proposal->wasUndone)
            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.3f, 1.f), "Undone in the editor: accept to apply it again");
         ImGui::TextDisabled("%d step%s", static_cast<int>(proposal->ops.size()), proposal->ops.size() == 1 ? "" : "s");
         if (ImGui::IsItemHovered())
         {
            ImGui::BeginTooltip();
            for (size_t i = 0; i < proposal->ops.size(); i++)
            {
               const Op& op = proposal->ops[i];
               std::string line = std::format("{}. {}", i + 1, op.note.empty() ? op.tool + " " + op.args.value("action", std::string()) : op.note);
               ImGui::TextUnformatted(line.c_str());
            }
            ImGui::EndTooltip();
         }
         if (!proposal->lastError.empty())
            ImGui::TextColored(ImVec4(1.f, 0.45f, 0.4f, 1.f), "Could not apply: %s", proposal->lastError.c_str());
         if (ImGui::Button("Accept"))
         {
            const std::string error = VPX::MCP::ApplyProposal(*g_pplayer, id);
            (void)error; // kept in the proposal (lastError) and shown above
            changed = true;
         }
         if (ImGui::IsItemHovered())
         {
            if (proposal->group.empty())
               ImGui::SetTooltip("Apply all the steps as one undo step");
            else
               ImGui::SetTooltip("Apply all the steps as one undo step, and reject the other options of '%s'", proposal->group.c_str());
         }
         ImGui::SameLine();
         if (ImGui::Button("Reject"))
         {
            VPX::MCP::RejectProposal(*g_pplayer, id, "Rejected in the editor");
            changed = true;
         }
         ImGui::Separator();
         ImGui::PopID();
         if (changed)
            break; // the list changed
      }
      ImGui::PopTextWrapPos();
   }
   ImGui::End();
}

}
