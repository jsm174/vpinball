// license:GPLv3+

#pragma once

#include "ProposalStore.h"
#include "SketchStore.h"

#include <functional>
#include <vector>

#include "imgui/imgui.h"

namespace VPX::Proposals
{

// What an operation of a proposal looks like on the table, in table units
struct Ghost
{
   enum class Kind { Add, Change, Remove, Marker };
   Kind kind = Kind::Add;
   std::vector<Sketch::Point> outline; // closed polygon (open polyline when closed is false)
   bool closed = true;
   std::vector<std::vector<Sketch::Point>> fills; // convex pieces to fill
   std::vector<Sketch::Point> arrow; // 2 points: where a part goes
   std::string label;
};

// The ghosts of an operation (empty for operations without a place on the playfield, like a script edit). 'table' resolves the parts the operation names.
std::vector<Ghost> GhostsOf(const Op& op, const PinTable* table);

// The locked-for-assistant parts a proposal changes (empty when none)
std::vector<std::string> LockedTargets(const Proposal& proposal, const PinTable* table);

// Colour of a proposal (each pending proposal has its own, so options can be compared side by side)
ImU32 ProposalColor(int proposalId, int alpha = 255);

// Draws the ghosts of the pending, visible proposals. project maps a playfield point to screen pixels (FLT_MAX in x when it can not be shown).
void DrawGhosts(ImDrawList* drawList, const Store& store, const PinTable* table, const std::function<ImVec2(Sketch::Point)>& project, float dpi);

}
