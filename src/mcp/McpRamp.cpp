// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

// vpx_ramp: the collision ramp a ball rides, judged and tested the way the ~/tables survey did (172 tables, 1598 raised ramps ride tested,
// 2026-11-03). fit grades a ramp against the rules that came out of that survey; test rolls a ball down it and up it in the running playtest.

namespace VPX::MCP
{

namespace
{
   struct RampPath
   {
      Ramp* ramp = nullptr;
      string name;
      vector<Vertex3Ds> pts; // floor centerline (z = floor height)
      vector<float> half; // collision half width per point
      vector<float> dist; // distance along from the first point
      float length = 0.f;
      float tanSlope = 0.f;
      float Eff(size_t i) const { return pts[i].z - pts[i].y * tanSlope; }
   };

   float TableTilt(const Player& p)
   {
      const PinTable* t = p.m_ptable;
      const float minSlope = t->m_overridePhysics ? t->m_fOverrideMinSlope : t->m_angletiltMin;
      const float maxSlope = t->m_overridePhysics ? t->m_fOverrideMaxSlope : t->m_angletiltMax;
      return lerp(minSlope, maxSlope, t->m_globalDifficulty);
   }

   bool BuildRampPath(Player& p, Ramp* ramp, RampPath& out, string& error)
   {
      int count = 0;
      float* height = nullptr;
      const Vertex2D* const outline = ramp->GetRampVertex(count, &height, nullptr, nullptr, nullptr, HIT_SHAPE_DETAIL_LEVEL, true);
      if (!outline || count < 2)
      {
         delete[] outline;
         delete[] height;
         error = "the ramp has no usable centerline"s;
         return false;
      }
      out.ramp = ramp;
      out.name = ramp->GetName();
      out.tanSlope = tanf(TableTilt(p) * (float)(M_PI / 180.0));
      for (int i = 0; i < count; i++)
      {
         const Vertex2D a = outline[i], b = outline[count * 2 - i - 1];
         out.pts.emplace_back((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, height[i]);
         out.half.push_back(sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)) * 0.5f);
      }
      delete[] outline;
      delete[] height;
      out.dist.push_back(0.f);
      for (size_t i = 1; i < out.pts.size(); i++)
         out.dist.push_back(out.dist.back() + Vertex2D(out.pts[i].x - out.pts[i - 1].x, out.pts[i].y - out.pts[i - 1].y).Length());
      out.length = out.dist.back();
      return true;
   }

   // Point and 2D tangent at a distance along the centerline
   Vertex3Ds PointAt(const RampPath& rp, float s, Vertex2D& tangent)
   {
      s = std::clamp(s, 0.f, rp.length);
      for (size_t i = 0; i + 1 < rp.pts.size(); i++)
         if (rp.dist[i + 1] >= s)
         {
            const float seg = rp.dist[i + 1] - rp.dist[i];
            const float t = seg < 1e-6f ? 0.f : (s - rp.dist[i]) / seg;
            const Vertex3Ds &a = rp.pts[i], &b = rp.pts[i + 1];
            tangent = Vertex2D(b.x - a.x, b.y - a.y);
            if (tangent.LengthSquared() < 1e-9f)
               tangent = Vertex2D(0.f, -1.f);
            tangent.Normalize();
            return Vertex3Ds(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
         }
      const Vertex3Ds &a = rp.pts[rp.pts.size() - 2], &b = rp.pts.back();
      tangent = Vertex2D(b.x - a.x, b.y - a.y);
      if (tangent.LengthSquared() < 1e-9f)
         tangent = Vertex2D(0.f, -1.f);
      tangent.Normalize();
      return b;
   }

   // Closest point of the centerline to (x, y): distance along, lateral distance and the floor height there
   void Project(const RampPath& rp, float x, float y, float& along, float& lateral, float& floorZ)
   {
      float best = FLT_MAX;
      along = 0.f;
      floorZ = rp.pts.front().z;
      for (size_t i = 0; i + 1 < rp.pts.size(); i++)
      {
         const Vertex3Ds &a = rp.pts[i], &b = rp.pts[i + 1];
         const float dx = b.x - a.x, dy = b.y - a.y;
         const float l2 = dx * dx + dy * dy;
         const float t = l2 < 1e-9f ? 0.f : std::clamp(((x - a.x) * dx + (y - a.y) * dy) / l2, 0.f, 1.f);
         const float px = a.x + dx * t, py = a.y + dy * t;
         const float lat = sqrtf((x - px) * (x - px) + (y - py) * (y - py));
         if (lat < best)
         {
            best = lat;
            along = rp.dist[i] + t * (rp.dist[i + 1] - rp.dist[i]);
            floorZ = a.z + (b.z - a.z) * t;
         }
      }
      lateral = best;
   }

   // Radius of the circle through three centerline points 30 units apart, the way the survey measured bends
   float TightestBend(const RampPath& rp, float fromS, float toS, float& whereS)
   {
      float tightest = FLT_MAX;
      whereS = 0.f;
      for (float s = max(fromS, 30.f); s <= min(toS, rp.length - 30.f); s += 10.f)
      {
         Vertex2D ta, tb, tc;
         const Vertex3Ds a = PointAt(rp, s - 30.f, ta), b = PointAt(rp, s, tb), c = PointAt(rp, s + 30.f, tc);
         const float ab = Vertex2D(b.x - a.x, b.y - a.y).Length(), bc = Vertex2D(c.x - b.x, c.y - b.y).Length(), ca = Vertex2D(a.x - c.x, a.y - c.y).Length();
         const float area2 = std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
         if (area2 > 1e-4f)
         {
            const float r = ab * bc * ca / (2.f * area2);
            if (r < tightest)
            {
               tightest = r;
               whereS = s;
            }
         }
      }
      return tightest;
   }

   float ClimbSpeedFor(float effectiveHeight) { return 16.4f + 0.077f * max(0.f, effectiveHeight); }

   // Survey bins: descents that got through, by parameter (raised ball-path ramps, 2026-11-03)
   struct Bin { float from, to; int through; };
   const Bin WIDTH_BINS[] = { { 0, 40, 13 }, { 40, 50, 13 }, { 50, 55, 79 }, { 55, 60, 91 }, { 60, 70, 86 }, { 70, 90, 82 }, { 90, FLT_MAX, 78 } };
   const Bin WALL_BINS[] = { { 0, 1, 66 }, { 1, 20, 69 }, { 20, 40, 93 }, { 40, 60, 91 }, { 60, 100, 88 }, { 100, FLT_MAX, 86 } };
   const Bin SLOPE_BINS[] = { { 0, 2, 64 }, { 2, 4, 72 }, { 4, 6, 86 }, { 6, 8, 82 }, { 8, 12, 85 }, { 12, 20, 89 }, { 20, FLT_MAX, 87 } };
   const Bin BEND_BINS[] = { { 0, 50, 69 }, { 50, 100, 80 }, { 100, 150, 80 }, { 150, 250, 89 }, { 250, 500, 93 }, { 500, FLT_MAX, 89 } };

   template <size_t N> int ThroughRate(const Bin (&bins)[N], float v)
   {
      for (const Bin& b : bins)
         if (v >= b.from && v < b.to)
            return b.through;
      return bins[N - 1].through;
   }

   json Rule(const string& rule, const string& status, float value, int surveyThrough, const string& note)
   {
      json j { { "rule", rule }, { "status", status }, { "value", Round(value) }, { "surveyThroughPercent", surveyThrough } };
      if (!note.empty())
         j["note"] = note;
      return j;
   }

   // Where the ball comes on, climbs to and rolls down from: the top is the highest effective point. A ramp whose top is inside it (a climb
   // then a descent, as plan lays out) is traveled from its first point to its last; otherwise the ball climbs from the low end and descends from the high one
   struct Layout
   {
      bool hump = false;
      bool highStart = true;
      float topS = 0.f;
      float climbStartS = 0.f; // the end the climb test enters from
      float descentGoalS = 0.f; // the end the descent test rolls toward
      float climbGain = 0.f; // effective height from the climb entry to the top
      float effDrop = 0.f; // effective height from the top down to the descent goal
      float rise = 0.f;
   };

   Layout LayoutOf(const RampPath& rp)
   {
      Layout l;
      size_t top = 0;
      float topEff = -FLT_MAX;
      for (size_t i = 0; i < rp.pts.size(); i++)
         if (const float e = rp.Eff(i); e > topEff)
         {
            topEff = e;
            top = i;
         }
      const float es = rp.Eff(0), ee = rp.Eff(rp.pts.size() - 1);
      l.highStart = es >= ee;
      const float topS = rp.dist[top];
      l.hump = topS > 60.f && topS < rp.length - 60.f && topEff > max(es, ee) + 8.f;
      if (l.hump)
      {
         l.topS = topS;
         l.climbStartS = 0.f;
         l.descentGoalS = rp.length;
         l.climbGain = topEff - es;
         l.effDrop = topEff - ee;
         l.rise = rp.pts[top].z - min(rp.pts.front().z, rp.pts.back().z);
      }
      else
      {
         l.topS = l.highStart ? 0.f : rp.length;
         l.climbStartS = l.highStart ? rp.length : 0.f;
         l.descentGoalS = l.climbStartS;
         l.climbGain = l.effDrop = std::abs(es - ee);
         l.rise = std::abs(rp.pts.front().z - rp.pts.back().z);
      }
      return l;
   }

   // Grade a ramp against the survey rules
   json FitJson(Player& p, const RampPath& rp, const json* rampcheck)
   {
      const RampData& d = rp.ramp->m_d;
      const Layout lay = LayoutOf(rp);
      const float width = 2.f * *std::ranges::min_element(rp.half);
      const float lowerWall = min(d.m_leftwallheight, d.m_rightwallheight);
      const float descentLen = std::abs(lay.descentGoalS - lay.topS);
      const float slopeDeg = descentLen > 0.f ? atan2f(lay.effDrop, descentLen) * (float)(180.0 / M_PI) : 0.f;
      float bendAt = 0.f;
      const float bend = TightestBend(rp, 0.f, rp.length, bendAt);
      // the flattest 60 unit stretch of the descent, in its direction: where a slow ball stalls
      float flattest = FLT_MAX, flattestAt = 0.f;
      const float dir = lay.descentGoalS > lay.topS ? 1.f : -1.f;
      for (float s = min(lay.topS, lay.descentGoalS); s + 60.f <= max(lay.topS, lay.descentGoalS); s += 10.f)
      {
         Vertex2D t;
         const Vertex3Ds a = PointAt(rp, s, t), b = PointAt(rp, s + 60.f, t);
         const float ea = a.z - a.y * rp.tanSlope, eb = b.z - b.y * rp.tanSlope;
         const float grade = (ea - eb) * dir / 60.f;
         if (grade < flattest)
         {
            flattest = grade;
            flattestAt = s + 30.f;
         }
      }
      json rules = json::array();
      rules.push_back(Rule("collision width 55 to 60 for wire, about 70 under a sheet ramp, never under 50", width < 50.f ? "fail" : width < 55.f ? "warn" : "pass", width, ThroughRate(WIDTH_BINS, width),
         width < 50.f ? "40 to 50 wide got through 13% of descents in the survey"s : ""s));
      rules.push_back(Rule("walls at least 20 (62 is the common value)", lowerWall < 1.f ? "fail" : lowerWall < 20.f ? "warn" : "pass", lowerWall, ThroughRate(WALL_BINS, lowerWall),
         lowerWall < 20.f ? "ramps without walls got through 66% of descents, with walls of 20 or more about 90%"s : ""s));
      rules.push_back(Rule("effective downhill grade (table tilt included) 4 degrees or more", slopeDeg < 2.f ? "fail" : slopeDeg < 4.f ? "warn" : "pass", slopeDeg, ThroughRate(SLOPE_BINS, slopeDeg),
         slopeDeg < 4.f ? std::format("effective drop {:.1f} over {:.0f} of descent; under 2 degrees 64% got through, mostly stalls", lay.effDrop, descentLen) : ""s));
      if (flattest != FLT_MAX)
      {
         const float flattestDeg = atanf(flattest) * (float)(180.0 / M_PI);
         rules.push_back(Rule("no stretch where the descending ball runs level or uphill", flattest < -0.01f ? "fail" : flattestDeg < 1.5f ? "warn" : "pass", flattestDeg, ThroughRate(SLOPE_BINS, max(0.f, flattestDeg)),
            flattestDeg < 1.5f ? std::format("flattest 60 units centered {:.0f} along the ramp", flattestAt) : ""s));
      }
      if (bend != FLT_MAX)
         rules.push_back(Rule("bend radius 150 or more, never under 50", bend < 47.5f ? "fail" : bend < 142.5f ? "warn" : "pass", bend, ThroughRate(BEND_BINS, bend),
            bend < 142.5f ? std::format("tightest bend {:.0f} along the ramp", bendAt) : ""s));
      const float climb = ClimbSpeedFor(lay.climbGain);
      rules.push_back(Rule("climb speed within a flipper shot (34 to 50)", climb > 50.f ? "fail" : climb > 40.f ? "warn" : "pass", climb, 0,
         std::format("speed needed where the ball comes on to reach the top, from the survey fit 16.4 + 0.077 x effective height gained ({:.0f})", lay.climbGain)));
      json findings = json::array();
      if (rampcheck)
      {
         const string prefix = rp.name + ' ';
         for (const json& w : rampcheck->value("warnings", json::array()))
         {
            const string s = w.get<string>();
            if (s.rfind(rp.name + ':', 0) == 0 || s.rfind(prefix, 0) == 0 || s.find(" -> " + rp.name) != string::npos || s.find("path " + rp.name) != string::npos)
               findings.push_back(s);
         }
         for (const json& c : rampcheck->value("pathClearance", json::array()))
            if (c.value("ramp", ""s) == rp.name)
            {
               const string s = std::format("{} cuts {:.1f} into the ball path {:.0f} along the ramp ({} side)", c.value("part", ""s), c.value("penetration", 0.f), c.value("along", 0.f), c.value("side", ""s));
               if (std::ranges::find(findings, json(s)) == findings.end())
                  findings.push_back(s);
            }
         rules.push_back(Rule("no rampcheck findings", findings.empty() ? "pass" : findings.size() >= 4 ? "fail" : "warn", static_cast<float>(findings.size()), findings.empty() ? 92 : findings.size() >= 4 ? 77 : 85,
            findings.empty() ? ""s : "parts cutting into the path, lips at the ends and wedge gaps are the commonest causes of a ball not getting through"s));
      }
      int fails = 0, warns = 0;
      for (const json& r : rules)
      {
         fails += r["status"] == "fail";
         warns += r["status"] == "warn";
      }
      json result { { "name", rp.name }, { "collidable", d.m_collidable }, { "length", Round(rp.length) }, { "zStart", Round(rp.pts.front().z) }, { "zEnd", Round(rp.pts.back().z) },
         { "top", { { "along", Round(lay.topS) }, { "inside", lay.hump } } }, { "climbsFrom", lay.climbStartS < 1.f ? "start" : "end" }, { "descendsTo", lay.descentGoalS < 1.f ? "start" : "end" },
         { "rise", Round(lay.rise) }, { "effectiveHeightGained", Round(lay.climbGain) }, { "effectiveDrop", Round(lay.effDrop) }, { "effectiveSlopeDeg", Round(slopeDeg, 2) }, { "collisionWidth", Round(width) },
         { "walls", { Round(d.m_leftwallheight), Round(d.m_rightwallheight) } }, { "tightestBendRadius", bend == FLT_MAX ? json() : json(Round(bend)) }, { "climbSpeedNeeded", Round(climb) },
         { "tableTiltDeg", Round(TableTilt(p), 2) }, { "rules", rules }, { "verdict", fails ? "fail" : warns ? "warn" : "pass" } };
      if (!findings.empty())
         result["findings"] = findings;
      return result;
   }

   // Follows the ball along the ramp: how far it got toward goalS, whether it left sideways, stalled or came back
   json Classify(const RampPath& rp, const json& run, float startS, float goalS, float halfWidth)
   {
      const float forward = goalS > startS ? 1.f : -1.f;
      const json samples = run.value("samples", json::array());
      float best = 0.f, lastOn = -1.f;
      bool onRampSeen = false;
      json fell;
      for (size_t k = 0; k < samples.size(); k++)
      {
         const json& smp = samples[k];
         float s, lat, floorZ;
         Project(rp, smp.value("x", 0.f), smp.value("y", 0.f), s, lat, floorZ);
         const float z = smp.value("z", 0.f);
         const bool on = lat <= halfWidth + 30.f && z - floorZ >= -15.f && z - floorZ <= 70.f;
         if (on)
         {
            onRampSeen = true;
            lastOn = s;
            best = max(best, (s - startS) * forward);
         }
         else if (onRampSeen && fell.is_null())
         {
            const float at = lastOn >= 0.f ? lastOn : s;
            if (at > 25.f && at < rp.length - 25.f)
            {
               const int t0 = smp.value("t", 0);
               float lowest = z;
               for (size_t j = k; j < samples.size() && samples[j].value("t", 0) - t0 <= 400; j++)
                  lowest = min(lowest, samples[j].value("z", z));
               fell = json { { "at", Round(at) }, { "lateral", Round(lat) }, { "zBelowFloor", Round(floorZ - z) }, { "dropAfter", Round(z - lowest) }, { "x", Round(smp.value("x", 0.f)) }, { "y", Round(smp.value("y", 0.f)) }, { "z", Round(z) } };
            }
         }
      }
      const float need = std::abs(goalS - startS);
      float endS = 0.f, endLat = FLT_MAX, endFloor = 0.f, endSpeed = 0.f;
      bool endOn = false;
      if (!samples.empty())
      {
         const json& e = samples.back();
         Project(rp, e.value("x", 0.f), e.value("y", 0.f), endS, endLat, endFloor);
         endSpeed = sqrtf(e.value("vx", 0.f) * e.value("vx", 0.f) + e.value("vy", 0.f) * e.value("vy", 0.f));
         endOn = endLat <= halfWidth + 30.f && e.value("z", 0.f) - endFloor >= -15.f && e.value("z", 0.f) - endFloor <= 70.f;
      }
      bool rodeRamp = false;
      for (const json& seg : run.value("rode", json::array()))
         rodeRamp |= seg.value("on", ""s) == rp.name;
      string verdict;
      if (!onRampSeen && !rodeRamp)
         verdict = "missed";
      else if (best >= need - 30.f)
         verdict = "through";
      else if (!fell.is_null())
         verdict = fell.value("dropAfter", 0.f) >= 30.f ? "fell_off" : "left_side";
      else if (endOn && endSpeed < 1.f)
         verdict = "stalled";
      else
         verdict = "rolled_back";
      json out { { "verdict", verdict }, { "progress", Round(best) }, { "needed", Round(need) }, { "end", { { "along", Round(endS) }, { "onRamp", endOn }, { "speed", Round(endSpeed, 2) } } } };
      if (!fell.is_null())
         out["fell"] = fell;
      return out;
   }

   json ShotSummary(const json& r)
   {
      json out { { "outcome", r.value("outcome", json()) }, { "maxHeight", r.value("maxHeight", 0.f) }, { "impactCount", r.value("impactCount", 0) } };
      json rode = json::array();
      for (const json& seg : r.value("rode", json::array()))
         rode.push_back(seg.value("on", ""s));
      out["rode"] = rode;
      json impacts = json::array();
      for (const json& i : r.value("impacts", json::array()))
      {
         impacts.push_back({ { "part", i.value("part", ""s) }, { "loss", i.value("loss", 0.f) }, { "speedIn", i.value("speedIn", 0.f) }, { "x", i.value("x", 0.f) }, { "y", i.value("y", 0.f) }, { "z", i.value("z", 0.f) } });
         if (impacts.size() >= 3)
            break;
      }
      out["impacts"] = impacts;
      float maxSpeed = 0.f;
      for (const json& s : r.value("samples", json::array()))
         maxSpeed = max(maxSpeed, sqrtf(s.value("vx", 0.f) * s.value("vx", 0.f) + s.value("vy", 0.f) * s.value("vy", 0.f)));
      out["maxSpeed"] = Round(maxSpeed, 2);
      if (const json errs = r.value("scriptErrors", json::object()); errs.value("count", 0) > 0)
         out["scriptErrors"] = errs["count"];
      return out;
   }

   // Where a failed run went wrong along the ramp and what is there (grade, bend, walls, parts cutting in, open edges, an end)
   json Diagnose(const RampPath& rp, const json& run, bool descending, float startS, float goalS, const json* rampcheck)
   {
      const float forward = goalS > startS ? 1.f : -1.f;
      float sf = run.contains("fell") ? run["fell"].value("at", 0.f) : startS + forward * run.value("progress", 0.f);
      sf = std::clamp(sf, 0.f, rp.length);
      Vertex2D t;
      const Vertex3Ds a = PointAt(rp, sf - 40.f, t), b = PointAt(rp, sf + 40.f, t);
      const float runLen = max(min(sf + 40.f, rp.length) - max(sf - 40.f, 0.f), 1e-3f);
      const float grade = ((a.z - a.y * rp.tanSlope) - (b.z - b.y * rp.tanSlope)) / runLen * forward;
      float bendAt = 0.f;
      const float bend = TightestBend(rp, sf - 60.f, sf + 60.f, bendAt);
      json nearby = json::array();
      if (rampcheck)
      {
         for (const json& c : rampcheck->value("pathClearance", json::array()))
            if (c.value("ramp", ""s) == rp.name && std::abs(c.value("along", -1e9f) - sf) < 60.f)
               nearby.push_back({ { "part", c.value("part", ""s) }, { "cuts", c.value("penetration", 0.f) }, { "side", c.value("side", ""s) } });
         for (const json& u : rampcheck->value("underside", json::array()))
         {
            if (u.value("ramp", ""s) != rp.name)
               continue;
            float lo = FLT_MAX, hi = -FLT_MAX;
            for (const char* key : { "from", "to" })
               if (u.contains(key) && u[key].is_array() && u[key].size() >= 2)
               {
                  float s, lat, fz;
                  Project(rp, u[key][0].get<float>(), u[key][1].get<float>(), s, lat, fz);
                  lo = min(lo, s);
                  hi = max(hi, s);
               }
            if (lo != FLT_MAX && lo - 60.f < sf && sf < hi + 60.f)
               nearby.push_back({ { "kind", "open edge a ball wedges under" }, { "side", u.value("side", ""s) } });
         }
      }
      if (sf < 40.f || sf > rp.length - 40.f)
         nearby.push_back({ { "kind", "end of the ramp (join or hand-over)" } });
      string cause;
      float worstCut = 0.f;
      for (const json& n : nearby)
         worstCut = max(worstCut, n.value("cuts", 0.f));
      if (worstCut >= 5.f)
         cause = "another part cuts into the ball path there";
      else if (descending && grade < 0.03f)
         cause = "too little downhill grade there (under 3%, table tilt included)";
      else if (std::ranges::any_of(nearby, [](const json& n) { return n.value("kind", ""s).rfind("end", 0) == 0; }))
         cause = "at an end (join or hand-over)";
      else if (min(rp.ramp->m_d.m_leftwallheight, rp.ramp->m_d.m_rightwallheight) < 20.f && (run.value("verdict", ""s) == "fell_off" || run.value("verdict", ""s) == "left_side"))
         cause = "low or no wall on that ramp";
      else if (std::ranges::any_of(nearby, [](const json& n) { return n.value("kind", ""s).rfind("open edge", 0) == 0; }))
         cause = "open edge at ball height (wedge gap)";
      else if (bend != FLT_MAX && bend < 100.f)
         cause = "tight bend (radius under 100)";
      else if (!descending)
         cause = "not enough speed for the climb";
      else
         cause = "unexplained: look at the impacts and the samples";
      json out { { "at", Round(sf) }, { "of", Round(rp.length) }, { "downhillGradeHere", Round(grade, 3) }, { "bendRadiusHere", bend == FLT_MAX ? json() : json(Round(bend)) }, { "likelyCause", cause } };
      if (!nearby.empty())
         out["nearby"] = nearby;
      return out;
   }


   struct Waypoint
   {
      Vertex2D xy;
      std::optional<float> z;
   };

   struct Plan
   {
      vector<Vertex3Ds> drag; // drag points with the per point z offset VPX stores
      vector<Vertex3Ds> floor; // the floor the ball rides, per drag point
      vector<float> s; // distance along, per drag point
      float length = 0.f;
      float heightBottom = 0.f, heightTop = 0.f;
      float width = 60.f, walls = 62.f;
      json bends = json::array();
      json grades = json::array();
      json warnings = json::array();
      float climbSpeed = 0.f;
      float topEff = 0.f;
      float exitExtension = 0.f;
   };

   bool ParseWaypoints(const json& pts, vector<Waypoint>& out, string& error)
   {
      if (!pts.is_array() || pts.size() < 2)
      {
         error = "points needs at least 2 waypoints: [[x, y], [x, y, z], ...] or [{x, y, z}, ...]"s;
         return false;
      }
      for (const json& q : pts)
      {
         Waypoint w;
         if (q.is_array() && q.size() >= 2)
         {
            w.xy = Vertex2D(q[0].get<float>(), q[1].get<float>());
            if (q.size() >= 3 && q[2].is_number())
               w.z = q[2].get<float>();
         }
         else if (q.is_object() && q.contains("x") && q.contains("y"))
         {
            w.xy = Vertex2D(q.value("x", 0.f), q.value("y", 0.f));
            if (q.contains("z") && q["z"].is_number())
               w.z = q.value("z", 0.f);
         }
         else
         {
            error = "each waypoint is [x, y], [x, y, z] or {x, y, z}"s;
            return false;
         }
         if (!out.empty() && Vertex2D(w.xy.x - out.back().xy.x, w.xy.y - out.back().xy.y).Length() < 1.f)
            continue;
         out.push_back(w);
      }
      if (out.size() < 2)
      {
         error = "the waypoints are all at the same place"s;
         return false;
      }
      return true;
   }

   // Straights joined by arcs of the requested radius (as much as the segments allow), arcs sampled every 6 degrees; the waypoint itself
   // becomes a knot on the arc's middle so a height given there lands where the user put it
   Plan BuildPlan(Player& p, const vector<Waypoint>& wps, float minRadius, float width, float walls, float minGradeDeg)
   {
      Plan plan;
      plan.width = width;
      plan.walls = walls;
      struct Knot { Vertex2D xy; size_t waypoint; };
      vector<Knot> route;
      route.push_back({ wps.front().xy, 0 });
      vector<float> appliedRadius(wps.size(), 0.f);
      for (size_t i = 1; i + 1 < wps.size(); i++)
      {
         const Vertex2D prev = route.back().xy, cur = wps[i].xy, next = wps[i + 1].xy;
         Vertex2D in(cur.x - prev.x, cur.y - prev.y), out(next.x - cur.x, next.y - cur.y);
         const float lenIn = in.Length(), lenOut = Vertex2D(next.x - wps[i].xy.x, next.y - wps[i].xy.y).Length();
         if (lenIn < 1e-3f || lenOut < 1e-3f)
            continue;
         in.Normalize();
         out.Normalize();
         const float cross = in.x * out.y - in.y * out.x;
         const float dot = std::clamp(in.x * out.x + in.y * out.y, -1.f, 1.f);
         const float theta = acosf(dot);
         if (theta < 0.5f * (float)(M_PI / 180.0))
         {
            route.push_back({ cur, i });
            continue;
         }
         const float tanHalf = tanf(theta * 0.5f);
         // the tangent length must leave room on both segments (the next corner takes its own share of the following one)
         const float maxT = min(lenIn, lenOut * 0.5f) * 0.98f;
         float r = minRadius;
         float t = r * tanHalf;
         if (t > maxT)
         {
            t = maxT;
            r = t / tanHalf;
         }
         appliedRadius[i] = r;
         const Vertex2D a(cur.x - in.x * t, cur.y - in.y * t), b(cur.x + out.x * t, cur.y + out.y * t);
         const float sign = cross >= 0.f ? 1.f : -1.f;
         const Vertex2D normalIn(-in.y * sign, in.x * sign);
         const Vertex2D center(a.x + normalIn.x * r, a.y + normalIn.y * r);
         const int steps = max(2, static_cast<int>(ceilf(theta / (6.f * (float)(M_PI / 180.0)))));
         const int half = (steps + 1) / 2 * 2; // even count so the middle sample exists
         const float a0 = atan2f(a.y - center.y, a.x - center.x);
         for (int k = 0; k <= half; k++)
         {
            const float ang = a0 + sign * theta * static_cast<float>(k) / static_cast<float>(half);
            route.push_back({ Vertex2D(center.x + cosf(ang) * r, center.y + sinf(ang) * r), k == half / 2 ? i : SIZE_MAX });
         }
         json bend { { "waypoint", i }, { "at", { Round(cur.x), Round(cur.y) } }, { "turnDeg", Round(theta * (float)(180.0 / M_PI)) }, { "radius", Round(r) } };
         if (r < minRadius - 0.5f)
            bend["note"] = std::format("the segments only allow {:.0f} here (asked {:.0f}); move the waypoints apart for a wider bend", r, minRadius);
         if (r < 50.f)
         {
            bend["status"] = "fail";
            plan.warnings.push_back(std::format("bend at waypoint {} has radius {:.0f}: under 50 only 69% of descents got through", i, r));
         }
         else if (r < 150.f)
         {
            bend["status"] = "warn";
            plan.warnings.push_back(std::format("bend at waypoint {} has radius {:.0f}: 150 or more is the rule (80% through at 50-150, 89-93% above)", i, r));
         }
         else
            bend["status"] = "pass";
         plan.bends.push_back(bend);
      }
      route.push_back({ wps.back().xy, wps.size() - 1 });
      // distances along, and the heights: piecewise linear in distance between the waypoints that carry a z
      vector<float> s;
      s.push_back(0.f);
      for (size_t i = 1; i < route.size(); i++)
         s.push_back(s.back() + Vertex2D(route[i].xy.x - route[i - 1].xy.x, route[i].xy.y - route[i - 1].xy.y).Length());
      plan.length = s.back();
      vector<std::pair<float, float>> knots; // (s, z)
      for (size_t i = 0; i < route.size(); i++)
         if (route[i].waypoint != SIZE_MAX && wps[route[i].waypoint].z)
            knots.emplace_back(s[i], *wps[route[i].waypoint].z);
      if (knots.empty())
         knots.emplace_back(0.f, 0.f);
      if (knots.front().first > 0.f)
         knots.insert(knots.begin(), { 0.f, knots.front().second });
      if (knots.back().first < plan.length)
         knots.emplace_back(plan.length, knots.back().second);
      const auto zAt = [&knots](float at)
      {
         for (size_t k = 0; k + 1 < knots.size(); k++)
            if (at <= knots[k + 1].first)
            {
               const float span = knots[k + 1].first - knots[k].first;
               const float t = span < 1e-6f ? 0.f : (at - knots[k].first) / span;
               return knots[k].second + (knots[k + 1].second - knots[k].second) * t;
            }
         return knots.back().second;
      };
      plan.heightBottom = zAt(0.f);
      plan.heightTop = zAt(plan.length);
      const float tanSlope = tanf(TableTilt(p) * (float)(M_PI / 180.0));
      for (size_t i = 0; i < route.size(); i++)
      {
         const float z = zAt(s[i]);
         const float frac = plan.length > 0.f ? s[i] / plan.length : 0.f;
         plan.floor.emplace_back(route[i].xy.x, route[i].xy.y, z);
         plan.drag.emplace_back(route[i].xy.x, route[i].xy.y, z - (plan.heightBottom + (plan.heightTop - plan.heightBottom) * frac));
         plan.s.push_back(s[i]);
      }
      // grades between the height knots, effective (table tilt included), in the direction of travel
      const float entryEff = plan.floor.front().z - plan.floor.front().y * tanSlope;
      float topEff = entryEff;
      for (size_t k = 0; k + 1 < knots.size(); k++)
      {
         const float s0 = knots[k].first, s1 = knots[k + 1].first;
         if (s1 - s0 < 1.f)
            continue;
         size_t i0 = 0, i1 = 0;
         for (size_t i = 0; i < plan.s.size(); i++)
         {
            if (plan.s[i] <= s0 + 0.01f)
               i0 = i;
            if (plan.s[i] <= s1 + 0.01f)
               i1 = i;
         }
         const float e0 = plan.floor[i0].z - plan.floor[i0].y * tanSlope, e1 = plan.floor[i1].z - plan.floor[i1].y * tanSlope;
         topEff = max(topEff, e1);
         const float deg = atan2f(e1 - e0, s1 - s0) * (float)(180.0 / M_PI);
         json g { { "from", Round(s0) }, { "to", Round(s1) }, { "rise", Round(knots[k + 1].second - knots[k].second) }, { "effectiveSlopeDeg", Round(deg, 2) }, { "direction", deg > 0.5f ? "climb" : deg < -0.5f ? "descent" : "level" } };
         if (deg < 0.5f && deg > -minGradeDeg)
         {
            g["status"] = deg <= -2.f ? "warn" : "fail";
            plan.warnings.push_back(std::format("{:.0f} to {:.0f} along the route runs at {:.1f} degrees effective ({}): a ball must keep rolling at {:.0f} degrees or more (under 2: 64% through, 4-6: 86%)", s0, s1, -deg, g["direction"].get<string>(), minGradeDeg));
         }
         else
            g["status"] = "pass";
         plan.grades.push_back(g);
      }
      plan.topEff = topEff - entryEff;
      plan.climbSpeed = ClimbSpeedFor(plan.topEff);
      if (plan.climbSpeed > 50.f)
         plan.warnings.push_back(std::format("the entry needs {:.1f} to reach the top ({:.0f} effective height gained): more than a flipper shot gives (34 to 50)", plan.climbSpeed, plan.topEff));
      else if (plan.climbSpeed > 40.f)
         plan.warnings.push_back(std::format("the entry needs {:.1f} to reach the top ({:.0f} effective height gained): only a strong flipper shot gets there", plan.climbSpeed, plan.topEff));
      if (width < 50.f)
         plan.warnings.push_back(std::format("width {:.0f} is under 50: 13% through in the survey", width));
      if (walls < 20.f)
         plan.warnings.push_back(std::format("walls of {:.0f}: 20 or more is the rule (66% through without walls, about 90% with)", walls));
      return plan;
   }

   json PlanJson(const Plan& plan, const string& finish)
   {
      json points = json::array(), floor = json::array();
      for (size_t i = 0; i < plan.drag.size(); i++)
      {
         points.push_back({ { "x", Round(plan.drag[i].x) }, { "y", Round(plan.drag[i].y) }, { "z", Round(plan.drag[i].z) } });
         floor.push_back({ Round(plan.floor[i].x), Round(plan.floor[i].y), Round(plan.floor[i].z), Round(plan.s[i]) });
      }
      json data { { "type", 0 }, { "height_bottom", Round(plan.heightBottom) }, { "height_top", Round(plan.heightTop) }, { "width_bottom", Round(plan.width) }, { "width_top", Round(plan.width) },
         { "left_wall_height", Round(plan.walls) }, { "right_wall_height", Round(plan.walls) }, { "left_wall_height_visible", 0 }, { "right_wall_height_visible", 0 }, { "visible", false }, { "collidable", true } };
      return json { { "finish", finish }, { "length", Round(plan.length) }, { "width", Round(plan.width) }, { "walls", Round(plan.walls) }, { "heightBottom", Round(plan.heightBottom) }, { "heightTop", Round(plan.heightTop) },
         { "effectiveHeightGained", Round(plan.topEff) }, { "climbSpeedNeeded", Round(plan.climbSpeed) }, { "exitExtension", Round(plan.exitExtension) }, { "bends", plan.bends }, { "grades", plan.grades }, { "warnings", plan.warnings },
         { "points", points }, { "floor", floor }, { "data", data },
         { "note", "points are the drag points as VPX stores them (z is the offset from the linear height_bottom to height_top run); floor is [x, y, z, along] where the ball's bottom rides. apply creates the invisible collision Ramp; dress it with vpx_wireform (wire) or vpx_ramp sheet (plastic, metal)." } };
   }

   bool PlanFromArgs(Player& p, const json& args, Plan& plan, string& finish, string& error)
   {
      vector<Waypoint> wps;
      if (!ParseWaypoints(args.value("points", json::array()), wps, error))
         return false;
      const string exitKind = lowerCase(args.value("exit", "none"s));
      float extension = args.value("exitExtension", 0.f);
      if (exitKind == "spread" && extension <= 0.f)
         extension = WireformSpreadFall(38.f, 6.5f);
      if (extension > 0.f)
      {
         // run on level past the last waypoint: the wires open over this stretch and the ball drops through at its end
         const Waypoint& a = wps[wps.size() - 2];
         Waypoint& b = wps.back();
         Vertex2D d(b.xy.x - a.xy.x, b.xy.y - a.xy.y);
         d.Normalize();
         Waypoint e;
         e.xy = Vertex2D(b.xy.x + d.x * extension, b.xy.y + d.y * extension);
         e.z = b.z;
         if (!b.z)
            b.z = 0.f;
         wps.push_back(e);
         plan.exitExtension = extension;
      }
      finish = args.value("finish", "wire"s);
      float width = args.value("width", 0.f);
      if (width <= 0.f)
         width = finish == "wire" ? 60.f : 70.f;
      const float walls = args.value("walls", 62.f);
      const float minRadius = args.value("minRadius", 150.f);
      const float grade = args.value("grade", 4.f);
      if (finish != "wire" && finish != "plastic" && finish != "metal" && finish != "clear")
      {
         error = "finish is wire | plastic | metal | clear"s;
         return false;
      }
      const float ext = plan.exitExtension;
      plan = BuildPlan(p, wps, minRadius, width, walls, grade);
      plan.exitExtension = ext;
      return true;
   }


   // The plastic or metal sheet ramp drawn over a collision ramp: a floor strip with thickness, side walls with a fillet at the floor (vacuum
   // formed plastic) or a hem flange along the top (sheet metal), an entry flap at the low end. Sizes from the survey of 1,000 sheet ramps:
   // visible floor 74 (plastic), 69 (metal), 76 (clear) wide, walls 48 to 52 high, under a collision ramp 70 wide with walls 62
   struct SheetSpec
   {
      string finish = "plastic";
      float floorWidth = 74.f, wallHeight = 50.f, thickness = 4.f, fillet = 6.f, flange = 0.f;
      bool leftWall = true, rightWall = true;
      vector<std::array<float, 3>> wallSpans; // from, to, side (0 both, -1 left, 1 right); empty: full length on both sides
      bool flap = true;
      float flapLength = 60.f;
      float handover = 0.f; // floor run on past the high end, without walls, under the first wires of the wireform it hands the ball to
   };

   struct SheetFrame
   {
      Vertex3Ds pos;
      Vertex2D tangent, right;
      float along;
   };

   vector<SheetFrame> SheetFrames(const vector<Vertex3Ds>& pts)
   {
      vector<SheetFrame> out;
      float along = 0.f;
      for (size_t i = 0; i < pts.size(); i++)
      {
         if (i > 0)
            along += Vertex2D(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y).Length();
         const Vertex3Ds& a = pts[i > 0 ? i - 1 : 0];
         const Vertex3Ds& b = pts[i + 1 < pts.size() ? i + 1 : pts.size() - 1];
         Vertex2D t(b.x - a.x, b.y - a.y);
         if (t.LengthSquared() < 1e-9f)
            t = out.empty() ? Vertex2D(0.f, -1.f) : out.back().tangent;
         t.Normalize();
         out.push_back({ pts[i], t, Vertex2D(-t.y, t.x), along });
      }
      return out;
   }

   struct SheetMesh
   {
      Mesh mesh;
      float length = 1.f;

      Vertex3D_NoTex2 Vert(const SheetFrame& f, const Vertex2D& local, const Vertex2D& normal, float v)
      {
         Vertex3D_NoTex2 o;
         o.x = f.pos.x + f.right.x * local.x;
         o.y = f.pos.y + f.right.y * local.x;
         o.z = f.pos.z + local.y;
         o.nx = f.right.x * normal.x;
         o.ny = f.right.y * normal.x;
         o.nz = normal.y;
         o.tu = f.along / max(length, 1.f);
         o.tv = v;
         return o;
      }

      // One strip of the profile (a polyline in the cross-section, drawn with its outward normal on the left of its direction) extruded over
      // a run of consecutive frames, each profile segment with its own vertices so the edges stay sharp
      void Strip(const vector<SheetFrame>& frames, size_t from, size_t to, const vector<Vertex2D>& profile, float v0, float v1)
      {
         if (to <= from || profile.size() < 2)
            return;
         float total = 0.f;
         for (size_t k = 0; k + 1 < profile.size(); k++)
            total += Vertex2D(profile[k + 1].x - profile[k].x, profile[k + 1].y - profile[k].y).Length();
         float walked = 0.f;
         for (size_t k = 0; k + 1 < profile.size(); k++)
         {
            const Vertex2D a = profile[k], b = profile[k + 1];
            Vertex2D d(b.x - a.x, b.y - a.y);
            const float segLen = d.Length();
            if (segLen < 1e-6f)
               continue;
            d.Normalize();
            const Vertex2D n(-d.y, d.x);
            const float va = v0 + (v1 - v0) * walked / max(total, 1e-6f), vb = v0 + (v1 - v0) * (walked + segLen) / max(total, 1e-6f);
            walked += segLen;
            const unsigned int base = static_cast<unsigned int>(mesh.m_vertices.size());
            for (size_t i = from; i <= to; i++)
            {
               mesh.m_vertices.push_back(Vert(frames[i], a, n, va));
               mesh.m_vertices.push_back(Vert(frames[i], b, n, vb));
            }
            for (size_t i = from; i < to; i++)
            {
               const unsigned int p0 = base + static_cast<unsigned int>(i - from) * 2, p1 = p0 + 1, q0 = p0 + 2, q1 = p0 + 3;
               mesh.m_indices.insert(mesh.m_indices.end(), { p0, q0, p1, p1, q0, q1 });
            }
         }
      }

      // A flat cap closing a profile at one frame, facing out of the run (backward at its start, forward at its end)
      void Cap(const SheetFrame& f, const vector<Vertex2D>& profile, bool forward)
      {
         vector<RenderVertex> rgv;
         for (const Vertex2D& q : profile)
            if (rgv.empty() || Vertex2D(q.x - rgv.back().x, q.y - rgv.back().y).LengthSquared() > 1e-6f)
            {
               RenderVertex v;
               v.x = q.x;
               v.y = q.y;
               rgv.push_back(v);
            }
         if (rgv.size() > 1 && Vertex2D(rgv.front().x - rgv.back().x, rgv.front().y - rgv.back().y).LengthSquared() < 1e-6f)
            rgv.pop_back();
         if (rgv.size() < 3)
            return;
         vector<unsigned int> poly(rgv.size());
         for (unsigned int i = 0; i < poly.size(); i++)
            poly[i] = i;
         vector<unsigned int> tri;
         PolygonToTriangles(rgv, poly, tri, true);
         const Vertex3Ds want(f.tangent.x * (forward ? 1.f : -1.f), f.tangent.y * (forward ? 1.f : -1.f), 0.f);
         const unsigned int base = static_cast<unsigned int>(mesh.m_vertices.size());
         for (const RenderVertex& q : rgv)
            mesh.m_vertices.push_back(Vert(f, Vertex2D(q.x, q.y), Vertex2D(0.f, 0.f), 0.f));
         for (unsigned int i = base; i < mesh.m_vertices.size(); i++)
         {
            mesh.m_vertices[i].nx = want.x;
            mesh.m_vertices[i].ny = want.y;
            mesh.m_vertices[i].nz = 0.f;
         }
         for (size_t k = 0; k + 2 < tri.size(); k += 3)
         {
            const Vertex3D_NoTex2 &a = mesh.m_vertices[base + tri[k]], &b = mesh.m_vertices[base + tri[k + 1]], &c = mesh.m_vertices[base + tri[k + 2]];
            const Vertex3Ds ab(b.x - a.x, b.y - a.y, b.z - a.z), ac(c.x - a.x, c.y - a.y, c.z - a.z);
            const Vertex3Ds n(ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x);
            if (n.x * want.x + n.y * want.y + n.z * want.z >= 0.f)
               mesh.m_indices.insert(mesh.m_indices.end(), { base + tri[k], base + tri[k + 1], base + tri[k + 2] });
            else
               mesh.m_indices.insert(mesh.m_indices.end(), { base + tri[k], base + tri[k + 2], base + tri[k + 1] });
         }
      }

      void Quad(const Vertex3Ds& a, const Vertex3Ds& b, const Vertex3Ds& c, const Vertex3Ds& d, const Vertex3Ds& n)
      {
         const unsigned int base = static_cast<unsigned int>(mesh.m_vertices.size());
         for (const Vertex3Ds& q : { a, b, c, d })
         {
            Vertex3D_NoTex2 o;
            o.x = q.x;
            o.y = q.y;
            o.z = q.z;
            o.nx = n.x;
            o.ny = n.y;
            o.nz = n.z;
            o.tu = 0.f;
            o.tv = 0.f;
            mesh.m_vertices.push_back(o);
         }
         mesh.m_indices.insert(mesh.m_indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
      }
   };

   // Profiles in the cross-section: x across (right positive), y up from the floor the ball rolls on. Each is drawn so its outward normal
   // is on the left of its direction (counter clockwise around the solid)
   vector<Vertex2D> FloorProfile(const SheetSpec& sp)
   {
      const float w = sp.floorWidth * 0.5f;
      return { Vertex2D(-w, 0.f), Vertex2D(w, 0.f), Vertex2D(w, -sp.thickness), Vertex2D(-w, -sp.thickness), Vertex2D(-w, 0.f) };
   }

   vector<Vertex2D> WallProfile(const SheetSpec& sp, float side)
   {
      // the wall rises from the floor's edge: inside face with the fillet, top (with the flange on metal), outside face, back to the floor bottom
      vector<Vertex2D> pts;
      const float x = sp.floorWidth * 0.5f, t = sp.thickness, h = sp.wallHeight;
      const auto P = [&](float px, float py) { pts.emplace_back(px * side, py); };
      if (sp.fillet > 0.f)
      {
         const float r = min(sp.fillet, h * 0.5f);
         P(x - r, 0.f);
         for (int k = 1; k < 6; k++)
         {
            const float a = (float)(M_PI * 0.5) * static_cast<float>(k) / 6.f;
            P(x - r + sinf(a) * r, r - cosf(a) * r);
         }
         P(x, r);
      }
      else
         P(x, 0.f);
      P(x, h);
      if (sp.flange > 0.f)
      {
         P(x + t + sp.flange, h);
         P(x + t + sp.flange, h - t);
         P(x + t, h - t);
      }
      else
         P(x + t, h);
      P(x + t, -t);
      P(x, -t);
      if (side < 0.f)
         std::ranges::reverse(pts);
      return pts;
   }

   json BuildSheet(Player& p, const vector<Vertex3Ds>& route, const SheetSpec& sp, Mesh& mesh, string& error)
   {
      if (route.size() < 2)
      {
         error = "the route needs at least 2 points"s;
         return json();
      }
      vector<Vertex3Ds> pts = route;
      float handoverFrom = -1.f;
      if (sp.handover > 0.f && pts.size() >= 2)
      {
         const bool highEnd = pts.back().z >= pts.front().z;
         const Vertex3Ds& a = highEnd ? pts[pts.size() - 2] : pts[1];
         const Vertex3Ds& b = highEnd ? pts.back() : pts.front();
         Vertex2D d(b.x - a.x, b.y - a.y);
         d.Normalize();
         const Vertex3Ds e(b.x + d.x * sp.handover, b.y + d.y * sp.handover, b.z);
         if (highEnd)
            pts.push_back(e);
         else
            pts.insert(pts.begin(), e);
      }
      vector<SheetFrame> frames = SheetFrames(pts);
      if (sp.handover > 0.f)
         handoverFrom = pts.back().z >= pts.front().z ? frames.back().along - sp.handover : sp.handover;
      SheetMesh sm;
      sm.length = frames.back().along;
      sm.Strip(frames, 0, frames.size() - 1, FloorProfile(sp), 0.f, 1.f);
      sm.Cap(frames.front(), FloorProfile(sp), false);
      sm.Cap(frames.back(), FloorProfile(sp), true);
      const auto wallAt = [&](float along, float side)
      {
         if (handoverFrom >= 0.f && (pts.back().z >= pts.front().z ? along > handoverFrom + 0.01f : along < handoverFrom - 0.01f))
            return false;
         if (sp.wallSpans.empty())
            return side < 0.f ? sp.leftWall : sp.rightWall;
         for (const auto& span : sp.wallSpans)
            if (along >= span[0] - 0.01f && along <= span[1] + 0.01f && (span[2] == 0.f || span[2] == side))
               return true;
         return false;
      };
      json walls = json::array();
      for (const float side : { -1.f, 1.f })
      {
         const vector<Vertex2D> profile = WallProfile(sp, side);
         size_t runStart = SIZE_MAX;
         for (size_t i = 0; i <= frames.size(); i++)
         {
            const bool on = i < frames.size() && wallAt(frames[i].along, side);
            if (on && runStart == SIZE_MAX)
               runStart = i;
            if (!on && runStart != SIZE_MAX)
            {
               if (i - 1 > runStart)
               {
                  sm.Strip(frames, runStart, i - 1, profile, 0.f, 1.f);
                  sm.Cap(frames[runStart], profile, false);
                  sm.Cap(frames[i - 1], profile, true);
                  walls.push_back({ { "side", side < 0.f ? "left" : "right" }, { "from", Round(frames[runStart].along) }, { "to", Round(frames[i - 1].along) } });
               }
               runStart = SIZE_MAX;
            }
         }
      }
      json flap;
      if (sp.flap)
      {
         // the hinged steel flap at the low end, lying from the floor's end down onto the surface in front of it
         const bool lowStart = route.front().z <= route.back().z;
         const SheetFrame& f = lowStart ? frames.front() : frames.back();
         const Vertex2D out = lowStart ? Vertex2D(-f.tangent.x, -f.tangent.y) : f.tangent;
         const float w = sp.floorWidth * 0.5f - 1.f, t = 2.f;
         const Vertex3Ds base = f.pos;
         const float dropZ = min(base.z, 0.f);
         const Vertex3Ds tip(base.x + out.x * sp.flapLength, base.y + out.y * sp.flapLength, max(0.f, dropZ) + t);
         const Vertex3Ds rx(f.right.x * w, f.right.y * w, 0.f);
         const Vertex3Ds a = base - rx, b = base + rx, c = tip + rx, d = tip - rx;
         const Vertex3Ds down(0.f, 0.f, -t);
         const Vertex3Ds n = Vertex3Ds(0.f, 0.f, 1.f);
         sm.Quad(a, b, c, d, n);
         sm.Quad(a + down, d + down, c + down, b + down, Vertex3Ds(0.f, 0.f, -1.f));
         sm.Quad(a, d, d + down, a + down, Vertex3Ds(-f.right.x, -f.right.y, 0.f));
         sm.Quad(b, b + down, c + down, c, Vertex3Ds(f.right.x, f.right.y, 0.f));
         sm.Quad(d, c, c + down, d + down, Vertex3Ds(out.x, out.y, 0.f));
         flap = json { { "at", lowStart ? "start" : "end" }, { "length", Round(sp.flapLength) }, { "width", Round(w * 2.f) }, { "tip", { Round(tip.x), Round(tip.y), Round(tip.z) } } };
      }
      mesh = std::move(sm.mesh);
      if (mesh.m_vertices.empty())
      {
         error = "nothing was built"s;
         return json();
      }
      mesh.UpdateBounds();
      const Vertex3Ds &lo = mesh.m_minAABound, &hi = mesh.m_maxAABound;
      json result { { "finish", sp.finish }, { "length", Round(sm.length) }, { "floorWidth", Round(sp.floorWidth) }, { "wallHeight", Round(sp.wallHeight) }, { "thickness", Round(sp.thickness) },
         { "fillet", Round(sp.fillet) }, { "flange", Round(sp.flange) }, { "walls", walls }, { "vertices", mesh.NumVertices() }, { "triangles", mesh.NumIndices() / 3 },
         { "bounds", { { "min", { Round(lo.x), Round(lo.y), Round(lo.z) } }, { "max", { Round(hi.x), Round(hi.y), Round(hi.z) } } } } };
      if (!flap.is_null())
         result["flap"] = flap;
      if (sp.handover > 0.f)
         result["handover"] = Round(sp.handover);
      return result;
   }

   Material* SheetMaterial(Player& p, const string& finish, bool& created)
   {
      PinTable* table = p.m_ptable;
      const string name = finish == "metal" ? "Ramp Metal"s : finish == "clear" ? "Ramp Clear"s : "Ramp Plastic"s;
      created = false;
      if (Material* mat = table->GetMaterial(name); !table->IsDummyMaterial(mat))
         return mat;
      Material* mat = new Material();
      mat->m_name = name;
      if (finish == "metal")
      {
         mat->m_type = Material::MaterialType::METAL;
         mat->m_cBase = 0xC0C0C0;
         mat->m_fRoughness = 0.85f;
         mat->m_fGlossyImageLerp = 1.f;
      }
      else if (finish == "clear")
      {
         mat->m_cBase = 0xF4F4F4;
         mat->m_fRoughness = 0.9f;
         mat->m_fOpacity = 0.55f;
         mat->m_bOpacityActive = true;
         mat->m_fThickness = 0.08f;
      }
      else
      {
         mat->m_cBase = 0xE8E8E8;
         mat->m_fRoughness = 0.6f;
      }
      table->AddMaterial(mat);
      table->SetupLookUpTables(true);
      RecordAction(p, table, [table, mat]()
      {
         table->RemoveMaterial(mat);
         table->SetupLookUpTables(true);
      });
      created = true;
      return mat;
   }

   json LocalRampcheck(Player& p, Ramp* ramp)
   {
      vector<Ramp*> ramps;
      for (IEditable* part : p.m_ptable->GetParts())
         if (part->GetItemType() == eItemRamp)
            ramps.push_back(static_cast<Ramp*>(part));
      if (std::ranges::find(ramps, ramp) == ramps.end())
         ramps.push_back(ramp);
      return RampCheckJson(p, ramps, 25.f, 20.f, false);
   }

   struct TestState
   {
      RampPath rp;
      Layout lay;
      float halfWidth = 10.f;
      int ms = 3000;
      json rampcheck;
      bool rampcheckDone = false;
      vector<float> descentSpeeds;
      size_t descentIndex = 0;
      bool climbWanted = true;
      bool climbReady = false;
      bool climbSkipped = false;
      string climbNote;
      Vertex3Ds climbFrom;
      Vertex2D climbDir;
      string climbSurface;
      std::map<float, json> climbTried;
      vector<float> climbQueue;
      float bisectLo = 6.f, bisectHi = 60.f;
      int bisectRounds = 0;
      bool bisecting = false;
      std::optional<float> minClimbSpeed;
      ToolStep current;
      float currentSpeed = 0.f;
      bool currentDescent = true;
      json runs = json::array();
   };

   float DescentStart(const TestState& st)
   {
      const float inset = min(25.f, st.rp.length * 0.2f);
      return std::clamp(st.lay.topS + (st.lay.descentGoalS > st.lay.topS ? inset : -inset), 0.f, st.rp.length);
   }

   json RunJson(TestState& st, const json& r, bool descending, float speed)
   {
      const float startS = descending ? DescentStart(st) : st.lay.climbStartS;
      const float goalS = descending ? st.lay.descentGoalS : st.lay.topS;
      json run { { "test", descending ? "descend" : "climb" }, { "speed", Round(speed) } };
      run.update(Classify(st.rp, r, startS, goalS, st.halfWidth));
      run.update(ShotSummary(r));
      const string v = run.value("verdict", ""s);
      if (v != "through" && v != "missed")
         run["diagnosis"] = Diagnose(st.rp, run, descending, startS, goalS, st.rampcheckDone ? &st.rampcheck : nullptr);
      return run;
   }
}

void AddRampTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_ramp",
      "The collision ramp a ball rides, judged against what works in real tables (a survey of every ramp in 172 tables, ride tested in VPX). Actions: fit (name, or names, default every collidable ramp: "
      "grades the ramp against the rules: collision width 55-60 for wire or ~70 under a sheet, walls 20+, effective downhill grade 4 degrees+ with the table tilt, no level stretch, bend radius 150+, "
      "climb speed within a flipper shot, rampcheck findings; each rule carries the survey's share of descents that got through at that value) | test (name: in a playtest, rolls a ball down "
      "the ramp from its high end at speeds 3 and 15 and up it from the low end, bisecting for the slowest speed that reaches the top; every run gets a verdict: through, stalled, rolled_back, "
      "fell_off, left_side, missed, and a failed run a diagnosis of where and why) | sheet (ramp or points, name: the plastic, metal or clear sheet ramp drawn over a collision ramp as a "
      "non collidable Primitive: floor 74 / 69 / 76 wide with thickness, walls 50 high with a fillet (plastic) or a hem flange (metal), walls per side or per span, the entry flap at the low "
      "end; the material is created when the table has none). Details: vpx_status guide:\"ramp\".",
      Schema({ { "action", Prop("string", "plan | apply | fit | test | sheet") }, { "name", Prop("string", "Ramp name (apply: the ramp to create; test; fit: one ramp; sheet: the Primitive to hold the mesh)") },
                { "ramp", Prop("string", "sheet: the collision ramp whose centerline and heights are the route") },
                { "floorWidth", Prop("number", "sheet: visible floor width, default 74 plastic / 69 metal / 76 clear") }, { "wallHeight", Prop("number", "sheet: drawn wall height, default 50") },
                { "thickness", Prop("number", "sheet: default 4 plastic / 2 metal") },
                { "wallSides", Prop("string", "sheet: both (default) | left | right | none") },
                { "wallSpans", json { { "type", "array" }, { "items", json { { "type", "object" } } }, { "description", "sheet: [{from, to, side: both|left|right}] distances along the route where walls are drawn (default: the whole length on wallSides)" } } },
                { "flap", Prop("boolean", "sheet: the steel entry flap at the low end, default true") },
                { "handover", Prop("number", "sheet: run the floor on (no walls) this far past the high end, under the first wires of the wireform that takes the ball (30 is typical)") }, { "material", Prop("string", "sheet: material name (default: Ramp Plastic / Ramp Metal / Ramp Clear, created when missing)") },
                { "image", Prop("string", "sheet: image mapped along the sheet (u along the route, v across the profile)") },
                { "points", json { { "type", "array" }, { "items", json { { "type", "array" } } }, { "description", "plan, apply: waypoints [[x, y], [x, y, z], ...] from the entry to the exit; z is the floor height there (the ball's bottom), interpolated where missing, 0 at the entry when none is given" } } },
                { "finish", Prop("string", "plan, apply: wire (default, width 60) | plastic | metal | clear (width 70); sheet: plastic (default) | metal | clear") }, { "width", Prop("number", "plan, apply: collision width override") },
                { "walls", Prop("number", "plan, apply: collision wall height, default 62") }, { "minRadius", Prop("number", "plan, apply: bend radius at the corners, default 150 (less when the segments are too short)") },
                { "grade", Prop("number", "plan, apply: the least effective downhill grade in degrees a descending stretch may have, default 4") },
                { "exit", Prop("string", "plan, apply: spread runs the collision ramp on level past the last waypoint by the distance a spread wireform exit needs before the ball drops through (about 58; exitExtension overrides); none (default)") },
                { "exitExtension", Prop("number", "plan, apply: level run past the last waypoint, in units") },
                { "names", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "fit: ramps to grade (default: every ramp but single wire guides)" } } },
                { "rampcheck", Prop("boolean", "fit: include the rampcheck findings for the ramp (default true; slow on big tables)") },
                { "descend", Prop("boolean", "test: run the descents (default true)") }, { "climb", Prop("boolean", "test: run the climb (default true)") },
                { "speeds", json { { "type", "array" }, { "items", json { { "type", "number" } } }, { "description", "test: descent speeds, default [3, 15]" } } },
                { "ms", Prop("integer", "test: game time per run, default from the ramp length (2500 to 7000)") },
                { "background", Prop("boolean", "test: run as a vpx_job") } },
         { "action" }),
      [](const json& args) -> ToolStep
      {
         const string action = args.value("action", ""s);
         if (action == "plan" || action == "apply")
         {
            return WithPlayer(
               [args, action](Player& p)
               {
                  Plan plan;
                  string finish, error;
                  if (!PlanFromArgs(p, args, plan, finish, error))
                     return ErrorOutput(error);
                  json result = PlanJson(plan, finish);
                  if (action == "plan")
                     return TextOutput(result);
                  if (const string err = RequireEditor(p); !err.empty())
                     return ErrorOutput(err);
                  const string name = args.value("name", ""s);
                  if (name.empty())
                     return ErrorOutput("name is required: the collision Ramp to create"s);
                  if (FindPart(p, name))
                     return ErrorOutput("'"s + name + "' exists already: delete it or use another name");
                  IEditable* part = p.m_liveUI->m_editorUI.CreatePart(eItemRamp, Vertex2D(plan.drag.front().x, plan.drag.front().y));
                  if (!part)
                     return ErrorOutput("Could not create the ramp"s);
                  p.m_ptable->RenamePart(part, name);
                  Ramp* ramp = static_cast<Ramp*>(part);
                  p.m_liveUI->m_editorUI.BeginPartEdit(part);
                  ramp->m_d.m_type = RampTypeFlat;
                  ramp->m_d.m_heightbottom = plan.heightBottom;
                  ramp->m_d.m_heighttop = plan.heightTop;
                  ramp->m_d.m_widthbottom = plan.width;
                  ramp->m_d.m_widthtop = plan.width;
                  ramp->m_d.m_leftwallheight = plan.walls;
                  ramp->m_d.m_rightwallheight = plan.walls;
                  ramp->m_d.m_leftwallheightvisible = 0.f;
                  ramp->m_d.m_rightwallheightvisible = 0.f;
                  ramp->m_d.m_visible = false;
                  ramp->m_d.m_collidable = true;
                  json points = json::array();
                  for (const Vertex3Ds& d : plan.drag)
                     points.push_back({ { "x", d.x }, { "y", d.y }, { "z", d.z }, { "smooth", false } });
                  if (DragPointCurve* curve = GetCurve(part))
                     SetCurvePoints(curve, points);
                  RefreshPart(p, part);
                  p.m_liveUI->m_editorUI.EndPartEdit(part);
                  McpServer::Get().NoteEdit();
                  result["name"] = part->GetName();
                  result["created"] = true;
                  RampPath rp;
                  if (BuildRampPath(p, ramp, rp, error))
                  {
                     const json rc = LocalRampcheck(p, ramp);
                     result["fit"] = FitJson(p, rp, &rc);
                  }
                  return TextOutput(result);
               });
         }
         if (action == "sheet")
         {
            return WithPlayer(
               [args](Player& p)
               {
                  if (const string err = RequireEditor(p); !err.empty())
                     return ErrorOutput(err);
                  const string name = args.value("name", ""s);
                  if (name.empty())
                     return ErrorOutput("name is required: the Primitive that holds the sheet mesh (created when missing)"s);
                  SheetSpec sp;
                  sp.finish = lowerCase(args.value("finish", "plastic"s));
                  if (sp.finish != "plastic" && sp.finish != "metal" && sp.finish != "clear")
                     return ErrorOutput("finish is plastic | metal | clear"s);
                  sp.floorWidth = args.value("floorWidth", sp.finish == "metal" ? 69.f : sp.finish == "clear" ? 76.f : 74.f);
                  sp.wallHeight = args.value("wallHeight", 50.f);
                  sp.thickness = args.value("thickness", sp.finish == "metal" ? 2.f : 4.f);
                  sp.fillet = args.value("fillet", sp.finish == "metal" ? 0.f : 6.f);
                  sp.flange = args.value("flange", sp.finish == "metal" ? 6.f : 0.f);
                  sp.flap = args.value("flap", true);
                  sp.handover = max(0.f, args.value("handover", 0.f));
                  const string sides = lowerCase(args.value("wallSides", "both"s));
                  sp.leftWall = sides == "both" || sides == "left";
                  sp.rightWall = sides == "both" || sides == "right";
                  for (const json& span : args.value("wallSpans", json::array()))
                     if (span.is_object())
                     {
                        const string side = lowerCase(span.value("side", "both"s));
                        sp.wallSpans.push_back({ span.value("from", 0.f), span.value("to", 1e9f), side == "left" ? -1.f : side == "right" ? 1.f : 0.f });
                     }
                  vector<Vertex3Ds> route;
                  string error;
                  if (const json points = args.value("points", json::array()); points.is_array() && points.size() >= 2)
                  {
                     for (const json& pt : points)
                        if (pt.is_array() && pt.size() >= 3)
                           route.emplace_back(pt[0].get<float>(), pt[1].get<float>(), pt[2].get<float>());
                  }
                  else
                  {
                     const string rampName = args.value("ramp", ""s);
                     IEditable* rampPart = rampName.empty() ? nullptr : FindPart(p, rampName);
                     if (!rampPart || rampPart->GetItemType() != eItemRamp)
                        return ErrorOutput(rampName.empty() ? "ramp (the collision ramp the sheet is drawn over) or points [[x, y, z], ...] along the floor is required"s : "'"s + rampName + "' is not a ramp");
                     if (!RampCenterline(static_cast<Ramp*>(rampPart), route, error))
                        return ErrorOutput(error);
                  }
                  Mesh mesh;
                  json result = BuildSheet(p, route, sp, mesh, error);
                  if (!error.empty())
                     return ErrorOutput(error);
                  IEditable* part = FindPart(p, name);
                  bool created = false;
                  if (part && part->GetItemType() != eItemPrimitive)
                     return ErrorOutput("'"s + name + "' exists and is not a Primitive");
                  if (!part)
                  {
                     part = p.m_liveUI->m_editorUI.CreatePart(eItemPrimitive, Vertex2D(0.f, 0.f));
                     if (!part)
                        return ErrorOutput("Could not create the primitive"s);
                     p.m_ptable->RenamePart(part, name);
                     created = true;
                  }
                  Primitive* prim = static_cast<Primitive*>(part);
                  RecordEdit(p, part, true);
                  p.m_liveUI->m_editorUI.BeginPartEdit(part);
                  prim->m_mesh = mesh;
                  prim->m_mesh.UpdateBounds();
                  prim->m_d.m_use3DMesh = true;
                  prim->m_d.m_vPosition = Vertex3Ds(0.f, 0.f, 0.f);
                  prim->m_d.m_vSize = Vertex3Ds(1.f, 1.f, 1.f);
                  for (float& f : prim->m_d.m_aRotAndTra)
                     f = 0.f;
                  prim->m_d.m_meshFileName = "generated:sheet"s;
                  prim->m_d.m_collidable = false;
                  prim->m_d.m_toy = true;
                  prim->m_d.m_visible = true;
                  prim->m_d.m_staticRendering = true;
                  string material = args.value("material", ""s);
                  bool materialCreated = false;
                  if (material.empty())
                     material = SheetMaterial(p, sp.finish, materialCreated)->m_name;
                  if (p.m_ptable->IsDummyMaterial(p.m_ptable->GetMaterial(material)))
                     result["materialWarning"] = "no material named '"s + material + "' in the table, the primitive keeps its material";
                  else
                     prim->m_d.m_szMaterial = material;
                  if (const string image = args.value("image", ""s); !image.empty())
                     prim->m_d.m_szImage = image;
                  RefreshPart(p, part);
                  p.m_liveUI->m_editorUI.EndPartEdit(part);
                  McpServer::Get().NoteEdit();
                  result["name"] = part->GetName();
                  result["created"] = created;
                  result["material"] = prim->m_d.m_szMaterial;
                  result["materialCreated"] = materialCreated;
                  result["note"] = "non collidable toy primitive in table coordinates; the physics stays on the collision ramp (70 wide, walls 62 under a sheet: the drawn walls at 50 sit outside it). Rebuild after changing the ramp's points.";
                  return TextOutput(result);
               });
         }
         if (action == "fit")
         {
            return WithPlayer(
               [args](Player& p)
               {
                  vector<Ramp*> ramps;
                  vector<string> names;
                  if (args.contains("name") && args["name"].is_string())
                     names.push_back(args.value("name", ""s));
                  for (const json& n : args.value("names", json::array()))
                     if (n.is_string())
                        names.push_back(n.get<string>());
                  for (const string& n : names)
                  {
                     IEditable* part = FindPart(p, n);
                     if (!part || part->GetItemType() != eItemRamp)
                        return ErrorOutput("'"s + n + "' is not a ramp");
                     ramps.push_back(static_cast<Ramp*>(part));
                  }
                  if (names.empty())
                     for (IEditable* part : p.m_ptable->GetParts())
                        if (part->GetItemType() == eItemRamp && static_cast<Ramp*>(part)->m_d.m_type != RampType1Wire)
                           ramps.push_back(static_cast<Ramp*>(part));
                  json rampcheck;
                  if (args.value("rampcheck", true) && !ramps.empty())
                     rampcheck = RampCheckJson(p, ramps, 25.f, 20.f, false);
                  json list = json::array();
                  for (Ramp* ramp : ramps)
                  {
                     RampPath rp;
                     string error;
                     if (!BuildRampPath(p, ramp, rp, error))
                     {
                        list.push_back({ { "name", ramp->GetName() }, { "error", error } });
                        continue;
                     }
                     if (rp.length < 60.f)
                     {
                        list.push_back({ { "name", ramp->GetName() }, { "skipped", "shorter than 60" }, { "length", Round(rp.length) } });
                        continue;
                     }
                     list.push_back(FitJson(p, rp, rampcheck.is_null() ? nullptr : &rampcheck));
                  }
                  json result { { "ramps", list },
                     { "note", "Rules and the survey rates behind them: vpx_status guide:\"ramp\". surveyThroughPercent is the share of descents that got through on real ramps with that value; climb speed is what the low end must receive (a flipper gives 34 to 50, a plunge 40 to 60)." } };
                  return TextOutput(result);
               });
         }
         if (action != "test")
            return Immediate([action]() { return ErrorOutput("Unknown action '"s + action + "': fit | test"); });
         const string name = args.value("name", ""s);
         if (name.empty())
            return Immediate([]() { return ErrorOutput("name is required: the ramp to test"s); });
         auto st = std::make_shared<TestState>();
         st->climbWanted = args.value("climb", true);
         if (args.value("descend", true))
         {
            for (const json& s : args.value("speeds", json::array({ 3.f, 15.f })))
               if (s.is_number())
                  st->descentSpeeds.push_back(s.get<float>());
         }
         const int msArg = args.value("ms", 0);
         return [st, name, msArg]() -> std::optional<ToolOutput>
         {
            if (!g_pplayer)
               return ErrorOutput("No table session is running"s);
            Player& p = *g_pplayer;
            if (p.IsEditorMode())
               return ErrorOutput("The editor is open: use vpx_editor playtest to start a play session first"s);
            if (!st->rampcheckDone)
            {
               IEditable* part = FindPart(p, name);
               if (!part || part->GetItemType() != eItemRamp)
                  return ErrorOutput("'"s + name + "' is not a ramp");
               string error;
               if (!BuildRampPath(p, static_cast<Ramp*>(part), st->rp, error))
                  return ErrorOutput(error);
               if (st->rp.length < 60.f)
                  return ErrorOutput("the ramp is shorter than 60: nothing to ride"s);
               st->lay = LayoutOf(st->rp);
               st->halfWidth = max(10.f, *std::ranges::min_element(st->rp.half));
               st->ms = msArg > 0 ? std::clamp(msArg, 500, 60000) : static_cast<int>(std::clamp(st->rp.length * 4.f, 2500.f, 7000.f));
               st->rampcheck = LocalRampcheck(p, st->rp.ramp);
               st->rampcheckDone = true;
               // the climb starts on the surface the low end meets, 70 units out, when that surface is within a ball's reach of the floor
               const string lowEnd = st->rp.name + (st->lay.climbStartS < 1.f ? " start" : " end");
               json surf;
               for (const json& e : st->rampcheck.value("surfaceEnds", json::array()))
                  if (e.value("end", ""s) == lowEnd)
                     surf = e;
               if (!st->climbWanted)
                  st->climbSkipped = true;
               else if (st->lay.climbGain < 8.f)
               {
                  st->climbSkipped = true;
                  st->climbNote = "flat: nothing to climb";
               }
               else if (surf.is_null())
               {
                  st->climbSkipped = true;
                  st->climbNote = "the low end joins another ramp or meets no surface a ball can roll in from";
               }
               else if (std::abs(surf.value("step", 99.f)) > 12.f)
               {
                  st->climbSkipped = true;
                  st->climbNote = std::format("step of {:.1f} where the low end meets {}", surf.value("step", 0.f), surf.value("meets", ""s));
               }
               else
               {
                  Vertex2D t;
                  const Vertex3Ds low = PointAt(st->rp, st->lay.climbStartS, t);
                  const float upSign = st->lay.topS > st->lay.climbStartS ? 1.f : -1.f;
                  st->climbDir = Vertex2D(t.x * upSign, t.y * upSign);
                  st->climbFrom = Vertex3Ds(low.x - st->climbDir.x * 70.f, low.y - st->climbDir.y * 70.f, surf.value("surfaceHeight", 0.f));
                  st->climbSurface = surf.value("meets", ""s);
                  st->climbQueue.push_back(60.f);
                  st->climbReady = true;
               }
               return std::nullopt;
            }
            if (!st->current)
            {
               if (st->descentIndex < st->descentSpeeds.size())
               {
                  st->currentDescent = true;
                  st->currentSpeed = st->descentSpeeds[st->descentIndex];
                  Vertex2D t;
                  const Vertex3Ds at = PointAt(st->rp, DescentStart(*st), t);
                  const float dir = st->lay.descentGoalS > st->lay.topS ? 1.f : -1.f;
                  McpServer::Get().ReportProgress(st->descentIndex, st->descentSpeeds.size() + (st->climbReady ? 6 : 0), "runs"s);
                  st->current = BuildSimStep(json { { "action", "shot" }, { "x", at.x }, { "y", at.y }, { "z", at.z + 0.5f }, { "vx", t.x * dir * st->currentSpeed }, { "vy", t.y * dir * st->currentSpeed }, { "ms", st->ms }, { "sampleMs", 20 } });
               }
               else if (st->climbReady && !st->climbQueue.empty())
               {
                  st->currentDescent = false;
                  st->currentSpeed = st->climbQueue.back();
                  st->climbQueue.pop_back();
                  McpServer::Get().ReportProgress(st->descentSpeeds.size() + st->climbTried.size(), st->descentSpeeds.size() + 6, "runs"s);
                  st->current = BuildSimStep(json { { "action", "shot" }, { "x", st->climbFrom.x }, { "y", st->climbFrom.y }, { "z", st->climbFrom.z + 0.5f }, { "vx", st->climbDir.x * st->currentSpeed },
                     { "vy", st->climbDir.y * st->currentSpeed }, { "ms", st->ms }, { "sampleMs", 20 } });
               }
               else
               {
                  for (const auto& [speed, run] : st->climbTried)
                     st->runs.push_back(run);
                  json verdicts = json::object();
                  for (const json& r : st->runs)
                  {
                     const string key = r.value("test", ""s) + ' ' + r.value("verdict", ""s);
                     verdicts[key] = verdicts.value(key, 0) + 1;
                  }
                  json result { { "name", st->rp.name }, { "length", Round(st->rp.length) }, { "rise", Round(st->lay.rise) }, { "effectiveHeightGained", Round(st->lay.climbGain) }, { "effectiveDrop", Round(st->lay.effDrop) },
                     { "top", { { "along", Round(st->lay.topS) }, { "inside", st->lay.hump } } }, { "climbsFrom", st->lay.climbStartS < 1.f ? "start" : "end" }, { "descendsTo", st->lay.descentGoalS < 1.f ? "start" : "end" },
                     { "collisionWidth", Round(2.f * st->halfWidth) }, { "runs", st->runs }, { "verdicts", verdicts } };
                  if (st->climbSkipped)
                     result["climbSkipped"] = st->climbNote.empty() ? "not requested"s : st->climbNote;
                  else
                  {
                     result["climbFrom"] = json { { "x", Round(st->climbFrom.x) }, { "y", Round(st->climbFrom.y) }, { "z", Round(st->climbFrom.z) }, { "surface", st->climbSurface } };
                     result["minClimbSpeed"] = st->minClimbSpeed ? json(Round(*st->minClimbSpeed)) : json();
                     if (!st->minClimbSpeed)
                        result["climbNote"] = "the ball did not reach the top even at 60";
                  }
                  const json fit = FitJson(p, st->rp, &st->rampcheck);
                  result["fit"] = json { { "verdict", fit.value("verdict", ""s) }, { "rules", fit.value("rules", json::array()) }, { "climbSpeedNeeded", fit.value("climbSpeedNeeded", json()) } };
                  if (fit.contains("findings"))
                     result["fit"]["findings"] = fit["findings"];
                  return TextOutput(result);
               }
            }
            std::optional<ToolOutput> out = st->current();
            if (!out)
               return std::nullopt;
            st->current = nullptr;
            json run;
            if (out->isError)
               run = json { { "test", st->currentDescent ? "descend" : "climb" }, { "speed", Round(st->currentSpeed) }, { "verdict", "error" }, { "error", out->text } };
            else
            {
               const json r = json::parse(out->text, nullptr, false);
               run = r.is_discarded() ? json { { "test", st->currentDescent ? "descend" : "climb" }, { "speed", Round(st->currentSpeed) }, { "verdict", "error" }, { "error", "the run result is not JSON" } }
                                      : RunJson(*st, r, st->currentDescent, st->currentSpeed);
            }
            if (st->currentDescent)
            {
               st->runs.push_back(run);
               st->descentIndex++;
               return std::nullopt;
            }
            st->climbTried[st->currentSpeed] = run;
            const bool through = run.value("verdict", ""s) == "through";
            if (!st->bisecting)
            {
               if (through)
               {
                  st->bisecting = true;
                  st->bisectLo = 6.f;
                  st->bisectHi = 60.f;
                  st->minClimbSpeed = 60.f;
               }
            }
            else
            {
               if (through)
               {
                  st->bisectHi = st->currentSpeed;
                  st->minClimbSpeed = st->currentSpeed;
               }
               else
                  st->bisectLo = st->currentSpeed;
               st->bisectRounds++;
            }
            if (st->bisecting && st->bisectRounds < 5)
               st->climbQueue.push_back(std::round((st->bisectLo + st->bisectHi) * 5.f) / 10.f);
            return std::nullopt;
         };
      },
      600.0 });
}

}
