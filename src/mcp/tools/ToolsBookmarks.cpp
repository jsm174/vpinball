// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "mcp/McpBookmarks.h"
#include "ui/live/editor/SketchTool.h"

namespace VPX::MCP
{

namespace
{

using namespace VPX::MCP::Bookmarks;

struct PartBox
{
   string name;
   float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// The parts a ball can hit, with their footprint: from the collision shapes of the running session, else from the bounds of the hitable parts
vector<PartBox> HittableParts(Player& p)
{
   vector<PartBox> boxes;
   for (const auto& [part, colliders] : CollidersByPart(p))
   {
      if (part == p.m_ptable || part->GetItemType() == eItemPartGroup || part->GetItemType() == eItemBall || part->GetName().empty() || p.m_ptable->IsPlayerSessionObject(part)
         || VPX::Sketch::IsBackdropPart(part, p.m_ptable))
         continue;
      PartBox b;
      b.name = part->GetName();
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
   if (!boxes.empty())
      return boxes;
   for (IEditable* part : p.m_ptable->GetParts())
   {
      if (part->GetItemType() == eItemPartGroup || part->GetItemType() == eItemBall || part->GetName().empty() || part->GetIHitable() == nullptr || VPX::Sketch::IsBackdropPart(part, p.m_ptable))
         continue;
      vector<Vertex3Ds> bounds;
      part->GetBoundingVertices(bounds, nullptr);
      if (bounds.empty())
         continue;
      PartBox b;
      b.name = part->GetName();
      b.x0 = b.y0 = FLT_MAX;
      b.x1 = b.y1 = -FLT_MAX;
      for (const Vertex3Ds& v : bounds)
      {
         b.x0 = std::min(b.x0, v.x);
         b.x1 = std::max(b.x1, v.x);
         b.y0 = std::min(b.y0, v.y);
         b.y1 = std::max(b.y1, v.y);
      }
      boxes.push_back(b);
   }
   return boxes;
}

double R1(float f) { return Round(f); }

float Speed(const BallSample& s) { return std::sqrt(s.vx * s.vx + s.vy * s.vy + s.vz * s.vz); }

string NearestPart(const vector<PartBox>& parts, float x, float y)
{
   string best = "unknown"s;
   float bestDistance = 45.f;
   for (const PartBox& b : parts)
   {
      const float dx = std::max({ b.x0 - x, 0.f, x - b.x1 }), dy = std::max({ b.y0 - y, 0.f, y - b.y1 });
      if (const float d = std::hypot(dx, dy); d < bestDistance)
      {
         bestDistance = d;
         best = b.name;
      }
   }
   return best;
}

json SummaryJson(const Bookmark& b)
{
   const uint32_t t0 = b.frames.empty() ? 0 : b.frames.front().t;
   std::set<int> ids;
   for (const Frame& f : b.frames)
      for (const BallSample& s : f.balls)
         ids.insert(s.id);
   json j { { "id", b.id }, { "created", b.created }, { "durationMs", b.frames.empty() ? 0 : b.frames.back().t - t0 }, { "balls", ids.size() }, { "gameTimeMs", b.gameTimeMs },
      { "shown", b.visible }, { "screenshot", b.screenshot } };
   if (!b.note.empty())
      j["note"] = b.note;
   if (!b.frames.empty() && !b.frames.back().balls.empty())
   {
      const BallSample& last = b.frames.back().balls.front();
      j["endsAt"] = json::array({ R1(last.x), R1(last.y) });
   }
   return j;
}

// What happened in the bookmarked moment, measured from the recorded frames
json Analyze(Player& p, const Bookmark& b)
{
   json out = SummaryJson(b);
   if (b.frames.size() < 2)
      return out;
   const uint32_t t0 = b.frames.front().t, tEnd = b.frames.back().t;

   // Flippers: when each went up and came down, from its recorded angle (what the flipper did, whoever pressed the button: the user, the bot, a script);
   // older bookmarks only have the button state
   const float tableCenter = (p.m_ptable->m_left + p.m_ptable->m_right) * 0.5f;
   vector<json> flipEvents;
   const bool haveAngles = !b.flippers.empty() && std::ranges::any_of(b.frames, [](const Frame& f) { return !f.flipperAngles.empty(); });
   if (haveAngles)
   {
      for (size_t fi = 0; fi < b.flippers.size(); fi++)
      {
         const FlipperInfo& info = b.flippers[fi];
         const float range = info.endAngle - info.startAngle;
         if (std::abs(range) < 1.f)
            continue;
         bool up = false;
         for (const Frame& f : b.frames)
         {
            if (fi >= f.flipperAngles.size())
               continue;
            const float progress = (f.flipperAngles[fi] - info.startAngle) / range;
            if ((!up && progress > 0.2f) || (up && progress < 0.1f))
            {
               up = !up;
               // A ball hitting a flipper that has only just started to rise pushes it back for a few ms: a release followed at once by a press is one press
               if (up && !flipEvents.empty() && flipEvents.back().value("flipper", ""s) == info.name && !flipEvents.back().value("pressed", true) && f.t - t0 - flipEvents.back().value("t", 0u) < 40)
               {
                  flipEvents.pop_back();
                  continue;
               }
               flipEvents.push_back(json { { "t", f.t - t0 }, { "flipper", info.name }, { "side", info.x < tableCenter ? "left" : "right" }, { "pressed", up } });
            }
         }
      }
   }
   else
   {
      uint8_t prev = 0;
      for (const Frame& f : b.frames)
      {
         for (const auto& [flag, side] : { std::pair<uint8_t, const char*> { FLAG_LEFT, "left" }, { FLAG_RIGHT, "right" } })
            if ((f.flags & flag) != (prev & flag))
               flipEvents.push_back(json { { "t", f.t - t0 }, { "side", side }, { "pressed", (f.flags & flag) != 0 } });
         prev = f.flags;
      }
   }
   std::ranges::sort(flipEvents, [](const json& a, const json& c) { return a.value("t", 0u) < c.value("t", 0u); });
   json flips = json::array();
   for (const json& e : flipEvents)
      flips.push_back(e);
   out["flipperEvents"] = flips;

   // The plunger: each pull back and the release (a fall of the pull to under 60% of its peak)
   json plunges = json::array();
   {
      // After a release the rod bounces on its spring: it is only armed again once it is back at rest
      bool pulling = false, armed = true;
      float peak = 0.f;
      uint32_t pullStart = 0;
      for (const Frame& f : b.frames)
      {
         if (!armed)
         {
            if (f.plunger < 0.03f)
               armed = true;
            continue;
         }
         if (!pulling && f.plunger > 0.08f)
         {
            pulling = true;
            peak = f.plunger;
            pullStart = f.t;
         }
         else if (pulling)
         {
            peak = std::max(peak, f.plunger);
            if (f.plunger < peak * 0.6f)
            {
               json plunge { { "fromMs", pullStart - t0 }, { "releaseMs", f.t - t0 }, { "pull", Round(peak, 2) } };
               if (pullStart == b.frames.front().t)
                  plunge["note"] = "the plunger was already pulled back when this moment began: the real hold is longer than fromMs..releaseMs";
               plunges.push_back(std::move(plunge));
               pulling = false;
               armed = false;
            }
         }
      }
      if (pulling)
         plunges.push_back(json { { "fromMs", pullStart - t0 }, { "pull", Round(peak, 2) }, { "note", "still pulled back at the end of the moment" } });
   }
   out["plunges"] = plunges;

   // Per ball: path, speeds, where it ended
   std::map<int, vector<std::pair<uint32_t, BallSample>>> byBall;
   for (const Frame& f : b.frames)
      for (const BallSample& s : f.balls)
         byBall[s.id].emplace_back(f.t, s);
   const vector<PartBox> parts = HittableParts(p);
   json balls = json::array(), impacts = json::array(), stuck = json::array(), facts = json::array(), turnedBack = json::array();
   for (const json& pl : plunges)
      if (pl.contains("releaseMs"))
         facts.push_back(std::format("Plunger pulled back to {:.2f} (0 at rest, 1 fully back) from {} ms, released at {} ms", pl.value("pull", 0.f), pl.value("fromMs", 0u), pl.value("releaseMs", 0u)));
   int mainBall = -1;
   size_t mainFrames = 0;
   for (const auto& [id, samples] : byBall)
   {
      if (samples.size() > mainFrames)
      {
         mainFrames = samples.size();
         mainBall = id;
      }
      float maxSpeed = 0.f;
      json path = json::array();
      uint32_t nextPath = samples.front().first;
      for (const auto& [t, s] : samples)
      {
         maxSpeed = std::max(maxSpeed, Speed(s));
         if (t >= nextPath)
         {
            path.push_back(json::array({ t - t0, R1(s.x), R1(s.y), R1(Speed(s)) }));
            nextPath = t + 100;
         }
      }
      const auto& [tFirst, first] = samples.front();
      const auto& [tLast, last] = samples.back();
      json bj { { "id", id }, { "fromMs", tFirst - t0 }, { "toMs", tLast - t0 }, { "start", json::array({ R1(first.x), R1(first.y) }) }, { "end", json::array({ R1(last.x), R1(last.y) }) },
         { "maxSpeed", R1(maxSpeed) }, { "path", path } };
      // A ball that disappears before the end of the moment, low on the table, drained
      const bool left = tLast + 150 < tEnd;
      if (left)
         bj["leftTheTableAtMs"] = tLast - t0;
      if (left && last.y > 1850.f)
      {
         bj["drained"] = true;
         facts.push_back(std::format("Ball {} drained at {} ms, after leaving the area around ({:.0f}, {:.0f})", id, tLast - t0, last.x, last.y));
      }
      balls.push_back(std::move(bj));

      // Hits: a speed loss of at least 4 and a fifth of the speed from one frame to the next (the simulator's rule)
      string lastPart;
      uint32_t lastImpactT = 0;
      for (size_t i = 1; i < samples.size(); i++)
      {
         const float before = Speed(samples[i - 1].second), after = Speed(samples[i].second), loss = before - after;
         if (loss >= 4.f && loss >= 0.2f * before)
         {
            const BallSample& s = samples[i].second;
            const string part = NearestPart(parts, s.x, s.y);
            const uint32_t t = samples[i].first - t0;
            if (part == lastPart && t - lastImpactT <= 40 && !impacts.empty())
            {
               json& last2 = impacts.back();
               last2["speedOut"] = R1(after);
               last2["loss"] = R1(last2.value("speedIn", 0.f) - after);
            }
            else
               impacts.push_back(json { { "t", t }, { "ball", id }, { "part", part }, { "x", R1(s.x) }, { "y", R1(s.y) }, { "speedIn", R1(before) }, { "speedOut", R1(after) }, { "loss", R1(loss) } });
            lastPart = part;
            lastImpactT = t;
         }
      }
      // The ball ran out of speed going up and fell back: its vertical speed turns from up to down at a low speed, above the flippers
      uint32_t lastTurnMs = 0;
      bool anyTurn = false;
      for (size_t i = 1; i < samples.size(); i++)
      {
         const BallSample& a = samples[i - 1].second;
         const BallSample& c = samples[i].second;
         if (a.vy < 0.f && c.vy >= 0.f && Speed(c) < 3.f && c.y < 1700.f)
         {
            // It was really going up before: the vertical speed fades through zero, so look back over the last 400 ms
            float strongestUp = 0.f;
            for (size_t k = i; k > 0 && samples[i].first - samples[k - 1].first <= 400; k--)
               strongestUp = std::min(strongestUp, samples[k - 1].second.vy);
            if (strongestUp > -1.5f)
               continue;
            // Did it go back the way it came (a ball that ran out of speed in a lane), or curve over the top of its path (an arch)? Compare where it was 600 ms
            // before and 600 ms after: the same x means it retraced its path
            size_t before = i, after = i;
            while (before > 0 && samples[i].first - samples[before].first < 600)
               before--;
            while (after + 1 < samples.size() && samples[after].first - samples[i].first < 600)
               after++;
            const bool known = samples[after].first - samples[i].first >= 300;
            const bool retraced = !known || std::abs(samples[before].second.x - samples[after].second.x) < 60.f;
            const uint32_t t = samples[i].first - t0;
            if (!anyTurn || t - lastTurnMs > 500)
            {
               turnedBack.push_back(json { { "ball", id }, { "t", t }, { "x", R1(c.x) }, { "y", R1(c.y) }, { "speed", R1(Speed(c)) }, { "retraced", retraced } });
               if (retraced)
                  facts.push_back(std::format("Ball {} ran out of speed going up at ({:.0f}, {:.0f}) at {} ms and rolled back down the way it came", id, c.x, c.y, t));
               else
                  facts.push_back(std::format("Ball {} slowed to {:.1f} at the top of its path at ({:.0f}, {:.0f}) at {} ms, then curved over and came down: it barely made it", id, Speed(c), c.x, c.y, t));
               lastTurnMs = t;
               anyTurn = true;
            }
         }
      }
      // A stretch of at least 1.2 s at almost no speed above the flippers: stuck, or a very slow ball
      std::ptrdiff_t slowFrom = -1;
      for (size_t i = 0; i <= samples.size(); i++)
      {
         const bool slow = i < samples.size() && Speed(samples[i].second) < 0.8f && samples[i].second.y < 1650.f;
         if (slow)
         {
            if (slowFrom < 0)
               slowFrom = static_cast<std::ptrdiff_t>(i);
            continue;
         }
         if (slowFrom >= 0)
         {
            const uint32_t span = samples[i - 1].first - samples[static_cast<size_t>(slowFrom)].first;
            if (span >= 1200)
            {
               const BallSample& s = samples[static_cast<size_t>(slowFrom)].second;
               stuck.push_back(json { { "ball", id }, { "fromMs", samples[static_cast<size_t>(slowFrom)].first - t0 }, { "toMs", samples[i - 1].first - t0 }, { "x", R1(s.x) }, { "y", R1(s.y) } });
               facts.push_back(std::format("Ball {} almost stopped at ({:.0f}, {:.0f}) for {:.1f} s: stuck or a dead spot", id, s.x, s.y, static_cast<float>(span) / 1000.f));
            }
            slowFrom = -1;
         }
      }
   }
   out["ballsInfo"] = balls;
   out["impacts"] = impacts;
   out["stuck"] = stuck;
   out["turnedBack"] = turnedBack;
   if (!flips.empty())
      facts.push_back(std::format("{} flipper moves (up or down)", flips.size()));
   if (!impacts.empty())
   {
      const json* worst = &impacts.front();
      for (const json& i : impacts)
         if (i.value("loss", 0.f) > worst->value("loss", 0.f))
            worst = &i;
      facts.push_back(std::format("Hardest hit: {} at {} ms lost {} of {} speed", worst->value("part", ""s), worst->value("t", 0u), worst->value("loss", 0.f), worst->value("speedIn", 0.f)));
   }
   out["facts"] = facts;

   // A replay of the moment in the simulator. A moment that starts with a plunge replays the plunge (the pull and how long it was held); otherwise the main ball
   // from where it was at the start of the moment, with its first flipper press
   uint32_t firstPress = UINT32_MAX;
   for (const json& fl : flips)
      if (fl.value("pressed", false))
      {
         firstPress = fl.value("t", 0u);
         break;
      }
   const json* firstPlunge = nullptr;
   for (const json& pl : plunges)
      if (pl.contains("releaseMs"))
      {
         firstPlunge = &pl;
         break;
      }
   if (firstPlunge != nullptr && firstPlunge->value("fromMs", 0u) <= firstPress)
   {
      const uint32_t from = firstPlunge->value("fromMs", 0u), release = firstPlunge->value("releaseMs", 0u);
      json args { { "action", "plunge" }, { "pull", firstPlunge->value("pull", 1.f) }, { "holdMs", std::max<uint32_t>(100, release - from) },
         { "ms", std::clamp<uint32_t>(tEnd - t0 - release + 300, 500, 60000) } };
      out["replay"] = json { { "tool", "vpx_sim" }, { "args", args },
         { "note", "Start a playtest first (vpx_editor playtest). This replays the plunge: the plunger pulled to the recorded amount for the recorded time, then every ball traced after the release. Compare with 'ballsInfo', 'turnedBack' and 'impacts'. The pull of a person is not constant: try a few nearby values with vpx_sim sweep." } };
   }
   else if (mainBall >= 0)
   {
      const BallSample& s = byBall[mainBall].front().second;
      json args { { "x", R1(s.x) }, { "y", R1(s.y) }, { "vx", R1(s.vx) }, { "vy", R1(s.vy) }, { "ms", tEnd - t0 }, { "isolate", true } };
      for (const json& fl : flips)
         if (fl.value("pressed", false))
         {
            args["action"] = "flip";
            args["side"] = fl.value("side", ""s);
            args["flipAtMs"] = fl.value("t", 0u);
            for (const json& rel : flips)
               if (!rel.value("pressed", true) && rel.value("side", ""s) == fl.value("side", ""s) && rel.value("t", 0u) > fl.value("t", 0u))
               {
                  args["holdMs"] = rel.value("t", 0u) - fl.value("t", 0u);
                  break;
               }
            break;
         }
      if (!args.contains("action"))
         args["action"] = "shot";
      out["replay"] = json { { "tool", "vpx_sim" }, { "args", args },
         { "note", "Start a playtest first (vpx_editor playtest). Approximate: it replays the main ball from its first recorded position and speed with its first flipper press; other balls and later presses are not replayed. Compare the result with 'ballsInfo' and 'impacts'." } };
   }
   return out;
}

}

void AddBookmarkTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_bookmarks",
         "The user's \"that felt wrong\" bookmarks: while the user plays the table (or a playtest) the last seconds are recorded, and the F8 key keeps the last 6 seconds as a bookmark with a screenshot of that moment. 'list' returns the bookmarks (id, time, duration, where the ball ended, the screenshot file: open it with Read to see the moment, note). "
         "'get' (id) measures the moment from the recording: per ball its path every 100 ms [t, x, y, speed], where it ended and whether it drained, the flippers (when each went up and came down, from its real angle), the plunger (each pull and release), the hits (speed lost, the part nearest), where the ball ran out of speed going up (turnedBack: retraced true means it rolled back the way it came, false that it curved over the top of its path at low speed), stretches where the ball almost stopped above the flippers (stuck, dead spots), a few plain facts, and a replay for vpx_sim (the plunge, or a flip or shot with the ball first position, speed and flipper press: run it in a playtest and compare with the recording). "
         "'show' / 'hide' (id, ids or all) draws the paths over the table in the editor and in vpx_screenshot (top and blueprint, bookmarks:false hides them): a line per ball, a dot every 100 ms, green start, red end, #id. 'note' (id, text) adds the user's words, 'delete' / 'clear' remove them. Use it when the user says something felt wrong: find the bookmark, look at the screenshot and the path, measure it, then suggest or propose a fix.",
         Schema({ { "action", Prop("string", "list (default) | get | show | hide | note | delete | clear") }, { "id", Prop("integer", "Bookmark id (get, show, hide, note, delete)") },
                  { "ids", json { { "type", "array" }, { "items", json { { "type", "integer" } } }, { "description", "Several ids (show, hide, delete)" } } },
                  { "all", Prop("boolean", "show / hide: every bookmark") }, { "text", Prop("string", "note: the text") } }),
         [](const json& args)
         {
            return WithPlayer([args](Player& p)
            {
               const string action = args.value("action", "list"s);
               Store& store = Store::For(p.m_ptable);
               vector<int> ids = args.value("ids", vector<int>());
               if (args.contains("id") && args["id"].is_number_integer())
                  ids.push_back(args["id"].get<int>());
               const auto result = [&](json j)
               {
                  j["saved"] = !store.Path().empty() ? json(PathToUTF8(store.Path())) : json("not saved: the table has no file yet, bookmarks only live in this session");
                  return TextOutput(j);
               };
               if (action == "list")
               {
                  json arr = json::array();
                  for (const Bookmark& b : store.All())
                     arr.push_back(SummaryJson(b));
                  return result(json { { "count", arr.size() }, { "bookmarks", arr }, { "hint", "The user presses F8 while playing to bookmark the last 6 seconds" } });
               }
               if (action == "clear")
               {
                  const size_t n = store.All().size();
                  store.Clear();
                  return result(json { { "cleared", n } });
               }
               if (action == "show" || action == "hide")
               {
                  size_t changed = 0;
                  for (const Bookmark& cb : store.All())
                  {
                     Bookmark* b = store.Find(cb.id);
                     if (b != nullptr && (args.value("all", false) || std::ranges::find(ids, b->id) != ids.end()))
                     {
                        b->visible = action == "show";
                        changed++;
                     }
                  }
                  return result(json { { "changed", changed } });
               }
               if (ids.empty())
                  return ErrorOutput("id is required"s);
               if (action == "get" || action == "note")
               {
                  Bookmark* b = store.Find(ids.front());
                  if (b == nullptr)
                     return ErrorOutput("Unknown bookmark id"s);
                  if (action == "note")
                  {
                     b->note = args.value("text", ""s);
                     store.Changed(*b);
                     return result(SummaryJson(*b));
                  }
                  return result(Analyze(p, *b));
               }
               if (action == "delete")
               {
                  json removed = json::array(), unknown = json::array();
                  for (const int id : ids)
                     (store.Remove(id) ? removed : unknown).push_back(id);
                  return result(json { { "deleted", removed }, { "unknown", unknown } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });
}

}
