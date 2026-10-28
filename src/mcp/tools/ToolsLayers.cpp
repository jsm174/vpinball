// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "ui/live/editor/HeatStore.h"
#include "ui/live/editor/ops/ReferenceImageOp.h"
#include "parts/flasher.h"
#include "ui/live/editor/SketchTool.h"

namespace VPX::MCP
{

namespace
{

struct PartBox
{
   IEditable* part = nullptr;
   float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// The parts the ball can touch (the ones with collision shapes: not VR cabinet parts, decals...), with the footprint of their colliders
vector<PartBox> PartBoxes(Player& p)
{
   vector<PartBox> boxes;
   for (const auto& [part, colliders] : CollidersByPart(p))
   {
      if (part == p.m_ptable || part->GetItemType() == eItemPartGroup || part->GetItemType() == eItemBall || part->GetName().empty() || p.m_ptable->IsPlayerSessionObject(part)
         || VPX::Sketch::IsBackdropPart(part, p.m_ptable))
         continue;
      PartBox b;
      b.part = part;
      b.x0 = b.y0 = FLT_MAX;
      b.x1 = b.y1 = -FLT_MAX;
      for (const HitObject* ho : colliders)
      {
         b.x0 = std::min(b.x0, ho->m_hitBBox.left);
         b.x1 = std::max(b.x1, ho->m_hitBBox.right);
         b.y0 = std::min(b.y0, ho->m_hitBBox.top);
         b.y1 = std::max(b.y1, ho->m_hitBBox.bottom);
      }
      if (b.x0 <= b.x1)
         boxes.push_back(b);
   }
   return boxes;
}

json HeatState(const Heat::Store& heat)
{
   return json { { "visible", heat.m_visible }, { "layer", heat.m_layer == Heat::Layer::Time ? "time" : heat.m_layer == Heat::Layer::Impacts ? "impacts" : "resting" }, { "empty", heat.Empty() } };
}

}

void AddLayerTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_heatmap",
         "The ball heatmap: every vpx_sim run (shot, flip, trace, plunge, autoplay, scenarios...) adds where its balls moved (time), where they sat almost still (resting: the plunger lane wait, stuck, cradled; kept out of the time layer) and where they lost speed hitting something (impacts), to a grid of 20 unit cells over the table. Autoplay records all three. It is drawn over the table in the editor (cold blue to hot red; a Layers panel shows it, hides it and clears it), in vpx_screenshot top and blueprint views (heatmap:false hides it), and read here. "
         "'get' (default) returns the hottest cells of a layer (time: ms of ball time per cell; impacts: speed lost per cell) with the parts nearby each, the total, and per 200 unit band of the table how much of its cells the balls visited (coverage): low coverage in the upper half is dead space nothing sends the ball to (see the no-dead-space rule). "
         "'layer' picks time, impacts or resting for the display and the next get, 'show' / 'hide' the overlay, 'clear' empties it (do it before a new series of runs). The data is not saved with the table.",
         Schema({ { "action", Prop("string", "get (default) | layer | show | hide | clear") }, { "layer", Prop("string", "time | impacts | resting (get, layer)") }, { "top", Prop("integer", "get: hottest cells to return, default 10") } }),
         [](const json& args)
         {
            return WithPlayer([args](Player& p)
            {
               const string action = args.value("action", "get"s);
               Heat::Store& heat = Heat::Store::For(p.m_ptable);
               const string layerName = args.value("layer", ""s);
               if (!layerName.empty())
               {
                  if (layerName != "time" && layerName != "impacts" && layerName != "resting")
                     return ErrorOutput("layer must be time or impacts"s);
                  heat.m_layer = layerName == "time" ? Heat::Layer::Time : layerName == "impacts" ? Heat::Layer::Impacts : Heat::Layer::Resting;
               }
               if (action == "show" || action == "hide")
               {
                  heat.m_visible = action == "show";
                  return TextOutput(HeatState(heat));
               }
               if (action == "clear")
               {
                  heat.Clear();
                  return TextOutput(HeatState(heat));
               }
               if (action == "layer")
                  return TextOutput(HeatState(heat));
               if (action != "get")
                  return ErrorOutput("Unknown action"s);

               json out = HeatState(heat);
               if (heat.Empty())
               {
                  out["note"] = "Nothing recorded yet: run vpx_sim shots, flips or an autoplay in a playtest first";
                  return TextOutput(out);
               }
               const Heat::Layer layer = heat.m_layer;
               const float cell = Heat::Store::CELL;
               const float total = heat.Total(layer), maxValue = heat.Max(layer);
               out["cellSize"] = cell;
               out["total"] = Round(total, 1);
               out["unit"] = layer == Heat::Layer::Time ? "ms of moving ball time" : layer == Heat::Layer::Impacts ? "speed lost" : "ms of ball time at rest";
               out["maxCell"] = Round(maxValue, 1);

               struct Cell { int col, row; float value; };
               vector<Cell> cells;
               for (int row = 0; row < heat.Rows(); row++)
                  for (int col = 0; col < heat.Cols(); col++)
                     if (const float v = heat.Value(layer, col, row); v > 0.f)
                        cells.push_back({ col, row, v });
               std::ranges::sort(cells, [](const Cell& a, const Cell& b) { return a.value > b.value; });
               const vector<PartBox> boxes = PartBoxes(p);
               json hot = json::array();
               for (size_t i = 0; i < cells.size() && i < static_cast<size_t>(std::clamp(args.value("top", 10), 1, 50)); i++)
               {
                  const float cx = heat.Left() + (static_cast<float>(cells[i].col) + 0.5f) * cell, cy = heat.Top() + (static_cast<float>(cells[i].row) + 0.5f) * cell;
                  vector<std::pair<float, string>> nearby;
                  for (const PartBox& b : boxes)
                  {
                     const float dx = std::max({ b.x0 - cx, 0.f, cx - b.x1 }), dy = std::max({ b.y0 - cy, 0.f, cy - b.y1 });
                     if (const float d = std::hypot(dx, dy); d <= 40.f)
                        nearby.emplace_back(d, b.part->GetName());
                  }
                  std::ranges::sort(nearby);
                  json parts = json::array();
                  for (size_t k = 0; k < nearby.size() && k < 3; k++)
                     parts.push_back(nearby[k].second);
                  hot.push_back(json { { "x", Round(cx, 0) }, { "y", Round(cy, 0) }, { "value", Round(cells[i].value, 1) }, { "share", Round(total > 0.f ? cells[i].value / total * 100.f : 0.f, 1) }, { "nearParts", parts } });
               }
               out["hotCells"] = hot;

               // Coverage per band of the table: the share of its cells the balls visited
               json bands = json::array();
               const int bandRows = static_cast<int>(200.f / cell);
               for (int row0 = 0; row0 < heat.Rows(); row0 += bandRows)
               {
                  int visited = 0, count = 0;
                  for (int row = row0; row < std::min(row0 + bandRows, heat.Rows()); row++)
                     for (int col = 0; col < heat.Cols(); col++)
                     {
                        count++;
                        visited += heat.Value(layer, col, row) > 0.f ? 1 : 0;
                     }
                  bands.push_back(json { { "yFrom", Round(heat.Top() + static_cast<float>(row0) * cell, 0) }, { "yTo", Round(heat.Top() + static_cast<float>(std::min(row0 + bandRows, heat.Rows())) * cell, 0) },
                     { "coverage", count > 0 ? Round(100.f * static_cast<float>(visited) / static_cast<float>(count), 0) : 0.0 } });
               }
               out["bandCoverage"] = bands;
               return TextOutput(out);
            });
         } });

   tools.push_back(ToolDef { "vpx_reference",
         "Reference images: pictures (a scan, a photo of the user's paper sketch, a whitepaper layout) laid flat on the playfield in the editor so you and the user can trace parts over them. Each one is an ordinary Flasher set up as an editor only image (not visible in play, drawn in the editor views); it is saved with the table and shows in the 3D views, the blueprint and vpx_screenshot (view:top or view:blueprint). "
         "'list' returns them (name, image, center x / y, width, height, rotation, opacity, visible, locked, image pixels). 'add' places one: path (an image file, imported into the table under a free name, or the table image with the same content when there is one: reusedImage) or image (an image already in the table), width in table units (default: the table width; the height keeps the image's proportions unless given), x / y (default: the table center), opacity. "
         "'set' changes the one named (name may be omitted when there is only one): x, y, width, height (one of them keeps the proportions), fit:true (back to the image's proportions), rotation (degrees about the vertical axis), opacity (0..1), visible, locked (the user can no longer pick or move it in the views), image (another table image). 'remove' deletes the named one, or all with all:true. add, set and remove need the editor open and are undoable.",
         Schema({ { "action", Prop("string", "list (default) | add | set | remove") }, { "name", Prop("string", "Reference image (flasher) name (set, remove)") }, { "path", Prop("string", "add: image file to import") },
                  { "image", Prop("string", "add, set: name of a table image") }, { "x", Prop("number", "Center x on the playfield") }, { "y", Prop("number", "Center y on the playfield") },
                  { "width", Prop("number", "Width on the playfield, table units") }, { "height", Prop("number", "Height on the playfield, table units") }, { "fit", Prop("boolean", "set: restore the image's proportions, keeping the center and width") },
                  { "rotation", Prop("number", "set: degrees about the vertical axis") }, { "opacity", Prop("number", "0..1") }, { "visible", Prop("boolean", "set: show or hide in the editor") },
                  { "locked", Prop("boolean", "set: lock it against picking and moving in the views") }, { "all", Prop("boolean", "remove: every reference image") } }),
         [](const json& args)
         {
            return WithPlayer([args](Player& p)
            {
               using VPX::EditorUI::ReferenceImageOp;
               const string action = args.value("action", "list"s);
               PinTable* const table = p.m_ptable;
               // During an editor session the authored visibility of a part is held by its UI part: the editor resolves it
               const VPX::EditorUI::EditorUI* const session = p.m_liveUI && p.m_liveUI->IsEditorUIOpened() ? &p.m_liveUI->m_editorUI : nullptr;
               const auto images = [table, session]()
               {
                  vector<Flasher*> found;
                  for (IEditable* part : table->GetParts())
                     if (part->GetItemType() == eItemFlasher && ReferenceImageOp::IsReferenceImage(part, session))
                        found.push_back(static_cast<Flasher*>(part));
                  return found;
               };
               const auto number = [&args](const char* key, float& value)
               {
                  const bool has = args.contains(key) && args[key].is_number();
                  if (has)
                     value = args[key].get<float>();
                  return has;
               };
               const auto state = [table, &images]()
               {
                  json arr = json::array();
                  for (Flasher* f : images())
                  {
                     const VPX::EditorUI::ReferenceImageMath::Rect rect = ReferenceImageOp::GetRect(f);
                     json j { { "name", f->GetName() }, { "image", f->m_d.m_szImageA }, { "x", Round(rect.center.x) }, { "y", Round(rect.center.y) }, { "width", Round(rect.width) }, { "height", Round(rect.height) },
                        { "rotation", Round(f->m_d.m_rotZ) }, { "opacity", Round(static_cast<float>(f->m_d.m_alpha) / 100.f, 2) }, { "visible", f->IsUIVisible(false) }, { "locked", f->IsUILocked() } };
                     if (const Texture* tex = table->GetImage(f->m_d.m_szImageA))
                        j["imagePixels"] = json::array({ tex->m_width.load(), tex->m_height.load() });
                     else
                        j["warning"] = "The table has no image named '" + f->m_d.m_szImageA + "'";
                     arr.push_back(std::move(j));
                  }
                  return json { { "count", arr.size() }, { "images", arr }, { "tableSize", json::array({ Round(table->m_right - table->m_left), Round(table->m_bottom - table->m_top) }) } };
               };
               if (action == "list" || action == "get")
                  return TextOutput(state());
               if (const string err = RequireEditor(p); !err.empty())
                  return ErrorOutput(err);
               auto& editor = p.m_liveUI->m_editorUI;
               if (action == "add")
               {
                  string image = args.value("image", ""s);
                  string reused;
                  if (const string file = args.value("path", ""s); !file.empty())
                  {
                     const std::filesystem::path path = PathFromUTF8(file);
                     if (!std::filesystem::is_regular_file(path))
                        return ErrorOutput("Image file not found: "s + file);
                     Texture* const loaded = Texture::CreateFromFile(path);
                     if (loaded == nullptr)
                        return ErrorOutput("Could not read the image"s);
                     const string base = PathToUTF8(path.stem());
                     image = base;
                     for (int i = 2; table->GetImage(image) != nullptr; i++)
                        image = base + '_' + std::to_string(i);
                     loaded->m_name = image;
                     // Same import as the editor's Add Reference Image: a file identical to an image of the table is not imported again
                     const Texture* const tex = table->ImportImage(loaded);
                     if (tex != loaded)
                        reused = image = tex->m_name;
                     else
                        table->SetNonUndoableDirty(SaveDirtyState::Dirty);
                  }
                  const Texture* const tex = image.empty() ? nullptr : table->GetImage(image);
                  if (tex == nullptr || tex->m_width == 0 || tex->m_height == 0)
                     return ErrorOutput(image.empty() ? "path (an image file) or image (a table image) is required"s : "No image named '"s + image + '\'');
                  float width = table->m_right - table->m_left, height = 0.f;
                  number("width", width);
                  if (!number("height", height))
                     height = VPX::EditorUI::ReferenceImageMath::HeightForWidth(width, tex->m_width, tex->m_height);
                  if (width <= 1.f || height <= 1.f)
                     return ErrorOutput("width and height must be larger than 1"s);
                  editor.BeginUndoGroup();
                  Flasher* const flasher = editor.m_referenceImageOp.Add(image, width, height);
                  VPX::EditorUI::ReferenceImageMath::Rect rect = ReferenceImageOp::GetRect(flasher);
                  float opacity = 0.f;
                  if (number("x", rect.center.x) | number("y", rect.center.y))
                     ReferenceImageOp::SetRect(flasher, rect);
                  if (number("opacity", opacity))
                     flasher->m_d.m_alpha = static_cast<int>(std::clamp(opacity, 0.f, 1.f) * 100.f);
                  RefreshPart(p, flasher);
                  editor.EndUndoGroup(true, "MCP reference image: "s + flasher->GetName());
                  McpServer::Get().NoteEdit();
                  json out = state();
                  out["added"] = flasher->GetName();
                  if (!reused.empty())
                     out["reusedImage"] = reused;
                  return TextOutput(out);
               }
               const vector<Flasher*> all = images();
               const string name = args.value("name", ""s);
               Flasher* target = nullptr;
               for (Flasher* f : all)
                  if (StrCompareNoCase(f->GetName(), name))
                     target = f;
               if (name.empty() && all.size() == 1)
                  target = all.front();
               if (action == "remove")
               {
                  vector<IEditable*> parts;
                  if (args.value("all", false))
                     parts.assign(all.begin(), all.end());
                  else if (target != nullptr)
                     parts.push_back(target);
                  else
                     return ErrorOutput(name.empty() ? "name is required (or all:true)"s : "No reference image named '"s + name + '\'');
                  editor.DeleteParts(parts);
                  McpServer::Get().NoteEdit();
                  json out = state();
                  out["removed"] = parts.size();
                  return TextOutput(out);
               }
               if (action != "set")
                  return ErrorOutput("Unknown action"s);
               if (target == nullptr)
                  return ErrorOutput(name.empty() ? "name is required: there are "s + std::to_string(all.size()) + " reference images" : "No reference image named '"s + name + '\'');
               const string image = args.value("image", ""s);
               if (!image.empty() && table->GetImage(image) == nullptr)
                  return ErrorOutput("No image named '"s + image + '\'');
               RecordEdit(p, target, "MCP reference image"s);
               editor.BeginPartEdit(target);
               if (!image.empty())
                  ReferenceImageOp::ReplaceImage(target, image);
               VPX::EditorUI::ReferenceImageMath::Rect rect = ReferenceImageOp::GetRect(target);
               const float aspect = rect.width > 0.f ? rect.height / rect.width : 1.f;
               const bool hasW = number("width", rect.width), hasH = number("height", rect.height);
               if (hasW && !hasH)
                  rect.height = rect.width * aspect;
               else if (hasH && !hasW && aspect > 0.f)
                  rect.width = rect.height / aspect;
               if (number("x", rect.center.x) | number("y", rect.center.y) | hasW | hasH)
               {
                  if (rect.width > 1.f && rect.height > 1.f)
                     ReferenceImageOp::SetRect(target, rect);
               }
               if (args.value("fit", false))
                  ReferenceImageOp::FitToImage(target);
               float opacity = 0.f;
               number("rotation", target->m_d.m_rotZ);
               if (number("opacity", opacity))
                  target->m_d.m_alpha = static_cast<int>(std::clamp(opacity, 0.f, 1.f) * 100.f);
               if (args.contains("visible") && args["visible"].is_boolean())
                  target->SetUIVisible(args["visible"].get<bool>());
               if (args.contains("locked") && args["locked"].is_boolean())
                  target->SetUILock(args["locked"].get<bool>());
               editor.EndPartEdit(target);
               RefreshPart(p, target);
               McpServer::Get().NoteEdit();
               return TextOutput(state());
            });
         } });
}

}
