// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

void AddPartTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_part", "Table parts, read and written through their stored definition: every field the table file keeps, named as in the VPZ packs (center {x,y}, height_top, material, image, visible, part_group, ui_locked...), nothing is gated by the script interface. "
         "'list' (optional type filter: Flipper, Wall, Ramp, Light, Kicker, Bumper, Primitive, PartGroup...); 'get' the definition of a part (optional fields: [...] for a subset; primitives also report their mesh) and its bounds: the world axis aligned box {min, max, size} in table coordinates (z up) after every transform, from the mesh, outline or collision shape (from); "
         "'vertices' returns the world mesh vertices of a part: a primitive after its full transform (size, prim_trans, prim_rot1 Z then Y then X, prim_rot2 Z then Y then X, position), ramps (incl. wire ramps), rubbers, walls, flippers, bumpers, gates, spinners, targets, kickers and triggers as VPX builds their visible mesh, all with the cabinet placement for parts in a cabinet or room space group, up to max (default 500) evenly sampled, or writes the transformed mesh to path as a .obj; "
         "'set' stored fields (data: {field: value}, merged into the definition, objects merge field by field so size: {x: 60} keeps y and z, then reloaded in place with meshes, physics and render state rebuilt). Script-style names are mapped when only case and underscores differ (HeightTop -> height_top, reported in mapped); unknown names are refused with suggestions before anything changes. "
         "'add' a part (type, x, y, optional name, data and points; type PartGroup creates a group; centerline + thickness generates a band outline for a Wall, circle + segments a round outline) in the editor; 'delete' parts (names; a non-empty group needs withChildren:true, the playfield mesh needs force:true); 'points' reads or replaces the drag points of a Wall/Ramp/Rubber/Light/Trigger/Flasher (points: [{x,y,z?,smooth?,slingshot?}], ramps go from the first point (bottom) to the last (top)). "
         "'mesh' gives a Primitive a real mesh: shape disc | cylinder | box with radius, height, width, depth, segments in VPX units (z up, bottom at the part's position z, top face mapped to the whole image, size reset to 1,1,1: the built-in shape's cap UVs only cover a quarter of the image, use this for discs, posts, plastics and targets), "
         "or path to a .obj (units: vpx, the default, z up; or meters: Blender's meters, +Y up) or a .glb (meters, +Y up). "
         "Images, materials and sounds are referenced by name: vpx_image add imports a file and returns its name, vpx_material list/add, vpx_sound list. "
         "'at' lists the parts whose collision shape a ball centered at x,y (z default 25) touches; 'near' the parts within radius (default 150) of x,y with their distance and closest point; 'gap' measures the closest distance between two parts' collision shapes (names: [a, b]), a ball needs 50. "
         "'rampcheck' checks ramps (names: ramp or group names, default every ramp): joins between ramp ends (position gap, lateral offset, height step, direction change, a narrower ramp whose edge sticks into the wider one's path, visible wire ends that do not line up), ramp ends that meet the playfield, a wall or a primitive (surfaceEnds: the surface there and the height step), visible wires of one ramp inside the path of a ball riding another, and wires of two ramps that touch or cross; warnings sums them up. "
         "'order' moves parts (names) to the end of the drawing order, in the given order: the last one is drawn last, over the others (decals, flashers and transparent parts depend on it); 'list' reports the current order. "
         "'schema' lists every stored field of a part type with its JSON type and the default (or an example from this table): call it before setting fields of a type for the first time. 'transform' moves (dx, dy), rotates (degrees), scales (scale or scaleX/scaleY) or flips (flipX, flipY) a part about its center: drag point parts fully, others through their rotation and size fields. "
         "add, delete, mesh and points need the editor open; set, add, delete, points, mesh and transform are undoable (vpx_editor undo). Save often (vpx_editor save): vpx_status reports mcpEditsSinceSave. Bare added parts have no materials: prefer vpx_library clone for complete assemblies.",
         Schema({ { "action", Prop("string", "list | get | set | add | delete | points | schema | transform | mesh | vertices | order | at | near | gap | rampcheck") }, { "joinDistance", Prop("number", "rampcheck: how far apart two ramp ends may be and still count as a join, default 20") }, { "max", Prop("integer", "vertices: most vertices returned, default 500") }, { "z", Prop("number", "at, near: height of the point, default 25 (a resting ball's center)") },
                  { "shape", Prop("string", "mesh: disc | cylinder | box") }, { "radius", Prop("number", "mesh: disc/cylinder radius; at, near: search radius; add: circle outline radius") }, { "height", Prop("number", "mesh: height") }, { "width", Prop("number", "mesh: box width (x)") }, { "depth", Prop("number", "mesh: box depth (y)") }, { "segments", Prop("integer", "mesh: disc/cylinder segments, default 48") },
                  { "path", Prop("string", "mesh: .obj or .glb file to load; vertices: .obj file to write") }, { "units", Prop("string", "mesh .obj: vpx (default) or meters") }, { "withChildren", Prop("boolean", "delete: also delete the parts of a group") },
                  { "centerline", PointsProp("add: build the outline of a band (wall, guide, rim) of thickness around these points") }, { "thickness", Prop("number", "add with centerline: band width, default 6") },
                  { "circle", Prop("number", "add: radius of a round outline centered at x,y for a drag point part (Light insert, Trigger, Wall post, Flasher)") },
                  { "dx", Prop("number", "transform") }, { "dy", Prop("number", "transform") }, { "rotate", Prop("number", "transform: degrees") }, { "scale", Prop("number", "transform: uniform") }, { "scaleX", Prop("number", "") }, { "scaleY", Prop("number", "") }, { "flipX", Prop("boolean", "transform") }, { "flipY", Prop("boolean", "transform") }, { "name", Prop("string", "Part name (get, set, points, add: optional new name)") }, { "names", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "delete, order; gap: the two parts" } } }, { "force", Prop("boolean", "delete: also remove parts that are not in any group (e.g. a stray playfield_mesh saved by an older build)") },
                  { "type", Prop("string", "list: filter; add: part type") }, { "x", Prop("number", "add") }, { "y", Prop("number", "add") },
                  { "fields", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "get: subset of stored fields" } } }, { "data", json { { "type", "object" }, { "description", "set, add: stored fields to merge, e.g. {\"height_top\": 50} (see schema)" } } },
                  { "points", PointsProp("add, points") } },
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
                  if (args.contains("fields") || args.contains("properties"))
                  {
                     json subset = json::object(), unknown = json::array();
                     vector<string> wanted = args.value("fields", vector<string>());
                     for (const string& field : args.value("properties", vector<string>()))
                        wanted.push_back(field);
                     for (const string& field : wanted)
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
                  json result = RampCheckJson(p, ramps, 25.f, args.value("joinDistance", 20.f));
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
                  const bool cabinetSpace = group != nullptr && group->GetReferenceSpace() != PartGroupData::SpaceReference::SR_PLAYFIELD;
                  json result { { "name", part->GetName() }, { "vertexCount", world.size() }, { "triangles", indices.size() / 3 }, { "bounds", PartBoundsJson(part) },
                     { "space", cabinetSpace ? "table coordinates, placed through the table's default cabinet transform (cabinet/room space group)" : "table coordinates" } };
                  if (part->GetItemType() == eItemRamp && static_cast<Ramp*>(part)->IsHabitrail())
                     result["note"] = "the visible mesh; a wire ramp collides as flat walls along its width, not as these wires"s;
                  const auto r = [](float f) { return std::strtod(std::format("{:.1f}", f).c_str(), nullptr); };
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
                     list.push_back({ r(world[i].x), r(world[i].y), r(world[i].z) });
                  result["vertices"] = list;
                  if (step > 1)
                     result["sampled"] = "every "s + std::to_string(step) + "th vertex; pass path to write them all as .obj";
                  return TextOutput(result);
               }
               if (action == "set")
               {
                  IEditable* part = FindPart(p, name);
                  if (!part)
                     return ErrorOutput("No part named '"s + name + '\'');
                  const json data = PartDataArg(args);
                  if (!data.is_object() || data.empty())
                     return ErrorOutput("data is required: {field: value} with the stored field names, e.g. data: {\"height_top\": 50}; vpx_part schema type:"s + ItemTypeName(part->GetItemType()) + " lists them");
                  json resolved, mapped = json::object();
                  nlohmann::ordered_json doc;
                  if (const string err = ResolvePartData(part, data, resolved, mapped, doc); !err.empty())
                     return ErrorOutput(err);
                  RecordEdit(p, part);
                  if (IsEditing(p))
                     p.m_liveUI->m_editorUI.BeginPartEdit(part);
                  json applied = json::array();
                  string warning;
                  ApplyPartData(p, part, resolved, doc, applied, warning);
                  if (IsEditing(p))
                     p.m_liveUI->m_editorUI.EndPartEdit(part);
                  McpServer::Get().NoteEdit();
                  json result { { "name", part->GetName() }, { "applied", applied } };
                  if (!mapped.empty())
                     result["mapped"] = mapped;
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
                  RecordEdit(p, part, true);
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
                  if (!points.empty())
                  {
                     if (const string err = RequireEditor(p); !err.empty())
                        return ErrorOutput(err);
                     if (static_cast<int>(points.size()) < curve->GetMinimumPoints())
                        return ErrorOutput("This part needs at least "s + std::to_string(curve->GetMinimumPoints()) + " points");
                     RecordEdit(p, part);
                     SetCurvePoints(curve, points);
                     RefreshPart(p, part);
                     McpServer::Get().NoteEdit();
                  }
                  return TextOutput(json { { "name", part->GetName() }, { "type", ItemTypeName(part->GetItemType()) }, { "points", PointsJson(curve) } });
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
                  return TextOutput(json { { "point", json { { "x", point.x }, { "y", point.y }, { "z", point.z } } }, { "radius", radius }, { "parts", parts },
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
                  return TextOutput(json { { "gap", gap }, { "ballFits", gap >= 50.f }, { "from", json { { "part", a->GetName() }, { "x", from.x }, { "y", from.y }, { "z", from.z } } },
                     { "to", json { { "part", b->GetName() }, { "x", best.closest.x }, { "y", best.closest.y }, { "z", best.closest.z } } },
                     { "note", "closest distance between the two collision shapes (sampled, about 1 unit precision); a standard ball needs 50 to pass, less than that traps or blocks it" } });
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
                     if (const char* meaning = FieldMeaning(type, key))
                        value["meaning"] = meaning;
                  if (temp)
                     temp->Release();
                  json result { { "type", ItemTypeName(type) }, { "fields", fields }, { "fieldsFrom", sample ? "the part '"s + sample->GetName() + "' of this table" : "a freshly created part with default values"s },
                     { "hasDragPoints", type == eItemSurface || type == eItemRamp || type == eItemRubber || type == eItemFlasher || type == eItemLight || type == eItemTrigger } };
                  return TextOutput(result);
               }
               if (action == "transform")
               {
                  IEditable* part = FindPart(p, name);
                  if (!part)
                     return ErrorOutput("No part named '"s + name + '\'');
                  const float dx = args.value("dx", 0.f), dy = args.value("dy", 0.f), rotate = args.value("rotate", 0.f);
                  const float scaleX = args.value("scale", args.value("scaleX", 1.f)), scaleY = args.value("scale", args.value("scaleY", 1.f));
                  const bool flipX = args.value("flipX", false), flipY = args.value("flipY", false);
                  RecordEdit(p, part);
                  json done = json::array(), skipped = json::array();
                  const Vertex2D center = part->GetCenter();
                  if (DragPointCurve* curve = GetCurve(part))
                  {
                     if (rotate != 0.f) { curve->RotatePoints(rotate, center); done.push_back("rotate"); }
                     if (scaleX != 1.f || scaleY != 1.f) { curve->ScalePoints(scaleX, scaleY, center); done.push_back("scale"); }
                     if (flipX) { curve->FlipPointX(center); done.push_back("flipX"); }
                     if (flipY) { curve->FlipPointY(center); done.push_back("flipY"); }
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
                  return WithLiveCopyNote(p, json { { "name", part->GetName() }, { "done", done }, { "skipped", skipped }, { "center", { { "x", after.x }, { "y", after.y } } } });
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
                  const json data = PartDataArg(args);
                  if (!data.is_object())
                     return ErrorOutput("data must be an object of stored fields"s);
                  json resolved = json::object(), mapped = json::object();
                  if (!data.empty())
                  {
                     IEditable* probe = EditableRegistry::CreateAndInit(type, p.m_ptable, 0.f, 0.f);
                     if (!probe)
                        return ErrorOutput("Could not instantiate the part type"s);
                     probe->AddRef();
                     nlohmann::ordered_json probeDoc;
                     const string err = ResolvePartData(probe, data, resolved, mapped, probeDoc);
                     probe->Release();
                     if (!err.empty())
                        return ErrorOutput(err);
                  }
                  IEditable* part = p.m_liveUI->m_editorUI.CreatePart(type, Vertex2D(args.value("x", 0.f), args.value("y", 0.f)));
                  if (!part)
                     return ErrorOutput("Part creation failed"s);
                  if (!name.empty() && FindPart(p, name) == nullptr)
                     p.m_ptable->RenamePart(part, name);
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
                  if (!resolved.empty())
                  {
                     string error;
                     nlohmann::ordered_json doc = PartDoc(part, error);
                     if (!error.empty())
                        return ErrorOutput(error);
                     ApplyPartData(p, part, resolved, doc, applied, warning);
                  }
                  if (DragPointCurve* curve = GetCurve(part); curve && !points.empty())
                     SetCurvePoints(curve, points);
                  if (!points.empty())
                     RefreshPart(p, part);
                  McpServer::Get().NoteEdit();
                  json j { { "name", part->GetName() }, { "type", ItemTypeName(type) }, { "applied", applied } };
                  if (!mapped.empty())
                     j["mapped"] = mapped;
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
                        for (const auto& [other, otherColliders] : colliders)
                        {
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
                  // ReorderParts appends its list back to front
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
                  p.m_liveUI->m_editorUI.DeleteParts(parts);
                  if (!parts.empty())
                     McpServer::Get().NoteEdit();
                  return TextOutput(json { { "deleted", parts.size() }, { "notDeleted", notDeleted } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

}

}
