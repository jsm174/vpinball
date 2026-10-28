// license:GPLv3+

#pragma once

#include <string>

class Player;

namespace VPX::MCP
{

// The tools a proposal may call (the ones that edit the table and can be undone)
bool IsProposalTool(const std::string& tool);

// Runs the operations of a pending proposal in order, as one undo step ("Proposal: <name>"), and marks it accepted. If an operation fails everything
// it did is reverted and the proposal stays pending. Returns an error text, empty when it was applied. The editor must be open on the table.
std::string ApplyProposal(Player& player, int proposalId);

// Marks a pending proposal rejected
void RejectProposal(Player& player, int proposalId, const std::string& reason);

}
