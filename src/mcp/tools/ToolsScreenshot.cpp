// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

void AddScreenshotTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_screenshot", "Capture the playfield window (play view, or the editor view when the editor is open) as an image downscaled to maxSize pixels. With uiPage (e.g. 'homepage', 'settings/mcp', 'table/options'), opens that in-game UI page for the capture and closes it afterwards. "
         "view:'top' (editor open or playtesting) renders the whole playfield from above, orthographic, without the editor panels: textured (shade:'wireframe' for outlines only), with the collision shapes outlined in red (colliders, default true), "
         "part names (labels, default true) and the ball paths of the last vpx_sim run with their hit points (trajectory, default true). The previous editor view is restored afterwards. "
         "target:'dmd' returns the current frame of the table's display (FlexDMD, PinMAME, script DMD...) at its native resolution scaled up, independent of where it is drawn. "
         "mode previews another play mode: the part groups visible in that mode (desktop, fss, cabinet, vr, mr) and, for desktop/fss/cabinet, that mode's view setup (camera, backdrop). "
         "camera {eye:[x,y,z], target:[x,y,z]} (table units, z up, editor open or playtesting) renders a perspective view from eye toward target, e.g. a standing player in front of the cabinet: eye [table center x, table bottom + 900, 1100]. "
         "mode 'vr' or 'mr' without camera uses that default player viewpoint. turntable {count: 6} returns that many camera views evenly spaced around a point (default the table center, maxSize default 640 each), e.g. to show a model from all sides. Mode and camera are restored afterwards.",
         Schema({ { "maxSize", Prop("integer", "Largest image dimension in pixels, default 1280") }, { "format", Prop("string", "jpg (default) or png") }, { "uiPage", Prop("string", "In-game UI page to show") },
                  { "view", Prop("string", "top: fitted top view of the playfield") }, { "target", Prop("string", "dmd: the current DMD (or CRT/LCD display) frame instead of the playfield") }, { "link", Prop("string", "target dmd: display link, default the table's default display") }, { "shade", Prop("string", "top view: textured (default) or wireframe") }, { "colliders", Prop("boolean", "top view: outline the collision shapes, default true") },
                  { "labels", Prop("boolean", "top view: part names, default true") }, { "trajectory", Prop("boolean", "top view: ball paths of the last vpx_sim run, default true") },
                  { "mode", Prop("string", "desktop | fss | cabinet | vr | mr: render with that play mode's part visibility (and view setup for desktop/fss/cabinet)") },
                  { "camera", json { { "type", "object" }, { "description", "{eye:[x,y,z], target:[x,y,z]} perspective camera in table units, z up" } } },
                  { "turntable", json { { "type", "object" }, { "description", "{center:[x,y,z] (default table center), radius, height (eye above center), count (default 4, up to 12), startAngle (degrees, 0 = from the player side)}: count camera views evenly around center, one image each" } } } }),
         [](const json& args) -> ToolStep
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
               return [tr, tt, count, shotArgs]() -> std::optional<ToolOutput>
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
                     const auto it = std::ranges::find_if(Tools(), [](const ToolDef& t) { return string(t.name) == "vpx_screenshot"; });
                     tr->current = it->build(one);
                     const auto r = [](float f) { return std::strtod(std::format("{:.1f}", f).c_str(), nullptr); };
                     tr->views.push_back({ { "angle", r(angle) }, { "eye", { r(eye[0].get<float>()), r(eye[1].get<float>()), r(eye[2].get<float>()) } }, { "target", center } });
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
            const bool png = args.value("format", "jpg"s) == "png";
            const string uiPage = args.value("uiPage", ""s);
            if (args.value("target", ""s) == "dmd")
            {
               const string link = args.value("link", ""s);
               return Immediate([link, png]() -> ToolOutput
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
                  return ToolOutput { "Display frame "s + std::to_string(w) + 'x' + std::to_string(h) + " (frame " + std::to_string(display.state.frameId) + "), shown " + std::to_string(scale) + "x", b64,
                     png ? "image/png"s : "image/jpeg"s, false };
               });
            }
            const bool topView = args.value("view", ""s) == "top";
            const bool textured = args.value("shade", "textured"s) != "wireframe";
            const bool colliders = args.value("colliders", true), labels = args.value("labels", true), trajectory = args.value("trajectory", true);
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
            struct Capture { int phase = 0; uint32_t waitFrames = 0; std::atomic<int> state { 0 }; std::filesystem::path path; std::any savedView; bool viewChanged = false; bool openedEditor = false; bool wasPlaying = false;
               ImVec2 cropMin { FLT_MAX, FLT_MAX }, cropMax { -FLT_MAX, -FLT_MAX }; float displayWidth = 0.f;
               bool modeApplied = false; unsigned int savedMask = 0; ViewSetupID savedViewOverride = BG_INVALID; };
            auto cap = std::make_shared<Capture>();
            return ToolStep([cap, maxSize, png, uiPage, topView, textured, colliders, labels, trajectory, modeMask, modeView, hasCamera, defaultCamera, eye, target]() -> std::optional<ToolOutput>
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
                  if (cap->viewChanged && g_pplayer && g_pplayer->m_liveUI)
                  {
                     g_pplayer->m_liveUI->m_editorUI.SetExternalOverlay(nullptr);
                     g_pplayer->m_liveUI->m_editorUI.RestoreViewState(cap->savedView);
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
                     cap->savedView = editor.SaveViewState();
                     cap->viewChanged = true;
                     editor.ShowPlayView();
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
                     cap->savedView = editor.SaveViewState();
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
                     cap->savedView = editor.SaveViewState();
                     cap->viewChanged = true;
                     editor.ShowTopView(textured, false);
                     g_pplayer->m_liveUI->m_hideNotifications = true;
                     editor.SetExternalOverlay([cap, colliders, labels, trajectory](const std::function<ImVec2(const Vertex3Ds&)>& project, ImDrawList* drawList)
                     {
                        if (!g_pplayer)
                           return;
                        const PinTable* table = g_pplayer->m_ptable;
                        ImVec2 cropMin { FLT_MAX, FLT_MAX }, cropMax { -FLT_MAX, -FLT_MAX };
                        for (const float x : { table->m_left, table->m_right })
                           for (const float y : { table->m_top, table->m_bottom })
                              if (const ImVec2 a = project(Vertex3Ds(x, y, 0.f)); a.x != FLT_MAX)
                              {
                                 cropMin = ImVec2(min(cropMin.x, a.x), min(cropMin.y, a.y));
                                 cropMax = ImVec2(max(cropMax.x, a.x), max(cropMax.y, a.y));
                              }
                        cap->cropMin = cropMin;
                        cap->cropMax = cropMax;
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
                              if (c.x < table->m_left || c.x > table->m_right || c.y < table->m_top || c.y > table->m_bottom)
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
               const string label = modeMask != 0 || hasCamera ? " ("s + (modeMask != 0 ? "mode preview" : "") + (modeMask != 0 && hasCamera ? ", " : "") + (hasCamera ? (defaultCamera ? "default player camera" : "camera") : "") + ")" : ""s;
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
               return ToolOutput { "Playfield screenshot "s + std::to_string(tex->width()) + 'x' + std::to_string(tex->height()) + " (" + std::to_string(bytes.size()) + " bytes)" + label, b64,
                  png ? "image/png"s : "image/jpeg"s, false };
            });
         } });

}

}
