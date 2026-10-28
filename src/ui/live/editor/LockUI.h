// license:GPLv3+

#pragma once

#include "SketchStore.h"

#include <functional>

#include "imgui/imgui.h"

class PinTable;
class IEditable;

namespace VPX::Locks
{

// The "Lock For Assistant" submenu of a part: lock or unlock the part itself, the part groups it is in and the collections it belongs to. Call inside a menu or popup.
void RenderLockMenu(IEditable* part, PinTable* table);

// A padlock on every part locked for the assistant (a small dot on the ones locked through their group or collection). project maps a playfield point to screen pixels.
void DrawBadges(ImDrawList* drawList, const PinTable* table, const std::function<ImVec2(Sketch::Point)>& project, float dpi);

}
