// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "mcp/McpProposals.h"
#include "ui/live/editor/ProposalStore.h"

namespace VPX::MCP
{

bool IsProposalTool(const std::string& tool)
{
   return tool == "vpx_part" || tool == "vpx_script" || tool == "vpx_material" || tool == "vpx_collection" || tool == "vpx_library";
}

std::string ApplyProposal(Player& p, int proposalId)
{
   using namespace VPX::Proposals;
   Store& store = Store::For(p.m_ptable);
   Proposal* proposal = store.Find(proposalId);
   if (proposal == nullptr)
      return "Unknown proposal";
   if (proposal->status != Status::Pending)
      return "The proposal is not pending";
   if (!IsEditing(p))
      return "Open the editor on the table (not a playtest) to accept a proposal";
   if (IsLiveCopy(p))
      return LiveCopyError();

   EditorUI::EditorUI& editor = p.m_liveUI->m_editorUI;
   const string name = proposal->name;
   const vector<Op> ops = proposal->ops; // the store does not change while the operations run, but they run tool code
   string error;
   // The user accepting a proposal is their decision: it may touch locked parts
   struct LockBypass
   {
      LockBypass() { g_ignoreLocks = true; }
      ~LockBypass() { g_ignoreLocks = false; }
   } lockBypass;
   editor.BeginUndoGroup();
   for (size_t i = 0; i < ops.size() && error.empty(); i++)
   {
      const ToolDef* def = nullptr;
      for (const ToolDef& t : Tools())
         if (ops[i].tool == t.name)
            def = &t;
      if (def == nullptr || !IsProposalTool(ops[i].tool))
      {
         error = std::format("Step {}: the tool '{}' can not be used in a proposal", i + 1, ops[i].tool);
         break;
      }
      try
      {
         const std::optional<ToolOutput> out = def->build(ops[i].args)();
         if (!out)
            error = std::format("Step {}: {} needs several frames to run, which a proposal does not support", i + 1, ops[i].tool);
         else if (out->isError)
            error = std::format("Step {}: {}", i + 1, out->text);
      }
      catch (const std::exception& e)
      {
         error = std::format("Step {} failed: {}", i + 1, e.what());
      }
   }
   if (!error.empty())
   {
      editor.CancelUndoGroup(); // nothing of the proposal stays
      if (Proposal* current = store.Find(proposalId))
         current->lastError = error;
      return error;
   }
   editor.EndUndoGroup(true, "Proposal: " + name);
   store.Decide(proposalId, Status::Accepted, "user");
   // Link the proposal to its entry in the change log, so that undoing the accept puts it back to pending (Store::Sync)
   {
      const auto& log = editor.GetUndoLog().ChangeLog();
      for (auto it = log.rbegin(); it != log.rend(); ++it)
         if (it->label == "Proposal: " + name && it->applied)
         {
            store.SetChangeId(proposalId, it->changeId);
            break;
         }
   }
   return {};
}

void RejectProposal(Player& p, int proposalId, const std::string& reason)
{
   VPX::Proposals::Store::For(p.m_ptable).Decide(proposalId, VPX::Proposals::Status::Rejected, "user", reason);
}

}
