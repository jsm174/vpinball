// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "ui/live/editor/ops/ArcMath.h"
#include "ui/live/editor/ops/BevelMath.h"
#include "core/VPXPluginAPIImpl.h"
#include "plugins/ResURIBuilder.h"

namespace VPX::MCP
{

void AddPartTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_part",
      "Table parts through their stored definition: every field the table file keeps, named as in the VPZ packs (vpx_part schema lists them); nothing is gated by the script interface. Actions: list (type filter) | get (name, fields subset; reports bounds and mesh, the collections the part is in, a Flasher the triangles it draws and its winding) | visible (name: why a part does or does not show: visible flag, group and play mode, bounds, screen rect and crop, flasher triangles, display link and frame, likely reasons) | links (kind, filter: the display, segment display and controller state sources a link field can name right now, with the link text to use) | set (name, data: {field: value} merged and reloaded in place) | add (type, x, y, name, data, points, centerline + thickness, circle + segments) | delete (names, withChildren, force) | points (name, points: read or replace drag points; bevel {width, steps, points}: cut or round corners; arc {bulge, segments, points}: pull edges into circular arcs) | mesh (name, shape disc/cylinder/box or path .obj/.glb: give a Primitive a real mesh) | schema (type: every stored field with meaning and default, call it before setting a type for the first time) | transform (name, dx, dy, rotate, scale, scaleX, scaleY, flipX, flipY; names + pivot or aboutOwnCenter: several parts as one group, editor open) | order (names: drawing order). Unknown field names are refused with suggestions, script-style names are mapped (HeightTop -> height_top). set, add, delete, points, mesh, transform and order are undoable; add, delete, mesh and points need the editor open. Details: vpx_status guide:\"parts\".",
      Schema({ { "action", Prop("string", "list | get | visible | links | set | add | delete | points | mesh | schema | transform | order") }, { "kind", Prop("string", "links: display | segments | state (default all)") }, { "filter", Prop("string", "links: text the source name, group or plugin must contain") }, { "name", Prop("string", "Part name (get, visible, set, points, mesh, transform; add: optional new name)") },
                { "names", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "delete, order, transform (group transform about a pivot)" } } }, { "type", Prop("string", "list: filter; add, schema: part type") }, { "x", Prop("number", "add") }, { "y", Prop("number", "add") },
                { "fields", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "get: subset of stored fields" } } }, { "data", json { { "type", "object" }, { "description", "set, add: stored fields to merge, e.g. {\"height_top\": 50} (see schema)" } } },
                { "points", PointsProp("add, points: [{x, y, z?, smooth?, slingshot?}]") }, { "centerline", PointsProp("add: build the outline of a band (wall, guide, rim) of thickness around these points") }, { "thickness", Prop("number", "add with centerline: band width, default 6") },
                { "circle", Prop("number", "add: radius of a round outline centered at x,y for a drag point part (Light insert, Trigger, Wall post, Flasher)") }, { "segments", Prop("integer", "add with circle: outline points, default 16; mesh: disc/cylinder segments, default 48") },
                { "bevel", json { { "type", "object" }, { "description", "points: {width (how far each corner is cut back along its edges), steps (1 = flat cut, more = round fillet, default 1, up to 16), points (0-based indices of the corners, default all)}" } } },
                { "arc", json { { "type", "object" }, { "description", "points: {bulge (how far the middle of each edge is pulled out; positive outward), segments (per edge, default 8), points (0-based indices: an edge is turned when both its ends are listed, default all)}" } } },
                { "fixWinding", Prop("boolean", "add, points on a Flasher: reverse clockwise points so the flasher draws (default true)") },
                { "withChildren", Prop("boolean", "delete: also delete the parts of a group") }, { "force", Prop("boolean", "delete: also remove the playfield mesh") },
                { "shape", Prop("string", "mesh: disc | cylinder | box") }, { "radius", Prop("number", "mesh: disc/cylinder radius") }, { "height", Prop("number", "mesh: height") }, { "width", Prop("number", "mesh: box width (x)") }, { "depth", Prop("number", "mesh: box depth (y)") },
                { "path", Prop("string", "mesh: .obj or .glb file to load") }, { "units", Prop("string", "mesh .obj: vpx (default) or meters") },
                { "dx", Prop("number", "transform") }, { "dy", Prop("number", "transform") }, { "rotate", Prop("number", "transform: degrees") }, { "scale", Prop("number", "transform: uniform") }, { "scaleX", Prop("number", "transform") }, { "scaleY", Prop("number", "transform") }, { "flipX", Prop("boolean", "transform") }, { "flipY", Prop("boolean", "transform") },
                { "pivot", json { { "type", "array" }, { "items", json { { "type", "number" } } }, { "description", "transform with names: [x, y] point to rotate and scale about (default the parts' bounding box center)" } } }, { "aboutOwnCenter", Prop("boolean", "transform with names: rotate and scale each part about its own center instead of the pivot") } },
         { "action" }),
         [](const json& args)
         {
            const string action = args.value("action", ""s);
            return WithPlayer([args, action](Player& p)
            {
               const string name = args.value("name", ""s);
               if (action == "list")
               {
                  const string type = args.value("type", ""s);
                  vector<IEditable*> parts;
                  for (IEditable* part : p.m_ptable->GetParts())
                     if (type.empty() || StrCompareNoCase(type, ItemTypeName(part->GetItemType())))
                        parts.push_back(part);
                  return TextOutput(json { { "count", parts.size() }, { "parts", PartsJson(parts) } });
               }
               if (action == "get")
               {
                  IEditable* part = FindPart(p, name);
                  if (!part)
                     return ErrorOutput("No part named '"s + name + '\'');
                  if (IsEditing(p))
                     p.m_liveUI->m_editorUI.BeginPartEdit(part);
                  string error;
                  const nlohmann::ordered_json doc = PartDoc(part, error);
                  if (!error.empty())
                     return ErrorOutput(error);
                  json data = ToJson(doc);
                  json result { { "name", part->GetName() }, { "type", ItemTypeName(part->GetItemType()) } };
                  if (json bounds = PartBoundsJson(part); !bounds.is_null())
                     result["bounds"] = bounds;
                  if (part->GetItemType() == eItemPrimitive)
                  {
                     const Primitive* prim = static_cast<Primitive*>(part);
                     if (prim->m_d.m_use3DMesh)
                        result["mesh"] = json { { "vertices", prim->m_mesh.NumVertices() }, { "triangles", prim->m_mesh.NumIndices() / 3 }, { "meshFile", prim->m_d.m_meshFileName } };
                     else
                        result["mesh"] = json { { "builtin", true }, { "sides", prim->m_d.m_Sides } };
                  }
                  if (part->GetItemType() == eItemFlasher)
                     result["flasher"] = FlasherMeshJson(static_cast<Flasher*>(part));
                  json collections = json::array();
                  for (const Collection* c : part->m_vCollection)
                     collections.push_back(c->m_name);
                  if (!collections.empty())
                     result["collections"] = collections;
                  if (args.contains("fields"))
                  {
                     json subset = json::object(), unknown = json::array();
                     for (const string& field : args.value("fields", vector<string>()))
                     {
                        string match = data.contains(field) ? field : ""s;
                        for (const auto& [key, value] : data.items())
                           if (match.empty() && NormalizeFieldName(key) == NormalizeFieldName(field))
                              match = key;
                        if (!match.empty())
                           subset[match] = data[match];
                        else
                           unknown.push_back(field);
                     }
                     result["data"] = subset;
                     if (!unknown.empty())
                        result["unknown"] = unknown;
                  }
                  else
                     result["data"] = data;
                  return TextOutput(result);
               }
               if (action == "links")
               {
                  const string kind = lowerCase(args.value("kind", ""s)), filter = args.value("filter", ""s);
                  if (!kind.empty() && kind != "display" && kind != "segments" && kind != "state")
                     return ErrorOutput("kind is display, segments or state (default: all three)"s);
                  MsgPI::MsgPluginManager& manager = p.m_pluginManager;
                  PinballPlugin::ResURIBuilder builder(&manager, &manager.GetMsgAPI(), p.m_pluginAPI.GetVPXEndPointId());
                  json result = json::object();
                  json plugins = json::array();
                  for (const auto& plugin : builder.GetPlugins(false))
                     if (plugin.loaded)
                     {
                        json j { { "id", plugin.id }, { "name", plugin.name } };
                        if (!plugin.runningGameId.empty())
                           j["runningGame"] = plugin.runningGameId;
                        plugins.push_back(j);
                     }
                  result["loadedPlugins"] = plugins;
                  if (kind.empty() || kind == "display")
                  {
                     json arr = json::array();
                     for (const auto& item : builder.GetDisplayItems(filter, PinballPlugin::DisplayFamily::Any, true))
                     {
                        json j { { "link", builder.MakeDisplayURI(item) }, { "plugin", item.pluginId } };
                        if (item.isDefault)
                           j["default"] = item.defaultDmdOnly == 1 ? "the default display when it is a dot matrix" : item.defaultDmdOnly == 0 ? "the default display when it is a CRT or LCD screen" : "the default display";
                        else
                        {
                           j["width"] = item.width;
                           j["height"] = item.height;
                        }
                        arr.push_back(j);
                     }
                     result["displays"] = arr;
                  }
                  if (kind.empty() || kind == "segments")
                  {
                     json arr = json::array();
                     for (const auto& item : builder.GetSegItems(filter))
                        arr.push_back({ { "link", builder.MakeSegURI(item, -1) }, { "plugin", item.pluginId }, { "elements", item.nElements } });
                     result["segmentDisplays"] = arr;
                  }
                  if (kind.empty() || kind == "state")
                  {
                     const size_t limit = static_cast<size_t>(std::clamp(args.value("max", 100), 1, 2000));
                     json arr = json::array();
                     const auto items = builder.GetStateItems(filter, -1, false, CTLPI_STATE_FORMAT_FLOAT);
                     for (const auto& item : items)
                     {
                        if (arr.size() >= limit)
                           break;
                        json j { { "link", builder.MakeStateURI(item, PinballPlugin::ResURIBuilder::StateLinkMode::Name) }, { "plugin", item.pluginId }, { "group", item.groupName }, { "name", item.name }, { "writable", item.writable } };
                        if (!item.desc.empty())
                           j["description"] = item.desc;
                        if (float value = 0.f; builder.PeekValue(item, value))
                           j["value"] = Round(value, 3);
                        arr.push_back(j);
                     }
                     result["states"] = arr;
                     if (items.size() > arr.size())
                        result["statesTruncated"] = std::to_string(items.size()) + " states match: narrow them with filter (a name, a group such as lamp or gi, a plugin) or raise max";
                  }
                  result["note"] = "links for a Flasher's image_src_link (displays: render_mode 1 DMD or 2 display; segmentDisplays: render_mode 3) and for the state links of flashers and lights (states: lamps, GI strings, solenoids of a running controller). Sources only exist while their plugin is loaded and, for a controller, while its game runs: during a playtest for most tables";
                  return TextOutput(result);
               }
               if (action == "visible")
               {
                  IEditable* part = FindPart(p, name);
                  if (!part)
                     return ErrorOutput("No part named '"s + name + '\'');
                  if (IsEditing(p))
                     p.m_liveUI->m_editorUI.BeginPartEdit(part);
                  const json report = PartVisibilityJson(p, part);
                  if (IsEditing(p))
                     p.m_liveUI->m_editorUI.EndPartEdit(part);
                  return TextOutput(report);
               }
               if (action == "set")
               {
                  IEditable* part = FindPart(p, name);
                  if (!part)
                     return ErrorOutput("No part named '"s + name + '\'');
                  const json data = args.value("data", json::object());
                  if (!data.is_object() || data.empty())
                     return ErrorOutput("data is required: {field: value} with the stored field names, e.g. data: {\"height_top\": 50}; vpx_part schema type:"s + ItemTypeName(part->GetItemType()) + " lists them");
                  json resolved, mapped = json::object();
                  nlohmann::ordered_json doc;
                  json valueNotes = json::object();
                  if (const string err = ResolvePartData(part, data, resolved, mapped, doc, valueNotes); !err.empty())
                     return ErrorOutput(err);
                  json applied = json::array();
                  json spaceChange;
                  if (part->GetItemType() == eItemPartGroup && resolved.contains("space_reference") && resolved["space_reference"].is_number() && IsEditing(p))
                  {
                     // The editor re-expresses the children in the new reference space so that they do not move; a raw field write would shift them
                     auto& editor = p.m_liveUI->m_editorUI;
                     PartGroup* const group = static_cast<PartGroup*>(part);
                     const int before = static_cast<int>(group->m_d.m_spaceReference);
                     const int requested = std::clamp(resolved["space_reference"].get<int>(), 0, 2);
                     if (requested == before)
                        spaceChange = "unchanged";
                     else
                     {
                        editor.BeginUndoGroup();
                        editor.m_layerSpaceChangeOp.Request(group, static_cast<PartGroupData::SpaceReference>(requested));
                        const bool changed = static_cast<int>(group->m_d.m_spaceReference) != before;
                        editor.EndUndoGroup(changed, "MCP layer space: "s + part->GetName());
                        if (changed)
                           applied.push_back("space_reference");
                        spaceChange = changed ? "children re-expressed in the new reference space, they did not move"s : "waiting for the user to confirm in the editor: some children can not be converted exactly"s;
                     }
                     resolved.erase("space_reference");
                     doc["space_reference"] = static_cast<int>(group->m_d.m_spaceReference);
                  }
                  json groupChange;
                  if (resolved.contains("part_group") && resolved["part_group"].is_string() && IsEditing(p))
                  {
                     // Reparenting across reference spaces re-expresses the part's transform so that it does not move (same op as the outliner drag & drop)
                     auto& editor = p.m_liveUI->m_editorUI;
                     const string targetName = resolved["part_group"].get<string>();
                     PartGroup* target = nullptr;
                     if (!targetName.empty())
                     {
                        IEditable* const found = FindPart(p, targetName);
                        if (!found || found->GetItemType() != eItemPartGroup)
                           return ErrorOutput("No part group named '"s + targetName + "'");
                        target = static_cast<PartGroup*>(found);
                     }
                     if (target == part->GetPartGroup())
                        groupChange = "unchanged";
                     else if (!VPX::EditorUI::EditorUI::CanMovePartToGroup(part, target))
                        return ErrorOutput(target == nullptr ? "Only part groups can be moved to the root"s : "A group can not be moved into itself or one of its descendants"s);
                     else
                     {
                        editor.BeginUndoGroup();
                        const bool moved = editor.m_layerSpaceChangeOp.RequestMove({ part }, target);
                        editor.EndUndoGroup(moved, "MCP move to group: "s + part->GetName());
                        if (moved)
                           applied.push_back("part_group");
                        groupChange = moved ? "moved, the transform was re-expressed in the group's reference space so the part did not move"s : "waiting for the user to confirm in the editor: the transform can not be converted exactly"s;
                     }
                     resolved.erase("part_group");
                     if (part->GetPartGroup() != nullptr)
                        doc["part_group"] = part->GetPartGroup()->GetName();
                     else
                        doc.erase("part_group");
                  }
                  string warning;
                  if (!resolved.empty())
                  {
                     RecordEdit(p, part, "MCP set"s);
                     if (IsEditing(p))
                        p.m_liveUI->m_editorUI.BeginPartEdit(part);
                     ApplyPartData(p, part, resolved, doc, applied, warning);
                     if (IsEditing(p))
                        p.m_liveUI->m_editorUI.EndPartEdit(part);
                  }
                  McpServer::Get().NoteEdit();
                  json result { { "name", part->GetName() }, { "applied", applied } };
                  if (!spaceChange.is_null())
                     result["spaceReference"] = spaceChange;
                  if (!groupChange.is_null())
                     result["partGroup"] = groupChange;
                  if (!mapped.empty())
                     result["mapped"] = mapped;
                  if (!valueNotes.empty())
                     result["valueNotes"] = valueNotes;
                  if (!warning.empty())
                     result["reloadWarning"] = warning;
                  return WithLiveCopyNote(p, result);
               }
               if (action == "mesh")
               {
                  IEditable* part = FindPart(p, name);
                  if (!part)
                     return ErrorOutput("No part named '"s + name + '\'');
                  if (part->GetItemType() != eItemPrimitive)
                     return ErrorOutput("mesh works on a Primitive"s);
                  Primitive* prim = static_cast<Primitive*>(part);
                  const string shape = lowerCase(args.value("shape", ""s));
                  const std::filesystem::path path = PathFromUTF8(args.value("path", ""s));
                  if (shape.empty() && path.empty())
                     return ErrorOutput("mesh needs shape (disc | cylinder | box) or path (.obj or .glb)"s);
                  Mesh mesh;
                  string source;
                  if (!path.empty())
                  {
                     if (!FileExists(path))
                        return ErrorOutput("File not found: "s + PathToUTF8(path));
                     const string ext = lowerCase(PathToUTF8(path.extension()));
                     if (ext == ".glb")
                     {
                        std::ifstream in(path, std::ios::binary);
                        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                        if (!mesh.LoadGLB(bytes.data(), bytes.size()))
                           return ErrorOutput("Could not read the GLB mesh"s);
                     }
                     else if (ext == ".obj")
                     {
                        const string units = lowerCase(args.value("units", "vpx"s));
                        if (!mesh.LoadWavefrontObj(path, units == "meters" ? MeshUnits::Meters : MeshUnits::VPUnits))
                           return ErrorOutput("Could not read the OBJ mesh"s);
                     }
                     else
                        return ErrorOutput("mesh path must be a .obj or a .glb file"s);
                     source = PathToUTF8(path.filename());
                  }
                  else
                  {
                     string error;
                     if (!GenerateMesh(mesh, shape, args.value("radius", 25.f), args.value("height", 10.f), args.value("width", 50.f), args.value("depth", 50.f), args.value("segments", 48), error))
                        return ErrorOutput(error);
                     source = "generated:"s + shape;
                  }
                  if (mesh.m_vertices.empty() || mesh.m_indices.empty())
                     return ErrorOutput("The mesh is empty"s);
                  RecordEdit(p, part, "MCP mesh"s, true);
                  if (IsEditing(p))
                     p.m_liveUI->m_editorUI.BeginPartEdit(part);
                  prim->m_mesh = mesh;
                  prim->m_mesh.UpdateBounds();
                  prim->m_d.m_use3DMesh = true;
                  prim->m_d.m_vSize = Vertex3Ds(1.f, 1.f, 1.f);
                  prim->m_d.m_meshFileName = source;
                  RefreshPart(p, part);
                  if (IsEditing(p))
                     p.m_liveUI->m_editorUI.EndPartEdit(part);
                  McpServer::Get().NoteEdit();
                  const Vertex3Ds& lo = prim->m_mesh.m_minAABound;
                  const Vertex3Ds& hi = prim->m_mesh.m_maxAABound;
                  return WithLiveCopyNote(p, json { { "name", part->GetName() }, { "meshFile", source }, { "vertices", prim->m_mesh.NumVertices() }, { "triangles", prim->m_mesh.NumIndices() / 3 },
                     { "bounds", json { { "min", json { { "x", lo.x }, { "y", lo.y }, { "z", lo.z } } }, { "max", json { { "x", hi.x }, { "y", hi.y }, { "z", hi.z } } } } },
                     { "note", "size reset to 1,1,1: the mesh is in VPX units around the part's position" } });
               }
               if (action == "points")
               {
                  IEditable* part = FindPart(p, name);
                  if (!part)
                     return ErrorOutput("No part named '"s + name + '\'');
                  DragPointCurve* curve = GetCurve(part);
                  if (!curve)
                     return ErrorOutput("This part type has no drag points"s);
                  const json points = args.value("points", json::array());
                  bool windingFixed = false;
                  if (!points.empty())
                  {
                     if (const string err = RequireEditor(p); !err.empty())
                        return ErrorOutput(err);
                     if (static_cast<int>(points.size()) < curve->GetMinimumPoints())
                        return ErrorOutput("This part needs at least "s + std::to_string(curve->GetMinimumPoints()) + " points");
                     RecordEdit(p, part, "MCP points"s);
                     SetCurvePoints(curve, points);
                     windingFixed = args.value("fixWinding", true) && FixFlasherWinding(part);
                     RefreshPart(p, part);
                     McpServer::Get().NoteEdit();
                  }
                  json curveEdit;
                  if (const json bevel = args.value("bevel", json()), arc = args.value("arc", json()); !bevel.is_null() || !arc.is_null())
                  {
                     if (const string err = RequireEditor(p); !err.empty())
                        return ErrorOutput(err);
                     const bool isBevel = !bevel.is_null();
                     const json& op = isBevel ? bevel : arc;
                     if (!op.is_object() || (!bevel.is_null() && !arc.is_null()))
                        return ErrorOutput("give one of bevel: {width, steps, points} or arc: {bulge, segments, points} (points: 0-based indices of the drag points, default all)"s);
                     struct State { Vertex3Ds pos; float texCoord; bool smooth, slingshot, autoTexture, uiLocked, uiVisible; };
                     vector<State> states;
                     vector<Vertex2D> pos;
                     vector<bool> slingshot;
                     for (const auto& dp : curve->GetPoints())
                     {
                        states.push_back({ dp->GetVertex(), dp->GetTextureCoordinateU(), dp->IsSmooth(), dp->m_slingshot, dp->IsAutoTextureCoordinate(), dp->m_uiLocked, dp->m_uiVisible });
                        pos.emplace_back(dp->GetVertex().x, dp->GetVertex().y);
                        slingshot.push_back(dp->m_slingshot);
                     }
                     const int n = static_cast<int>(states.size());
                     const bool closed = part->GetItemType() != eItemRamp;
                     vector<int> sel;
                     if (op.contains("points"))
                     {
                        for (const json& index : op["points"])
                           if (index.is_number_integer() && index.get<int>() >= 0 && index.get<int>() < n && std::ranges::find(sel, index.get<int>()) == sel.end())
                              sel.push_back(index.get<int>());
                           else
                              return ErrorOutput("points lists 0-based drag point indices, each once, below "s + std::to_string(n));
                     }
                     else
                        for (int i = 0; i < n; i++)
                           sel.push_back(i);
                     vector<State> out;
                     if (isBevel)
                     {
                        const float width = op.value("width", 0.f);
                        const int steps = std::clamp(op.value("steps", 1), 1, 16);
                        if (width <= 0.f)
                           return ErrorOutput("bevel needs width: how far each corner is cut back along its two edges, in table units"s);
                        vector<int> corners;
                        std::ranges::copy_if(sel, std::back_inserter(corners), [&states](const int i) { return !states[i].smooth; });
                        const VPX::EditorUI::BevelMath::Result result = VPX::EditorUI::BevelMath::Bevel(pos, corners, closed, width, steps);
                        const vector<bool> flags = VPX::EditorUI::BevelMath::SegmentFlags(result, slingshot);
                        int generated = 0;
                        for (size_t i = 0; i < result.points.size(); i++)
                        {
                           State state = states[result.source[i]];
                           if (result.generated[i])
                           {
                              state.pos.x = result.points[i].x;
                              state.pos.y = result.points[i].y;
                              state.smooth = false;
                              generated++;
                           }
                           state.slingshot = flags[i];
                           out.push_back(state);
                        }
                        if (generated == 0)
                           return ErrorOutput("nothing to bevel: the listed points are smooth, straight, or the ends of an open curve"s);
                        curveEdit = json { { "bevel", json { { "width", width }, { "steps", steps }, { "pointsBefore", n }, { "pointsAfter", out.size() } } } };
                        if (result.minRadius != FLT_MAX)
                           curveEdit["bevel"]["smallestRadius"] = Round(result.minRadius);
                     }
                     else
                     {
                        const float bulge = op.value("bulge", 0.f);
                        const int segments = std::clamp(op.value("segments", 8), 2, 64);
                        if (bulge == 0.f)
                           return ErrorOutput("arc needs bulge: how far the middle of each edge is pulled out, in table units (positive: outward of a closed outline, to the right of the direction of an open ramp; negative the other way)"s);
                        const vector<int> edges = VPX::EditorUI::ArcMath::SelectedEdges(n, sel, closed);
                        if (edges.empty())
                           return ErrorOutput("arc works on edges: list both end points of each edge to turn into an arc"s);
                        const VPX::EditorUI::ArcMath::Result result = VPX::EditorUI::ArcMath::Arc(pos, edges, closed, bulge, segments);
                        for (size_t i = 0; i < result.points.size(); i++)
                        {
                           State state = states[result.source[i]];
                           if (result.generated[i])
                           {
                              const State& end = states[(result.source[i] + 1) % n];
                              const float t = result.along[i];
                              state.pos.x = result.points[i].x;
                              state.pos.y = result.points[i].y;
                              state.pos.z += (end.pos.z - state.pos.z) * t;
                              state.texCoord += (end.texCoord - state.texCoord) * t;
                              state.smooth = false;
                           }
                           out.push_back(state);
                        }
                        curveEdit = json { { "arc", json { { "bulge", bulge }, { "segments", segments }, { "edges", result.arcs }, { "pointsBefore", n }, { "pointsAfter", out.size() } } } };
                        if (result.clamped)
                           curveEdit["arc"]["clamped"] = "the bulge was limited to a half circle on at least one edge";
                     }
                     RecordEdit(p, part, isBevel ? "MCP bevel"s : "MCP arc"s);
                     curve->ClearPoints();
                     for (const State& state : out)
                     {
                        auto point = std::make_unique<DragPoint>(curve, state.pos.x, state.pos.y, state.pos.z, state.smooth);
                        point->SetTextureCoordinateU(state.texCoord);
                        point->SetAutoTextureCoordinate(state.autoTexture);
                        point->m_slingshot = state.slingshot;
                        point->m_uiLocked = state.uiLocked;
                        point->m_uiVisible = state.uiVisible;
                        curve->PushPoint(std::move(point));
                     }
                     RefreshPart(p, part);
                     McpServer::Get().NoteEdit();
                  }
                  json result { { "name", part->GetName() }, { "type", ItemTypeName(part->GetItemType()) }, { "points", PointsJson(curve) } };
                  if (!curveEdit.is_null())
                     result["curveEdit"] = curveEdit;
                  if (part->GetItemType() == eItemFlasher)
                     result["flasher"] = FlasherMeshJson(static_cast<Flasher*>(part));
                  if (windingFixed)
                     result["windingFixed"] = "the points ran clockwise, which a flasher does not draw: their order was reversed";
                  return TextOutput(result);
               }
               if (action == "schema")
               {
                  const ItemTypeEnum type = ItemTypeFromName(args.value("type", ""s));
                  if (type == eItemInvalid || type == eItemTable || type == eItemDragPoint || type == eItemCollection || type == eItemLightCenter || type == eItemBall || type == eItemPartGroup)
                     return ErrorOutput("Unsupported part type"s);
                  IEditable* sample = nullptr;
                  for (IEditable* part : p.m_ptable->GetParts())
                     if (part->GetItemType() == type)
                        sample = part;
                  IEditable* temp = sample ? nullptr : EditableRegistry::CreateAndInit(type, p.m_ptable, 0.f, 0.f);
                  IEditable* part = sample ? sample : temp;
                  if (!part)
                     return ErrorOutput("Could not instantiate the part type"s);
                  if (temp)
                     temp->AddRef();
                  string error;
                  const nlohmann::ordered_json doc = ObjectDoc(static_cast<int>(type), [part](JSONObjectWriter& w) { part->Save(w, false); }, error);
                  json fields = json::object();
                  for (const auto& [key, value] : doc.items())
                     fields[key] = json { { "type", value.type_name() }, { sample ? "example" : "default", ToJson(value) } };
                  for (auto& [key, value] : fields.items())
                  {
                     if (const char* meaning = FieldMeaning(type, key))
                        value["meaning"] = meaning;
                     if (const FieldEnum* e = FieldEnumOf(type, key))
                     {
                        json values = json::object();
                        for (const auto& [v, label] : e->values)
                           values[std::to_string(v)] = label;
                        value["values"] = values;
                        if (!e->strict)
                           value["valuesOpen"] = true;
                     }
                     if (const FieldRange* r = FieldRangeOf(type, key))
                     {
                        json range { { "min", Round(r->min, 4) }, { "enforced", r->enforced } };
                        if (r->max != FLT_MAX)
                           range["max"] = Round(r->max, 4);
                        if (r->note[0])
                           range["note"] = r->note;
                        value["range"] = range;
                     }
                  }
                  if (temp)
                     temp->Release();
                  json result { { "type", ItemTypeName(type) }, { "fields", fields }, { "fieldsFrom", sample ? "the part '"s + sample->GetName() + "' of this table" : "a freshly created part with default values"s },
                     { "hasDragPoints", type == eItemSurface || type == eItemRamp || type == eItemRubber || type == eItemFlasher || type == eItemLight || type == eItemTrigger } };
                  return TextOutput(result);
               }
               if (action == "transform" && (args.contains("names") || args.contains("pivot") || args.contains("aboutOwnCenter")))
               {
                  // Several parts as one group (the editor's selection transforms): rotate and scale about a shared pivot or each part's own center
                  if (const string err = RequireEditor(p); !err.empty())
                     return ErrorOutput(err);
                  vector<string> names = args.value("names", vector<string>());
                  if (!name.empty() && std::ranges::find(names, name) == names.end())
                     names.push_back(name);
                  vector<IEditable*> parts;
                  json notFound = json::array();
                  for (const string& n : names)
                     if (IEditable* part = FindPart(p, n); part && !p.m_ptable->IsPlayerSessionObject(part))
                        parts.push_back(part);
                     else
                        notFound.push_back(n);
                  if (parts.empty())
                     return ErrorOutput("names lists no part of this table"s);
                  const float dx = args.value("dx", 0.f), dy = args.value("dy", 0.f), rotate = args.value("rotate", 0.f);
                  const float scaleX = args.value("scale", args.value("scaleX", 1.f)), scaleY = args.value("scale", args.value("scaleY", 1.f));
                  const bool flipX = args.value("flipX", false), flipY = args.value("flipY", false), ownCenter = args.value("aboutOwnCenter", false);
                  auto& editor = p.m_liveUI->m_editorUI;
                  editor.SelectParts(parts);
                  Vertex2D pivot = editor.GetMultiSelCenter();
                  if (const json pv = args.value("pivot", json()); pv.is_array() && pv.size() == 2 && pv[0].is_number() && pv[1].is_number())
                     pivot = Vertex2D(pv[0].get<float>(), pv[1].get<float>());
                  else if (!pv.is_null())
                     return ErrorOutput("pivot must be [x, y] in table units"s);
                  json done = json::array();
                  editor.BeginUndoGroup();
                  if (rotate != 0.f)
                  {
                     editor.RotateMultiSel(rotate, pivot, ownCenter);
                     done.push_back("rotate");
                  }
                  if (scaleX != 1.f || scaleY != 1.f)
                  {
                     editor.ScaleMultiSel(scaleX, scaleY, pivot, ownCenter);
                     done.push_back("scale");
                  }
                  if (flipX)
                  {
                     editor.FlipSelection(true);
                     done.push_back("flipX (around the selection center)");
                  }
                  if (flipY)
                  {
                     editor.FlipSelection(false);
                     done.push_back("flipY (around the selection center)");
                  }
                  if (dx != 0.f || dy != 0.f)
                  {
                     editor.TranslateMultiSel(Vertex2D(dx, dy));
                     done.push_back("translate");
                  }
                  editor.EndUndoGroup(!done.empty(), "MCP transform: "s + (parts.size() == 1 ? parts.front()->GetName() : std::to_string(parts.size()) + " parts"));
                  if (!done.empty())
                     McpServer::Get().NoteEdit();
                  json centers = json::object();
                  for (IEditable* part : parts)
                  {
                     const Vertex2D c = part->GetCenter();
                     centers[part->GetName()] = { { "x", Round(c.x) }, { "y", Round(c.y) } };
                  }
                  return TextOutput(json { { "done", done }, { "pivot", { { "x", Round(pivot.x) }, { "y", Round(pivot.y) } } }, { "aboutOwnCenter", ownCenter }, { "centers", centers }, { "notFound", notFound }, { "note", "one undo record; the parts stay selected in the editor" } });
               }
               if (action == "transform")
               {
                  IEditable* part = FindPart(p, name);
                  if (!part)
                     return ErrorOutput("No part named '"s + name + '\'');
                  const float dx = args.value("dx", 0.f), dy = args.value("dy", 0.f), rotate = args.value("rotate", 0.f);
                  const float scaleX = args.value("scale", args.value("scaleX", 1.f)), scaleY = args.value("scale", args.value("scaleY", 1.f));
                  const bool flipX = args.value("flipX", false), flipY = args.value("flipY", false);
                  RecordEdit(p, part, "MCP transform"s);
                  json done = json::array(), skipped = json::array();
                  const Vertex2D center = part->GetCenter();
                  if (DragPointCurve* curve = GetCurve(part))
                  {
                     if (rotate != 0.f) { curve->RotatePoints(rotate, center); done.push_back("rotate"); }
                     if (scaleX != 1.f || scaleY != 1.f) { curve->ScalePoints(scaleX, scaleY, center); done.push_back("scale"); }
                     if (flipX) { curve->FlipPointX(center); done.push_back("flipX"); }
                     if (flipY) { curve->FlipPointY(center); done.push_back("flipY"); }
                     if (FixFlasherWinding(part))
                        done.push_back("point order reversed (a flasher only draws counter-clockwise points)");
                  }
                  else
                  {
                     string error;
                     nlohmann::ordered_json doc = PartDoc(part, error);
                     if (!error.empty())
                        return ErrorOutput(error);
                     json patch = json::object();
                     if (rotate != 0.f)
                     {
                        bool rotated = false;
                        for (const char* field : { "rotation", "orientation", "rot_z", "prim_rot1_z", "start_angle" })
                           if (doc.contains(field) && doc[field].is_number())
                           {
                              patch[field] = doc[field].get<float>() + rotate;
                              if (string(field) == "start_angle" && doc.contains("end_angle"))
                                 patch["end_angle"] = doc["end_angle"].get<float>() + rotate;
                              done.push_back("rotate ("s + field + ')');
                              rotated = true;
                              break;
                           }
                        if (!rotated)
                           skipped.push_back("rotate: no rotation field");
                     }
                     if (scaleX != 1.f || scaleY != 1.f)
                     {
                        bool scaled = false;
                        if (doc.contains("size") && doc["size"].is_object() && doc["size"].contains("x") && doc["size"].contains("y"))
                        {
                           patch["size"] = json { { "x", doc["size"]["x"].get<float>() * scaleX }, { "y", doc["size"]["y"].get<float>() * scaleY } };
                           done.push_back("scale (size)");
                           scaled = true;
                        }
                        else
                           for (const char* field : { "radius", "length", "flipper_radius_max" })
                              if (doc.contains(field) && doc[field].is_number())
                              {
                                 patch[field] = doc[field].get<float>() * scaleX;
                                 done.push_back("scale ("s + field + ')');
                                 scaled = true;
                                 break;
                              }
                        if (!scaled)
                           skipped.push_back("scale: no size field");
                     }
                     if (flipX || flipY)
                        skipped.push_back("flip: only drag point parts can be flipped");
                     if (!patch.empty())
                     {
                        MergeDoc(doc, patch);
                        ReloadPart(p, part, doc);
                     }
                  }
                  if (dx != 0.f || dy != 0.f)
                  {
                     TranslatePart(p, part, dx, dy);
                     done.push_back("translate");
                  }
                  else
                     RefreshPart(p, part);
                  McpServer::Get().NoteEdit();
                  const Vertex2D after = part->GetCenter();
                  return WithLiveCopyNote(p, json { { "name", part->GetName() }, { "done", done }, { "skipped", skipped }, { "center", { { "x", Round(after.x) }, { "y", Round(after.y) } } } });
               }
               if (const string err = RequireEditor(p); !err.empty())
                  return ErrorOutput(err);
               if (action == "add")
               {
                  const ItemTypeEnum type = ItemTypeFromName(args.value("type", ""s));
                  if (type == eItemInvalid || type == eItemTable || type == eItemDragPoint || type == eItemCollection || type == eItemLightCenter || type == eItemBall)
                     return ErrorOutput("Unsupported part type"s);
                  if (type != eItemPartGroup && (!args.contains("x") || !args.contains("y")))
                     return ErrorOutput("x and y are required"s);
                  const json data = args.value("data", json::object());
                  if (!data.is_object())
                     return ErrorOutput("data must be an object of stored fields"s);
                  json resolved = json::object(), mapped = json::object(), valueNotes = json::object();
                  if (!data.empty())
                  {
                     IEditable* probe = EditableRegistry::CreateAndInit(type, p.m_ptable, 0.f, 0.f);
                     if (!probe)
                        return ErrorOutput("Could not instantiate the part type"s);
                     probe->AddRef();
                     nlohmann::ordered_json probeDoc;
                     const string err = ResolvePartData(probe, data, resolved, mapped, probeDoc, valueNotes);
                     probe->Release();
                     if (!err.empty())
                        return ErrorOutput(err);
                  }
                  IEditable* part = p.m_liveUI->m_editorUI.CreatePart(type, Vertex2D(args.value("x", 0.f), args.value("y", 0.f)));
                  if (!part)
                     return ErrorOutput("Part creation failed"s);
                  if (!name.empty() && FindPart(p, name) == nullptr)
                     p.m_ptable->RenamePart(part, name);
                  // The editor hides a new empty group in its outliner until a visible part lands in it; parts added through the tools would then stay hidden
                  if (type == eItemPartGroup)
                  {
                     part->SetUIVisible(true);
                     if (!resolved.contains("part_group"))
                        part->SetPartGroup(nullptr);
                  }
                  json points = args.value("points", json::array());
                  if (args.contains("centerline"))
                  {
                     const json centerline = args.value("centerline", json::array());
                     if (!centerline.is_array() || centerline.size() < 2)
                        return ErrorOutput("centerline needs at least 2 points"s);
                     points = BandOutline(centerline, args.value("thickness", 6.f));
                  }
                  else if (args.contains("circle"))
                     points = CircleOutline(args.value("x", 0.f), args.value("y", 0.f), args.value("circle", 25.f), args.value("segments", 16));
                  json applied = json::array();
                  string warning;
                  p.m_liveUI->m_editorUI.BeginPartEdit(part);
                  if (!resolved.empty())
                  {
                     string error;
                     nlohmann::ordered_json doc = PartDoc(part, error);
                     if (!error.empty())
                        return ErrorOutput(error);
                     ApplyPartData(p, part, resolved, doc, applied, warning);
                  }
                  bool windingFixed = false;
                  if (DragPointCurve* curve = GetCurve(part); curve && !points.empty())
                  {
                     SetCurvePoints(curve, points);
                     windingFixed = args.value("fixWinding", true) && FixFlasherWinding(part);
                  }
                  if (!points.empty())
                     RefreshPart(p, part);
                  p.m_liveUI->m_editorUI.EndPartEdit(part);
                  McpServer::Get().NoteEdit();
                  json j { { "name", part->GetName() }, { "type", ItemTypeName(type) }, { "applied", applied } };
                  {
                     string error;
                     const nlohmann::ordered_json stored = PartDoc(part, error);
                     if (error.empty())
                        for (const char* field : { "visible", "collidable", "ui_visible" })
                           if (stored.contains(field))
                              j["stored"][field] = ToJson(stored[field]);
                  }
                  if (!mapped.empty())
                     j["mapped"] = mapped;
                  if (!valueNotes.empty())
                     j["valueNotes"] = valueNotes;
                  {
                     auto colliders = CollidersByPart(p);
                     vector<Vertex3Ds> samples;
                     FRect3D box(FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX);
                     for (const HitObject* ho : colliders[part])
                        if (ho->m_ObjType < eTrigger)
                        {
                           SampleCollider(ho, samples);
                           box.Extend(ho->m_hitBBox);
                        }
                     const auto nearBox = [&box](const FRect3D& b)
                     {
                        return b.left <= box.right + 1.f && b.right >= box.left - 1.f && b.top <= box.bottom + 1.f && b.bottom >= box.top - 1.f
                           && min(b.zlow, b.zhigh) <= max(box.zlow, box.zhigh) + 1.f && max(b.zlow, b.zhigh) >= min(box.zlow, box.zhigh) - 1.f;
                     };
                     json overlaps = json::array();
                     if (!samples.empty() && samples.size() < 20000)
                        for (const auto& entry : colliders)
                        {
                           IEditable* const other = entry.first;
                           const vector<const HitObject*>& otherColliders = entry.second;
                           if (other == part || other == p.m_ptable)
                              continue;
                           vector<const HitObject*> solid;
                           for (const HitObject* ho : otherColliders)
                              if (ho->m_ObjType < eTrigger && nearBox(ho->m_hitBBox))
                                 solid.push_back(ho);
                           if (solid.empty())
                              continue;
                           float best = FLT_MAX;
                           for (const Vertex3Ds& pt : samples)
                              if ((best = min(best, DistanceToPart(other, solid, pt).distance)) < 1.f)
                                 break;
                           if (best < 1.f)
                              overlaps.push_back(other->GetName());
                        }
                     if (!overlaps.empty())
                        j["overlaps"] = overlaps;
                  }
                  if (!warning.empty())
                     j["warning"] = warning;
                  if (DragPointCurve* curve = GetCurve(part))
                     j["points"] = PointsJson(curve);
                  if (type == eItemFlasher)
                     j["flasher"] = FlasherMeshJson(static_cast<Flasher*>(part));
                  if (windingFixed)
                     j["windingFixed"] = "the points ran clockwise, which a flasher does not draw: their order was reversed";
                  return TextOutput(j);
               }
               if (action == "order")
               {
                  vector<IEditable*> parts;
                  json missing = json::array();
                  for (const string& n : args.value("names", vector<string>()))
                     if (IEditable* part = FindPart(p, n); part && !p.m_ptable->IsPlayerSessionObject(part))
                        parts.push_back(part);
                     else
                        missing.push_back(n);
                  if (parts.empty())
                     return ErrorOutput("names lists no part of this table"s);
                  // ReorderParts appends its list back to front: the previous full order, reversed, restores it
                  PinTable* const table = p.m_ptable;
                  const auto capture = [table]() -> std::function<void()>
                  {
                     vector<IEditable*> order = table->GetParts();
                     std::ranges::reverse(order);
                     return [table, order]() { table->ReorderParts(order); };
                  };
                  RecordAction(p, table, "MCP draw order"s, capture(), capture);
                  std::ranges::reverse(parts);
                  p.m_ptable->ReorderParts(parts);
                  McpServer::Get().NoteEdit();
                  json order = json::array();
                  for (IEditable* part : p.m_ptable->GetParts())
                     if (std::ranges::find(parts, part) != parts.end())
                        order.push_back(part->GetName());
                  return TextOutput(json { { "order", order }, { "notFound", missing }, { "note", "these parts are now drawn last, in this order" } });
               }
               if (action == "delete")
               {
                  const bool force = args.value("force", false), withChildren = args.value("withChildren", false);
                  vector<IEditable*> parts;
                  json notDeleted = json::array();
                  auto isInside = [](IEditable* part, IEditable* group)
                  {
                     for (PartGroup* g = part->GetPartGroup(); g; g = g->GetPartGroup())
                        if (g == group)
                           return true;
                     return false;
                  };
                  auto add = [&parts](IEditable* part)
                  {
                     if (std::ranges::find(parts, part) == parts.end())
                        parts.push_back(part);
                  };
                  for (const string& n : args.value("names", vector<string>()))
                  {
                     IEditable* part = FindPart(p, n);
                     string reason;
                     if (!part)
                        reason = "not found";
                     else if (part->GetItemType() == eItemBall)
                        reason = "balls are destroyed with vpx_ball";
                     else if (p.m_ptable->IsPlayerSessionObject(part))
                        reason = "owned by the play session";
                     else if (part->GetItemType() == eItemPrimitive && static_cast<Primitive*>(part)->IsPlayfield() && !force)
                        reason = "the playfield mesh: pass force:true to remove it";
                     else if (part->GetItemType() == eItemPartGroup)
                     {
                        vector<IEditable*> children;
                        for (IEditable* other : p.m_ptable->GetParts())
                           if (other != part && isInside(other, part))
                              children.push_back(other);
                        if (!children.empty() && !withChildren)
                           reason = "group with "s + std::to_string(children.size()) + " parts: pass withChildren:true or delete them first";
                        else
                        {
                           for (IEditable* child : children)
                              add(child);
                           add(part);
                        }
                     }
                     else
                        add(part);
                     if (!reason.empty())
                        notDeleted.push_back({ { "name", n }, { "reason", reason } });
                  }
                  const size_t before = p.m_ptable->GetParts().size();
                  p.m_liveUI->m_editorUI.DeleteParts(parts);
                  const size_t deleted = before - p.m_ptable->GetParts().size();
                  if (deleted > 0)
                     McpServer::Get().NoteEdit();
                  json result { { "deleted", deleted }, { "notDeleted", notDeleted } };
                  if (deleted < parts.size())
                     result["warning"] = std::to_string(parts.size() - deleted) + " of the named parts were left in place: the editor does not delete parts that are in no part group (move them into one with part_group first)";
                  return TextOutput(result);
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

   tools.push_back(ToolDef { "vpx_geometry",
      "Geometry queries over the table's collision shapes and meshes. Actions: vertices (name; max sampled vertices, default 500, or path to write the whole transformed mesh as .obj) | at (x, y, z: the parts a ball centered there touches) | near (x, y, radius: the parts within radius with distance and closest point) | gap (names: [a, b]: closest distance between two collision shapes, a ball needs 50) | rampcheck (names: ramp or group names, default every ramp; joinDistance: joins, surface ends, wires in a ball path, wire crossings, warnings). Details: vpx_status guide:\"geometry\".",
      Schema({ { "action", Prop("string", "vertices | at | near | gap | rampcheck") }, { "name", Prop("string", "vertices: part name") }, { "names", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "gap: the two parts; rampcheck: ramp or group names" } } },
                { "max", Prop("integer", "vertices: most vertices returned, default 500") }, { "path", Prop("string", "vertices: .obj file to write") }, { "x", Prop("number", "at, near") }, { "y", Prop("number", "at, near") },
                { "z", Prop("number", "at, near: height of the point, default 25 (a resting ball's center)") }, { "radius", Prop("number", "near: search radius, default 150") }, { "joinDistance", Prop("number", "rampcheck: how far apart two ramp ends may be and still count as a join, default 20") }, { "paths", Prop("boolean", "rampcheck: also return each ramp's floor centerline as [x, y, z, halfWidth] every ~10 units") } },
         { "action" }),
      [](const json& args)
      {
         const string action = args.value("action", ""s);
         return WithPlayer([args, action](Player& p)
         {
            const string name = args.value("name", ""s);
            if (action == "rampcheck")
            {
               vector<Ramp*> ramps;
               json missing = json::array();
               const json names = args.value("names", json::array());
               for (IEditable* part : p.m_ptable->GetParts())
               {
                  if (part->GetItemType() != eItemRamp)
                     continue;
                  bool wanted = names.empty();
                  for (const json& n : names)
                     if (n.is_string() && (StrCompareNoCase(n.get<string>(), part->GetName()) || (part->GetPartGroup() && StrCompareNoCase(n.get<string>(), part->GetPartGroup()->GetName()))))
                        wanted = true;
                  if (wanted)
                     ramps.push_back(static_cast<Ramp*>(part));
               }
               for (const json& n : names)
                  if (n.is_string() && !FindPart(p, n.get<string>()) && std::ranges::none_of(p.m_ptable->GetParts(), [&](IEditable* part) { return part->GetPartGroup() && StrCompareNoCase(n.get<string>(), part->GetPartGroup()->GetName()); }))
                     missing.push_back(n);
               if (ramps.empty())
                  return ErrorOutput("rampcheck found no ramps"s + (names.empty() ? ""s : " among the given names or groups"s));
               json result = RampCheckJson(p, ramps, 25.f, args.value("joinDistance", 20.f), args.value("paths", false));
               if (!missing.empty())
                  result["unknownNames"] = missing;
               return TextOutput(result);
            }
            if (action == "vertices")
            {
               IEditable* part = FindPart(p, name);
               if (!part)
                  return ErrorOutput("No part named '"s + name + '\'');
               vector<Vertex3Ds> world;
               vector<unsigned int> indices;
               if (string error; !PartWorldMesh(part, world, indices, error))
                  return ErrorOutput("vertices: "s + error);
               const PartGroup* group = part->GetPartGroup();
               const bool roomSpace = group != nullptr && group->GetReferenceSpace() != PartGroupData::SpaceReference::Playfield;
               json result { { "name", part->GetName() }, { "vertexCount", world.size() }, { "triangles", indices.size() / 3 }, { "bounds", PartBoundsJson(part) },
                  { "space", roomSpace ? "table coordinates, placed through the table's default room transform (room space group)" : "table coordinates" } };
               if (part->GetItemType() == eItemRamp && static_cast<Ramp*>(part)->IsHabitrail())
                  result["note"] = "the visible mesh; a wire ramp collides as flat walls along its width, not as these wires"s;
               if (const string out = args.value("path", ""s); !out.empty())
               {
                  std::ofstream obj(PathFromUTF8(out));
                  if (!obj)
                     return ErrorOutput("Could not write "s + out);
                  obj << "# " << part->GetName() << " world vertices, VPX table units, z up\n";
                  for (const Vertex3Ds& v : world)
                     obj << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
                  for (size_t i = 0; i + 2 < indices.size(); i += 3)
                     obj << "f " << indices[i] + 1 << ' ' << indices[i + 1] + 1 << ' ' << indices[i + 2] + 1 << '\n';
                  result["written"] = out;
                  return TextOutput(result);
               }
               const size_t maxCount = static_cast<size_t>(std::clamp(args.value("max", 500), 1, 20000));
               const size_t step = (world.size() + maxCount - 1) / maxCount;
               json list = json::array();
               for (size_t i = 0; i < world.size(); i += max(step, static_cast<size_t>(1)))
                  list.push_back({ Round(world[i].x), Round(world[i].y), Round(world[i].z) });
               result["vertices"] = list;
               if (step > 1)
                  result["sampled"] = "every "s + std::to_string(step) + "th vertex; pass path to write them all as .obj";
               return TextOutput(result);
            }
            if (action == "at" || action == "near")
            {
               if (!args.contains("x") || !args.contains("y"))
                  return ErrorOutput("x and y are required"s);
               const float ballRadius = 25.f;
               const Vertex3Ds point(args.value("x", 0.f), args.value("y", 0.f), args.value("z", ballRadius));
               const float radius = args.value("radius", action == "at" ? ballRadius : 150.f);
               json parts = json::array();
               for (const PartDistance& d : PartsNear(p, point, radius))
                  parts.push_back(PartDistanceJson(d, ballRadius));
               return TextOutput(json { { "point", json { { "x", Round(point.x) }, { "y", Round(point.y) }, { "z", Round(point.z) } } }, { "radius", radius }, { "parts", parts },
                  { "note", "distances are from the point to each part's collision shape; gapForBall subtracts a standard ball radius (25): a ball centered at the point touches parts with gapForBall <= 0" } });
            }
            if (action == "gap")
            {
               const vector<string> names = args.value("names", vector<string>());
               if (names.size() != 2)
                  return ErrorOutput("gap needs names: [partA, partB]"s);
               IEditable* a = FindPart(p, names[0]);
               IEditable* b = FindPart(p, names[1]);
               if (!a || !b)
                  return ErrorOutput("No part named '"s + (a ? names[1] : names[0]) + '\'');
               auto colliders = CollidersByPart(p);
               if (colliders[a].empty() || colliders[b].empty())
                  return ErrorOutput("'"s + (colliders[a].empty() ? names[0] : names[1]) + "' has no collision shape (not collidable, or not set up yet)");
               vector<Vertex3Ds> samples;
               for (const HitObject* ho : colliders[a])
                  SampleCollider(ho, samples);
               PartDistance best;
               Vertex3Ds from;
               for (const Vertex3Ds& s : samples)
                  if (PartDistance d = DistanceToPart(b, colliders[b], s); d.distance < best.distance)
                  {
                     best = d;
                     from = s;
                  }
               const float gap = best.distance;
               return TextOutput(json { { "gap", Round(gap) }, { "ballFits", gap >= 50.f }, { "from", json { { "part", a->GetName() }, { "x", Round(from.x) }, { "y", Round(from.y) }, { "z", Round(from.z) } } },
                  { "to", json { { "part", b->GetName() }, { "x", Round(best.closest.x) }, { "y", Round(best.closest.y) }, { "z", Round(best.closest.z) } } },
                  { "note", "closest distance between the two collision shapes (sampled, about 1 unit precision); a standard ball needs 50 to pass, less than that traps or blocks it" } });
            }
            return ErrorOutput("Unknown action"s);
         });
      }, 300.0 });

}

}
