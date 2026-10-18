// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

void AddSessionTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_status", "Current session: loaded table, play mode, playing/paused, game time, ball count, editor state, undo availability, recent script errors, mcpEditsSinceSave (edits made through MCP since the last save: a crash loses them, save often), uptimeMinutes, and on macOS lastCrashReport (the newest VPinballX crash report of the last day with its age: older than the uptime means the previous run crashed). Options: logLines to append the VPX log tail, guide:true for the usage guide, guide:\"<topic>\" for the detailed notes of a topic (parts, geometry, sim, screenshot, library, table, script, export, vr, flexdmd).",
         Schema({ { "logLines", Prop("integer", "Append the last N log lines (max 500)") },
            { "guide", json { { "type", json::array({ "boolean", "string" }) }, { "description", "true: the usage guide; a topic name: its detailed notes (parts, geometry, sim, screenshot, library, table, script, export, vr, flexdmd)" } } } }),
         [](const json& args)
         {
            const size_t logLines = std::min<size_t>(500, args.value("logLines", 0));
            const json guide = args.value("guide", json(false));
            return Immediate([logLines, guide]()
            {
               if (guide.is_string())
               {
                  const auto it = GuideTopics().find(lowerCase(guide.get<string>()));
                  json topics = json::array();
                  for (const auto& [topic, text] : GuideTopics())
                     topics.push_back(topic);
                  if (it == GuideTopics().end())
                     return ErrorOutput("Unknown guide topic '"s + guide.get<string>() + "', topics: " + topics.dump());
                  return TextOutput(json { { "topic", it->first }, { "guide", it->second }, { "topics", topics } });
               }
               json j = g_pplayer ? StatusJson(*g_pplayer) : json { { "session", "none" } };
               McpServer::Get().SyncSession();
               const int session = McpServer::Get().GetSession();
               vector<ScriptError> current, previous;
               for (const ScriptError& e : McpServer::Get().GetScriptErrors())
                  (e.session == session ? current : previous).push_back(e);
               auto lastFive = [](const vector<ScriptError>& v)
               {
                  json arr = json::array();
                  for (size_t i = v.size() > 5 ? v.size() - 5 : 0; i < v.size(); i++)
                     arr.push_back(ScriptErrorJson(v[i]));
                  return arr;
               };
               j["scriptErrors"] = current.size();
               j["recentScriptErrors"] = lastFive(current);
               if (current.empty() && !previous.empty())
               {
                  std::erase_if(previous, [&previous](const ScriptError& e) { return e.session != previous.back().session; });
                  j["previousSessionScriptErrors"] = lastFive(previous);
               }
               j["lastScriptErrorSeq"] = McpServer::Get().GetScriptErrorCount();
               json logErrors = json::array();
               for (const string& line : McpServer::Get().GetLogTail(400))
                  if ((line.rfind("ERROR ", 0) == 0 || line.rfind("FATAL ", 0) == 0) && line.find("Runtime error on line") == string::npos && line.find("Compile error on line") == string::npos)
                     logErrors.push_back(line);
               if (logErrors.size() > 5)
                  logErrors.erase(logErrors.begin(), logErrors.end() - 5);
               j["recentLogErrors"] = logErrors;
               if (logLines > 0)
                  j["log"] = McpServer::Get().GetLogTail(logLines);
               if (guide.is_boolean() && guide.get<bool>())
               {
                  j["guide"] = GUIDE_TEXT;
                  json topics = json::array();
                  for (const auto& [topic, text] : GuideTopics())
                     topics.push_back(topic);
                  j["guideTopics"] = topics;
               }
               j["server"] = json { { "version", VP_VERSION_STRING_DIGITS }, { "toolsFingerprint", ToolsFingerprint() },
                  { "note", "If toolsFingerprint differs from the version suffix this client saw at connect (serverInfo.version), or a documented parameter is refused, the tool list is stale: reconnect the MCP client (e.g. /mcp)" } };
               return TextOutput(j);
            });
         } });

   tools.push_back(ToolDef { "vpx_play", "Play or pause the table. With ms, runs exactly that much game time and pauses again (blocks until done). Lifts the window focus requirement for automation.",
         Schema({ { "playing", Prop("boolean", "true to play, false to pause (default true)") }, { "ms", Prop("integer", "Game time to simulate before pausing again") } }),
         [](const json& args)
         {
            const bool playing = args.value("playing", true);
            const int ms = args.value("ms", 0);
            auto done = std::make_shared<bool>(false);
            auto errorsBefore = std::make_shared<uint64_t>(0);
            auto withErrors = [errorsBefore](Player& p)
            {
               json j = StatusJson(p);
               j["newScriptErrors"] = ScriptErrorsJson(*errorsBefore);
               return TextOutput(j);
            };
            return [playing, ms, done, errorsBefore, withErrors]() -> std::optional<ToolOutput>
            {
               if (!g_pplayer)
                  return ErrorOutput("No table session is running"s);
               if (g_pplayer->IsEditorMode())
                  return ErrorOutput("The editor is open: use vpx_editor playtest to start a play session first"s);
               if (!*done)
               {
                  *done = true;
                  *errorsBefore = McpServer::Get().GetScriptErrorCount();
                  g_pplayer->m_ignoreWndFocus = true;
                  g_pplayer->SetPlayState(playing, playing ? ms : 0);
                  if (!playing || ms <= 0)
                     return withErrors(*g_pplayer);
                  return std::nullopt;
               }
               if (g_pplayer->IsPlaying())
                  return std::nullopt;
               return withErrors(*g_pplayer);
            };
         }, 600.0 });

   tools.push_back(ToolDef { "vpx_ball", "Balls on the table: 'list' them (position, velocity, radius), 'create' one at x,y with an optional velocity, 'set' a ball's position/velocity by id, 'destroy' one by id (or all without id).",
         Schema({ { "action", Prop("string", "list | create | set | destroy") }, { "id", Prop("integer", "Ball id (set, destroy)") }, { "x", Prop("number", "") }, { "y", Prop("number", "") },
                  { "z", Prop("number", "Height above playfield") }, { "vx", Prop("number", "") }, { "vy", Prop("number", "") }, { "vz", Prop("number", "") },
                  { "radius", Prop("number", "Default 25 (create)") }, { "mass", Prop("number", "Default 1 (create)") } },
            { "action" }),
         [](const json& args)
         {
            const string action = args.value("action", "list"s);
            return WithPlayer([args, action](Player& p)
            {
               if (action == "list")
               {
                  json arr = json::array();
                  for (Ball* b : p.m_vball)
                     arr.push_back(BallJson(b));
                  return TextOutput(json { { "gameTimeMs", p.m_time_msec }, { "balls", arr } });
               }
               if (action == "create")
               {
                  if (p.IsEditorMode())
                     return ErrorOutput("The editor is open: balls can only be created in a play session"s);
                  if (!args.contains("x") || !args.contains("y"))
                     return ErrorOutput("x and y are required"s);
                  Ball* ball = p.CreateBall(args.value("x", 0.f), args.value("y", 0.f), args.value("z", 0.f), args.value("vx", 0.f), args.value("vy", 0.f), args.value("vz", 0.f),
                     args.value("radius", 25.f), args.value("mass", 1.f));
                  return TextOutput(BallJson(ball));
               }
               if (action == "set")
               {
                  Ball* ball = FindBall(p, args.value("id", -1));
                  if (!ball)
                     return ErrorOutput("No ball with that id"s);
                  vec3 pos = ball->GetPosition(), vel = ball->GetVelocity();
                  pos.x = args.value("x", pos.x); pos.y = args.value("y", pos.y); pos.z = args.value("z", pos.z);
                  vel.x = args.value("vx", vel.x); vel.y = args.value("vy", vel.y); vel.z = args.value("vz", vel.z);
                  ball->SetPosition(pos);
                  ball->SetVelocity(vel);
                  return TextOutput(BallJson(ball));
               }
               if (action == "destroy")
               {
                  if (args.contains("id"))
                  {
                     Ball* ball = FindBall(p, args.value("id", -1));
                     if (!ball)
                        return ErrorOutput("No ball with that id"s);
                     p.DestroyBall(ball);
                     return TextOutput(json { { "destroyed", 1 } });
                  }
                  const size_t n = p.m_vball.size();
                  for (Ball* ball : vector<Ball*>(p.m_vball))
                     p.DestroyBall(ball);
                  return TextOutput(json { { "destroyed", n } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

   tools.push_back(ToolDef { "vpx_input", "Press or release an input action (flippers, start, nudges...), hold it for holdMs of wall time, or set the plunger position.",
         Schema({ { "action", Prop("string", "One of LeftFlipper, RightFlipper, LaunchBall, StartGame, AddCredit, LeftNudge, CenterNudge, RightNudge, LeftMagnaSave, RightMagnaSave, Tilt, Lockbar, ExitGame...") },
                  { "pressed", Prop("boolean", "Default true") }, { "holdMs", Prop("integer", "Release after this many milliseconds") },
                  { "plunger", Prop("number", "Plunger position 0 (rest) to 1 (fully retracted); omit to leave the plunger alone") } }),
         [](const json& args) -> ToolStep
         {
            const int action = ActionIndex(args.value("action", ""s));
            const bool pressed = args.value("pressed", true);
            const int holdMs = args.value("holdMs", 0);
            const bool hasPlunger = args.contains("plunger");
            const float plunger = args.value("plunger", 0.f);
            if (action < 0 && !hasPlunger)
               return Immediate([]() { return ErrorOutput("Unknown action"s); });
            auto apply = [=](bool state)
            {
               VPXInputState st {};
               if (action >= 0)
               {
                  st.actionMask = 1ULL << action;
                  st.actionState = state ? st.actionMask : 0;
               }
               if (hasPlunger)
               {
                  st.stateMask = 1;
                  st.plungerPosition = plunger;
               }
               g_pplayer->m_pluginAPI.getAPI().SetInputState(&st);
            };
            auto releaseAt = std::make_shared<std::chrono::steady_clock::time_point>();
            auto started = std::make_shared<bool>(false);
            return [=]() -> std::optional<ToolOutput>
            {
               if (!g_pplayer)
                  return ErrorOutput("No table session is running"s);
               if (!*started)
               {
                  *started = true;
                  apply(pressed);
                  if (!pressed || holdMs <= 0)
                     return TextOutput(json { { "action", action >= 0 ? ACTION_NAMES[action] : "" }, { "pressed", pressed } });
                  *releaseAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(holdMs);
                  return std::nullopt;
               }
               if (std::chrono::steady_clock::now() < *releaseAt)
                  return std::nullopt;
               apply(false);
               return TextOutput(json { { "action", ACTION_NAMES[action] }, { "heldMs", holdMs } });
            };
         }, 120.0 });

}

}
