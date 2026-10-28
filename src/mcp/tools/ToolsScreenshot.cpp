// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "ui/live/editor/SketchDraw.h"
#include "ui/live/editor/ProposalDraw.h"
#include "ui/live/editor/HeatStore.h"
#include "mcp/McpBookmarks.h"

#include <mongoose/mongoose.h>

namespace VPX::MCP
{

ToolStep BuildScreenshotStep(const json& args)
{
   if (args.contains("turntable"))
   {
      const json tt = args["turntable"];
      if (!tt.is_object())
         return Immediate([]() { return ErrorOutput("turntable must be {center:[x,y,z], radius, height, count, startAngle}"s); });
      const int count = std::clamp(tt.value("count", 4), 1, 12);
      struct Turntable { int index = 0; ToolStep current; ToolOutput out; json views = json::array(); };
      auto tr = std::make_shared<Turntable>();
      json shotArgs = args;
      shotArgs.erase("turntable");
      if (!shotArgs.contains("maxSize"))
         shotArgs["maxSize"] = 640;
      const string savePath = args.value("path", ""s);
      return [tr, tt, count, shotArgs, savePath]() -> std::optional<ToolOutput>
      {
         if (!g_pplayer)
            return ErrorOutput("No table session is running"s);
         if (!tr->current)
         {
            if (tr->index >= count)
            {
               tr->out.text = json { { "turntable", tr->views }, { "note", "one image per view, in this order; angle 0 looks from the player side (+y), 90 from the right (+x)" } }.dump();
               return tr->out;
            }
            const PinTable* table = g_pplayer->m_ptable;
            const float w = table->m_right - table->m_left, h = table->m_bottom - table->m_top;
            const json c = tt.value("center", json::array({ (table->m_left + table->m_right) * 0.5f, (table->m_top + table->m_bottom) * 0.5f, 0.f }));
            if (!c.is_array() || c.size() != 3)
               return ErrorOutput("turntable center must be [x, y, z]"s);
            const float radius = tt.value("radius", 1.3f * std::max(w, h)), height = tt.value("height", 0.6f * std::max(w, h));
            const float angle = tt.value("startAngle", 0.f) + 360.f * static_cast<float>(tr->index) / static_cast<float>(count);
            const float a = ANGTORAD(angle);
            const json center = json::array({ c[0].get<float>(), c[1].get<float>(), c[2].get<float>() });
            const json eye = json::array({ c[0].get<float>() + radius * sinf(a), c[1].get<float>() + radius * cosf(a), c[2].get<float>() + height });
            json one = shotArgs;
            one["camera"] = json { { "eye", eye }, { "target", center } };
            if (!savePath.empty())
            {
               std::filesystem::path numbered = PathFromUTF8(savePath);
               const string ext = PathToUTF8(numbered.extension());
               numbered.replace_extension();
               one["path"] = PathToUTF8(numbered) + '-' + std::to_string(tr->index + 1) + ext;
            }
            tr->current = BuildScreenshotStep(one);
            tr->views.push_back({ { "angle", Round(angle) }, { "eye", { Round(eye[0].get<float>()), Round(eye[1].get<float>()), Round(eye[2].get<float>()) } }, { "target", center } });
         }
         std::optional<ToolOutput> shot = tr->current();
         if (!shot)
            return std::nullopt;
         if (shot->isError)
            return shot;
         if (tr->out.imageBase64.empty())
         {
            tr->out.imageBase64 = shot->imageBase64;
            tr->out.imageMime = shot->imageMime;
         }
         else
            tr->out.moreImages.emplace_back(shot->imageBase64, shot->imageMime);
         tr->index++;
         tr->current = nullptr;
         return std::nullopt;
      };
   }
   const unsigned int maxSize = std::clamp(args.value("maxSize", 1280), 64, 8192);
   const string savePath = args.value("path", ""s);
   const bool png = savePath.empty() ? args.value("format", "jpg"s) == "png" : args.value("format", lowerCase(PathToUTF8(PathFromUTF8(savePath).extension())) == ".png" ? "png"s : "jpg"s) == "png";
   const string uiPage = args.value("uiPage", ""s);
   // Writes the encoded image to the requested file as well, so the user can look at it (tool images are only shown to the assistant)
   const auto saveCopy = [savePath](const std::vector<unsigned char>& bytes, string& label)
   {
      if (savePath.empty())
         return;
      const std::filesystem::path out = PathFromUTF8(savePath);
      std::error_code ec;
      if (out.has_parent_path())
         std::filesystem::create_directories(out.parent_path(), ec);
      std::ofstream file(out, std::ios::binary);
      if (file && file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
         label += ", saved to " + savePath;
      else
         label += ", could not write " + savePath;
   };
   if (args.value("target", ""s) == "dmd")
   {
      const string link = args.value("link", ""s);
      return Immediate([link, png, saveCopy]() -> ToolOutput
      {
         if (!g_pplayer)
            return ErrorOutput("No table session is running"s);
         PinballPlugin::ResURIResolver& resolver = g_pplayer->m_resURIResolver;
         PinballPlugin::ResURIResolver::DisplayState display { nullptr };
         for (const string& uri : link.empty() ? vector<string> { PinballPlugin::ResURIResolver::DefaultDmdUri, PinballPlugin::ResURIResolver::DefaultCrtUri } : vector<string> { link })
         {
            display = resolver.GetDisplayState(uri);
            if (display.source && display.state.frame)
               break;
         }
         if (!display.source || !display.state.frame)
            return ErrorOutput("No display frame is available (the table has no DMD, or it has not been fed yet: run the game a moment). Display sources: "s + resolver.DumpDisplaySources());
         const unsigned int w = display.source->width, h = display.source->height;
         const unsigned int scale = std::clamp(1024u / std::max(1u, w), 1u, 8u);
         std::shared_ptr<BaseTexture> tex = BaseTexture::Create(w * scale, h * scale, BaseTexture::SRGBA);
         if (!tex)
            return ErrorOutput("Could not allocate the image"s);
         uint8_t* out = static_cast<uint8_t*>(tex->data());
         for (unsigned int y = 0; y < h; y++)
            for (unsigned int x = 0; x < w; x++)
            {
               uint8_t r = 0, g = 0, b = 0;
               const size_t i = static_cast<size_t>(y) * w + x;
               switch (display.source->frameFormat)
               {
               case CTLPI_DISPLAY_FORMAT_LUM32F:
               {
                  const float l = std::clamp(static_cast<const float*>(display.state.frame)[i], 0.f, 1.f);
                  const float srgb = l <= 0.0031308f ? 12.92f * l : 1.055f * powf(l, 1.f / 2.4f) - 0.055f;
                  r = static_cast<uint8_t>(srgb * 255.f);
                  g = static_cast<uint8_t>(srgb * 0.55f * 255.f);
                  break;
               }
               case CTLPI_DISPLAY_FORMAT_SRGB888:
               {
                  const uint8_t* px = static_cast<const uint8_t*>(display.state.frame) + i * 3;
                  r = px[0];
                  g = px[1];
                  b = px[2];
                  break;
               }
               case CTLPI_DISPLAY_FORMAT_SRGB565:
               {
                  const uint16_t v = static_cast<const uint16_t*>(display.state.frame)[i];
                  r = static_cast<uint8_t>(((v >> 11) & 31) * 255 / 31);
                  g = static_cast<uint8_t>(((v >> 5) & 63) * 255 / 63);
                  b = static_cast<uint8_t>((v & 31) * 255 / 31);
                  break;
               }
               default: break;
               }
               for (unsigned int dy = 0; dy < scale; dy++)
                  for (unsigned int dx = 0; dx < scale; dx++)
                  {
                     uint8_t* o = out + (static_cast<size_t>(y * scale + dy) * w * scale + x * scale + dx) * 4;
                     o[0] = r;
                     o[1] = g;
                     o[2] = b;
                     o[3] = 255;
                  }
            }
         const std::filesystem::path path = std::filesystem::temp_directory_path() / ("vpx-mcp-dmd-"s + std::to_string(usec()) + (png ? ".png" : ".jpg"));
         if (!tex->Save(path))
            return ErrorOutput("Could not encode the image"s);
         std::ifstream file(path, std::ios::binary);
         std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
         file.close();
         std::filesystem::remove(path);
         std::string b64((bytes.size() + 2) / 3 * 4 + 1, '\0');
         b64.resize(mg_base64_encode(bytes.data(), bytes.size(), b64.data(), b64.size()));
         string label = "Display frame "s + std::to_string(w) + 'x' + std::to_string(h) + " (frame " + std::to_string(display.state.frameId) + "), shown " + std::to_string(scale) + "x";
         saveCopy(bytes, label);
         return ToolOutput { label, b64, png ? "image/png"s : "image/jpeg"s, false };
      });
   }
   const json crop = args.value("crop", json());
   if (!crop.is_null() && !(crop.is_object() && ((crop.contains("part") && crop["part"].is_string()) || (crop.contains("w") && crop.contains("h") && crop["w"].is_number() && crop["h"].is_number()))))
      return Immediate([]() { return ErrorOutput("crop must be {x, y, w, h} as fractions 0..1 of the frame (x, y: top-left corner) or {part: name, margin}"s); });
   const string view = args.value("view", ""s);
   if (!view.empty() && view != "top" && view != "blueprint")
      return Immediate([]() { return ErrorOutput("view must be top or blueprint"s); });
   const bool blueprint = view == "blueprint";
   const bool topView = view == "top" || blueprint;
   const bool textured = args.value("shade", "textured"s) != "wireframe";
   const bool hasShade = args.contains("shade");
   // Region of the table to frame and crop to (top view and blueprint), else the whole playfield
   Vertex2D regionLo, regionHi;
   bool hasRegion = false;
   if (args.contains("region"))
   {
      const json r = args["region"];
      if (r.is_array() && r.size() == 4 && r[0].is_number() && r[1].is_number() && r[2].is_number() && r[3].is_number())
         regionLo = Vertex2D(r[0].get<float>(), r[1].get<float>()), regionHi = Vertex2D(r[2].get<float>(), r[3].get<float>());
      else if (r.is_object() && r.contains("left") && r.contains("top") && r.contains("right") && r.contains("bottom"))
         regionLo = Vertex2D(r.value("left", 0.f), r.value("top", 0.f)), regionHi = Vertex2D(r.value("right", 0.f), r.value("bottom", 0.f));
      else
         return Immediate([]() { return ErrorOutput("region must be {left, top, right, bottom} or [left, top, right, bottom] in table units"s); });
      if (regionHi.x <= regionLo.x || regionHi.y <= regionLo.y)
         return Immediate([]() { return ErrorOutput("region must have right > left and bottom > top"s); });
      hasRegion = true;
   }
   const float zoom = args.value("zoom", 0.f);
   Vertex2D zoomCenter;
   if (zoom > 0.f)
   {
      const json c = args.value("center", json());
      if (!c.is_array() || c.size() != 2 || !c[0].is_number() || !c[1].is_number())
         return Immediate([]() { return ErrorOutput("zoom needs center:[x, y] (table units)"s); });
      zoomCenter = Vertex2D(c[0].get<float>(), c[1].get<float>());
   }
   const std::optional<bool> grid = args.contains("grid") ? std::optional<bool>(args.value("grid", true)) : std::nullopt;
   const std::optional<bool> backdrop = args.contains("backdrop") ? std::optional<bool>(args.value("backdrop", true)) : std::nullopt;
   const bool colliders = args.value("colliders", !blueprint), labels = args.value("labels", true), trajectory = args.value("trajectory", true), marks = args.value("marks", true), proposals = args.value("proposals", true), heatmap = args.value("heatmap", true), bookmarks = args.value("bookmarks", true);
   const string mode = args.value("mode", ""s);
   unsigned int modeMask = 0;
   ViewSetupID modeView = BG_INVALID;
   if (mode == "desktop")
      modeMask = PartGroupData::PlayerModeVisibilityMask::PMVM_DESKTOP, modeView = BG_DESKTOP;
   else if (mode == "fss")
      modeMask = PartGroupData::PlayerModeVisibilityMask::PMVM_FSS, modeView = BG_FSS;
   else if (mode == "cabinet")
      modeMask = PartGroupData::PlayerModeVisibilityMask::PMVM_CABINET, modeView = BG_FULLSCREEN;
   else if (mode == "vr")
      modeMask = PartGroupData::PlayerModeVisibilityMask::PMVM_VIRTUAL_REALITY;
   else if (mode == "mr")
      modeMask = PartGroupData::PlayerModeVisibilityMask::PMVM_MIXED_REALITY;
   else if (!mode.empty())
      return Immediate([]() { return ErrorOutput("mode must be desktop, fss, cabinet, vr or mr"s); });
   const auto readPoint = [](const json& v, vec3& out)
   {
      if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number())
         return false;
      out = vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
      return true;
   };
   bool hasCamera = false, defaultCamera = false;
   vec3 eye, target;
   if (args.contains("camera"))
   {
      const json camera = args["camera"];
      if (!camera.is_object() || !readPoint(camera.value("eye", json()), eye) || !readPoint(camera.value("target", json()), target))
         return Immediate([]() { return ErrorOutput("camera must be {eye:[x,y,z], target:[x,y,z]}"s); });
      hasCamera = true;
   }
   else if (modeView == BG_INVALID && modeMask != 0 && !topView)
      hasCamera = defaultCamera = true;
   struct Capture { int phase = 0; uint32_t waitFrames = 0; std::atomic<int> state { 0 }; std::filesystem::path path; bool viewChanged = false; bool openedEditor = false; bool wasPlaying = false;
      ImVec2 cropMin { FLT_MAX, FLT_MAX }, cropMax { -FLT_MAX, -FLT_MAX }; float displayWidth = 0.f;
      bool modeApplied = false; unsigned int savedMask = 0; ViewSetupID savedViewOverride = BG_INVALID;
      bool flagsSaved = false, savedGrid = false, savedBackdrop = false, savedSolid = false; };
   auto cap = std::make_shared<Capture>();
   return ToolStep([cap, crop, maxSize, png, uiPage, saveCopy, topView, blueprint, textured, hasShade, hasRegion, regionLo, regionHi, zoom, zoomCenter, grid, backdrop, colliders, labels, trajectory, marks, proposals, heatmap, bookmarks, modeMask, modeView, hasCamera, defaultCamera, eye, target]() -> std::optional<ToolOutput>
   {
      if (!g_pplayer)
         return ErrorOutput("No table session is running"s);
      auto restoreView = [cap]()
      {
         if (cap->modeApplied && g_pplayer)
         {
            g_pplayer->m_renderer->m_visibilityMaskOverride = cap->savedMask;
            g_pplayer->m_renderer->DisableStaticPrePass(false);
            g_pplayer->m_ptable->SetViewSetupOverride(cap->savedViewOverride);
            cap->modeApplied = false;
         }
         if (cap->flagsSaved && g_pplayer)
         {
            g_pplayer->m_ptable->m_winEditorGrid = cap->savedGrid;
            g_pplayer->m_ptable->m_winEditorBackdrop = cap->savedBackdrop;
            g_pplayer->m_ptable->m_renderSolid = cap->savedSolid;
            cap->flagsSaved = false;
         }
         if (cap->viewChanged && g_pplayer && g_pplayer->m_liveUI)
         {
            g_pplayer->m_liveUI->m_editorUI.EndCapture();
            g_pplayer->m_liveUI->m_hideNotifications = false;
            cap->viewChanged = false;
            if (cap->openedEditor)
            {
               g_pplayer->m_liveUI->m_editorUI.Close();
               g_pplayer->SetPlayState(cap->wasPlaying);
            }
         }
      };
      if (cap->phase == 0)
      {
         cap->phase = 1;
         if (topView || hasCamera)
         {
            if (!g_pplayer->m_liveUI)
               return ErrorOutput("No UI available for this view"s);
            if (!g_pplayer->m_liveUI->IsEditorUIOpened())
            {
               if (g_pplayer->m_ptable->m_liveBaseTable == nullptr)
                  return ErrorOutput(topView ? "view:'top' needs the editor (vpx_editor open) or a playtest started from it"s : "camera (and mode vr/mr) needs the editor (vpx_editor open) or a playtest started from it"s);
               cap->wasPlaying = g_pplayer->IsPlaying(false);
               g_pplayer->m_liveUI->OpenEditorUI();
               cap->openedEditor = true;
            }
         }
         else if (modeMask != 0 && g_pplayer->m_liveUI && g_pplayer->m_liveUI->IsEditorUIOpened())
         {
            auto& editor = g_pplayer->m_liveUI->m_editorUI;
            cap->viewChanged = true;
            editor.ShowPlayView(modeView);
            g_pplayer->m_liveUI->m_hideNotifications = true;
            cap->waitFrames = g_pplayer->m_overall_frames + 4;
         }
         if (modeMask != 0)
         {
            cap->savedMask = g_pplayer->m_renderer->m_visibilityMaskOverride;
            cap->savedViewOverride = g_pplayer->m_ptable->GetViewSetupOverride();
            g_pplayer->m_renderer->m_visibilityMaskOverride = modeMask;
            g_pplayer->m_renderer->DisableStaticPrePass(true);
            if (modeView != BG_INVALID)
               g_pplayer->m_ptable->SetViewSetupOverride(modeView);
            cap->modeApplied = true;
            cap->waitFrames = max(cap->waitFrames, g_pplayer->m_overall_frames + 4);
         }
         if (hasCamera && !topView)
         {
            auto& editor = g_pplayer->m_liveUI->m_editorUI;
            cap->viewChanged = true;
            const PinTable* table = g_pplayer->m_ptable;
            const float cx = (table->m_left + table->m_right) * 0.5f;
            editor.ShowCameraView(defaultCamera ? vec3(cx, table->m_bottom + 900.f, 1100.f) : eye, defaultCamera ? vec3(cx, table->m_bottom * 0.45f, 0.f) : target);
            g_pplayer->m_liveUI->m_hideNotifications = true;
            cap->waitFrames = g_pplayer->m_overall_frames + 4;
         }
         if (topView)
         {
            auto& editor = g_pplayer->m_liveUI->m_editorUI;
            cap->viewChanged = true;
            if (blueprint)
            {
               PinTable* const table = g_pplayer->m_ptable;
               cap->flagsSaved = true;
               cap->savedGrid = table->m_winEditorGrid;
               cap->savedBackdrop = table->m_winEditorBackdrop;
               cap->savedSolid = table->m_renderSolid;
               if (grid)
                  table->m_winEditorGrid = *grid;
               if (backdrop)
                  table->m_winEditorBackdrop = *backdrop;
               if (hasShade)
                  table->m_renderSolid = textured;
               if (zoom > 0.f)
                  editor.ShowBlueprintView(zoom, zoomCenter);
               else
                  editor.ShowBlueprintView(regionLo, regionHi);
            }
            else
               editor.ShowTopView(textured, regionLo, regionHi);
            g_pplayer->m_liveUI->m_hideNotifications = true;
            const bool cropToRegion = hasRegion || zoom <= 0.f;
            editor.SetExternalOverlay([cap, colliders, labels, trajectory, marks, proposals, heatmap, bookmarks, cropToRegion, hasRegion, regionLo, regionHi](const std::function<ImVec2(const Vertex3Ds&)>& project, ImDrawList* drawList)
            {
               if (!g_pplayer)
                  return;
               const PinTable* table = g_pplayer->m_ptable;
               ImVec2 cropMin { FLT_MAX, FLT_MAX }, cropMax { -FLT_MAX, -FLT_MAX };
               const Vertex2D lo = hasRegion ? regionLo : Vertex2D(table->m_left, table->m_top), hi = hasRegion ? regionHi : Vertex2D(table->m_right, table->m_bottom);
               for (const float x : { lo.x, hi.x })
                  for (const float y : { lo.y, hi.y })
                     if (const ImVec2 a = project(Vertex3Ds(x, y, 0.f)); a.x != FLT_MAX)
                     {
                        cropMin = ImVec2(min(cropMin.x, a.x), min(cropMin.y, a.y));
                        cropMax = ImVec2(max(cropMax.x, a.x), max(cropMax.y, a.y));
                     }
               if (cropToRegion)
               {
                  const ImVec2 disp = ImGui::GetIO().DisplaySize;
                  cap->cropMin = ImVec2(max(0.f, cropMin.x), max(0.f, cropMin.y));
                  cap->cropMax = ImVec2(min(disp.x, cropMax.x), min(disp.y, cropMax.y));
               }
               cap->displayWidth = ImGui::GetIO().DisplaySize.x;
               if (colliders && g_pplayer->m_physics)
               {
                  auto project2D = [&project](Vertex3Ds v)
                  {
                     const ImVec2 pt = project(v);
                     return Vertex2D(pt.x, pt.y);
                  };
                  ImGui::PushStyleColor(ImGuiCol_PlotLines, IM_COL32(255, 40, 40, 255));
                  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IM_COL32(255, 40, 40, 40));
                  for (const HitObject* ho : g_pplayer->m_physics->GetHitObjects())
                     if (ho && ho->m_editable && ho->m_editable != table && ho->m_enabled && ho->GetType() != eBall)
                        ho->DrawUI(project2D, drawList, false);
                  ImGui::PopStyleColor(2);
               }
               if (bookmarks)
                  VPX::MCP::Bookmarks::DrawPaths(drawList, VPX::MCP::Bookmarks::Store::For(table), [&project](VPX::Sketch::Point p) { return project(Vertex3Ds(p.x, p.y, 0.f)); }, g_pplayer->m_liveUI->GetDPI());
               if (heatmap)
                  VPX::Heat::Draw(drawList, VPX::Heat::Store::For(table), [&project](VPX::Sketch::Point p) { return project(Vertex3Ds(p.x, p.y, 0.f)); }, g_pplayer->m_liveUI->GetDPI());
               if (proposals)
                  VPX::Proposals::DrawGhosts(drawList, VPX::Proposals::Store::For(table), table, [&project](VPX::Sketch::Point p) { return project(Vertex3Ds(p.x, p.y, 0.f)); }, g_pplayer->m_liveUI->GetDPI());
               if (marks)
               {
                  // The sketch layer, whatever the user hides in the editor; done marks are drawn faded
                  const VPX::Sketch::Store& sketch = VPX::Sketch::Store::For(table);
                  VPX::Sketch::DrawMarks(drawList, sketch.Marks(), false, [&project](VPX::Sketch::Point p) { return project(Vertex3Ds(p.x, p.y, 0.f)); }, g_pplayer->m_liveUI->GetDPI());
               }
               if (trajectory)
               {
                  for (const auto& path : g_lastSimTrace.paths)
                     for (size_t i = 1; i < path.size(); i++)
                     {
                        const ImVec2 a = project(path[i - 1]), b = project(path[i]);
                        if (a.x != FLT_MAX && b.x != FLT_MAX)
                           drawList->AddLine(a, b, IM_COL32(0, 200, 255, 255), 3.f);
                     }
                  for (const auto& path : g_lastSimTrace.paths)
                     if (const ImVec2 a = project(path.front()); a.x != FLT_MAX)
                        drawList->AddCircleFilled(a, 6.f, IM_COL32(0, 255, 0, 255));
                  for (const auto& [pos, label] : g_lastSimTrace.hits)
                     if (const ImVec2 a = project(pos); a.x != FLT_MAX)
                     {
                        drawList->AddCircleFilled(a, 5.f, IM_COL32(255, 220, 0, 255));
                        drawList->AddText(ImVec2(a.x + 7.f, a.y - 7.f), IM_COL32(255, 220, 0, 255), label.c_str());
                     }
               }
               vector<std::pair<ImVec2, ImVec2>> placed;
               if (labels)
                  for (IEditable* part : table->GetParts())
                  {
                     if (part->GetItemType() == eItemPartGroup || part->GetItemType() == eItemBall || table->IsPlayerSessionObject(part) || part->GetPartGroup() == nullptr)
                        continue;
                     const Vertex2D c = part->GetCenter();
                     if (c.x < lo.x || c.x > hi.x || c.y < lo.y || c.y > hi.y)
                        continue;
                     const ImVec2 a = project(Vertex3Ds(c.x, c.y, 0.f));
                     if (a.x == FLT_MAX)
                        continue;
                     const string& name = part->GetName();
                     const ImVec2 size = ImGui::CalcTextSize(name.c_str());
                     const ImVec2 at(a.x - size.x * 0.5f, a.y - size.y * 0.5f);
                     const ImVec2 lo(at.x - 2.f, at.y - 1.f), hi(at.x + size.x + 2.f, at.y + size.y + 1.f);
                     if (std::ranges::any_of(placed, [&lo, &hi](const auto& r) { return lo.x < r.second.x && hi.x > r.first.x && lo.y < r.second.y && hi.y > r.first.y; }))
                        continue;
                     placed.emplace_back(lo, hi);
                     drawList->AddRectFilled(ImVec2(at.x - 2.f, at.y - 1.f), ImVec2(at.x + size.x + 2.f, at.y + size.y + 1.f), IM_COL32(0, 0, 0, 160));
                     drawList->AddText(at, IM_COL32(255, 255, 255, 255), name.c_str());
                  }
            });
            cap->waitFrames = g_pplayer->m_overall_frames + 4;
         }
         if (!uiPage.empty() && g_pplayer->m_liveUI)
         {
            g_pplayer->m_liveUI->OpenInGameUI(uiPage);
            cap->waitFrames = g_pplayer->m_overall_frames + 3;
         }
         return std::nullopt;
      }
      if (cap->phase == 1)
      {
         if (g_pplayer->m_overall_frames < cap->waitFrames)
            return std::nullopt;
         cap->phase = 2;
         cap->path = std::filesystem::temp_directory_path() / ("vpx-mcp-"s + std::to_string(usec()) + ".png");
         g_pplayer->m_renderer->m_renderDevice->CaptureScreenshot({ g_pplayer->m_playfieldWnd }, { cap->path }, [cap](bool success) { cap->state = success ? 1 : -1; });
         return std::nullopt;
      }
      if (cap->state == 0)
         return std::nullopt;
      restoreView();
      if (!uiPage.empty() && g_pplayer->m_liveUI)
         g_pplayer->m_liveUI->HideUI();
      if (cap->state < 0)
         return ErrorOutput("Screenshot capture failed"s);
      string label = modeMask != 0 || hasCamera ? " ("s + (modeMask != 0 ? "mode preview" : "") + (modeMask != 0 && hasCamera ? ", " : "") + (hasCamera ? (defaultCamera ? "default player camera" : "camera") : "") + ")" : ""s;
      if (topView)
         label += blueprint ? " (blueprint"s : " (top view"s;
      if (topView && hasRegion)
         label += ", region "s + std::to_string(static_cast<int>(regionLo.x)) + ',' + std::to_string(static_cast<int>(regionLo.y)) + " to " + std::to_string(static_cast<int>(regionHi.x)) + ',' + std::to_string(static_cast<int>(regionHi.y));
      if (topView && zoom > 0.f)
         label += ", zoom "s + std::to_string(zoom) + " px/unit";
      if (topView)
         label += ')';
      if (topView && cap->displayWidth > 0.f && cap->cropMax.x > cap->cropMin.x)
      {
         if (std::shared_ptr<BaseTexture> full = BaseTexture::CreateFromFile(cap->path); full && full->width() > 0)
         {
            const float scale = static_cast<float>(full->width()) / cap->displayWidth;
            const int margin = 8;
            const int x0 = std::clamp(static_cast<int>(cap->cropMin.x * scale) - margin, 0, static_cast<int>(full->width()) - 1);
            const int y0 = std::clamp(static_cast<int>(cap->cropMin.y * scale) - margin, 0, static_cast<int>(full->height()) - 1);
            const int x1 = std::clamp(static_cast<int>(cap->cropMax.x * scale) + margin, x0 + 1, static_cast<int>(full->width()));
            const int y1 = std::clamp(static_cast<int>(cap->cropMax.y * scale) + margin, y0 + 1, static_cast<int>(full->height()));
            if (std::shared_ptr<BaseTexture> cropped = BaseTexture::Create(x1 - x0, y1 - y0, full->m_format))
            {
               const size_t bpp = full->pitch() / full->width();
               for (int y = y0; y < y1; y++)
                  memcpy(static_cast<uint8_t*>(cropped->data()) + (y - y0) * cropped->pitch(), static_cast<const uint8_t*>(full->datac()) + y * full->pitch() + x0 * bpp, (x1 - x0) * bpp);
               cropped->Save(cap->path);
            }
         }
      }
      if (crop.is_object())
      {
         float fx = crop.value("x", 0.f), fy = crop.value("y", 0.f), fw = crop.value("w", 1.f), fh = crop.value("h", 1.f);
         if (crop.contains("part"))
         {
            IEditable* const part = FindPart(*g_pplayer, crop.value("part", ""s));
            const json screen = part ? PartVisibilityJson(*g_pplayer, part).value("screen", json()) : json();
            if (!screen.is_object() || !screen.contains("rect"))
            {
               std::filesystem::remove(cap->path);
               return ErrorOutput(part ? "crop: '"s + part->GetName() + "' is not in the player camera's frame (vpx_part visible explains why)" : "crop: no part named '"s + crop.value("part", ""s) + '\'');
            }
            const float vw = screen["viewport"][0].get<float>(), vh = screen["viewport"][1].get<float>(), margin = crop.value("margin", 0.05f);
            fx = screen["rect"][0].get<float>() / vw - margin;
            fy = screen["rect"][1].get<float>() / vh - margin;
            fw = (screen["rect"][2].get<float>() - screen["rect"][0].get<float>()) / vw + 2.f * margin;
            fh = (screen["rect"][3].get<float>() - screen["rect"][1].get<float>()) / vh + 2.f * margin;
         }
         if (std::shared_ptr<BaseTexture> full = BaseTexture::CreateFromFile(cap->path); full && full->width() > 0)
         {
            const int fullW = static_cast<int>(full->width()), fullH = static_cast<int>(full->height());
            const int x0 = std::clamp(static_cast<int>(fx * fullW), 0, fullW - 1), y0 = std::clamp(static_cast<int>(fy * fullH), 0, fullH - 1);
            const int x1 = std::clamp(static_cast<int>((fx + fw) * fullW), x0 + 1, fullW), y1 = std::clamp(static_cast<int>((fy + fh) * fullH), y0 + 1, fullH);
            if (std::shared_ptr<BaseTexture> cropped = BaseTexture::Create(x1 - x0, y1 - y0, full->m_format))
            {
               const size_t bpp = full->pitch() / full->width();
               for (int y = y0; y < y1; y++)
                  memcpy(static_cast<uint8_t*>(cropped->data()) + (y - y0) * cropped->pitch(), static_cast<const uint8_t*>(full->datac()) + y * full->pitch() + x0 * bpp, (x1 - x0) * bpp);
               cropped->Save(cap->path);
               label += " (cropped to "s + std::to_string(x0) + ',' + std::to_string(y0) + ' ' + std::to_string(x1 - x0) + 'x' + std::to_string(y1 - y0) + " of " + std::to_string(fullW) + 'x' + std::to_string(fullH) + ')';
            }
         }
      }
      std::shared_ptr<BaseTexture> tex = BaseTexture::CreateFromFile(cap->path, maxSize);
      std::filesystem::remove(cap->path);
      if (!tex)
         return ErrorOutput("Could not read the captured image"s);
      const std::filesystem::path outPath = std::filesystem::path(cap->path).replace_extension(png ? ".png" : ".jpg");
      if (!tex->Save(outPath))
         return ErrorOutput("Could not encode the image"s);
      std::ifstream file(outPath, std::ios::binary);
      std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      file.close();
      std::filesystem::remove(outPath);
      if (bytes.empty())
         return ErrorOutput("Screenshot file is empty"s);
      std::string b64((bytes.size() + 2) / 3 * 4 + 1, '\0');
      const size_t n = mg_base64_encode(bytes.data(), bytes.size(), b64.data(), b64.size());
      b64.resize(n);
      string text = "Playfield screenshot "s + std::to_string(tex->width()) + 'x' + std::to_string(tex->height()) + " (" + std::to_string(bytes.size()) + " bytes)" + label;
      saveCopy(bytes, text);
      return ToolOutput { text, b64, png ? "image/png"s : "image/jpeg"s, false };
   });
}

void AddScreenshotTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_screenshot",
      "Capture the playfield window as an image (jpg default, maxSize is the largest dimension). Options: uiPage (open an in-game UI page for the capture) | view:\"top\" (orthographic top render of the playfield with the colliders, part labels and the last vpx_sim trajectory; shade wireframe) | view:\"blueprint\" (the editor's 2D schematic of the stored parts, win32 editor style, same overlays; grid, backdrop, shade wireframe = outlines only) | region {left, top, right, bottom} (top view and blueprint: frame and crop to that part of the table) | zoom + center [x, y] (blueprint: px per table unit around a point) | target:\"dmd\" (the current display frame, link) | mode (desktop, fss, cabinet, vr, mr: preview that play mode's part visibility and view) | camera {eye, target} (perspective view, table units, z up) | turntable {center, radius, height, count, startAngle} (one image per view around a point) | crop {x, y, w, h} (fractions of the frame) or {part, margin} (the area a part covers in the play view): zoom on a piece of the capture, works in a plain play session. Top view, blueprint, camera and vr/mr need the editor open or a playtest started from it; the previous view is restored. Details: vpx_status guide:\"screenshot\".",
         Schema({ { "maxSize", Prop("integer", "Largest image dimension in pixels, default 1280") }, { "format", Prop("string", "jpg (default) or png") }, { "path", Prop("string", "Also write the image to this file (the format follows its extension unless format is given; turntable views get -1, -2... appended)") }, { "uiPage", Prop("string", "In-game UI page to show") },
                  { "view", Prop("string", "top: fitted orthographic render of the playfield | blueprint: the editor's 2D schematic") }, { "region", json { { "type", "object" }, { "description", "top view and blueprint: {left, top, right, bottom} table region to frame and crop to (default the whole playfield)" } } },
                  { "zoom", Prop("number", "blueprint: display px per table unit (with center)") }, { "center", json { { "type", "array" }, { "items", json { { "type", "number" } } }, { "description", "blueprint zoom: [x, y] table point at the image center" } } },
                  { "grid", Prop("boolean", "blueprint: draw the editor grid (default the table's setting)") }, { "backdrop", Prop("boolean", "blueprint: draw the playfield image under the parts (default the table's setting)") }, { "target", Prop("string", "dmd: the current DMD (or CRT/LCD display) frame instead of the playfield") }, { "link", Prop("string", "target dmd: display link, default the table's default display") }, { "shade", Prop("string", "top view and blueprint: textured (default) or wireframe") }, { "colliders", Prop("boolean", "top view and blueprint: outline the collision shapes in red, default true for the top view, false for the blueprint") },
                  { "labels", Prop("boolean", "top view and blueprint: part names, default true") }, { "trajectory", Prop("boolean", "top view and blueprint: ball paths of the last vpx_sim run, default true") }, { "bookmarks", Prop("boolean", "top view and blueprint: the paths of the shown play bookmarks (vpx_bookmarks show), default true") }, { "heatmap", Prop("boolean", "top view and blueprint: the ball heatmap of the sim runs (vpx_heatmap), default true") }, { "proposals", Prop("boolean", "top view and blueprint: the ghosts of the pending proposals (vpx_proposal), default true") }, { "marks", Prop("boolean", "top view and blueprint: the sketch marks (vpx_notes) in their colours with #id labels, default true") },
                  { "mode", Prop("string", "desktop | fss | cabinet | vr | mr: render with that play mode's part visibility (and view setup for desktop/fss/cabinet)") },
                  { "camera", json { { "type", "object" }, { "description", "{eye:[x,y,z], target:[x,y,z]} perspective camera in table units, z up" } } },
                  { "crop", json { { "type", "object" }, { "description", "{x, y, w, h}: fractions 0..1 of the captured frame, x,y the top-left corner; or {part: name, margin (fraction, default 0.05)}: the area the part covers in the play view. The crop keeps the full capture resolution up to maxSize" } } },
                  { "turntable", json { { "type", "object" }, { "description", "{center:[x,y,z] (default table center), radius, height (eye above center), count (default 4, up to 12), startAngle (degrees, 0 = from the player side)}: count camera views evenly around center, one image each" } } } }),
         BuildScreenshotStep });

}

}
