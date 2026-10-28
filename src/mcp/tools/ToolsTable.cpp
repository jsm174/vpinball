// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "ui/live/editor/ops/SetOriginOp.h"

#include <miniz/miniz.h>

namespace VPX::MCP
{

static json VRAuditJson(PinTable* table)
{
   constexpr unsigned int flatModes = PartGroupData::PlayerModeVisibilityMask::PMVM_DESKTOP | PartGroupData::PlayerModeVisibilityMask::PMVM_FSS | PartGroupData::PlayerModeVisibilityMask::PMVM_CABINET;
   constexpr unsigned int xrModes = PartGroupData::PlayerModeVisibilityMask::PMVM_VIRTUAL_REALITY | PartGroupData::PlayerModeVisibilityMask::PMVM_MIXED_REALITY;
   const auto modes = [](unsigned int mask)
   {
      json list = json::array();
      for (const auto& [bit, label] : { std::pair { PartGroupData::PlayerModeVisibilityMask::PMVM_DESKTOP, "desktop" }, { PartGroupData::PlayerModeVisibilityMask::PMVM_FSS, "fss" },
              { PartGroupData::PlayerModeVisibilityMask::PMVM_CABINET, "cabinet" }, { PartGroupData::PlayerModeVisibilityMask::PMVM_VIRTUAL_REALITY, "vr" }, { PartGroupData::PlayerModeVisibilityMask::PMVM_MIXED_REALITY, "mr" } })
         if (mask & bit)
            list.push_back(label);
      return list;
   };
   const auto spaceName = [](PartGroupData::SpaceReference space)
   {
      switch (space)
      {
      case PartGroupData::SpaceReference::SR_CABINET: return "cabinet";
      case PartGroupData::SpaceReference::SR_CABINET_FEET: return "cabinet feet";
      case PartGroupData::SpaceReference::SR_ROOM: return "room";
      default: return "playfield";
      }
   };
   json groups = json::array(), warnings = json::array(), cabinetParts = json::array(), outside = json::array();
   bool hasXRContent = false;
   float cabinetLength = 0.f;
   string xrOnly;
   for (IEditable* part : table->GetParts())
   {
      if (part->GetItemType() != eItemPartGroup)
         continue;
      const PartGroup* group = static_cast<PartGroup*>(part);
      const unsigned int mask = group->GetPlayerModeVisibilityMask();
      const PartGroupData::SpaceReference space = group->GetReferenceSpace();
      if (mask == PartGroupData::PlayerModeVisibilityMask::PMVM_ALL && space == PartGroupData::SpaceReference::SR_PLAYFIELD)
         continue;
      hasXRContent |= (mask & xrModes) && !(mask & flatModes);
      hasXRContent |= space != PartGroupData::SpaceReference::SR_PLAYFIELD;
      groups.push_back(json { { "name", group->GetName() }, { "visibleIn", modes(mask) }, { "space", spaceName(space) } });
      if ((mask & xrModes) && !(mask & flatModes))
         xrOnly += (xrOnly.empty() ? ""s : ", "s) + group->GetName();
   }
   if (!xrOnly.empty())
      warnings.push_back("Groups shown only in VR/MR (" + xrOnly + "): VPX uses that visibility only when a headset is connected. With RenderingModeOverride 2 (forced VR) and no headset, "
         "the script sees RenderingMode 2 but parts are filtered with the desktop, FSS or cabinet visibility of the current view mode, so these groups stay hidden. Preview with vpx_screenshot mode:'vr'");
   for (IEditable* part : table->GetParts())
   {
      const PartGroup* group = part->GetPartGroup();
      if (part->GetItemType() == eItemPartGroup || part->GetItemType() == eItemBall || group == nullptr || table->IsPlayerSessionObject(part))
         continue;
      const unsigned int mask = group->GetPlayerModeVisibilityMask();
      const json bounds = PartBoundsJson(part);
      if (bounds.is_null())
         continue;
      const float x0 = bounds["min"][0], y0 = bounds["min"][1], x1 = bounds["max"][0], y1 = bounds["max"][1];
      if (group->GetReferenceSpace() != PartGroupData::SpaceReference::SR_PLAYFIELD)
      {
         cabinetParts.push_back(json { { "name", part->GetName() }, { "bounds", bounds } });
         cabinetLength = max(cabinetLength, y1 - y0);
      }
      else if ((mask & PartGroupData::PlayerModeVisibilityMask::PMVM_VIRTUAL_REALITY) && hasXRContent
         && (x1 < table->m_left || x0 > table->m_right || y1 < table->m_top || y0 > table->m_bottom))
         outside.push_back(part->GetName());
   }
   if (!outside.empty())
   {
      string names;
      for (size_t i = 0; i < outside.size() && i < 12; i++)
         names += (i ? ", "s : ""s) + outside[i].get<string>();
      warnings.push_back(std::to_string(outside.size()) + " part(s) visible in VR lie entirely outside the playfield (" + names + (outside.size() > 12 ? ", ..." : "")
         + "): desktop backdrop decorations or spare parts there float around the cabinet in VR; put them in a group hidden in VR (vpx_part get reports each one's bounds)");
   }
   if (groups.empty() && cabinetParts.empty())
      return json();
   const float tableLength = table->m_bottom - table->m_top;
   if (!cabinetParts.empty() && cabinetLength < tableLength * 0.95f)
      warnings.push_back("The longest cabinet/room space part spans "s + std::to_string(static_cast<int>(cabinetLength)) + " units along y, shorter than the playfield (" + std::to_string(static_cast<int>(tableLength))
         + "): the cabinet does not cover the table; compare the bounds in cabinetParts with the table extents");
   json result { { "groups", groups }, { "table", json { { "left", table->m_left }, { "top", table->m_top }, { "right", table->m_right }, { "bottom", table->m_bottom }, { "glassTop", table->m_glassTopHeight }, { "glassBottom", table->m_glassBottomHeight } } } };
   if (!cabinetParts.empty())
   {
      if (cabinetParts.size() > 40)
         cabinetParts.erase(cabinetParts.begin() + 40, cabinetParts.end());
      result["cabinetParts"] = cabinetParts;
   }
   if (!warnings.empty())
      result["warnings"] = warnings;
   return result;
}

void AddTableTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_editor", "The live editor: 'open' it on the current table, 'close' it back to play, 'playtest' the edited table (a play session on a copy; a backup copy <table>.before-playtest.vpx is written first unless backup:false, since a crash while playtesting loses the unsaved edits), 'stop' the playtest (both return once the switch is done, with the new status), 'undo' / 'redo' edits (steps, default 1; the history labels MCP edits 'MCP ...'), 'save' the table to its file. Screen state for the user: 'layout' (layout | library | scripting | backdrop | blueprint, the editor's preset slots; reset:true puts the slot back to its default areas, leaving fly mode and maximized areas, so the user sees the 3D view, outliner and properties again), 'show' an area (outliner, properties, script, library, viewport, backdrop, preview, blueprint) in the layout, 'select' parts (names, or all / invert; empty clears) and 'selection' (what is selected, with the transform of the active part and the bounds and size of the whole selection, as the viewport sidebar shows them; read only, edit parts through their fields), 'frame' every 3D view and blueprint on the selection (names selects first) or on the whole table (all). Selection edits: 'duplicate' the selected parts (offset [dx, dy], new names returned), 'set_origin' of the selected primitives (mode geometry: bounding box center, bottom: center at the lowest z, scene: the origin of the part's reference space; the mesh moves so the part does not), 'lights' sets the state of every light (or of the named ones): the editor does not run the script, so lamps show their saved state unless set here (state 0 off, 1 on, 2 blinking; display only, nothing is saved).",
         Schema({ { "action", Prop("string", "open | close | playtest | stop | undo | redo | save | layout | show | select | selection | frame | duplicate | set_origin | lights") }, { "backup", Prop("boolean", "playtest: write <table>.before-playtest.vpx next to the table first, default true") },
                  { "steps", Prop("integer", "undo, redo: number of history steps, default 1") }, { "layout", Prop("string", "layout: layout | library | scripting | backdrop | blueprint (or the slot index 0-4), default the active one") },
                  { "reset", Prop("boolean", "layout: put the slot back to its preset areas") },
                  { "area", Prop("string", "show: outliner | properties | script | library | viewport | backdrop | preview | blueprint") },
                  { "names", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "select, frame, duplicate, set_origin: part names (the selection is used when omitted); lights: the lights to set (every light of the table when omitted)" } } },
                  { "all", Prop("boolean", "select: every part; frame: the whole table") }, { "invert", Prop("boolean", "select: invert the current selection") },
                  { "offset", json { { "type", "array" }, { "items", json { { "type", "number" } } }, { "description", "duplicate: [dx, dy] move of the copies in table units, default [0, 0]" } } },
                  { "mode", Prop("string", "set_origin: geometry | bottom | scene (default geometry)") }, { "state", Prop("number", "lights: 0 off, 0..1 intensity, 1 on, 2 blinking") } }, { "action" }),
         [](const json& args) -> ToolStep
         {
            const string action = args.value("action", ""s);
            if (action == "layout" || action == "show" || action == "frame")
            {
               struct Pending { bool requested = false; uint32_t until = 0; json notFound = json::array(); };
               auto st = std::make_shared<Pending>();
               return [action, args, st]() -> std::optional<ToolOutput>
               {
                  if (!g_pplayer)
                     return ErrorOutput("No table session is running"s);
                  Player& p = *g_pplayer;
                  if (!p.m_liveUI || !p.m_liveUI->IsEditorUIOpened())
                     return ErrorOutput("The editor is not open: call vpx_editor open first"s);
                  auto& editor = p.m_liveUI->m_editorUI;
                  if (!st->requested)
                  {
                     if (action == "layout")
                     {
                        static const std::map<string, int> slots = { { "layout", 0 }, { "default", 0 }, { "library", 1 }, { "scripting", 2 }, { "script", 2 }, { "backdrop", 3 }, { "blueprint", 4 }, { "0", 0 }, { "1", 1 }, { "2", 2 }, { "3", 3 }, { "4", 4 } };
                        const json l = args.value("layout", json());
                        const bool reset = args.value("reset", false);
                        if (!l.is_null() || !reset)
                        {
                           const string key = l.is_number() ? std::to_string(l.get<int>()) : lowerCase(l.is_string() ? l.get<string>() : ""s);
                           const auto it = slots.find(key);
                           if (it == slots.end())
                              return ErrorOutput("layout must be layout, library, scripting, backdrop or blueprint (or reset:true for the active one)"s);
                           editor.SetLayout(it->second);
                        }
                        if (reset)
                           editor.ResetLayout();
                     }
                     else if (action == "show")
                     {
                        static const std::map<string, AreaType> areas = { { "outliner", AreaType::Outliner }, { "properties", AreaType::Properties }, { "script", AreaType::Script }, { "library", AreaType::PartLibrary }, { "partlibrary", AreaType::PartLibrary },
                           { "viewport", AreaType::Viewport }, { "3d", AreaType::Viewport }, { "backdrop", AreaType::ViewportBackdrop }, { "viewportbackdrop", AreaType::ViewportBackdrop }, { "preview", AreaType::ViewportPreview }, { "viewportpreview", AreaType::ViewportPreview }, { "blueprint", AreaType::Blueprint } };
                        const auto it = areas.find(lowerCase(args.value("area", ""s)));
                        if (it == areas.end())
                           return ErrorOutput("area must be outliner, properties, script, library, viewport, backdrop, preview or blueprint"s);
                        editor.ShowArea(it->second);
                     }
                     else if (args.value("all", false))
                        editor.FrameViews(true);
                     else
                     {
                        if (args.contains("names"))
                        {
                           vector<IEditable*> parts;
                           for (const string& n : args.value("names", vector<string>()))
                              if (IEditable* part = FindPart(p, n); part)
                                 parts.push_back(part);
                              else
                                 st->notFound.push_back(n);
                           editor.SelectParts(parts);
                        }
                        if (editor.GetSelectedParts().empty())
                           return ErrorOutput("Nothing is selected: pass names, all:true or vpx_editor select first"s);
                        editor.FrameViews(false);
                     }
                     st->requested = true;
                     st->until = p.m_overall_frames + 2;
                     return std::nullopt;
                  }
                  if (p.m_overall_frames < st->until)
                     return std::nullopt;
                  json result = editor.GetLayoutInfo();
                  if (action == "frame")
                  {
                     json names = json::array();
                     for (IEditable* part : editor.GetSelectedParts())
                        names.push_back(part->GetName());
                     result["selection"] = names;
                     result["notFound"] = st->notFound;
                  }
                  return TextOutput(result);
               };
            }
            if (action == "playtest" || action == "stop")
            {
               struct Switch { bool requested = false; int framesAfter = 0; std::chrono::steady_clock::time_point start; json backup; int unsavedMarks = 0; };
               auto sw = std::make_shared<Switch>();
               const bool backup = args.value("backup", true);
               return [action, sw, backup]() -> std::optional<ToolOutput>
               {
                  if (!g_pplayer)
                     return ErrorOutput("No table session is running"s);
                  Player& p = *g_pplayer;
                  if (!sw->requested)
                  {
                     if (!p.m_liveUI)
                        return ErrorOutput("No UI available"s);
                     if (action == "playtest")
                     {
                        if (!p.m_liveUI->IsEditorUIOpened())
                           return ErrorOutput("The editor is not open"s);
                        if (p.m_liveUI->m_editorUI.IsInspectMode())
                           return ErrorOutput("A playtest is already running"s);
                        if (backup && !p.m_ptable->m_filename.empty())
                        {
                           const std::filesystem::path path = p.m_ptable->m_filename.parent_path() / (PathToUTF8(p.m_ptable->m_filename.stem()) + ".before-playtest.vpx");
                           string error;
                           if (WriteTableSnapshot(p, p.m_ptable, path, error))
                              sw->backup = PathToUTF8(path);
                           else
                              sw->backup = json { { "error", error } };
                        }
                        sw->unsavedMarks = UnsavedMarkCount(p);
                        p.m_ignoreWndFocus = true;
                        p.m_liveUI->m_editorUI.PlayTest();
                     }
                     else
                     {
                        if (p.m_ptable->m_liveBaseTable == nullptr || !p.HasStackedTableSession())
                           return ErrorOutput("No playtest session to stop"s);
                        p.SetCloseState(Player::CS_STOP_PLAY);
                     }
                     sw->requested = true;
                     sw->start = std::chrono::steady_clock::now();
                     return std::nullopt;
                  }
                  const bool done = action == "playtest" ? IsLiveCopy(p) && !p.IsEditorMode() : !IsLiveCopy(p) && IsEditing(p);
                  if (done && ++sw->framesAfter > 1)
                  {
                     json result = StatusJson(p);
                     result["switched"] = action == "playtest" ? "playtest running on a copy of the edited table: part, table and script edits now reach only that copy; vpx_editor stop returns to the editor"s
                                                               : "editor restored on the edited table: edits now reach the table that is saved"s;
                     if (!sw->backup.is_null())
                        result["backup"] = sw->backup;
                     if (sw->unsavedMarks > 0)
                        result["unsavedMarksWarning"] = std::to_string(sw->unsavedMarks) + " sketch marks only live in this session (the table has no file yet): a crash during the playtest loses them, save the table to keep them";
                     return TextOutput(result);
                  }
                  if (std::chrono::steady_clock::now() - sw->start > std::chrono::seconds(20))
                     return ErrorOutput("The "s + (action == "playtest" ? "playtest did not start" : "editor was not restored") + " within 20 s; check vpx_status"s);
                  return std::nullopt;
               };
            }
            return WithPlayer([action, args](Player& p)
            {
               if (!p.m_liveUI)
                  return ErrorOutput("No UI available"s);
               if (action == "open")
               {
                  p.m_liveUI->OpenEditorUI();
                  return TextOutput(StatusJson(p));
               }
               if (action == "close")
               {
                  p.m_liveUI->m_editorUI.Close();
                  return TextOutput(StatusJson(p));
               }
               if (action == "select" || action == "selection")
               {
                  if (!p.m_liveUI->IsEditorUIOpened())
                     return ErrorOutput("The editor is not open: call vpx_editor open first"s);
                  auto& editor = p.m_liveUI->m_editorUI;
                  json notFound = json::array();
                  if (action == "select")
                  {
                     if (args.value("all", false))
                        editor.SelectAllParts();
                     else if (args.value("invert", false))
                        editor.InvertSelection();
                     else
                     {
                        vector<IEditable*> parts;
                        for (const string& n : args.value("names", vector<string>()))
                           if (IEditable* part = FindPart(p, n); part)
                              parts.push_back(part);
                           else
                              notFound.push_back(n);
                        editor.SelectParts(parts);
                     }
                  }
                  json names = json::array();
                  for (IEditable* part : editor.GetSelectedParts())
                     names.push_back(part->GetName());
                  json result { { "selection", names } };
                  if (IEditable* active = editor.GetActivePart(); active)
                  {
                     result["active"] = active->GetName();
                     const Vertex2D c = editor.GetMultiSelCenter();
                     result["center"] = { { "x", Round(c.x) }, { "y", Round(c.y) } };
                  }
                  vec3 pos, rot, scale;
                  FRect3D bounds;
                  // What the Item tab of the viewport sidebar shows; computed by the editor, not stored fields of a part
                  if (editor.GetSelectionPlacement(pos, rot, scale))
                     result["transform"] = { { "position", { Round(pos.x), Round(pos.y), Round(pos.z) } }, { "rotation", { Round(rot.x), Round(rot.y), Round(rot.z) } },
                        { "scale", { Round(scale.x, 3), Round(scale.y, 3), Round(scale.z, 3) } } };
                  if (editor.GetSelectionExtent(bounds))
                  {
                     result["bounds"] = { { "min", { Round(bounds.left), Round(bounds.top), Round(bounds.zlow) } }, { "max", { Round(bounds.right), Round(bounds.bottom), Round(bounds.zhigh) } },
                        { "size", { Round(bounds.right - bounds.left), Round(bounds.bottom - bounds.top), Round(bounds.zhigh - bounds.zlow) } } };
                  }
                  if (action == "select")
                     result["notFound"] = notFound;
                  return TextOutput(result);
               }
               if (action == "undo" || action == "redo")
               {
                  if (const string err = RequireEditor(p); !err.empty())
                     return ErrorOutput(err);
                  auto& editor = p.m_liveUI->m_editorUI;
                  const bool undo = action == "undo";
                  if (undo ? !editor.CanUndo() : !editor.CanRedo())
                     return ErrorOutput(undo ? "Nothing to undo"s : "Nothing to redo"s);
                  const size_t steps = static_cast<size_t>(std::max(1, args.value("steps", 1)));
                  const string label = undo ? editor.UndoLabel() : editor.RedoLabel();
                  if (undo)
                     editor.UndoSteps(steps);
                  else
                     editor.RedoSteps(steps);
                  json result { { undo ? "undone" : "redone", label }, { "canUndo", editor.CanUndo() }, { "canRedo", editor.CanRedo() } };
                  if (editor.CanUndo())
                     result["nextUndo"] = editor.UndoLabel();
                  if (editor.CanRedo())
                     result["nextRedo"] = editor.RedoLabel();
                  return TextOutput(result);
               }
               if (action == "duplicate" || action == "set_origin" || action == "lights")
               {
                  if (const string err = RequireEditor(p); !err.empty())
                     return ErrorOutput(err);
                  auto& editor = p.m_liveUI->m_editorUI;
                  json notFound = json::array();
                  if (args.contains("names"))
                  {
                     vector<IEditable*> parts;
                     for (const string& n : args.value("names", vector<string>()))
                        if (IEditable* part = FindPart(p, n); part)
                           parts.push_back(part);
                        else
                           notFound.push_back(n);
                     editor.SelectParts(parts);
                  }
                  const auto namesOf = [](const vector<IEditable*>& parts)
                  {
                     json names = json::array();
                     for (IEditable* part : parts)
                        names.push_back(part->GetName());
                     return names;
                  };
                  if (action == "lights")
                  {
                     const float state = std::clamp(args.value("state", 1.f), 0.f, 2.f);
                     const vector<IEditable*> parts = args.contains("names") ? editor.GetSelectedParts() : p.m_ptable->GetParts();
                     int count = 0;
                     for (IEditable* part : parts)
                        count += part->GetItemType() == eItemLight;
                     if (count == 0)
                        return ErrorOutput(args.contains("names") ? "No light among the named parts"s : "The table has no light"s);
                     VPX::EditorUI::EditorUI::SetLightStates(parts, state);
                     return TextOutput(json { { "lights", count }, { "state", state }, { "note", "Display state only, the table file is unchanged: the script sets the lamps during play" }, { "notFound", notFound } });
                  }
                  if (editor.GetSelectedParts().empty())
                     return ErrorOutput("Nothing is selected: pass names or vpx_editor select first"s);
                  if (action == "duplicate")
                  {
                     Vertex2D offset(0.f, 0.f);
                     if (const json o = args.value("offset", json()); o.is_array() && o.size() == 2 && o[0].is_number() && o[1].is_number())
                        offset = Vertex2D(o[0].get<float>(), o[1].get<float>());
                     else if (!o.is_null())
                        return ErrorOutput("offset must be [dx, dy] in table units"s);
                     const json sources = namesOf(editor.GetSelectedParts());
                     const vector<IEditable*> copies = editor.DuplicateParts(offset);
                     if (copies.empty())
                        return ErrorOutput("Nothing was duplicated (drag point edit mode, or the selection holds no part)"s);
                     McpServer::Get().NoteEdit();
                     return TextOutput(json { { "duplicated", sources }, { "parts", namesOf(copies) }, { "notFound", notFound } });
                  }
                  const string mode = lowerCase(args.value("mode", "geometry"s));
                  if (mode != "geometry" && mode != "bottom" && mode != "scene")
                     return ErrorOutput("mode must be geometry, bottom or scene"s);
                  if (!VPX::EditorUI::SetOriginOp::CanApply(editor))
                     return ErrorOutput("set_origin needs selected primitives with their own mesh (not the playfield)"s);
                  VPX::EditorUI::SetOriginOp::Apply(editor, mode == "bottom" ? VPX::EditorUI::SetOriginOp::Mode::GeometryBottom : mode == "scene" ? VPX::EditorUI::SetOriginOp::Mode::Scene : VPX::EditorUI::SetOriginOp::Mode::Geometry);
                  McpServer::Get().NoteEdit();
                  json parts = json::array();
                  for (IEditable* part : editor.GetSelectedParts())
                     if (part->GetItemType() == eItemPrimitive)
                     {
                        const Vertex3Ds& pos = static_cast<Primitive*>(part)->m_d.m_vPosition;
                        parts.push_back({ { "name", part->GetName() }, { "position", { Round(pos.x), Round(pos.y), Round(pos.z) } } });
                     }
                  return TextOutput(json { { "mode", mode }, { "parts", parts }, { "notFound", notFound } });
               }
               if (action == "save")
               {
                  PinTable* table = p.m_ptable->m_liveBaseTable ? p.m_ptable->m_liveBaseTable : p.m_ptable;
                  if (table->m_filename.empty())
                     return ErrorOutput("The table has no file name yet: use vpx_table save_as"s);
                  const bool saved = SaveTableFile(p, table);
                  if (saved)
                     McpServer::Get().ResetEdits();
                  return saved ? TextOutput(json { { "saved", PathToUTF8(table->m_filename) } }) : ErrorOutput("Save failed"s);
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

   tools.push_back(ToolDef { "vpx_table",
      "The table as a whole. Actions: open (path) | new (a blank table): both replace the running session and return once the new one is up | save_as (path) | get (fields subset: table level properties with their meaning) | set (data: {field: value}, applied live, undoable while the editor is open) | audit (the VPX table audit, plus a vr section for tables with VR content) | settings (key: \"Section.Name\" of the ini files: the value this session reads and whether it comes from the table ini, the global ini or the default, with both file paths) | checkpoint (name: keep a copy of the edited table under <table>.checkpoints/, the table's own file is untouched) | checkpoints (list them) | restore (name: reload that copy as the current table, keeping the table's file name; replaces the session like open). Details: vpx_status guide:\"table\".",
         Schema({ { "action", Prop("string", "open | new | save_as | audit | settings | get | set | checkpoint | checkpoints | restore") }, { "key", Prop("string", "settings: <Section>.<Name>, e.g. Plugin.WebView.Enable") }, { "data", json { { "type", "object" }, { "description", "set: {field: value}" } } }, { "fields", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "get: subset" } } }, { "path", Prop("string", "Absolute table path, a .vpx file or a .vpz pack (open, save_as: .vpx writes a table file, .vpz a zip pack, an existing folder a folder pack)") },
                  { "name", Prop("string", "checkpoint (default: a timestamp), restore: the checkpoint name") } }, { "action" }),
         [](const json& args) -> ToolStep
         {
            const string action = args.value("action", ""s);
            const std::filesystem::path path = PathFromUTF8(args.value("path", ""s));
            if (action == "open" || action == "new" || action == "restore")
            {
               // The player replaces its session on the next frame: poll until the new table runs (jobs are not processed while it loads)
               struct Switch { const PinTable* target = nullptr; int framesAfter = 0; std::chrono::steady_clock::time_point start; bool reopenEditor = false; string checkpoint; };
               auto sw = std::make_shared<Switch>();
               const string name = args.value("name", ""s);
               return [action, path, name, sw]() -> std::optional<ToolOutput>
               {
                  if (!g_pplayer)
                     return ErrorOutput("No table session is running"s);
                  Player& p = *g_pplayer;
                  if (!sw->target)
                  {
                     if (action == "open" && path.empty())
                        return ErrorOutput("path is required"s);
                     std::filesystem::path source = path;
                     std::filesystem::path original;
                     if (action == "restore")
                     {
                        if (IsLiveCopy(p))
                           return ErrorOutput("A playtest is running: stop it with vpx_editor stop first"s);
                        if (name.empty())
                           return ErrorOutput("name is required: vpx_table checkpoints lists them"s);
                        const std::filesystem::path folder = CheckpointFolder(p.m_ptable);
                        if (folder.empty())
                           return ErrorOutput("The table has no file name, so it has no checkpoints"s);
                        source = folder / (SafeFileStem(name) + ".vpx");
                        if (!std::filesystem::exists(source))
                           return ErrorOutput("No checkpoint named '"s + name + "': vpx_table checkpoints lists them");
                        original = p.m_ptable->m_filename;
                        sw->reopenEditor = IsEditing(p);
                        sw->checkpoint = PathToUTF8(source);
                     }
                     string error;
                     CComObject<PinTable>* table = LoadTableForSwitch(action == "new" ? std::filesystem::path() : source, error);
                     if (!table)
                        return ErrorOutput(error);
                     if (action == "restore")
                     {
                        table->m_filename = original;
                        table->m_title = PathToUTF8(original.stem());
                        if (const std::filesystem::path ini = table->GetSettingsFileName(); !ini.empty())
                        {
                           table->GetSettings().SetIniPath(ini);
                           if (FileExists(ini))
                              table->GetSettings().Load(false);
                        }
                        table->SetNonUndoableDirty(SaveDirtyState::Dirty);
                     }
                     sw->target = table;
                     sw->start = std::chrono::steady_clock::now();
                     p.SetTable(table, Player::TableTransition::Replace);
                     McpServer::Get().ResetEdits();
                     return std::nullopt;
                  }
                  if ((p.m_ptable == sw->target || p.m_ptable->m_liveBaseTable == sw->target) && ++sw->framesAfter > 1)
                  {
                     if (action == "restore" && sw->reopenEditor && p.m_liveUI && !p.m_liveUI->IsEditorUIOpened())
                     {
                        p.m_liveUI->OpenEditorUI();
                        if (sw->framesAfter < 4)
                           return std::nullopt;
                     }
                     json result = StatusJson(p);
                     result["switched"] = action == "open" ? "the table was opened and its session started"s
                        : action == "new"                  ? "a new blank table was created, save it with vpx_table save_as"s
                                                           : "the checkpoint was loaded as the current table: it keeps the table's file name and is marked unsaved, vpx_editor save writes it over the table file"s;
                     if (action == "restore")
                        result["restored"] = sw->checkpoint;
                     return TextOutput(result);
                  }
                  if (std::chrono::steady_clock::now() - sw->start > std::chrono::seconds(90))
                     return ErrorOutput("The new table session did not start within 90 s; check vpx_status"s);
                  return std::nullopt;
               };
            }
            return WithPlayer([action, path, args](Player& p)
            {
               if (action == "get")
               {
                  const vector<string> wanted = args.value("fields", vector<string>());
                  json values = json::object(), meaning = json::object(), unknown = json::array();
                  for (const string& w : wanted)
                     if (std::ranges::none_of(TableFields(), [&w](const TableField& f) { return w == f.name; }))
                        unknown.push_back(w);
                  for (const TableField& f : TableFields())
                     if (wanted.empty() || std::ranges::find(wanted, string(f.name)) != wanted.end())
                     {
                        values[f.name] = f.get(p.m_ptable);
                        meaning[f.name] = string(f.kind) + ": " + f.description;
                     }
                  json result { { "table", p.m_ptable->m_title }, { "data", values } };
                  if (wanted.empty())
                     result["fields"] = meaning;
                  if (!unknown.empty())
                     result["unknown"] = unknown;
                  return TextOutput(result);
               }
               if (action == "settings")
               {
                  const string key = args.value("key", ""s);
                  if (key.empty())
                     return ErrorOutput("settings needs key: <Section>.<Name> as in the ini file, e.g. Plugin.WebView.Enable"s);
                  const json result = SettingSourceJson(p, key);
                  if (result.contains("error"))
                     return ErrorOutput(result.value("error", ""s));
                  return TextOutput(result);
               }
               if (action == "set")
               {
                  const json data = args.value("data", json::object());
                  if (!data.is_object() || data.empty())
                     return ErrorOutput("data is required: {field: value}, e.g. data: {\"image\": \"MyPlayfield\"}; vpx_table get lists the fields"s);
                  vector<std::pair<const TableField*, json>> updates;
                  for (const auto& [key, value] : data.items())
                  {
                     const auto it = std::ranges::find_if(TableFields(), [&key](const TableField& f) { return NormalizeFieldName(key) == NormalizeFieldName(f.name); });
                     if (it == TableFields().end())
                        return ErrorOutput("Unknown table field '"s + key + "': vpx_table get lists them");
                     const string kind = it->kind;
                     const bool ok = kind == "string" ? value.is_string() : kind == "boolean" ? value.is_boolean() : value.is_number();
                     if (!ok)
                        return ErrorOutput("'"s + it->name + "' expects a " + kind);
                     if (kind == "string" && !value.get<string>().empty())
                     {
                        const string v = value.get<string>();
                        const bool isMaterial = string(it->name) == "material";
                        if (isMaterial ? p.m_ptable->IsDummyMaterial(p.m_ptable->GetMaterial(v)) : p.m_ptable->GetImage(v) == nullptr)
                           return ErrorOutput("'"s + it->name + "': no " + (isMaterial ? "material" : "image") + " named '" + v + '\'');
                     }
                     updates.emplace_back(&*it, value);
                  }
                  bool shaderDirty = false;
                  json applied = json::array();
                  PinTable* const table = p.m_ptable;
                  vector<const TableField*> fields;
                  for (const auto& [field, value] : updates)
                     fields.push_back(field);
                  const auto capture = [table, fields]() -> std::function<void()>
                  {
                     vector<std::pair<const TableField*, json>> values;
                     bool shader = false;
                     for (const TableField* field : fields)
                     {
                        values.emplace_back(field, field->get(table));
                        shader |= field->shaderDirty;
                     }
                     return [table, values, shader]()
                     {
                        for (const auto& [field, value] : values)
                           field->set(table, value);
                        if (shader && g_pplayer && g_pplayer->m_ptable == table)
                           g_pplayer->m_renderer->MarkShaderDirty();
                     };
                  };
                  const std::function<void()> undo = capture();
                  for (const auto& [field, value] : updates)
                  {
                     field->set(p.m_ptable, value);
                     if (p.m_ptable->m_liveBaseTable)
                        field->set(p.m_ptable->m_liveBaseTable, value);
                     shaderDirty |= field->shaderDirty;
                     applied.push_back(field->name);
                  }
                  if (shaderDirty)
                     p.m_renderer->MarkShaderDirty();
                  RecordAction(p, table, "MCP table: "s + (fields.size() == 1 ? string(fields.front()->name) : std::to_string(fields.size()) + " fields"), undo, capture);
                  if (p.m_ptable->m_liveBaseTable)
                     p.m_ptable->m_liveBaseTable->SetNonUndoableDirty(SaveDirtyState::Dirty);
                  McpServer::Get().NoteEdit();
                  json result { { "applied", applied } };
                  if (IsLiveCopy(p))
                     result["note"] = "Applied to the running playtest and to the edited table, so it stays after the playtest stops; save with vpx_editor save";
                  return TextOutput(result);
               }
               if (action == "audit")
               {
                  const PinTable* table = p.m_ptable->m_liveBaseTable ? p.m_ptable->m_liveBaseTable : p.m_ptable;
                  json errors = json::array(), warnings = json::array(), other = json::array();
                  std::istringstream lines(table->AuditTable(false));
                  for (string line; std::getline(lines, line);)
                  {
                     if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                     if (line.rfind(". ", 0) == 0)
                        line = line.substr(2);
                     if (line.empty())
                        continue;
                     if (line.rfind("Error: ", 0) == 0)
                        errors.push_back(line.substr(7));
                     else if (line.rfind("Warning: ", 0) == 0)
                        warnings.push_back(line.substr(9));
                     else
                        other.push_back(line);
                  }
                  json result { { "errors", errors }, { "warnings", warnings } };
                  if (!other.empty())
                     result["notes"] = other;
                  if (json vr = VRAuditJson(p.m_ptable); !vr.is_null())
                     result["vr"] = vr;
                  return TextOutput(result);
               }
               if (action == "checkpoint" || action == "checkpoints")
               {
                  PinTable* table = p.m_ptable->m_liveBaseTable ? p.m_ptable->m_liveBaseTable : p.m_ptable;
                  const std::filesystem::path folder = CheckpointFolder(table);
                  if (folder.empty())
                     return ErrorOutput("The table has no file name yet: save it with vpx_table save_as first"s);
                  json result = json::object();
                  if (action == "checkpoint")
                  {
                     string name = SafeFileStem(args.value("name", ""s));
                     if (name.empty())
                     {
                        const std::time_t now = std::time(nullptr);
                        char buf[32];
                        std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", std::localtime(&now));
                        name = buf;
                     }
                     const std::filesystem::path file = folder / (name + ".vpx");
                     string error;
                     if (!WriteTableSnapshot(p, table, file, error))
                        return ErrorOutput("checkpoint failed: "s + error);
                     result["checkpoint"] = name;
                     result["written"] = PathToUTF8(file);
                     if (IsLiveCopy(p))
                        result["note"] = "the edited table was written, not the running playtest copy";
                  }
                  json list = json::array();
                  std::error_code ec;
                  for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
                     if (entry.is_regular_file() && lowerCase(PathToUTF8(entry.path().extension())) == ".vpx")
                     {
                        const auto mtime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(entry.last_write_time() - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
                        const std::time_t t = std::chrono::system_clock::to_time_t(mtime);
                        char buf[32];
                        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
                        list.push_back({ { "name", PathToUTF8(entry.path().stem()) }, { "written", buf }, { "bytes", entry.file_size() } });
                     }
                  vector<json> sorted(list.begin(), list.end());
                  std::ranges::sort(sorted, [](const json& a, const json& b) { return a.value("written", ""s) < b.value("written", ""s); });
                  result["folder"] = PathToUTF8(folder);
                  result["checkpoints"] = json(sorted);
                  return TextOutput(result);
               }
               if (action == "save_as")
               {
                  if (path.empty())
                     return ErrorOutput("path is required"s);
                  PinTable* table = p.m_ptable->m_liveBaseTable ? p.m_ptable->m_liveBaseTable : p.m_ptable;
                  table->m_filename = path;
                  table->m_title = PathToUTF8(path.stem());
                  const bool saved = SaveTableFile(p, table);
                  if (saved)
                     McpServer::Get().ResetEdits();
                  return saved ? TextOutput(json { { "saved", PathToUTF8(table->m_filename) } }) : ErrorOutput("Save failed"s);
               }
               return ErrorOutput("Unknown action"s);
            });
         }, 120.0 });

   tools.push_back(ToolDef { "vpx_export",
      "Package the table folder as a .vpxz archive for the mobile apps: one folder holding exactly one table with its companion files (sidecar script, ini, backglass, DMD, pinmame, music...), cache/, other tables (.vpx files, .vpz packs) and the editor's collaboration files (<table>.notes/locks/proposals/bookmarks.json) left out. Options: path (default ~/Downloads/<table folder>.vpxz) | save (save the table first, else unsaved edits are reported and left out) | dryRun (list only) | exclude (relative paths, names or wildcards, a trailing / for folders). Written in the background. Details: vpx_status guide:\"export\".",
         Schema({ { "path", Prop("string", "Output .vpxz file, default ~/Downloads/<table>.vpxz") }, { "save", Prop("boolean", "Save the table before packing, default false") },
                  { "dryRun", Prop("boolean", "List the files without writing the archive") },
                  { "exclude", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "Relative paths, names or wildcard patterns to leave out ('art/', '*.psd', 'notes.txt')" } } } }),
         [](const json& args)
         {
            struct Export
            {
               bool started = false;
               std::atomic<bool> done { false };
               string error;
               std::filesystem::path out;
               vector<std::pair<std::filesystem::path, string>> files; // source, archive name
               json summary;
               std::thread worker;
               ~Export()
               {
                  if (worker.joinable())
                     worker.join();
               }
            };
            auto ex = std::make_shared<Export>();
            return ToolStep([ex, args]() -> std::optional<ToolOutput>
            {
               if (!ex->started)
               {
                  ex->started = true;
                  if (!g_pplayer)
                     return ErrorOutput("No table session is running"s);
                  Player& p = *g_pplayer;
                  PinTable* table = p.m_ptable->m_liveBaseTable ? p.m_ptable->m_liveBaseTable : p.m_ptable;
                  if (table->m_filename.empty())
                     return ErrorOutput("The table has never been saved: use vpx_table save_as first"s);
                  json warnings = json::array();
                  if (args.value("save", false))
                  {
                     if (!SaveTableFile(p, table))
                        return ErrorOutput("Saving the table failed"s);
                     McpServer::Get().ResetEdits();
                  }
                  else if (table->GetDirtyState() != SaveDirtyState::Clean)
                     warnings.push_back("The table has unsaved changes: the archive holds the version on disk (pass save:true to include them)");
                  const std::filesystem::path tableFile = table->m_filename;
                  const std::filesystem::path folder = tableFile.parent_path();
                  const string stem = PathToUTF8(tableFile.stem());
                  std::error_code ec;
                  const string root = PathToUTF8(folder.filename());
                  json otherTables = json::array(), excluded = json::array(), sourceFiles = json::array(), collaborationFiles = json::array();
                  int systemFiles = 0;
                  const vector<string> excludes = args.value("exclude", vector<string>());
                  const auto glob = [](const string& pattern, const string& text)
                  {
                     size_t p = 0, t = 0, star = string::npos, mark = 0;
                     while (t < text.size())
                     {
                        if (p < pattern.size() && (pattern[p] == '?' || tolower(pattern[p]) == tolower(text[t])))
                           p++, t++;
                        else if (p < pattern.size() && pattern[p] == '*')
                           star = p++, mark = t;
                        else if (star != string::npos)
                           p = star + 1, t = ++mark;
                        else
                           return false;
                     }
                     while (p < pattern.size() && pattern[p] == '*')
                        p++;
                     return p == pattern.size();
                  };
                  const auto isExcluded = [&excludes, &glob](const string& relPath, const string& fileName, bool isDir)
                  {
                     for (string pattern : excludes)
                     {
                        std::ranges::replace(pattern, '\\', '/');
                        const bool dirPattern = !pattern.empty() && pattern.back() == '/';
                        if (dirPattern)
                           pattern.pop_back();
                        if (dirPattern && !isDir)
                           continue;
                        if (glob(pattern, relPath) || (pattern.find('/') == string::npos && glob(pattern, fileName)))
                           return true;
                     }
                     return false;
                  };
                  auto add = [&ex, &root](const std::filesystem::path& src, const std::filesystem::path& rel)
                  {
                     string name = root + '/' + PathToUTF8(rel.generic_u8string());
                     std::ranges::replace(name, '\\', '/');
                     if (std::ranges::none_of(ex->files, [&name](const auto& f) { return f.second == name; }))
                        ex->files.emplace_back(src, name);
                  };
                  for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                  {
                     if (ec)
                        break;
                     const std::filesystem::path rel = std::filesystem::relative(it->path(), folder, ec);
                     const string relName = PathToUTF8(rel.generic_u8string()), fileName = PathToUTF8(it->path().filename());
                     if (!fileName.empty() && (fileName[0] == '.' || StrCompareNoCase(fileName, "Thumbs.db"s) || StrCompareNoCase(fileName, "desktop.ini"s)))
                     {
                        if (it->is_directory(ec))
                           it.disable_recursion_pending();
                        else
                           systemFiles++;
                        continue;
                     }
                     if (fileName.size() > stem.size() && StrCompareNoCase(fileName.substr(0, stem.size()), stem))
                     {
                        // The editor's collaboration sidecars (sketch notes, locks, proposals, play bookmarks and their screenshots) are authoring files
                        static const char* const sidecars[] = { ".notes.json", ".locks.json", ".proposals.json", ".bookmarks.json", ".bookmarks" };
                        const string tail = fileName.substr(stem.size());
                        if (std::ranges::any_of(sidecars, [&tail](const char* e) { return StrCompareNoCase(tail, string(e)); }))
                        {
                           collaborationFiles.push_back(relName + (it->is_directory(ec) ? "/" : ""));
                           if (it->is_directory(ec))
                              it.disable_recursion_pending();
                           continue;
                        }
                     }
                     if (isExcluded(relName, fileName, it->is_directory(ec)))
                     {
                        excluded.push_back(relName + (it->is_directory(ec) ? "/" : ""));
                        if (it->is_directory(ec))
                           it.disable_recursion_pending();
                        continue;
                     }
                     if (it->is_directory(ec))
                     {
                        if (it.depth() == 0 && StrCompareNoCase(PathToUTF8(rel), "cache"s))
                           it.disable_recursion_pending();
                        else if (it->path() != tableFile && std::filesystem::exists(it->path() / "manifest.json", ec))
                        {
                           otherTables.push_back(PathToUTF8(rel.generic_u8string()));
                           it.disable_recursion_pending();
                        }
                        continue;
                     }
                     if (!it->is_regular_file(ec) || StrCompareNoCase(PathToUTF8(it->path().extension()), ".vpxz"s) || StrCompareNoCase(PathToUTF8(it->path().extension()), ".tmp"s))
                        continue;
                     if ((StrCompareNoCase(PathToUTF8(it->path().extension()), ".vpx"s) || StrCompareNoCase(PathToUTF8(it->path().extension()), ".vpz"s)) && it->path() != tableFile)
                     {
                        otherTables.push_back(PathToUTF8(rel.generic_u8string()));
                        continue;
                     }
                     static const char* const authoringExts[] = { ".psd", ".psb", ".xcf", ".kra", ".ora", ".ai", ".afphoto", ".afdesign", ".procreate", ".blend", ".blend1", ".fbx", ".max", ".ma", ".mb", ".c4d", ".zip", ".7z", ".rar", ".vpxz", ".bak" };
                     const string ext = PathToUTF8(it->path().extension());
                     if (std::ranges::any_of(authoringExts, [&ext](const char* e) { return StrCompareNoCase(ext, string(e)); }))
                        sourceFiles.push_back(relName);
                     add(it->path(), rel);
                  }
                  if (!otherTables.empty())
                     ex->summary["otherTablesLeftOut"] = otherTables;
                  if (!collaborationFiles.empty())
                     ex->summary["collaborationFilesLeftOut"] = collaborationFiles;
                  if (!excluded.empty())
                     ex->summary["excluded"] = excluded;
                  if (systemFiles > 0)
                     ex->summary["systemFilesSkipped"] = systemFiles;
                  if (!sourceFiles.empty())
                  {
                     ex->summary["possibleSourceFiles"] = sourceFiles;
                     warnings.push_back("Some packed files look like authoring sources (see possibleSourceFiles): pass exclude to leave them out");
                  }
                  // The table ini may live outside the table folder (settings folder)
                  if (const std::filesystem::path ini = table->GetSettingsFileName(); !ini.empty() && FileExists(ini))
                     add(ini, std::filesystem::path(stem + ".ini"));
                  if (!std::filesystem::is_directory(tableFile, ec) && std::ranges::none_of(ex->files, [&tableFile](const auto& f) { return f.first == tableFile; }))
                     add(tableFile, tableFile.filename());
                  const string outArg = args.value("path", ""s);
                  if (!outArg.empty())
                     ex->out = PathFromUTF8(outArg);
                  else
                  {
                     const char* home = getenv("HOME");
                     std::filesystem::path downloads = home ? std::filesystem::path(home) / "Downloads" : std::filesystem::temp_directory_path();
                     if (!std::filesystem::is_directory(downloads, ec))
                        downloads = std::filesystem::temp_directory_path();
                     ex->out = downloads / (root + ".vpxz");
                  }
                  if (ex->out.extension() != ".vpxz")
                     ex->out += ".vpxz";
                  uintmax_t total = 0;
                  json list = json::array();
                  for (const auto& [src, name] : ex->files)
                  {
                     total += std::filesystem::file_size(src, ec);
                     if (list.size() < 200)
                        list.push_back(name);
                  }
                  ex->summary["files"] = ex->files.size();
                  ex->summary["sourceBytes"] = total;
                  ex->summary["contents"] = list;
                  if (!warnings.empty())
                     ex->summary["warnings"] = warnings;
                  if (args.value("dryRun", false))
                  {
                     ex->summary["path"] = PathToUTF8(ex->out);
                     ex->summary["dryRun"] = true;
                     return TextOutput(ex->summary);
                  }
                  ex->worker = std::thread([ex]()
                  {
                     const std::filesystem::path tmp = std::filesystem::path(ex->out).concat(".tmp");
                     mz_zip_archive zip {};
                     if (!mz_zip_writer_init_file_v2(&zip, PathToUTF8(tmp).c_str(), 0, MZ_ZIP_FLAG_WRITE_ZIP64))
                        ex->error = "Could not create "s + PathToUTF8(tmp);
                     else
                     {
                        static const std::set<string> stored { ".zip", ".7z", ".rar", ".mp4", ".mkv", ".avi", ".webm", ".m4v", ".png", ".jpg", ".jpeg", ".webp", ".gif", ".apng", ".ogg", ".opus", ".mp3", ".m4a", ".flac", ".crz", ".cromc", ".exr", ".hdr" };
                        for (const auto& [src, name] : ex->files)
                        {
                           const bool alreadyCompressed = stored.contains(lowerCase(PathToUTF8(src.extension())));
                           if (!mz_zip_writer_add_file(&zip, name.c_str(), PathToUTF8(src).c_str(), nullptr, 0, alreadyCompressed ? MZ_NO_COMPRESSION : MZ_BEST_SPEED))
                           {
                              ex->error = "Could not add "s + PathToUTF8(src) + " to the archive";
                              break;
                           }
                        }
                        if (ex->error.empty() && !mz_zip_writer_finalize_archive(&zip))
                           ex->error = "Could not finalize the archive"s;
                        mz_zip_writer_end(&zip);
                     }
                     std::error_code ec;
                     if (ex->error.empty())
                     {
                        std::filesystem::rename(tmp, ex->out, ec);
                        if (ec)
                           ex->error = "Could not write "s + PathToUTF8(ex->out) + ": " + ec.message();
                     }
                     if (!ex->error.empty())
                        std::filesystem::remove(tmp, ec);
                     ex->done = true;
                  });
                  return std::nullopt;
               }
               if (!ex->done)
                  return std::nullopt;
               ex->worker.join();
               if (!ex->error.empty())
                  return ErrorOutput(ex->error);
               std::error_code ec;
               ex->summary["path"] = PathToUTF8(ex->out);
               ex->summary["archiveBytes"] = std::filesystem::file_size(ex->out, ec);
               ex->summary["note"] = "Copy it to the device (AirDrop, Files, the app's web server upload, or adb push) and open it with VPinball: it imports as one table";
               return TextOutput(ex->summary);
            });
         }, 600.0 });

}

}
