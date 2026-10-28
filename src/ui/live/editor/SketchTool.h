// license:GPLv3+

#pragma once

#include "SketchStore.h"

#include <functional>
#include <string>
#include <vector>

#include "imgui/imgui.h"

namespace VPX::Sketch
{

enum class ToolType { Pen, Arrow, Arc, Circle, Box, Text, Eraser, Edit };

constexpr int TOOL_COUNT = 8;

// What a pane gives the tool each frame: how to go between screen pixels and playfield points in that pane
struct Env
{
   const void* owner = nullptr; // the pane (a gesture started in one pane is finished in it)
   Store* store = nullptr;
   ImVec2 mouse;
   std::function<Point(ImVec2)> unproject;
   std::function<ImVec2(Point)> project;
   float dpi = 1.f;
   bool canStart = false; // the pane is hovered and nothing else (gizmo, camera orbit) owns the click
   // The editor snapping (Shift+Tab, Ctrl inverts it): snapActive when it applies right now, snapToParts snaps a point to the grid or to the part points
   bool snapActive = false;
   bool snapEnabled = false;
   std::function<bool(Point&)> snapToParts;
   std::function<void()> toggleSnap;
   // Forwards to the editor's undo history (must not capture a pane: it is used after the pane may be gone)
   std::function<void(std::function<void()> undo, std::function<std::function<void()>()> snapshotForRedo, const std::string& label)> recordUndo;
};

// True for a part that covers most of the table (the playfield mesh): it is under every point, so Ask Assistant treats a click on it as a click on the spot
bool IsBackdropPart(IEditable* part, const PinTable* table);

// The sketch tool of the live editor: one state shared by the blueprint and the 3D viewports (a drawing is made in one pane at a time).
class Tool
{
public:
   static Tool& Get();

   bool Active() const { return m_active; }
   void SetActive(bool active);

   // Called by a pane every frame while the tool is active: starts, updates and ends gestures
   void Input(const Env& env);
   // Hover / selection highlights and handles (every pane), and the rubber band of the gesture in progress (the pane that owns it)
   void DrawPreview(ImDrawList* drawList, const std::function<ImVec2(Point)>& project, float dpi, const void* owner) const;
   // A pane tells where it is (its top left corner) and whether the mouse is over it: the toolbar sits on the hovered pane
   void NotePane(const void* owner, ImVec2 topLeft, bool hovered, Store* store);
   // Toolbar, text popup and keyboard shortcuts: once per frame, after the panes
   void RenderBar(float dpi);

   // Ask Assistant: a text pin flagged as a request, with the part it is about and the view. BeginAsk opens the text popup at once (screenPos),
   // OpenAskMenu first shows a one item menu there. Both work while the sketch tool is not active.
   void BeginAsk(const Env& env, Point where, const std::string& part, const nlohmann::json& view, ImVec2 screenPos);
   void OpenAskMenu(const Env& env, Point where, const std::string& part, const nlohmann::json& view, ImVec2 screenPos);

   // True in the frame a right click went to a mark (selecting it and arming its menu): the pane must not use that click for its own context menu
   bool ConsumedRightPress() const { return m_active && m_rightFrame == ImGui::GetFrameCount(); }

   bool InGesture() const { return m_phase != Phase::Idle; }
   bool OwnedBy(const void* owner) const { return m_phase != Phase::Idle && m_owner == owner; }

private:
   enum class Phase { Idle, Dragging, Bend };
   enum class Drag { None, Handle, Move };

   void Cancel();
   void CommitMark(MarkType type, std::vector<Point> points, const std::string& label = {}, bool request = false, const std::string& part = {}, const nlohmann::json& view = {});
   void CommitArc(Point start, Point mid, Point end, bool extend);
   void Undoable(const std::string& label, std::vector<Mark> before);
   void StartLabelEdit(const Env& env, int id, bool request = false);
   void InputEdit(const Env& env, Point p);
   // Starts dragging a control point of the selected mark (any tool); false when the click is not on one
   bool TryHandleDrag(const Env& env);
   void BeginDrag(Drag drag, const Mark& mark, const Env& env);
   void RenderContextMenu(float dpi);
   void RenderAskUI(float dpi); // the Ask Assistant menu and the text popup (also while the tool is not active)
   // Snaps a point to the points of the existing marks (except ignoreMarkId), else to the grid / part points; remembers it for the indicator
   Point Snap(const Env& env, Point raw, int ignoreMarkId);

   bool m_active = false;
   ToolType m_tool = ToolType::Pen;
   MarkKind m_kind = MarkKind::Path;
   Phase m_phase = Phase::Idle;
   const void* m_owner = nullptr;
   Env m_env; // of the gesture in progress
   std::vector<Point> m_points; // pen: all points; others: start and end
   ImVec2 m_lastScreen;
   Point m_mid; // arc bend
   bool m_tangent = false; // arc continuing the previous one
   Point m_tangentStart, m_tangentDir;
   int m_chainId = 0; // last arc mark made by the tool (Shift continues it)

   // Edit tool: the selected mark with its handles, and the drag in progress
   int m_selected = 0;
   int m_hoverId = 0;
   int m_hoverFrame = -100;
   Drag m_drag = Drag::None;
   int m_dragHandle = 0;
   bool m_dragMoved = false;
   Point m_dragStart;
   Mark m_dragOrig;
   std::vector<Mark> m_dragBefore;

   // Right click menu of a mark (any tool): armed on the press, opened on the release when the mouse did not drag
   int m_ctxId = 0;
   bool m_ctxArmed = false;
   bool m_ctxOpenPending = false;
   ImVec2 m_ctxPress;
   ImVec2 m_ctxPos;
   Env m_ctxEnv;
   int m_rightFrame = -100;

   // Snapping: where the last point snapped to (drawn as a ring), and the editor preference for the toolbar checkbox
   Point m_snapPoint;
   int m_snapFrame = -100;
   bool m_snapEnabledPref = false;
   std::function<void()> m_toggleSnap;

   // Text popup: a new text pin, or the label of an existing mark (m_labelEditId)
   bool m_textPending = false;
   bool m_textFocus = false;
   int m_labelEditId = 0;
   Point m_textPoint;
   ImVec2 m_textScreen;
   char m_textBuf[160] = {};
   Env m_textEnv;
   bool m_textRequest = false; // the popup makes a request for the assistant
   std::string m_textPart;
   nlohmann::json m_textView;

   // Ask Assistant menu
   bool m_askMenuPending = false;
   Env m_askEnv;
   Point m_askPoint;
   std::string m_askPart;
   nlohmann::json m_askView;
   ImVec2 m_askScreen;

   // Toolbar anchor
   const void* m_barOwner = nullptr;
   ImVec2 m_barPos = ImVec2(0, 0);
   Store* m_store = nullptr;
};

}
