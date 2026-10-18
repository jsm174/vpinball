// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

ToolStep BuildSimStep(const json& args)
{
   const string action = args.value("action", ""s);
   if (args.value("isolate", false) && (action == "shot" || action == "flip"))
   {
      if (action == "flip" && !(args.contains("x") && args.contains("y")) && !args.contains("ballId"))
         return Immediate([]() { return ErrorOutput("isolate with flip needs x,y (a new ball) or a ballId, so the run knows which ball to keep"s); });
      json inner = args;
      inner.erase("isolate");
      const int keepId = args.value("ballId", -1);
      ToolStep step = BuildSimStep(inner);
      struct Isolation { bool cleared = false; std::set<int> before; json removed = json::array(); };
      auto iso = std::make_shared<Isolation>();
      return [step, iso, keepId, keepBall = args.value("keepBall", false)]() -> std::optional<ToolOutput>
      {
         if (g_pplayer && !g_pplayer->IsEditorMode() && !iso->cleared)
         {
            iso->cleared = true;
            Player& p = *g_pplayer;
            for (Ball* b : vector<Ball*>(p.m_vball))
            {
               int id = -1;
               b->get_ID(&id);
               if (id == keepId)
               {
                  iso->before.insert(id);
                  continue;
               }
               iso->removed.push_back(id);
               p.DestroyBall(b);
            }
         }
         std::optional<ToolOutput> out = step();
         if (!out || out->isError || !g_pplayer)
            return out;
         Player& p = *g_pplayer;
         json result = json::parse(out->text, nullptr, false);
         if (result.is_discarded())
            return out;
         const int runBall = result.value("ballId", -1);
         json removedAfter = json::array();
         for (Ball* b : vector<Ball*>(p.m_vball))
         {
            int id = -1;
            b->get_ID(&id);
            if (iso->before.contains(id) || (keepBall && id == runBall))
               continue;
            removedAfter.push_back(id);
            p.DestroyBall(b);
         }
         result["isolated"] = json { { "removedBefore", iso->removed }, { "removedAfter", removedAfter } };
         if (!iso->removed.empty())
            result["isolated"]["note"] = "balls of the game were removed too (kicker, trough or plunger lane): the table script may still count them; stop and restart the playtest for a clean game"s;
         return TextOutput(result);
      };
   }
   const int ms = std::clamp(args.value("ms", 3000), 1, 60000);
   const int sampleMs = std::max(1, args.value("sampleMs", 100));
   auto run = std::make_shared<SimRun>();
   if (const json cross = args.value("crossAt", json()); cross.is_object())
   {
      if (cross.contains("y"))
         run->crossAxis = 1, run->crossValue = cross.value("y", 0.f);
      else if (cross.contains("x"))
         run->crossAxis = 0, run->crossValue = cross.value("x", 0.f);
   }
   const string goal = args.value("goal", ""s);
   auto addGoal = [run, goal](json& result, const vector<int>& ids)
   {
      if (goal.empty())
         return;
      const string g = lowerCase(goal);
      string how;
      const bool rideOn = g_pplayer && [&]() { IEditable* part = FindPart(*g_pplayer, goal); return part && (part->GetItemType() == eItemRamp); }();
      if (rideOn)
      {
         for (const auto& [id, segments] : run->rode)
            if ((ids.empty() || std::ranges::find(ids, id) != ids.end()) && std::ranges::any_of(segments, [&](const json& seg) { return lowerCase(seg.value("on", ""s)) == g; }))
               how = "ball rode it"s;
         result["goal"] = json { { "part", goal }, { "made", !how.empty() } };
         if (!how.empty())
            result["goal"]["by"] = how;
         else if (std::ranges::any_of(run->contacts, [&](const auto& kv) { return std::ranges::any_of(kv.second, [&](const string& n) { return lowerCase(n) == g; }); }))
            result["goal"]["note"] = "the ball touched the ramp (its lip or a side) without riding it"s;
         return;
      }
      for (const json& e : result.value("events", json::array()))
         if (lowerCase(e.value("part", ""s)) == g && (ids.empty() || !e.contains("ball") || std::ranges::find(ids, e.value("ball", -1)) != ids.end()))
         {
            how = "event "s + e.value("event", ""s);
            break;
         }
      if (how.empty())
         for (const auto& contact : run->contacts)
            if ((ids.empty() || std::ranges::find(ids, contact.first) != ids.end()) && std::ranges::any_of(contact.second, [&](const string& n) { return lowerCase(n) == g; }))
            {
               how = "ball touched it"s;
               break;
            }
      result["goal"] = json { { "part", goal }, { "made", !how.empty() } };
      if (!how.empty())
         result["goal"]["by"] = how;
   };
   auto all = [](Ball*) { return true; };
   auto ballsJson = [run](Player& p)
   {
      json balls = json::array();
      for (auto& [id, samples] : run->samples)
      {
         json b { { "id", id }, { "maxHeight", run->maxZ[id] }, { "samples", samples } };
         b.update(run->BallPathJson(id));
         if (Ball* ball = FindBall(p, id))
         {
            const vec3& pos = ball->GetPosition();
            b["end"] = json { { "x", Round(pos.x) }, { "y", Round(pos.y) }, { "z", Round(pos.z) } };
            b["inKicker"] = ball->m_hitBall.m_d.m_lockedInKicker;
            const vec3& vel = ball->GetVelocity();
            b["state"] = ball->m_hitBall.m_d.m_lockedInKicker ? "in kicker"s : (vel.x * vel.x + vel.y * vel.y + vel.z * vel.z) < 0.25f ? "settled"s : "moving"s;
            json touching = json::array();
            for (const PartDistance& d : PartsNear(p, Vertex3Ds(pos.x, pos.y, pos.z), ball->GetRadius() + 2.f))
               if (!d.volume)
                  touching.push_back(d.part->GetName());
            b["touching"] = touching;
         }
         else
            b["destroyed"] = true;
         balls.push_back(b);
      }
      return balls;
   };
   if (action == "shot")
   {
      if (!args.contains("x") || !args.contains("y"))
         return Immediate([]() { return ErrorOutput("x and y are required"s); });
      const float x = args.value("x", 0.f), y = args.value("y", 0.f), z = args.value("z", 0.f);
      float vx = args.value("vx", 0.f), vy = args.value("vy", 0.f);
      if (args.contains("targetX") && args.contains("targetY"))
      {
         const float dx = args.value("targetX", 0.f) - x, dy = args.value("targetY", 0.f) - y;
         const float len = sqrtf(dx * dx + dy * dy);
         const float speed = args.value("speed", 30.f);
         if (len > 0.f)
         {
            vx = speed * dx / len;
            vy = speed * dy / len;
         }
      }
      auto ballId = std::make_shared<int>(-1);
      const bool keepBall = args.value("keepBall", false);
      return [=]() -> std::optional<ToolOutput>
      {
         if (!g_pplayer)
            return ErrorOutput("No table session is running"s);
         Player& p = *g_pplayer;
         if (p.IsEditorMode())
            return ErrorOutput("The editor is open: use vpx_editor playtest to start a play session first"s);
         auto mine = [ballId](Ball* b) { int id = -1; b->get_ID(&id); return id == *ballId; };
         if (!run->started)
         {
            Ball* ball = p.CreateBall(x, y, z, vx, vy, 0.f, 25.f, 1.f);
            ball->get_ID(ballId.get());
            run->Begin(p, ms);
            run->Sample(p, mine);
            return std::nullopt;
         }
         Ball* ball = FindBall(p, *ballId);
         run->Track(p, mine);
         if (ball && p.m_time_msec - run->lastSampleMs >= static_cast<uint32_t>(sampleMs))
            run->Sample(p, mine);
         if (p.IsPlaying() && ball)
            return std::nullopt;
         if (ball)
            run->Sample(p, mine);
         json result = run->Finish(p);
         FocusEvents(result, *ballId);
         result["samples"] = run->samples.contains(*ballId) ? run->samples[*ballId] : json::array();
         result["maxHeight"] = run->maxZ.contains(*ballId) ? run->maxZ[*ballId] : 0.f;
         result.update(run->BallPathJson(*ballId));
         addGoal(result, { *ballId });
         AddOutcome(result, p, ball);
         if (ball)
         {
            const vec3& pos = ball->GetPosition();
            const vec3& vel = ball->GetVelocity();
            result["end"] = json { { "x", Round(pos.x) }, { "y", Round(pos.y) }, { "z", Round(pos.z) }, { "vx", Round(vel.x, 2) }, { "vy", Round(vel.y, 2) }, { "vz", Round(vel.z, 2) } };
            result["inKicker"] = ball->m_hitBall.m_d.m_lockedInKicker;
            result["ballId"] = *ballId;
            if (!keepBall)
               p.DestroyBall(ball);
         }
         return TextOutput(result);
      };
   }
   if (action == "plunge")
   {
      const bool startGame = args.value("startGame", false);
      const float pull = std::clamp(args.value("pull", 1.f), 0.f, 1.f);
      const int holdMs = std::clamp(args.value("holdMs", 400), 1, 10000);
      auto phase = std::make_shared<int>(0);
      auto pullAtRelease = std::make_shared<json>(json::object());
      auto setPlunger = [](bool override, float position)
      {
         VPXInputState st {};
         st.stateMask = override ? 1 : 0;
         st.plungerPosition = position;
         g_pplayer->m_pluginAPI.getAPI().SetInputState(&st);
      };
      auto setStart = [](bool pressed)
      {
         VPXInputState st {};
         st.actionMask = 1ULL << VPXACTION_StartGame;
         st.actionState = pressed ? st.actionMask : 0;
         g_pplayer->m_pluginAPI.getAPI().SetInputState(&st);
      };
      return [=]() -> std::optional<ToolOutput>
      {
         if (!g_pplayer)
            return ErrorOutput("No table session is running"s);
         Player& p = *g_pplayer;
         if (p.IsEditorMode())
            return ErrorOutput("The editor is open: use vpx_editor playtest to start a play session first"s);
         if (*phase == 0)
         {
            *phase = 1;
            if (startGame)
               setStart(true);
            setPlunger(true, pull);
            p.m_ignoreWndFocus = true;
            p.SetPlayState(true, holdMs);
            return std::nullopt;
         }
         if (*phase == 1)
         {
            if (p.IsPlaying())
               return std::nullopt;
            *phase = 2;
            for (IEditable* part : p.m_ptable->GetParts())
               if (part->GetItemType() == eItemPlunger)
               {
                  float pos = 0.f;
                  if (SUCCEEDED(static_cast<Plunger*>(part)->Position(&pos)))
                     (*pullAtRelease)[part->GetName()] = pos / 25.f;
               }
            if (startGame)
               setStart(false);
            setPlunger(true, 0.f);
            run->Begin(p, ms);
            run->Sample(p, all);
            return std::nullopt;
         }
         run->Track(p, all);
         if (p.m_time_msec - run->lastSampleMs >= static_cast<uint32_t>(sampleMs))
            run->Sample(p, all);
         if (p.IsPlaying())
            return std::nullopt;
         run->Sample(p, all);
         setPlunger(false, 0.f);
         json result = run->Finish(p);
         result["balls"] = ballsJson(p);
         addGoal(result, {});
         result["pullAtRelease"] = *pullAtRelease;
         if (std::ranges::any_of(pullAtRelease->items(), [pull](const auto& kv) { return kv.value().template get<float>() < pull - 0.05f; }))
            result["pullNote"] = "the plunger had not reached the requested pull at release: hold longer (holdMs), its speed_pull sets how fast it pulls back";
         return TextOutput(result);
      };
   }
   if (action == "flip")
   {
      const string side = lowerCase(args.value("side", ""s));
      const bool preHold = args.value("preHold", false);
      const int dropMs = std::clamp(args.value("dropMs", 150), 1, 5000);
      const int holdMs = std::clamp(args.value("holdMs", 300), 1, 10000);
      const int flipAtMs = args.value("flipAtMs", -1);
      const bool hasWhenX = args.contains("flipWhenX"), hasWhenY = args.contains("flipWhenY"), hasAlong = args.contains("flipWhenAlong");
      const float whenX = args.value("flipWhenX", 0.f), whenY = args.value("flipWhenY", 0.f), whenAlong = args.value("flipWhenAlong", 0.5f);
      const bool createBall = args.contains("x") && args.contains("y");
      const float x = args.value("x", 0.f), y = args.value("y", 0.f), z = args.value("z", 0.f), vx = args.value("vx", 0.f), vy = args.value("vy", 0.f);
      const int givenBallId = args.value("ballId", -1);
      struct Flip { int phase = 0; int ballId = -1; bool created = false; int sideX = 0, sideY = 0; bool triggered = false; uint32_t flipMs = 0, dropUntil = 0, pressUntil = 0; json atFlip; int action = -1; string flipperName; FlipperGeom geom; bool exitDone = false; json exit; };
      auto f = std::make_shared<Flip>();
      auto setFlipper = [f](bool pressed)
      {
         VPXInputState st {};
         st.actionMask = 1ULL << f->action;
         st.actionState = pressed ? st.actionMask : 0;
         g_pplayer->m_pluginAPI.getAPI().SetInputState(&st);
      };
      auto flipperAngle = [f](Player& p) -> json
      {
         IEditable* part = f->flipperName.empty() ? nullptr : FindPart(p, f->flipperName);
         float angle = 0.f;
         if (part && part->GetItemType() == eItemFlipper && SUCCEEDED(static_cast<Flipper*>(part)->get_CurrentAngle(&angle)))
            return angle;
         return nullptr;
      };
      return [=]() -> std::optional<ToolOutput>
      {
         if (!g_pplayer)
            return ErrorOutput("No table session is running"s);
         Player& p = *g_pplayer;
         if (p.IsEditorMode())
            return ErrorOutput("The editor is open: use vpx_editor playtest to start a play session first"s);
         auto mine = [f](Ball* b) { int id = -1; b->get_ID(&id); return id == f->ballId; };
         if (f->phase == 0)
         {
            Ball* ball = nullptr;
            if (createBall)
            {
               ball = p.CreateBall(x, y, z, vx, vy, 0.f, 25.f, 1.f);
               f->created = true;
            }
            else if (givenBallId >= 0)
               ball = FindBall(p, givenBallId);
            else if (!p.m_vball.empty())
               ball = p.m_vball.front();
            if (!ball)
               return ErrorOutput("No ball to flip: give x,y to create one or a ballId"s);
            ball->get_ID(&f->ballId);
            // Flipper side: explicit, else the half of the table the ball is on
            const bool left = side.empty() ? (ball->GetPosition().x < (p.m_ptable->m_left + p.m_ptable->m_right) * 0.5f) : side == "left";
            f->action = left ? VPXACTION_LeftFlipper : VPXACTION_RightFlipper;
            IEditable* best = nullptr;
            for (IEditable* part : p.m_ptable->GetParts())
               if (part->GetItemType() == eItemFlipper && (best == nullptr || (left ? part->GetCenter().x < best->GetCenter().x : part->GetCenter().x > best->GetCenter().x)))
                  best = part;
            if (best)
            {
               f->flipperName = best->GetName();
               f->geom = GetFlipperGeom(p, best);
            }
            f->sideX = hasWhenX ? (ball->GetPosition().x < whenX ? -1 : 1) : 0;
            f->sideY = hasWhenY ? (ball->GetPosition().y < whenY ? -1 : 1) : 0;
            if (preHold)
               setFlipper(true);
            run->Begin(p, ms);
            run->Sample(p, mine);
            f->phase = 1;
            return std::nullopt;
         }
         Ball* ball = FindBall(p, f->ballId);
         run->Track(p, mine);
         if (ball && p.m_time_msec - run->lastSampleMs >= static_cast<uint32_t>(sampleMs))
            run->Sample(p, mine);
         const uint32_t elapsed = p.m_time_msec - run->startMs;
         if (f->phase == 1 && ball)
         {
            bool trigger = flipAtMs >= 0 && elapsed >= static_cast<uint32_t>(flipAtMs);
            if (hasWhenX && ((ball->GetPosition().x < whenX ? -1 : 1) != f->sideX))
               trigger = true;
            if (hasWhenY && ((ball->GetPosition().y < whenY ? -1 : 1) != f->sideY))
               trigger = true;
            if (hasAlong && f->geom.valid && ball->GetVelocity().y > 0.f && ball->GetPosition().y >= std::min(f->geom.base.y, f->geom.tip.y) - 80.f
               && f->geom.Along(Vertex2D(ball->GetPosition().x, ball->GetPosition().y)) >= whenAlong)
               trigger = true;
            if (flipAtMs < 0 && !hasWhenX && !hasWhenY && !hasAlong)
               trigger = true;
            if (trigger)
            {
               f->triggered = true;
               f->flipMs = elapsed;
               const vec3& pos = ball->GetPosition();
               const vec3& vel = ball->GetVelocity();
               f->atFlip = json { { "t", elapsed }, { "x", Round(pos.x) }, { "y", Round(pos.y) }, { "z", Round(pos.z) }, { "vx", Round(vel.x, 2) }, { "vy", Round(vel.y, 2) }, { "flipperAngle", flipperAngle(p) },
                  { "along", f->geom.valid ? json(Round(f->geom.Along(Vertex2D(pos.x, pos.y)), 3)) : json(nullptr) } };
               if (preHold)
               {
                  setFlipper(false);
                  f->dropUntil = p.m_time_msec + dropMs;
                  f->phase = 2;
               }
               else
               {
                  setFlipper(true);
                  f->pressUntil = p.m_time_msec + holdMs;
                  f->phase = 3;
               }
            }
         }
         else if (f->phase == 2 && p.m_time_msec >= f->dropUntil)
         {
            setFlipper(true);
            f->pressUntil = p.m_time_msec + holdMs;
            f->phase = 3;
         }
         else if (f->phase == 3 && p.m_time_msec >= f->pressUntil)
         {
            setFlipper(false);
            f->phase = 4;
         }
         // Exit vector: the ball velocity once it has left the flipper (moving up, clear of the flipper's reach)
         if (f->triggered && !f->exitDone && ball && f->phase >= 3)
         {
            const vec3& vel = ball->GetVelocity();
            const vec3& pos = ball->GetPosition();
            const float clearY = f->geom.valid ? std::min(f->geom.base.y, f->geom.tip.y) - f->geom.length * 0.5f : f->atFlip.value("y", 0.f) - 100.f;
            if (vel.y < -0.5f && pos.y < clearY)
            {
               const float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
               f->exit = json { { "t", elapsed }, { "x", Round(pos.x) }, { "y", Round(pos.y) }, { "speed", Round(speed, 2) }, { "angle", Round(atan2f(vel.x, -vel.y) * (float)(180.0 / M_PI)) } };
               f->exitDone = true;
            }
         }
         if (p.IsPlaying() && ball)
            return std::nullopt;
         if (f->phase == 1 && preHold)
            setFlipper(false);
         if (f->phase == 2 || f->phase == 3)
            setFlipper(false);
         if (ball)
            run->Sample(p, mine);
         json result = run->Finish(p);
         if (f->created)
            FocusEvents(result, f->ballId);
         result["flipper"] = json { { "name", f->flipperName }, { "side", f->action == VPXACTION_LeftFlipper ? "left" : "right" } };
         if (f->geom.valid)
            result["flipper"]["base"] = json { { "x", f->geom.base.x }, { "y", f->geom.base.y } }, result["flipper"]["tip"] = json { { "x", f->geom.tip.x }, { "y", f->geom.tip.y } };
         result["triggered"] = f->triggered;
         if (f->exitDone)
            result["exit"] = f->exit;
         if (f->triggered)
         {
            result["flipAtMs"] = f->flipMs;
            result["atFlip"] = f->atFlip;
            json after = json::array();
            for (const json& e : result["events"])
               if (e["t"].get<uint32_t>() >= f->flipMs && e["event"] != "Collide")
                  after.push_back(e["part"].get<string>() + " " + e["event"].get<string>());
            result["hitAfterFlip"] = after;
         }
         result["samples"] = run->samples.contains(f->ballId) ? run->samples[f->ballId] : json::array();
         result["maxHeight"] = run->maxZ.contains(f->ballId) ? run->maxZ[f->ballId] : 0.f;
         result.update(run->BallPathJson(f->ballId));
         addGoal(result, { f->ballId });
         AddOutcome(result, p, ball);
         if (ball)
         {
            const vec3& pos = ball->GetPosition();
            const vec3& vel = ball->GetVelocity();
            result["end"] = json { { "x", Round(pos.x) }, { "y", Round(pos.y) }, { "z", Round(pos.z) }, { "vx", Round(vel.x, 2) }, { "vy", Round(vel.y, 2) }, { "vz", Round(vel.z, 2) } };
            result["inKicker"] = ball->m_hitBall.m_d.m_lockedInKicker;
            if (f->created)
               p.DestroyBall(ball);
         }
         return TextOutput(result);
      };
   }
   if (action == "stuckscan")
   {
      const json area = args.value("area", json::object());
      const float step = std::clamp(args.value("step", 60.f), 20.f, 400.f);
      const float z = args.value("z", 25.f);
      const int scanMs = std::clamp(args.value("ms", 3000), 1000, 20000);
      const size_t maxBalls = static_cast<size_t>(std::clamp(args.value("maxBalls", 20), 1, 200));
      struct Probe { float x, y; int id = -1; vec3 before; };
      struct Scan
      {
         int phase = 0;
         std::deque<Probe> pending;
         vector<Probe> batch;
         json stuck = json::array();
         int tested = 0, skipped = 0, drained = 0, kicked = 0, moving = 0, laneRests = 0, batches = 0, removedScriptBalls = 0;
         std::set<int> existing;
         uint64_t errorsBefore = 0;
      };
      auto sc = std::make_shared<Scan>();
      return [=]() -> std::optional<ToolOutput>
      {
         if (!g_pplayer)
            return ErrorOutput("No table session is running"s);
         Player& p = *g_pplayer;
         if (p.IsEditorMode())
            return ErrorOutput("The editor is open: use vpx_editor playtest to start a play session first"s);
         PinTable* table = p.m_ptable;
         vector<Vertex2D> plungers;
         for (IEditable* part : table->GetParts())
            if (part->GetItemType() == eItemPlunger)
               plungers.push_back(part->GetCenter());
         auto nearPlunger = [&plungers](const vec3& pos) { return std::ranges::any_of(plungers, [&pos](const Vertex2D& c) { return fabsf(pos.x - c.x) < 45.f && pos.y > c.y - 120.f && pos.y < c.y + 60.f; }); };
         if (sc->phase == 0)
         {
            sc->phase = 1;
            sc->errorsBefore = McpServer::Get().GetScriptErrorCount();
            for (Ball* b : p.m_vball)
            {
               int id = -1;
               b->get_ID(&id);
               sc->existing.insert(id);
            }
            const float left = area.value("left", table->m_left + 30.f), right = area.value("right", table->m_right - 30.f);
            const float top = area.value("top", table->m_top + 30.f), bottom = area.value("bottom", table->m_bottom - 30.f);
            // Surfaces the ball can not be inside of: a probe inside a wall outline is skipped unless it rests on its top
            struct Solid { vector<Vertex2D> outline; float top; };
            vector<Solid> solids;
            for (IEditable* part : table->GetParts())
               if (part->GetItemType() == eItemSurface)
               {
                  const Surface* surf = static_cast<Surface*>(part);
                  Solid solid { {}, surf->m_d.m_heighttop };
                  for (const auto& dp : surf->m_curve.GetPoints())
                     solid.outline.emplace_back(dp->GetX(), dp->GetY());
                  if (solid.outline.size() >= 3)
                     solids.push_back(solid);
               }
            auto inside = [](const vector<Vertex2D>& poly, float x, float y)
            {
               bool in = false;
               for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
                  if (((poly[i].y > y) != (poly[j].y > y)) && (x < (poly[j].x - poly[i].x) * (y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
                     in = !in;
               return in;
            };
            // Interleaved passes keep the balls of a batch apart from each other
            vector<Probe> passes[4];
            int iy = 0;
            for (float y = top; y <= bottom; y += step, iy++)
            {
               int ix = 0;
               for (float x = left; x <= right; x += step, ix++)
               {
                  bool blocked = false;
                  for (const Solid& solid : solids)
                     if (solid.top > z - 25.f + 2.f && inside(solid.outline, x, y))
                        blocked = true;
                  if (!blocked)
                     for (const PartDistance& d : PartsNear(p, Vertex3Ds(x, y, z), 25.f))
                        if (!d.volume)
                           blocked = true;
                  if (blocked)
                     sc->skipped++;
                  else
                     passes[(iy % 2) * 2 + (ix % 2)].push_back({ x, y });
               }
            }
            for (auto& pass : passes)
               for (size_t i = 0; i < pass.size(); i += maxBalls)
               {
                  sc->pending.insert(sc->pending.end(), pass.begin() + i, pass.begin() + std::min(pass.size(), i + maxBalls));
                  sc->pending.push_back({ FLT_MAX, FLT_MAX });
               }
            p.m_ignoreWndFocus = true;
         }
         if (sc->phase == 1)
         {
            if (sc->pending.empty())
            {
               // Merge nearby resting spots
               json clusters = json::array();
               for (const json& s1 : sc->stuck)
               {
                  bool merged = false;
                  for (json& c : clusters)
                     if (fabsf(c["x"].get<float>() - s1["x"].get<float>()) < 30.f && fabsf(c["y"].get<float>() - s1["y"].get<float>()) < 30.f)
                     {
                        c["count"] = c["count"].get<int>() + 1;
                        if (c["droppedFrom"].size() < 4)
                           c["droppedFrom"].push_back(s1["from"]);
                        merged = true;
                        break;
                     }
                  if (!merged)
                     clusters.push_back({ { "x", s1["x"] }, { "y", s1["y"] }, { "z", s1["z"] }, { "count", 1 }, { "touching", s1["touching"] }, { "droppedFrom", json::array({ s1["from"] }) } });
               }
               std::sort(clusters.begin(), clusters.end(), [](const json& a, const json& b) { return a["count"].get<int>() > b["count"].get<int>(); });
               return TextOutput(json { { "stuckSpots", clusters }, { "tested", sc->tested }, { "skippedInsideParts", sc->skipped }, { "drained", sc->drained }, { "endedInKicker", sc->kicked },
                  { "stillMoving", sc->moving }, { "restingInPlungerLane", sc->laneRests }, { "batches", sc->batches }, { "scriptErrors", ScriptErrorsSummary(sc->errorsBefore) }, { "removedScriptBalls", sc->removedScriptBalls },
                  { "note", "stuckSpots are places where dropped balls came to rest outside kickers and the plunger lane after the run: each is a potential trap. touching lists the parts holding the ball" } });
            }
            sc->batch.clear();
            while (!sc->pending.empty())
            {
               const Probe pr = sc->pending.front();
               sc->pending.pop_front();
               if (pr.x == FLT_MAX)
                  break;
               sc->batch.push_back(pr);
            }
            if (sc->batch.empty())
               return std::nullopt;
            for (Probe& pr : sc->batch)
            {
               Ball* ball = p.CreateBall(pr.x, pr.y, z, 0.f, 0.f, 0.f, 25.f, 1.f);
               ball->get_ID(&pr.id);
            }
            sc->batches++;
            sc->phase = 2;
            p.SetPlayState(true, std::max(500, scanMs - 500));
            return std::nullopt;
         }
         if (p.IsPlaying())
            return std::nullopt;
         if (sc->phase == 2)
         {
            for (Probe& pr : sc->batch)
               if (Ball* b = FindBall(p, pr.id))
                  pr.before = b->GetPosition();
            sc->phase = 3;
            p.SetPlayState(true, 500);
            return std::nullopt;
         }
         for (const Probe& pr : sc->batch)
         {
            sc->tested++;
            Ball* b = FindBall(p, pr.id);
            if (!b)
            {
               sc->drained++;
               continue;
            }
            const vec3 pos = b->GetPosition();
            if (pos.y > table->m_bottom)
               sc->drained++;
            else if (b->m_hitBall.m_d.m_lockedInKicker)
               sc->kicked++;
            else if (b->GetVelocity().Length() < 0.3f && (pos - pr.before).Length() < 3.f)
            {
               bool onPlunger = false;
               const json touching = TouchingJson(p, b, &onPlunger);
               if (onPlunger || nearPlunger(pos))
                  sc->laneRests++;
               else
               {
                  sc->stuck.push_back({ { "x", pos.x }, { "y", pos.y }, { "z", pos.z }, { "touching", touching }, { "from", json { { "x", pr.x }, { "y", pr.y } } } });
               }
            }
            else
               sc->moving++;
            p.DestroyBall(b);
         }
         // Balls the table script released meanwhile (a trough refilling the plunger lane after a probe drained) are not part of the scan
         vector<Ball*> extra;
         for (Ball* b : p.m_vball)
         {
            int id = -1;
            b->get_ID(&id);
            if (!sc->existing.contains(id))
               extra.push_back(b);
         }
         for (Ball* b : extra)
            p.DestroyBall(b);
         sc->removedScriptBalls += static_cast<int>(extra.size());
         sc->phase = 1;
         return std::nullopt;
      };
   }
   if (action == "autoplay")
   {
      const int playMs = std::clamp(args.value("ms", 60000), 2000, 540000);
      const unsigned int seed = args.value("seed", 1u);
      struct Auto
      {
         bool started = false;
         std::mt19937 rng;
         vector<FlipperGeom> flippers;
         uint32_t startMs = 0, leftUntil = 0, rightUntil = 0, startUntil = 0, nudgeUntil = 0, noBallSince = 0, lastStart = 0;
         int nudgeAction = -1;
         std::map<int, float> target;
         std::map<int, uint32_t> born, slowSince, laneSince;
         std::map<int, int> nudges;
         int plungeBall = -1, plungePhase = 0;
         uint64_t sentActions = ~0ULL;
         int sentPlunger = -1;
         uint32_t plungeUntil = 0, lastPlungeMs = 0;
         float pull = 0.85f;
         int flips = 0, plunges = 0, starts = 0, ballsLost = 0;
         json pulls = json::array();
         double ballTimeTotal = 0.0;
         json stuck = json::array();
         std::map<string, std::map<string, int>> eventCounts;
         uint64_t errorsBefore = 0;
      };
      auto au = std::make_shared<Auto>();
      au->rng.seed(seed);
      return [=]() -> std::optional<ToolOutput>
      {
         if (!g_pplayer)
            return ErrorOutput("No table session is running"s);
         Player& p = *g_pplayer;
         if (p.IsEditorMode())
            return ErrorOutput("The editor is open: use vpx_editor playtest to start a play session first"s);
         PinTable* table = p.m_ptable;
         auto collectEvents = [&]()
         {
            for (const CapturedEvent& e : McpServer::Get().TakeEvents())
               au->eventCounts[e.part][e.event]++;
         };
         auto sendInput = [&](bool done)
         {
            VPXInputState st {};
            st.actionMask = (1ULL << VPXACTION_LeftFlipper) | (1ULL << VPXACTION_RightFlipper) | (1ULL << VPXACTION_StartGame) | (1ULL << VPXACTION_LeftNudge) | (1ULL << VPXACTION_RightNudge) | (1ULL << VPXACTION_CenterNudge);
            st.actionState = 0;
            const uint32_t now = p.m_time_msec;
            if (!done)
            {
               if (now < au->leftUntil)
                  st.actionState |= 1ULL << VPXACTION_LeftFlipper;
               if (now < au->rightUntil)
                  st.actionState |= 1ULL << VPXACTION_RightFlipper;
               if (now < au->startUntil)
                  st.actionState |= 1ULL << VPXACTION_StartGame;
               if (now < au->nudgeUntil && au->nudgeAction >= 0)
                  st.actionState |= 1ULL << au->nudgeAction;
            }
            // One call carries both: any input call also sets or releases the plunger override
            const int plunger = done ? 0 : au->plungePhase;
            if (st.actionState != au->sentActions || plunger != au->sentPlunger)
            {
               au->sentActions = st.actionState;
               au->sentPlunger = plunger;
               st.stateMask = plunger > 0 ? 1 : 0;
               st.plungerPosition = plunger == 1 ? au->pull : 0.f;
               p.m_pluginAPI.getAPI().SetInputState(&st);
            }
         };
         if (!au->started)
         {
            au->started = true;
            au->flippers = TableFlippers(p);
            au->startMs = p.m_time_msec;
            au->errorsBefore = McpServer::Get().GetScriptErrorCount();
            McpServer::Get().StartEventCapture();
            p.m_ignoreWndFocus = true;
            p.SetPlayState(true, playMs);
            return std::nullopt;
         }
         const uint32_t now = p.m_time_msec;
         collectEvents();
         // Balls appearing and leaving
         std::set<int> alive;
         for (Ball* b : p.m_vball)
         {
            int id = -1;
            b->get_ID(&id);
            alive.insert(id);
            if (!au->born.contains(id))
            {
               au->born[id] = now;
               au->target[id] = std::uniform_real_distribution<float>(0.35f, 0.75f)(au->rng);
            }
         }
         for (auto it = au->born.begin(); it != au->born.end();)
            if (!alive.contains(it->first))
            {
               au->ballsLost++;
               au->ballTimeTotal += (now - it->second) / 1000.0;
               au->slowSince.erase(it->first);
               au->laneSince.erase(it->first);
               au->nudges.erase(it->first);
               it = au->born.erase(it);
            }
            else
               ++it;
         if (p.IsPlaying())
         {
            Vertex2D plungerPos(FLT_MAX, FLT_MAX);
            for (IEditable* part : table->GetParts())
               if (part->GetItemType() == eItemPlunger)
                  plungerPos = part->GetCenter();
            for (Ball* b : p.m_vball)
            {
               int id = -1;
               b->get_ID(&id);
               const vec3& pos = b->GetPosition();
               const float speed = b->GetVelocity().Length();
               float along = 0.f;
               if (const FlipperGeom* f = ApproachingFlipper(au->flippers, b, along); f && along >= au->target[id])
               {
                  uint32_t& until = f->left ? au->leftUntil : au->rightUntil;
                  if (now >= until + 150)
                  {
                     until = now + 250;
                     au->flips++;
                     au->target[id] = std::uniform_real_distribution<float>(0.35f, 0.75f)(au->rng);
                  }
               }
               bool onPlunger = false;
               const json touching = speed < 0.3f ? TouchingJson(p, b, &onPlunger) : json::array();
               const bool inLane = onPlunger || (fabsf(pos.x - plungerPos.x) < 45.f && pos.y > plungerPos.y - 120.f && pos.y < plungerPos.y + 60.f);
               if (inLane && speed < 0.3f && !b->m_hitBall.m_d.m_lockedInKicker)
               {
                  if (!au->laneSince.contains(id))
                     au->laneSince[id] = now;
                  else if (now - au->laneSince[id] > 600 && au->plungePhase == 0)
                  {
                     // Back in the lane soon after a plunge: the pull was too weak to clear the lane, pull harder
                     if (au->lastPlungeMs != 0 && now - au->lastPlungeMs < 8000)
                        au->pull = std::min(1.f, au->pull + 0.05f);
                     au->lastPlungeMs = now;
                     au->plungePhase = 1;
                     au->plungeBall = id;
                     au->plungeUntil = now + 500;
                     au->plunges++;
                  }
               }
               else
                  au->laneSince.erase(id);
               if (!inLane && speed < 0.3f && !b->m_hitBall.m_d.m_lockedInKicker)
               {
                  if (!au->slowSince.contains(id))
                     au->slowSince[id] = now;
                  else if (now - au->slowSince[id] > 3000)
                  {
                     int& n = au->nudges[id];
                     if (n == 0)
                     {
                        au->stuck.push_back({ { "t", now - au->startMs }, { "x", pos.x }, { "y", pos.y }, { "z", pos.z }, { "touching", touching } });
                     }
                     if (n < 3)
                     {
                        n++;
                        au->nudgeAction = (n % 2) ? VPXACTION_LeftNudge : VPXACTION_RightNudge;
                        au->nudgeUntil = now + 100;
                     }
                     au->slowSince[id] = now;
                  }
               }
               else
               {
                  au->slowSince.erase(id);
                  au->nudges.erase(id);
               }
            }
            if (au->plungePhase == 1 && now >= au->plungeUntil)
            {
               for (IEditable* part : table->GetParts())
                  if (part->GetItemType() == eItemPlunger)
                  {
                     float pos = 0.f;
                     if (SUCCEEDED(static_cast<Plunger*>(part)->Position(&pos)) && au->pulls.size() < 20)
                        au->pulls.push_back(pos / 25.f);
                  }
               au->plungePhase = 2;
               au->plungeUntil = now + 2000;
            }
            else if (au->plungePhase == 2 && now >= au->plungeUntil)
               au->plungePhase = 0;
            if (p.m_vball.empty())
            {
               if (au->noBallSince == 0)
                  au->noBallSince = now;
               else if (now - au->noBallSince > 2500 && now - au->lastStart > 4000)
               {
                  au->startUntil = now + 200;
                  au->lastStart = now;
                  au->starts++;
               }
            }
            else
               au->noBallSince = 0;
            sendInput(false);
            return std::nullopt;
         }
         sendInput(true);
         collectEvents();
         McpServer::Get().StopEventCapture();
         json counts = json::object();
         for (const auto& [part, evs] : au->eventCounts)
            for (const auto& [ev, n] : evs)
               counts[part][ev] = n;
         json neverFired = json::array();
         for (IEditable* part : table->GetParts())
         {
            const ItemTypeEnum t = part->GetItemType();
            if ((t == eItemTrigger || t == eItemHitTarget || t == eItemKicker || t == eItemBumper || t == eItemSpinner || t == eItemGate) && part->GetPartGroup() != nullptr && !au->eventCounts.contains(part->GetName()))
               neverFired.push_back(part->GetName());
         }
         return TextOutput(json { { "playedMs", now - au->startMs }, { "ballsLost", au->ballsLost }, { "averageBallSeconds", au->ballsLost ? au->ballTimeTotal / au->ballsLost : 0.0 },
            { "ballsOnTableAtEnd", p.m_vball.size() }, { "flips", au->flips }, { "plunges", au->plunges }, { "pullAtRelease", au->pulls }, { "finalPull", au->pull }, { "startPresses", au->starts }, { "stuckEpisodes", au->stuck },
            { "scriptErrors", ScriptErrorsSummary(au->errorsBefore) }, { "switchesNeverFired", neverFired }, { "eventCounts", counts },
            { "note", "a simple bot: flips when a ball comes down onto a flipper (random point along it), plunges a ball resting in the plunger lane, presses Start when no ball is on the table, nudges a ball resting elsewhere for 3 s and reports it as stuck" } });
      };
   }
   if (action == "sweep")
   {
      json overArg = args.value("over", json());
      json valuesArg = args.value("values", json::array());
      json base = args.value("base", json::object());
      const auto parseJsonString = [](json& v)
      {
         if (v.is_string() && !v.get<string>().empty() && (v.get<string>().front() == '[' || v.get<string>().front() == '{'))
            if (json parsed = json::parse(v.get<string>(), nullptr, false); !parsed.is_discarded())
               v = parsed;
      };
      parseJsonString(overArg);
      parseJsonString(valuesArg);
      parseJsonString(base);
      json pointsArg = args.value("points", json());
      parseJsonString(pointsArg);
      vector<string> names;
      vector<json> lists;
      if (overArg.is_array() && overArg.size() == 1 && overArg[0].is_string())
         overArg = overArg[0];
      if (pointsArg.is_array() && !pointsArg.empty())
      {
         if (!overArg.is_array() || std::ranges::any_of(pointsArg, [&](const json& pt) { return !pt.is_array() || pt.size() != overArg.size(); }))
            return Immediate([]() { return ErrorOutput("points needs over as a list of names and one value per name in each point, e.g. over: [\"x\", \"y\"], points: [[100, 1500], [120, 1500]]"s); });
         for (const json& n : overArg)
            names.push_back(n.is_string() ? n.get<string>() : ""s);
      }
      else if (overArg.is_string())
      {
         names.push_back(overArg.get<string>());
         lists.push_back(valuesArg);
      }
      else if (overArg.is_array() && valuesArg.is_array() && overArg.size() == valuesArg.size())
         for (size_t i = 0; i < overArg.size(); i++)
         {
            names.push_back(overArg[i].is_string() ? overArg[i].get<string>() : ""s);
            lists.push_back(valuesArg[i]);
         }
      if (names.empty() || std::ranges::any_of(names, [](const string& n) { return n.empty(); }) || std::ranges::any_of(lists, [](const json& l) { return !l.is_array() || l.empty(); })
         || !base.is_object() || !base.contains("action"))
         return Immediate([]() { return ErrorOutput("sweep needs over (a parameter name, or a list of names), values (a list, or one list per name) and base (the shot/flip/plunge arguments including action)"s); });
      if (base["action"] == "sweep")
         return Immediate([]() { return ErrorOutput("sweep cannot nest"s); });
      for (const char* key : { "goal", "crossAt", "isolate" })
         if (args.contains(key) && !base.contains(key))
            base[key] = args[key];
      json values = json::array();
      if (pointsArg.is_array() && !pointsArg.empty())
         for (const json& pt : pointsArg)
         {
            json combo = json::object();
            for (size_t i = 0; i < names.size(); i++)
               combo[names[i]] = pt[i];
            values.push_back(combo);
            if (values.size() >= 400)
               break;
         }
      else
      {
         vector<size_t> idx(names.size(), 0);
         while (true)
         {
            json combo = json::object();
            for (size_t i = 0; i < names.size(); i++)
               combo[names[i]] = lists[i][idx[i]];
            values.push_back(combo);
            size_t k = names.size();
            while (k > 0)
            {
               k--;
               if (++idx[k] < lists[k].size())
                  break;
               idx[k] = 0;
               if (k == 0)
               {
                  k = SIZE_MAX;
                  break;
               }
            }
            if (k == SIZE_MAX || values.size() >= 400)
               break;
         }
      }
      const json over = names.size() == 1 ? json(names[0]) : json(names);
      struct Sweep { size_t index = 0; ToolStep current; json results = json::array(); };
      auto sw = std::make_shared<Sweep>();
      return [=]() -> std::optional<ToolOutput>
      {
         if (!g_pplayer)
            return ErrorOutput("No table session is running"s);
         if (!sw->current)
         {
            if (sw->index >= values.size())
            {
               json result { { "over", over }, { "runs", sw->results } };
               if (base.contains("goal"))
               {
                  const auto made = std::ranges::count_if(sw->results, [](const json& r) { return r.value("goal", false); });
                  result["goalRate"] = json { { "part", base["goal"] }, { "made", made }, { "runs", sw->results.size() },
                     { "percent", sw->results.empty() ? 0.0 : std::strtod(std::format("{:.1f}", 100.0 * static_cast<double>(made) / static_cast<double>(sw->results.size())).c_str(), nullptr) } };
               }
               return TextOutput(result);
            }
            json runArgs = base;
            for (const auto& [k, v] : values[sw->index].items())
               runArgs[k] = v;
            sw->current = BuildSimStep(runArgs);
         }
         std::optional<ToolOutput> out = sw->current();
         if (!out)
            return std::nullopt;
         json summary { { "value", values[sw->index].size() == 1 ? values[sw->index].begin().value() : values[sw->index] } };
         if (out->isError)
            summary["error"] = out->text;
         else
         {
            json r = json::parse(out->text);
            summary["outcome"] = r.value("outcome", ""s);
            summary["maxHeight"] = r.value("maxHeight", 0.f);
            if (r.contains("exit"))
               summary["exit"] = r["exit"];
            if (r.contains("atFlip"))
               summary["atFlip"] = json { { "x", r["atFlip"].value("x", 0.f) }, { "y", r["atFlip"].value("y", 0.f) }, { "along", r["atFlip"].value("along", json()) } };
            if (r.contains("triggered"))
               summary["triggered"] = r["triggered"];
            json hits = json::array();
            for (const json& e : r.value("events", json::array()))
               if (e.value("event", ""s) == "Hit" || e.value("event", ""s) == "Slingshot" || e.value("event", ""s) == "Spin")
                  hits.push_back(e.value("part", ""s));
            summary["hits"] = hits;
            float topY = FLT_MAX;
            if (r.contains("samples"))
               for (const json& smp : r["samples"])
                  topY = std::min(topY, smp.value("y", FLT_MAX));
            if (r.contains("balls"))
               for (const json& b : r["balls"])
                  for (const json& smp : b.value("samples", json::array()))
                     topY = std::min(topY, smp.value("y", FLT_MAX));
            if (topY != FLT_MAX)
               summary["topY"] = topY;
            if (r.contains("end"))
               summary["end"] = json { { "x", r["end"].value("x", 0.f) }, { "y", r["end"].value("y", 0.f) } };
            const json* path = &r;
            if (!r.contains("rode") && r.contains("balls") && !r["balls"].empty())
               path = &r["balls"][0];
            json rode = json::array();
            for (const json& seg : path->value("rode", json::array()))
               rode.push_back(seg.value("on", ""s));
            summary["rode"] = rode;
            if (path->contains("crossings"))
               summary["crossed"] = (*path)["crossings"].empty() ? json(nullptr) : (*path)["crossings"][0];
            if (r.contains("goal"))
               summary["goal"] = r["goal"].value("made", false);
            if (r.contains("isolated"))
               summary["isolated"] = r["isolated"];
            if (const json errs = r.value("scriptErrors", json::object()); errs.value("count", 0) > 0)
               summary["scriptErrors"] = errs["count"];
         }
         sw->results.push_back(summary);
         sw->index++;
         sw->current = nullptr;
         return std::nullopt;
      };
   }
   if (action == "trace")
   {
      const json until = args.value("until", json::object());
      const int watchId = until.value("ballId", -1);
      const bool hasY = until.contains("y"), hasYBelow = until.contains("yBelow"), hasX = until.contains("x"), hasXBelow = until.contains("xBelow");
      const float uy = until.value("y", 0.f), uyBelow = until.value("yBelow", 0.f), ux = until.value("x", 0.f), uxBelow = until.value("xBelow", 0.f);
      const bool hasSpeed = until.contains("speedBelow");
      const float speedBelow = until.value("speedBelow", 0.f);
      const int settleMs = std::max(1, until.value("settleMs", 300));
      const string eventFilter = lowerCase(until.value("event", ""s));
      const bool ballLost = until.value("ballLost", false);
      const bool approaching = until.value("approachingFlipper", false);
      const int minMs = std::max(0, until.value("minMs", 50));
      struct Watch { bool startY = false, startYBelow = false, startX = false, startXBelow = false; uint32_t slowSince = 0; size_t nBalls = 0; size_t eventsSeen = 0; string stoppedBy; vector<FlipperGeom> flippers; json approach; };
      auto w = std::make_shared<Watch>();
      auto watched = [watchId](Ball* b) { int id = -1; b->get_ID(&id); return watchId < 0 || id == watchId; };
      return [=]() -> std::optional<ToolOutput>
      {
         if (!g_pplayer)
            return ErrorOutput("No table session is running"s);
         Player& p = *g_pplayer;
         if (p.IsEditorMode())
            return ErrorOutput("The editor is open: use vpx_editor playtest to start a play session first"s);
         auto anyBall = [&](const std::function<bool(Ball*)>& pred) { for (Ball* b : p.m_vball) if (watched(b) && pred(b)) return true; return false; };
         if (!run->started)
         {
            w->startY = anyBall([&](Ball* b) { return b->GetPosition().y >= uy; });
            w->startYBelow = anyBall([&](Ball* b) { return b->GetPosition().y <= uyBelow; });
            w->startX = anyBall([&](Ball* b) { return b->GetPosition().x >= ux; });
            w->startXBelow = anyBall([&](Ball* b) { return b->GetPosition().x <= uxBelow; });
            w->nBalls = p.m_vball.size();
            if (approaching)
               w->flippers = TableFlippers(p);
            run->Begin(p, ms);
            run->Sample(p, all);
            return std::nullopt;
         }
         run->Track(p, watched);
         if (p.m_time_msec - run->lastSampleMs >= static_cast<uint32_t>(sampleMs))
            run->Sample(p, all);
         const uint32_t elapsed = p.m_time_msec - run->startMs;
         if (p.IsPlaying() && elapsed >= static_cast<uint32_t>(minMs))
         {
            if (hasY && !w->startY && anyBall([&](Ball* b) { return b->GetPosition().y >= uy; }))
               w->stoppedBy = "y"s;
            else if (hasYBelow && !w->startYBelow && anyBall([&](Ball* b) { return b->GetPosition().y <= uyBelow; }))
               w->stoppedBy = "yBelow"s;
            else if (hasX && !w->startX && anyBall([&](Ball* b) { return b->GetPosition().x >= ux; }))
               w->stoppedBy = "x"s;
            else if (hasXBelow && !w->startXBelow && anyBall([&](Ball* b) { return b->GetPosition().x <= uxBelow; }))
               w->stoppedBy = "xBelow"s;
            if (hasY && w->startY && !anyBall([&](Ball* b) { return b->GetPosition().y >= uy; }))
               w->startY = false;
            if (hasYBelow && w->startYBelow && !anyBall([&](Ball* b) { return b->GetPosition().y <= uyBelow; }))
               w->startYBelow = false;
            if (hasX && w->startX && !anyBall([&](Ball* b) { return b->GetPosition().x >= ux; }))
               w->startX = false;
            if (hasXBelow && w->startXBelow && !anyBall([&](Ball* b) { return b->GetPosition().x <= uxBelow; }))
               w->startXBelow = false;
            if (hasSpeed && w->stoppedBy.empty())
            {
               bool slow = !p.m_vball.empty();
               for (Ball* b : p.m_vball)
                  if (watched(b) && b->GetVelocity().Length() >= speedBelow)
                     slow = false;
               if (!slow)
                  w->slowSince = 0;
               else if (w->slowSince == 0)
                  w->slowSince = p.m_time_msec;
               else if (p.m_time_msec - w->slowSince >= static_cast<uint32_t>(settleMs))
                  w->stoppedBy = "settled"s;
            }
            if (!eventFilter.empty() && w->stoppedBy.empty())
            {
               const auto& events = McpServer::Get().PeekEvents();
               for (size_t i = w->eventsSeen; i < events.size(); i++)
               {
                  const string full = lowerCase(events[i].part + ' ' + events[i].event);
                  if (full == eventFilter || lowerCase(events[i].part) == eventFilter)
                     w->stoppedBy = "event: "s + events[i].part + ' ' + events[i].event;
               }
               w->eventsSeen = events.size();
            }
            if (ballLost && w->stoppedBy.empty())
            {
               const bool lost = watchId >= 0 ? FindBall(p, watchId) == nullptr : p.m_vball.size() < w->nBalls;
               if (lost)
                  w->stoppedBy = "ballLost"s;
            }
            if (approaching && w->stoppedBy.empty())
               for (Ball* b : p.m_vball)
               {
                  float along = 0.f;
                  if (!watched(b))
                     continue;
                  if (const FlipperGeom* f = ApproachingFlipper(w->flippers, b, along))
                  {
                     int id = -1;
                     b->get_ID(&id);
                     const vec3& pos = b->GetPosition();
                     const vec3& vel = b->GetVelocity();
                     w->approach = json { { "ballId", id }, { "flipper", f->name }, { "side", f->left ? "left" : "right" }, { "along", Round(along, 3) }, { "x", Round(pos.x) }, { "y", Round(pos.y) }, { "vx", Round(vel.x, 2) }, { "vy", Round(vel.y, 2) } };
                     w->stoppedBy = "approachingFlipper"s;
                     break;
                  }
               }
            if (!w->stoppedBy.empty())
               p.SetPlayState(false);
         }
         if (p.IsPlaying())
            return std::nullopt;
         run->Sample(p, all);
         json result = run->Finish(p);
         result["stoppedBy"] = w->stoppedBy.empty() ? "timeout"s : w->stoppedBy;
         if (!w->approach.is_null())
            result["approach"] = w->approach;
         result["balls"] = ballsJson(p);
         addGoal(result, watchId >= 0 ? vector<int> { watchId } : vector<int> {});
         return TextOutput(result);
      };
   }
   return Immediate([]() { return ErrorOutput("Unknown action"s); });
}

void AddSimTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_sim",
      "Deterministic game-time physics runs that report trajectories, max height, end state, what the ball rode and touched, and the script events fired; the table is paused afterwards. Actions: shot (x, y, vx, vy or targetX, targetY, speed; keepBall) | plunge (pull, holdMs, startGame) | flip (ballId or x, y; side; trigger: flipWhenAlong 0 base..1 tip, flipAtMs, flipWhenX, flipWhenY; preHold, dropMs, holdMs) | trace (until: {y, yBelow, x, xBelow, speedBelow, settleMs, event, ballLost, approachingFlipper, ballId, minMs}) | sweep (over, values or points, base: one run per value, summarized) | stuckscan (area, step, z, maxBalls: drop resting balls on a grid and report the traps) | autoplay (ms, seed: soak test bot). Common: ms (game time, default 3000), sampleMs (default 100), goal (a part: reached or not), crossAt ({y} or {x}), isolate (shot, flip: clear the other balls). The turn-based play loop: trace until approachingFlipper, then flip with flipWhenAlong. Details: vpx_status guide:\"sim\".",
      Schema({ { "action", Prop("string", "shot | plunge | flip | trace | sweep | stuckscan | autoplay") }, { "area", json { { "type", "object" }, { "description", "stuckscan: {left, top, right, bottom}, default the whole table" } } },
                { "step", Prop("number", "stuckscan: grid spacing, default 60") }, { "maxBalls", Prop("integer", "stuckscan: balls per batch, default 20") }, { "seed", Prop("integer", "autoplay: random seed") },
                { "over", json { { "type", json::array({ "string", "array" }) }, { "items", json { { "type", "string" } } }, { "description", "sweep: parameter name, or a list of names" } } },
                { "values", json { { "type", "array" }, { "description", "sweep: values, or one list per name of over" } } },
                { "points", json { { "type", "array" }, { "items", json { { "type", "array" } } }, { "description", "sweep: with over as a list of names, the exact combinations to run, e.g. [[x1, y1], [x2, y2]]" } } },
                { "base", json { { "type", "object" }, { "description", "sweep: run arguments incl. action" } } }, { "side", Prop("string", "flip: left | right") }, { "ballId", Prop("integer", "flip: existing ball") },
                { "flipAtMs", Prop("integer", "flip: game ms after the start") }, { "flipWhenAlong", Prop("number", "flip: when the ball reaches this fraction along the flipper, 0 base to 1 tip") }, { "flipWhenX", Prop("number", "flip: when the ball crosses this x") }, { "flipWhenY", Prop("number", "flip: when the ball crosses this y") },
                { "preHold", Prop("boolean", "flip: cradle first") }, { "dropMs", Prop("integer", "flip: drop time before the flip when cradling, default 150") }, { "keepBall", Prop("boolean", "shot: leave the ball on the table") }, { "isolate", Prop("boolean", "shot, flip, sweep: remove every other ball before and the leftovers after") },
                { "goal", Prop("string", "a part name: the result says whether the ball reached it") }, { "crossAt", json { { "type", "object" }, { "description", "{y: 1000} or {x: 500}: report where the ball crossed that line" } } },
                { "x", Prop("number", "shot, flip") }, { "y", Prop("number", "shot, flip") }, { "z", Prop("number", "shot: default 0; stuckscan: drop height, default 25") }, { "vx", Prop("number", "shot, flip") }, { "vy", Prop("number", "shot, flip: negative goes up the table") },
                { "targetX", Prop("number", "shot: aim instead of vx/vy") }, { "targetY", Prop("number", "") }, { "speed", Prop("number", "shot: speed when aiming, default 30") }, { "startGame", Prop("boolean", "plunge: press Start first") }, { "pull", Prop("number", "plunge: 0..1, default 1") },
                { "holdMs", Prop("integer", "plunge: hold before release, default 400; flip: flipper press duration, default 300") }, { "ms", Prop("integer", "Game time to simulate, default 3000, max 60000") }, { "sampleMs", Prop("integer", "Sample interval, default 100") },
                { "until", json { { "type", "object" }, { "description", "trace: stop condition {y, yBelow, x, xBelow, speedBelow, settleMs, event, ballLost, approachingFlipper, ballId, minMs}" } } } },
         { "action" }),
      BuildSimStep, 1800.0 });
}

}
