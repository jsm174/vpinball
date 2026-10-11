// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

const char* const GUIDE_TEXT = R"(Visual Pinball X MCP server. The tools act on the table session currently running inside VPX.
Coordinates are VPX units (1 inch = 47.06 units); x grows to the right, y grows toward the player, z is up (0 = playfield).
Tools: vpx_status (session, errors, log), vpx_table (open/new/save_as/audit), vpx_editor (open/close/playtest/stop/undo/save), vpx_part (list/get/set/add/delete/points),
vpx_library (list/inspect/get_script/clone/import/export: complete assemblies with materials and script handlers from donor tables such as the VPX example tables),
vpx_script (get/set/replace/append, run/eval in the live script), vpx_play (play/pause/step N ms), vpx_ball (list/create/set/destroy), vpx_sim (shot/plunge/trace with trajectories and script events), vpx_input, vpx_screenshot, vpx_export (pack the table and its companion files as a .vpxz for the mobile apps).
Building flow: vpx_editor open (or launch VPX with -LiveEdit), vpx_library clone or vpx_part add, vpx_script to wire logic, vpx_editor playtest, vpx_sim plunge/shot, vpx_editor stop, repeat, vpx_editor save.
Bare parts from vpx_part add have no materials: prefer vpx_library clone from a donor table.
Assets: vpx_image add <file> returns the image name to put in a part's image field; vpx_material list/add, vpx_sound list/add. Parts are edited through their stored fields (vpx_part schema <type> lists them).
A flat textured disc, post or plastic is vpx_part add Primitive then vpx_part mesh shape:disc (the image covers the top face); meshes from .obj/.glb files load the same way.
Save often with vpx_editor save: vpx_status shows mcpEditsSinceSave, and a VPX crash loses everything since the last save.
Game time only advances while the table is playing; vpx_play with ms runs exactly that much game time then pauses. Automation ignores window focus once a play, sim, plunge or playtest tool has been called.
Conventions:
- Units: VPX units (50 units = 1.0625 inch, a standard ball has radius 25 and diameter 50); heights are above the playfield (z up); angles in degrees.
- 2D angles (flipper start_angle/end_angle, kicker orientation and Kick angle, gate and spinner rotation, trigger and decal rotation, hit target rot_z, primitive prim_rot1_z) turn clockwise seen from above; for directions 0 points up the table (toward the backglass, -y), 90 to the right (+x).
- Colors are integers 0x00BBGGRR (red in the low byte): pure red is 255, pure blue is 16711680.
- Primitive rotations: prim_rot1_x/y/z are the script's RotX/RotY/RotZ, prim_rot2_x/y/z the script's ObjRotX/ObjRotY/ObjRotZ, prim_trans_x/y/z the TransX/TransY/TransZ offset applied before RotX/Y/Z (so it orbits around position); size multiplies the mesh.
- Walls: is_bottom_solid makes the bottom face collide too, so a wall above the playfield acts as a ceiling (keeps a ball on an upper deck).
- Ramps: height goes linearly from height_bottom at the first point to height_top at the last one, along the path (vpx_part points reports calcHeight per point); wire ramp types sit at that height with their wires above it.
- vpx_part schema type:<Type> lists each stored field with its meaning, enum values and units where known.
- Where a part ends up: vpx_part get reports bounds (world box after every transform, z up); vpx_part vertices returns a primitive's transformed vertices (or writes them as .obj), so positions and rotation directions never need to be worked out by hand.
Play modes and VR: part groups choose the play modes they show in (desktop, fss, cabinet, vr, mr) and a space (playfield, or cabinet/cabinet feet/room, placed through the table's default cabinet transform).
VPX uses the VR/MR visibility only when a headset is actually connected. RenderingModeOverride 2 (forced VR, e.g. on a phone) without a headset makes the script see RenderingMode 2, but parts are filtered and the camera set by the current view mode (desktop, FSS or cabinet):
VR-only groups stay hidden and cabinet space parts are drawn on the normal view. vpx_screenshot mode:'vr' (or desktop/fss/cabinet) previews a mode, camera {eye, target} picks any viewpoint; vpx_table audit lists the VR groups, cabinet bounds and likely problems.
If vpx_status server.toolsFingerprint differs from the suffix of the server version seen at connect, or a documented parameter is refused, the client's tool list is stale: ask the user to reconnect the MCP client.
FlexDMD in VPX: the plugin needs Run = True to produce frames (Show is already True by default, False hides it). Images named "VPX.<image>" are read from the saved table file, so images added since the last save are missing: save first, or use image files on disk relative to ProjectFolder. To show it on the playfield, use a Flasher with render_mode 1 (DMD) and an empty image_src_link (the default DMD). vpx_screenshot target:"dmd" returns the current frame at native resolution; a plugin call that fails (e.g. a FlexDMD object reporting an error) raises a script error, reported like any other (vpx_status recentScriptErrors, the errors of vpx_script run); other plugin messages (missing assets...) are in vpx_status recentLogErrors and in the warnings of sim results.)";

const char* const ITEM_TYPE_NAMES[] = { "Wall", "Flipper", "Timer", "Plunger", "Textbox", "Bumper", "Trigger", "Light", "Kicker", "Decal", "Gate", "Spinner", "Ramp", "Table",
   "LightCenter", "DragPoint", "Collection", "DispReel", "LightSeq", "Primitive", "Flasher", "Rubber", "HitTarget", "Ball", "PartGroup" };

const char* const ACTION_NAMES[] = { "LeftFlipper", "RightFlipper", "StagedLeftFlipper", "StagedRightFlipper", "LeftMagnaSave", "RightMagnaSave", "LaunchBall", "LeftNudge",
   "CenterNudge", "RightNudge", "Tilt", "AddCredit", "AddCredit2", "StartGame", "Lockbar", "Pause", "PerfOverlay", "OpenInGameUI", "ExitGame", "InGameUI", "VolumeDown", "VolumeUp",
   "VRRecenter", "VRUp", "VRDown" };

string ItemTypeName(ItemTypeEnum type)
{
   const size_t i = static_cast<size_t>(type);
   return i < std::size(ITEM_TYPE_NAMES) ? ITEM_TYPE_NAMES[i] : "Unknown"s;
}

ItemTypeEnum ItemTypeFromName(const string& name)
{
   for (size_t i = 0; i < std::size(ITEM_TYPE_NAMES); i++)
      if (StrCompareNoCase(name, ITEM_TYPE_NAMES[i]))
         return static_cast<ItemTypeEnum>(i);
   return eItemInvalid;
}

string EventName(int dispid)
{
   switch (dispid)
   {
   case DISPID_HitEvents_Hit: return "Hit"s;
   case DISPID_HitEvents_Unhit: return "Unhit"s;
   case DISPID_SurfaceEvents_Slingshot: return "Slingshot"s;
   case DISPID_FlipperEvents_Collide: return "Collide"s;
   case DISPID_SpinnerEvents_Spin: return "Spin"s;
   case DISPID_TargetEvents_Dropped: return "Dropped"s;
   case DISPID_TargetEvents_Raised: return "Raised"s;
   case DISPID_LimitEvents_EOS: return "EOS"s;
   case DISPID_LimitEvents_BOS: return "BOS"s;
   default: return ""s;
   }
}

ToolOutput TextOutput(const json& j) { return ToolOutput { j.dump(2), ""s, ""s, false }; }

ToolOutput ErrorOutput(const string& msg) { return ToolOutput { msg, ""s, ""s, true }; }

ToolStep Immediate(std::function<ToolOutput()> fn)
{
   return [fn]() -> std::optional<ToolOutput> { return fn(); };
}

ToolStep WithPlayer(std::function<ToolOutput(Player&)> fn)
{
   return Immediate([fn]() { return g_pplayer ? fn(*g_pplayer) : ErrorOutput("No table session is running"s); });
}

string VariantToString(VARIANT& v, json& out)
{
   switch (V_VT(&v))
   {
   case VT_EMPTY:
   case VT_NULL: out = nullptr; return ""s;
   case VT_BOOL: out = V_BOOL(&v) != VARIANT_FALSE; return ""s;
   case VT_I2: out = V_I2(&v); return ""s;
   case VT_I4: out = V_I4(&v); return ""s;
   case VT_UI4: out = V_UI4(&v); return ""s;
   case VT_R4: out = V_R4(&v); return ""s;
   case VT_R8: out = V_R8(&v); return ""s;
   case VT_BSTR: out = MakeString(V_BSTR(&v)); return ""s;
   case VT_DISPATCH: out = "[object]"s; return ""s;
   default: break;
   }
   VARIANT conv;
   VariantInit(&conv);
   if (SUCCEEDED(VariantChangeType(&conv, &v, 0, VT_BSTR)))
   {
      out = MakeString(V_BSTR(&conv));
      VariantClear(&conv);
      return ""s;
   }
   return "Unsupported variant type "s + std::to_string(V_VT(&v));
}

HRESULT GetDispProperty(IDispatch* disp, const string& name, json& out, string& error)
{
   wstring wname = MakeWString(name);
   LPOLESTR names[] = { const_cast<LPOLESTR>(wname.c_str()) };
   DISPID dispid;
   HRESULT hr = disp->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &dispid);
   if (FAILED(hr))
   {
      error = "Unknown property '"s + name + '\'';
      return hr;
   }
   DISPPARAMS params = { nullptr, nullptr, 0, 0 };
   VARIANT result;
   VariantInit(&result);
   EXCEPINFO excep {};
   hr = disp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &params, &result, &excep, nullptr);
   if (FAILED(hr))
   {
      error = "Failed to read '"s + name + "' (hr=" + std::to_string(hr) + ')';
      return hr;
   }
   error = VariantToString(result, out);
   VariantClear(&result);
   return error.empty() ? S_OK : E_FAIL;
}

IEditable* FindPart(Player& player, const string& name)
{
   for (IEditable* part : player.m_ptable->GetParts())
      if (StrCompareNoCase(part->GetName(), name))
         return part;
   return nullptr;
}

Ball* FindBall(Player& player, int id)
{
   for (Ball* ball : player.m_vball)
   {
      int ballId;
      if (SUCCEEDED(ball->get_ID(&ballId)) && ballId == id)
         return ball;
   }
   return nullptr;
}

json BallJson(Ball* ball)
{
   int id = -1;
   ball->get_ID(&id);
   const vec3& p = ball->GetPosition();
   const vec3& v = ball->GetVelocity();
   return json { { "id", id }, { "x", p.x }, { "y", p.y }, { "z", p.z }, { "vx", v.x }, { "vy", v.y }, { "vz", v.z }, { "radius", ball->GetRadius() } };
}

json EventsJson(const std::vector<CapturedEvent>& events, uint32_t startMs)
{
   json arr = json::array();
   for (const CapturedEvent& e : events)
   {
      json j { { "t", e.gameTimeMs - startMs }, { "part", e.part }, { "event", e.event } };
      if (e.ballId >= 0)
         j["ball"] = e.ballId;
      arr.push_back(j);
   }
   return arr;
}

// Keeps the events of one ball (and the ones without a ball, like timers) in a sim result, counting the others
void FocusEvents(json& result, int ballId)
{
   json mine = json::array();
   int others = 0;
   for (const json& e : result["events"])
   {
      if (!e.contains("ball") || e["ball"].get<int>() == ballId)
         mine.push_back(e);
      else
         others++;
   }
   result["events"] = mine;
   result["otherBallEvents"] = others;
}

// Summarizes a run for one ball: stayed on the table, drained, or captured by a kicker; a ball destroyed by the script right after a kicker hit counts as captured by it
void AddOutcome(json& result, Player& p, Ball* ball)
{
   string lastKicker;
   for (const json& e : result["events"])
      if (e["event"] == "Hit")
         if (IEditable* part = FindPart(p, e["part"].get<string>()); part && part->GetItemType() == eItemKicker)
            lastKicker = part->GetName();
   if (ball)
   {
      const vec3& pos = ball->GetPosition();
      const bool drained = pos.y > p.m_ptable->m_bottom;
      result["drained"] = drained;
      const vec3& vel = ball->GetVelocity();
      const float speed = sqrtf(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
      const bool inKicker = ball->m_hitBall.m_d.m_lockedInKicker;
      const string state = drained ? "drained"s : inKicker ? "in kicker"s : speed < 0.5f ? "settled"s : "moving"s;
      result["state"] = state;
      result["outcome"] = drained ? "drained"s : inKicker ? ("captured by "s + lastKicker) : ("on the table, "s + state);
      if (!drained)
      {
         json touching = json::array();
         for (const PartDistance& d : PartsNear(p, Vertex3Ds(pos.x, pos.y, pos.z), ball->GetRadius() + 2.f))
            if (!d.volume)
               touching.push_back(d.part->GetName());
         result["touching"] = touching;
      }
   }
   else
   {
      const bool drainLike = !lastKicker.empty() && (lowerCase(lastKicker).find("drain") != string::npos || lowerCase(lastKicker).find("outhole") != string::npos);
      result["ballDestroyedByScript"] = true;
      result["drained"] = drainLike;
      result["outcome"] = lastKicker.empty() ? "destroyed by the script"s : (drainLike ? "drained ("s + lastKicker + ')' : "captured by "s + lastKicker + " and destroyed by the script");
   }
   if (!lastKicker.empty())
      result["lastKicker"] = lastKicker;
}

int ActionIndex(const string& name)
{
   for (size_t i = 0; i < std::size(ACTION_NAMES); i++)
      if (StrCompareNoCase(name, ACTION_NAMES[i]))
         return static_cast<int>(i);
   return -1;
}

DragPointCurve* GetCurve(IEditable* part)
{
   switch (part->GetItemType())
   {
   case eItemSurface: return &static_cast<Surface*>(part)->m_curve;
   case eItemRamp: return &static_cast<Ramp*>(part)->m_curve;
   case eItemRubber: return &static_cast<Rubber*>(part)->m_curve;
   case eItemFlasher: return &static_cast<Flasher*>(part)->m_curve;
   case eItemLight: return &static_cast<Light*>(part)->m_curve;
   case eItemTrigger: return &static_cast<Trigger*>(part)->m_curve;
   default: return nullptr;
   }
}

json PointsJson(const DragPointCurve* curve)
{
   json arr = json::array();
   for (const auto& dp : curve->GetPoints())
   {
      const Vertex3Ds& v = dp->GetVertex();
      arr.push_back({ { "x", v.x }, { "y", v.y }, { "z", v.z }, { "smooth", dp->IsSmooth() }, { "slingshot", dp->m_slingshot }, { "calcHeight", dp->GetCalcHeight() } });
   }
   return arr;
}

void SetCurvePoints(DragPointCurve* curve, const json& points)
{
   curve->ClearPoints();
   for (const json& pt : points)
      curve->PushPoint(std::make_unique<DragPoint>(curve, pt.value("x", 0.f), pt.value("y", 0.f), pt.value("z", 0.f), pt.value("smooth", false)));
   for (size_t i = 0; i < points.size(); i++)
      curve->GetPoints()[i]->m_slingshot = points[i].value("slingshot", false);
}

bool IsEditing(Player& p) { return p.m_liveUI && p.m_liveUI->IsEditorUIOpened() && !p.m_liveUI->m_editorUI.IsInspectMode(); }

bool IsLiveCopy(Player& p) { return p.m_ptable->m_liveBaseTable != nullptr; }

string LiveCopyError() { return "A playtest is running: stop it with vpx_editor stop first, a change made now would only reach the playtest copy and be lost when it stops"s; }

ToolOutput WithLiveCopyNote(Player& p, json result)
{
   if (IsLiveCopy(p))
      result["warning"] = "Applied to the playtest copy only: it is lost when the playtest stops. Stop the playtest first to edit the table itself";
   return TextOutput(result);
}

string RequireEditor(Player& p)
{
   if (!p.m_liveUI || !p.m_liveUI->IsEditorUIOpened())
      return "The editor is not open: call vpx_editor open first"s;
   if (p.m_liveUI->m_editorUI.IsInspectMode())
      return "A playtest is running: stop it with vpx_editor stop before editing"s;
   return ""s;
}

// Record the part state for undo before an MCP edit (only meaningful while the editor is open on the base table)
void RecordEdit(Player& p, IEditable* part, bool withMesh)
{
   if (IsEditing(p))
      p.m_liveUI->m_editorUI.RecordPartEdit(part, withMesh);
}

void RefreshPart(Player& p, IEditable* part)
{
   p.m_renderer->ReinitRenderable(part->GetIRenderable());
   if (part->GetIHitable() && p.m_physics)
      p.m_physics->Update(part);
}

void TranslatePart(Player& p, IEditable* part, float dx, float dy)
{
   part->Translate(Vertex2D(dx, dy));
   RefreshPart(p, part);
}

void PlaceParts(Player& p, const vector<IEditable*>& parts, float x, float y)
{
   if (parts.empty())
      return;
   Vertex2D centroid(0.f, 0.f);
   int n = 0;
   for (IEditable* part : parts)
      if (part->GetItemType() != eItemPartGroup)
      {
         const Vertex2D c = part->GetCenter();
         centroid.x += c.x;
         centroid.y += c.y;
         n++;
      }
   if (n == 0)
      return;
   const float dx = x - centroid.x / n, dy = y - centroid.y / n;
   for (IEditable* part : parts)
      if (part->GetItemType() != eItemPartGroup)
         TranslatePart(p, part, dx, dy);
}

json PartsJson(const vector<IEditable*>& parts)
{
   json arr = json::array();
   for (IEditable* part : parts)
   {
      json j { { "name", part->GetName() }, { "type", ItemTypeName(part->GetItemType()) } };
      if (part->GetPartGroup())
         j["group"] = part->GetPartGroup()->GetName();
      if (part->GetItemType() != eItemPartGroup)
      {
         const Vertex2D c = part->GetCenter();
         j["x"] = c.x;
         j["y"] = c.y;
      }
      arr.push_back(j);
   }
   return arr;
}

json PackInfoJson(const std::filesystem::path& path)
{
   json j { { "path", PathToUTF8(path) } };
   const auto pack = JSONSerializer::CreateReader(path);
   if (!pack)
   {
      j["valid"] = false;
      return j;
   }
   j["valid"] = true;
   json parts = json::array();
   int materials = 0, images = 0, sounds = 0, collections = 0;
   for (const std::filesystem::path& file : pack->ListFiles())
   {
      const string folder = PathToUTF8(file.parent_path());
      if (file.extension() != ".json")
         continue;
      if (folder == "parts")
         parts.push_back(PathToUTF8(file.stem()));
      else if (folder == "materials")
         materials++;
      else if (folder == "images")
         images++;
      else if (folder == "sounds")
         sounds++;
      else if (folder == "collections")
         collections++;
   }
   j["parts"] = parts;
   j["materials"] = materials;
   j["images"] = images;
   j["sounds"] = sounds;
   j["collections"] = collections;
   return j;
}

void ScanLibraryFolder(const std::filesystem::path& folder, json& packs, int depth)
{
   std::error_code ec;
   for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, ec))
   {
      if (entry.is_directory(ec) && depth < 6)
         ScanLibraryFolder(entry.path(), packs, depth + 1);
      else if (entry.is_regular_file(ec) && lowerCase(PathToUTF8(entry.path().extension())) == ".vpz"s)
         packs.push_back(PackInfoJson(entry.path()));
   }
}

vector<std::filesystem::path> LibraryFolders()
{
   vector<std::filesystem::path> folders;
   const string list = g_settingsService.GetAppSettings().GetEditor_PartLibraryFolders();
   size_t start = 0;
   while (start <= list.size())
   {
      const size_t end = list.find(';', start);
      const string entry = list.substr(start, end == string::npos ? string::npos : end - start);
      if (!entry.empty())
         folders.push_back(PathFromUTF8(entry));
      if (end == string::npos)
         break;
      start = end + 1;
   }
   return folders;
}

CComObject<PinTable>* LoadDonorTable(const std::filesystem::path& path, string& error)
{
   if (path.empty() || !FileExists(path))
   {
      error = "Table not found: "s + PathToUTF8(path);
      return nullptr;
   }
   CComObject<PinTable>* table;
   CComObject<PinTable>::CreateInstance(&table);
   table->AddRef();
   VPXFileFeedback feedback;
   const HRESULT hr = table->LoadGameFromFilename(path, feedback);
   if (FAILED(hr) && hr != APPX_E_BLOCK_HASH_INVALID && hr != APPX_E_CORRUPT_CONTENT)
   {
      table->Release();
      error = "Failed to load "s + PathToUTF8(path);
      return nullptr;
   }
   return table;
}

// Saves the edited table like the editor does: with the editor open on it, through the editor (it saves the authored part visibility, not its
// display overrides, and sets the undo clean point); otherwise directly
bool SaveTableFile(Player& p, PinTable* table)
{
   if (p.m_liveUI && p.m_liveUI->IsEditorUIOpened() && p.m_ptable == table && !p.m_liveUI->m_editorUI.IsInspectMode())
      return p.m_liveUI->m_editorUI.SaveTable();
   VPXFileFeedback feedback;
   if (FAILED(table->Save(feedback)))
      return false;
   table->SetDirty(SaveDirtyState::Clean);
   return true;
}

json StatusJson(Player& player)
{
   json j;
   j["tablePath"] = PathToUTF8(player.m_ptable->m_filename);
   j["tableName"] = player.m_ptable->m_title;
   j["tableWidth"] = player.m_ptable->m_right - player.m_ptable->m_left;
   j["tableHeight"] = player.m_ptable->m_bottom - player.m_ptable->m_top;
   switch (player.m_playMode)
   {
   case Player::PlayMode::Play: j["playMode"] = "Play"; break;
   case Player::PlayMode::LiveEdit: j["playMode"] = "LiveEdit"; break;
   case Player::PlayMode::FullEdit: j["playMode"] = "FullEdit"; break;
   case Player::PlayMode::CaptureAttract: j["playMode"] = "CaptureAttract"; break;
   default: j["playMode"] = "Unknown"; break;
   }
   j["playing"] = player.IsPlaying();
   j["windowFocused"] = player.m_playfieldWnd->IsFocused();
   j["gameTimeMs"] = player.m_time_msec;
   j["balls"] = player.m_vball.size();
   j["editorOpened"] = player.m_liveUI && player.m_liveUI->IsEditorUIOpened();
   j["isLiveCopy"] = player.m_ptable->m_liveBaseTable != nullptr;
   j["scriptRunning"] = player.m_scriptInterpreter != nullptr;
   j["canUndo"] = IsEditing(player) && player.m_liveUI->m_editorUI.HasUndo();
   j["mcpPort"] = McpServer::Get().GetPort();
   j["mcpEditsSinceSave"] = McpServer::Get().GetEditsSinceSave();
   if (player.m_audioPlayer)
      j["musicPlaying"] = player.m_audioPlayer->IsMusicPlaying();
   AddSidecarWarning(player.m_ptable->m_liveBaseTable ? player.m_ptable->m_liveBaseTable : player.m_ptable, j);
   static const auto startup = std::chrono::steady_clock::now();
   j["uptimeMinutes"] = std::chrono::duration_cast<std::chrono::minutes>(std::chrono::steady_clock::now() - startup).count();
#ifdef __APPLE__
   if (const char* home = getenv("HOME"))
   {
      std::error_code ec;
      std::filesystem::path newest;
      std::filesystem::file_time_type newestTime;
      for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(home) / "Library/Logs/DiagnosticReports", ec))
      {
         const string fn = entry.path().filename().string();
         if (fn.rfind("VPinballX", 0) != 0 || entry.path().extension() != ".ips")
            continue;
         const auto t = entry.last_write_time(ec);
         if (newest.empty() || t > newestTime)
         {
            newest = entry.path();
            newestTime = t;
         }
      }
      if (!newest.empty())
      {
         const auto age = std::chrono::duration_cast<std::chrono::minutes>(std::filesystem::file_time_type::clock::now() - newestTime).count();
         if (age <= 24 * 60)
            j["lastCrashReport"] = json { { "path", PathToUTF8(newest) }, { "ageMinutes", age } };
      }
   }
#endif
   return j;
}

CComObject<PinTable>* LoadTableForSwitch(const std::filesystem::path& path, string& error)
{
   if (!path.empty() && !FileExists(path))
   {
      error = "File not found: "s + PathToUTF8(path);
      return nullptr;
   }
   CComObject<PinTable>* table;
   CComObject<PinTable>::CreateInstance(&table);
   table->AddRef();
   VPXFileFeedback feedback;
   if (path.empty())
   {
      table->m_glassTopHeight = table->m_glassBottomHeight = 210;
      for (int i = 0; i < 16; i++)
         table->m_rgcolorcustom[i] = RGB(0, 0, 0);
      table->LoadGameFromFilename(g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Assets, "blankTable.vpx"), feedback);
      table->m_title = "Table1"s;
      table->GetSettings().SetIniPath(std::filesystem::path());
      table->m_filename.clear();
   }
   else
   {
      const HRESULT hr = table->LoadGameFromFilename(path, feedback);
      if (FAILED(hr) && hr != APPX_E_BLOCK_HASH_INVALID && hr != APPX_E_CORRUPT_CONTENT)
      {
         table->Release();
         error = "Failed to load "s + PathToUTF8(path);
         return nullptr;
      }
   }
   return table;
}

SimTrace g_lastSimTrace;

json Schema(json properties, json required)
{
   return json { { "type", "object" }, { "properties", std::move(properties) }, { "required", std::move(required) } };
}

json Prop(const char* type, const char* description) { return json { { "type", type }, { "description", description } }; }

json PointsProp(const char* description) { return json { { "type", "array" }, { "items", json { { "type", "object" } } }, { "description", description } }; }

}
