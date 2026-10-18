// license:GPLv3+

#pragma once

#include "mcp/McpServer.h"
#include "core/VPApp.h"
#include "core/vpversion.h"
#include "parts/ball.h"
#include "parts/flasher.h"
#include "parts/flipper.h"
#include "parts/primitive.h"
#include "parts/PartGroup.h"
#include "parts/plunger.h"
#include "physics/collideex.h"
#include "physics/PhysicsEngine.h"
#include "parts/light.h"
#include "parts/ramp.h"
#include "parts/rubber.h"
#include "parts/surface.h"
#include "parts/trigger.h"
#include "math/dragpoint.h"
#include "renderer/Renderer.h"
#include "renderer/Texture.h"
#include "renderer/Window.h"
#include "ui/live/LiveUI.h"
#include "ui/VPXFileFeedback.h"
#include "utils/eventproxy.h"
#include "utils/JSONSerializer.h"
#include "parts/Material.h"
#include "parts/Sound.h"
#include "parts/Collection.h"
#include "core/editablereg.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <random>

namespace VPX::MCP
{

using json = nlohmann::json;
using namespace std::string_literals;

static constexpr double DEFAULT_TOOL_TIMEOUT_S = 30.0;

// Numbers in tool results, rounded through std::format: -ffast-math turns x/10 into x*0.1 and leaves 667.9000000000001 in the JSON
inline double Round(float f, int decimals = 1) { return std::strtod(std::format("{:.{}f}", f, decimals).c_str(), nullptr); }

// Flipper geometry: base, tip at rest, fraction of the ball position along the flipper (0 = base, 1 = tip)
struct FlipperGeom
{
   string name;
   bool left = true;
   Vertex2D base;
   Vertex2D tip;
   float length = 0.f;
   bool valid = false;
   float Along(const Vertex2D& pos) const
   {
      const float dx = tip.x - base.x, dy = tip.y - base.y;
      const float len2 = dx * dx + dy * dy;
      return len2 > 0.f ? ((pos.x - base.x) * dx + (pos.y - base.y) * dy) / len2 : 0.f;
   }
};

// Geometric queries over the gameplay colliders (the shapes the ball actually collides with)
struct PartDistance
{
   IEditable* part = nullptr;
   float distance = FLT_MAX;
   Vertex3Ds closest;
   bool volume = false; // trigger or kicker volume (the ball goes through it)
};

// Table level properties, as edited by the live editor's table panel (the table itself is not a part: its stored
// definition also holds every material and can not be reloaded while a session runs)
struct TableField
{
   const char* name;
   const char* kind; // string (image or material name), number, integer, boolean, color (0x00BBGGRR)
   const char* description;
   std::function<json(const PinTable*)> get;
   std::function<void(PinTable*, const json&)> set;
   bool shaderDirty = false;
};

// Ball paths and hit points of the last simulation run, drawn by the top view screenshot
struct SimTrace
{
   vector<vector<Vertex3Ds>> paths;
   vector<std::pair<Vertex3Ds, string>> hits;
};

///////////////////////////////////////////////////////////////////////////////
// Tool registry
struct ToolDef
{
   const char* name;
   const char* description;
   json inputSchema;
   std::function<ToolStep(const json& args)> build;
   double timeoutS = DEFAULT_TOOL_TIMEOUT_S;
};

// Served as the server instructions at connect: overview, building flow and conventions
extern const char* const GUIDE_TEXT;
// Detailed notes per topic (parts, sim, screenshot...), served by vpx_status guide:<topic>
const std::map<string, const char*>& GuideTopics();
// Hash of the tool definitions and guide: a client whose tool list was fetched from an older build sees a different value in vpx_status
const string& ToolsFingerprint();
extern const char* const ACTION_NAMES[];
string ItemTypeName(ItemTypeEnum type);
ItemTypeEnum ItemTypeFromName(const string& name);
string EventName(int dispid);
ToolOutput TextOutput(const json& j);
ToolOutput ErrorOutput(const string& msg);
ToolStep Immediate(std::function<ToolOutput()> fn);
ToolStep WithPlayer(std::function<ToolOutput(Player&)> fn);
HRESULT GetDispProperty(IDispatch* disp, const string& name, json& out, string& error);
IEditable* FindPart(Player& player, const string& name);
Ball* FindBall(Player& player, int id);
json BallJson(Ball* ball);
json EventsJson(const std::vector<CapturedEvent>& events, uint32_t startMs);
// Keeps the events of one ball (and the ones without a ball, like timers) in a sim result, counting the others
void FocusEvents(json& result, int ballId);
FlipperGeom GetFlipperGeom(Player& p, IEditable* part);
vector<FlipperGeom> TableFlippers(Player& p);
// The flipper a ball is heading to: above the flipper, moving down, within its horizontal span (with a margin)
const FlipperGeom* ApproachingFlipper(const vector<FlipperGeom>& flippers, Ball* ball, float& along);
// Outline of a band of the given thickness around a polyline (walls, guides, rims): left side forward, right side back, mitered joints
json BandOutline(const json& centerline, float thickness);
json CircleOutline(float x, float y, float radius, int segments);
Vertex3Ds ClosestOnSegment(const Vertex3Ds& p, const Vertex3Ds& a, const Vertex3Ds& b);
Vertex3Ds ClosestOnTriangle(const Vertex3Ds& p, const Vertex3Ds& a, const Vertex3Ds& b, const Vertex3Ds& c);
// Closest point of a collider to p
Vertex3Ds ClosestOnCollider(const HitObject* ho, const Vertex3Ds& p);
ankerl::unordered_dense::map<IEditable*, vector<const HitObject*>> CollidersByPart(Player& p);
PartDistance DistanceToPart(IEditable* part, const vector<const HitObject*>& colliders, const Vertex3Ds& p, float maxDistance = FLT_MAX);
// Parts whose colliders come within radius of p, nearest first
vector<PartDistance> PartsNear(Player& p, const Vertex3Ds& point, float radius);
json PartDistanceJson(const PartDistance& d, float ballRadius);
// Solid parts (and other balls) a ball is resting against
json TouchingJson(Player& p, Ball* ball, bool* onPlunger = nullptr);
// Sample points along a collider's shape, used to measure the gap between two parts
void SampleCollider(const HitObject* ho, vector<Vertex3Ds>& out);
// Summarizes a run for one ball: stayed on the table, drained, or captured by a kicker; a ball destroyed by the script right after a kicker hit counts as captured by it
void AddOutcome(json& result, Player& p, Ball* ball);
int ActionIndex(const string& name);
DragPointCurve* GetCurve(IEditable* part);
json PointsJson(const DragPointCurve* curve);
void SetCurvePoints(DragPointCurve* curve, const json& points);
bool IsEditing(Player& p);
bool IsLiveCopy(Player& p);
string LiveCopyError();
ToolOutput WithLiveCopyNote(Player& p, json result);
string RequireEditor(Player& p);
// Record the part state for undo before an MCP edit (only meaningful while the editor is open on the base table)
void RecordEdit(Player& p, IEditable* part, bool withMesh = false);
// Record how to revert a non-part edit (asset, script, table property) made to table: undoable while the editor is open on it, otherwise the table is only marked dirty
void RecordAction(Player& p, PinTable* table, std::function<void()> undo);
// Flat textured shapes in VPX units, z up, bottom at z 0, the top face mapped to the whole image
bool GenerateMesh(Mesh& mesh, const string& shape, float radius, float height, float width, float depth, int segments, string& error);
json RampCheckJson(Player& p, const vector<Ramp*>& ramps, float ballRadius, float joinDistance);
bool PartWorldMesh(IEditable* part, vector<Vertex3Ds>& vertices, vector<unsigned int>& indices, string& error);
// World axis aligned bounds of a part ({min, max} in table coordinates, z up), null when the part has no geometry
json PartBoundsJson(IEditable* part);
void RefreshPart(Player& p, IEditable* part);
void TranslatePart(Player& p, IEditable* part, float dx, float dy);
void PlaceParts(Player& p, const vector<IEditable*>& parts, float x, float y);
json PartsJson(const vector<IEditable*>& parts);
json PackInfoJson(const std::filesystem::path& path);
vector<std::filesystem::path> LibraryFolders();
CComObject<PinTable>* LoadDonorTable(const std::filesystem::path& path, string& error);
vector<string> ScriptLines(const string& script);
// Direct access to the persisted state of an object: every field the table file stores, named as in the VPZ packs, no script gate
nlohmann::ordered_json ObjectDoc(int nodeKind, const std::function<void(JSONObjectWriter&)>& save, string& error);
json ToJson(const nlohmann::ordered_json& doc);
void MergeDoc(nlohmann::ordered_json& doc, const json& patch);
string NormalizeFieldName(const string& name);
// Reloads a part from a document in place, the way undo restores a part: name and group are applied through the table, render and physics state is refreshed
string ReloadPart(Player& p, IEditable* part, const nlohmann::ordered_json& doc);
nlohmann::ordered_json PartDoc(IEditable* part, string& error);
// Maps the caller's field names onto the stored ones: exact names pass, names that only differ by case and underscores (HeightTop -> height_top) are mapped, the rest is an error
string ResolvePartData(IEditable* part, const json& data, json& resolved, json& mapped, nlohmann::ordered_json& doc);
// Merges resolved stored fields into the part definition and reloads it in place
void ApplyPartData(Player& p, IEditable* part, const json& resolved, nlohmann::ordered_json& doc, json& applied, string& warning);
void AddSidecarWarning(const PinTable* table, json& result);
// Saves the edited table like the editor does: with the editor open on it, through the editor (it saves the authored part visibility, not its
// display overrides, and sets the undo clean point); otherwise directly
bool SaveTableFile(Player& p, PinTable* table);
json StatusJson(Player& player);
CComObject<PinTable>* LoadTableForSwitch(const std::filesystem::path& path, string& error);
json ScriptErrorJson(const ScriptError& e);
// Meaning, enum values, units and directions of the stored fields that can not be guessed from their name and value
const char* FieldMeaning(ItemTypeEnum type, const string& field);
const vector<TableField>& TableFields();
extern SimTrace g_lastSimTrace;
json Schema(json properties, json required = json::array());
json Prop(const char* type, const char* description);
json PointsProp(const char* description);
// Script errors after a sequence number, deduplicated by line and message (a script failing every frame would flood the result)
json ScriptErrorsSummary(uint64_t after, size_t limit = 10);
// Errors with a sequence number above the given one
json ScriptErrorsJson(uint64_t after);
ToolOutput ScriptGet(const string& script, const json& args);
// Event handlers (Sub <Part>_<Event>) of the given parts found in a script, plus the donor Subs and Functions they call (transitively, a few levels),
// as text, skipping the ones already declared in 'existing'
string CollectPartHandlers(const string& script, const vector<string>& partNames, const string& existing, json& copied);

// Shared by the vpx_sim actions: runs the physics for a while, sampling the given balls
struct SimRun
{
   bool started = false;
   uint32_t startMs = 0;
   uint32_t lastSampleMs = 0;
   uint64_t errorsBefore = 0;
   uint64_t logCursor = 0;
   std::map<int, json> samples;
   std::map<int, float> maxZ;
   // Per frame contact tracking (Track): the surfaces each ball rode on, every part it touched, and the crossings of an optional line
   ankerl::unordered_dense::map<IEditable*, vector<const HitObject*>> colliders;
   ankerl::unordered_dense::map<IEditable*, FRect3D> partBoxes; // Union of a part's collider boxes: parts out of reach are skipped before their colliders are visited
   bool collidersReady = false;
   std::map<int, json> rode;
   std::map<int, string> support;
   std::map<int, vector<string>> contacts;
   int crossAxis = -1; // 0 = x, 1 = y
   float crossValue = 0.f;
   std::map<int, json> crossings;
   std::map<int, vec3> lastPos;

   void Track(Player& p, const std::function<bool(Ball*)>& filter);
   json BallPathJson(int id);

   void Sample(Player& p, const std::function<bool(Ball*)>& filter)
   {
      for (Ball* b : p.m_vball)
      {
         if (!filter(b))
            continue;
         int id = -1;
         b->get_ID(&id);
         const vec3& pos = b->GetPosition();
         const vec3& vel = b->GetVelocity();
         samples[id].push_back({ { "t", p.m_time_msec - startMs }, { "x", Round(pos.x) }, { "y", Round(pos.y) }, { "z", Round(pos.z) }, { "vx", Round(vel.x, 2) }, { "vy", Round(vel.y, 2) }, { "vz", Round(vel.z, 2) } });
         maxZ[id] = std::max(maxZ.contains(id) ? maxZ[id] : 0.f, pos.z - b->GetRadius());
      }
      lastSampleMs = p.m_time_msec;
   }

   void Begin(Player& p, int ms)
   {
      started = true;
      startMs = p.m_time_msec;
      errorsBefore = McpServer::Get().GetScriptErrorCount();
      logCursor = McpServer::Get().GetLogCount();
      McpServer::Get().StartEventCapture();
      p.m_ignoreWndFocus = true;
      p.SetPlayState(true, ms);
   }

   json Finish(Player& p)
   {
      p.SetPlayState(false);
      const json errs = ScriptErrorsSummary(errorsBefore);
      json events = json::array();
      for (const json& e : EventsJson(McpServer::Get().StopEventCapture(), startMs))
      {
         if (!events.empty())
         {
            json& last = events.back();
            if (last["part"] == e["part"] && last["event"] == e["event"] && last.value("ball", -1) == e.value("ball", -1))
            {
               last["count"] = last.value("count", 1) + 1;
               last["tEnd"] = e["t"];
               continue;
            }
         }
         events.push_back(e);
      }
      json result { { "simulatedMs", p.m_time_msec - startMs }, { "events", events }, { "scriptErrors", errs } };
      g_lastSimTrace = SimTrace();
      for (const auto& [id, ballSamples] : samples)
      {
         vector<Vertex3Ds> path;
         for (const json& sm : ballSamples)
            path.emplace_back(sm.value("x", 0.f), sm.value("y", 0.f), sm.value("z", 0.f));
         if (!path.empty())
            g_lastSimTrace.paths.push_back(path);
         for (const json& e : events)
         {
            if (e.value("ball", -1) != id || ballSamples.empty())
               continue;
            const int t = e.value("t", 0);
            const json* nearest = &ballSamples[0];
            for (const json& sm : ballSamples)
               if (std::abs(sm.value("t", 0) - t) < std::abs(nearest->value("t", 0) - t))
                  nearest = &sm;
            g_lastSimTrace.hits.emplace_back(Vertex3Ds(nearest->value("x", 0.f), nearest->value("y", 0.f), nearest->value("z", 0.f)), e.value("part", ""s) + ' ' + e.value("event", ""s));
         }
      }
      json warnings = json::array();
      for (const string& line : McpServer::Get().GetLogSince(logCursor))
         if ((line.rfind("WARN ", 0) == 0 || line.rfind("ERROR ", 0) == 0 || line.rfind("FATAL ", 0) == 0) && std::ranges::find(warnings, json(line)) == warnings.end() && warnings.size() < 20)
            warnings.push_back(line);
      if (!warnings.empty())
         result["warnings"] = warnings;
      return result;
   }
};

const std::vector<ToolDef>& Tools();
void AddSessionTools(std::vector<ToolDef>& tools);
void AddSimTools(std::vector<ToolDef>& tools);
void AddScriptTools(std::vector<ToolDef>& tools);
void AddPartTools(std::vector<ToolDef>& tools);
void AddAssetsTools(std::vector<ToolDef>& tools);
void AddLibraryTools(std::vector<ToolDef>& tools);
void AddTableTools(std::vector<ToolDef>& tools);
void AddScreenshotTools(std::vector<ToolDef>& tools);
// The steps behind vpx_sim and vpx_screenshot, also used by the wrappers that run them several times (sweep, isolate, turntable)
ToolStep BuildSimStep(const json& args);
ToolStep BuildScreenshotStep(const json& args);

}
