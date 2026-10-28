// license:GPLv3+

#pragma once

#include "SketchStore.h"

#include <functional>
#include <vector>

#include "imgui/imgui.h"

namespace VPX::Sketch
{

// Draws the marks of a store on an ImGui draw list. 'project' maps a playfield point to screen pixels (FLT_MAX in x when it can not be shown,
// e.g. behind the camera). Used by both the blueprint and the 3D viewport; the caller sets the clip rect.
void Draw(ImDrawList* drawList, const Store& store, const std::function<ImVec2(Point)>& project, float dpi);
// Same for a list of marks (the tool preview); marks with id 0 get no #id in their label
void DrawMarks(ImDrawList* drawList, const std::vector<Mark>& marks, bool hideDone, const std::function<ImVec2(Point)>& project, float dpi);

// The mark under a screen position: its drawn line or text pin within tolerancePx, else its label or the inside of a box or circle; 0 when none
int HitTest(const Store& store, const std::function<ImVec2(Point)>& project, ImVec2 mouse, float tolerancePx, float dpi);
// Screen distance in pixels from a position to the drawn line (or text pin) of a mark; FLT_MAX when it can not be shown
float ScreenDistance(const Mark& mark, const std::function<ImVec2(Point)>& project, ImVec2 mouse);
// A wide translucent outline over a mark (hover and selection)
void DrawHighlight(ImDrawList* drawList, const Mark& mark, const std::function<ImVec2(Point)>& project, float dpi, ImU32 color);

ImU32 KindColorU32(MarkKind kind, int alpha = 255);

}
