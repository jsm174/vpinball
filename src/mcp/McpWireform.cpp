// license:GPLv3+

// Wireform (habitrail) generator: the visible bent wire rail built around the route the ball rides (the collision ramp's centerline),
// shaped after the wireform meshes of real tables: rails in a cross-section around the route, stirrups, rings, rungs and cradles along
// it, struts and stands down to the playfield, hairpins, drops and elbows at the ends. The fixture vocabulary follows VPE's Wire Rail
// component; the numbers come from measuring Theatre of Magic, Tee'd Off, Cirqus Voltaire and Cactus Canyon.

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

namespace
{
   constexpr float DEFAULT_WIRE_DIAMETER = 6.3f; // measured on Theatre of Magic's wireforms
   constexpr float BOTTOM_RAIL_SPACING = 33.f; // the presets below carry the measured layouts; these are the williams values
   constexpr float MIDDLE_RAIL_SPACING = 67.f;
   constexpr float MIDDLE_RAIL_HEIGHT = 28.5f;
   constexpr float TOP_RAIL_SPACING = 30.f;
   constexpr float MAX_BEND_STEP = (float)(M_PI / 30.0); // 6 degrees per segment on a rounded bend at most: wire bends read as curves, not facets
   constexpr float UNITS_PER_INCH = 47.0588f;

   float Len(const Vertex3Ds& v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }
   Vertex3Ds Norm(const Vertex3Ds& v)
   {
      const float l = Len(v);
      return l > 1e-6f ? Vertex3Ds(v.x / l, v.y / l, v.z / l) : Vertex3Ds(0.f, 0.f, 0.f);
   }
   Vertex3Ds Cross(const Vertex3Ds& a, const Vertex3Ds& b) { return Vertex3Ds(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
   float Dot(const Vertex3Ds& a, const Vertex3Ds& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
   Vertex3Ds Rotate(const Vertex3Ds& v, const Vertex3Ds& axis, float angle)
   {
      const float c = cosf(angle), s = sinf(angle);
      return v * c + Cross(axis, v) * s + axis * (Dot(axis, v) * (1.f - c));
   }

   // Frame at a point of the route: tangent along the route, right across it (the right hand side seen in the direction of travel,
   // from above), up perpendicular to both (world up unless the route goes vertical, where the frame is transported along)
   struct Frame
   {
      Vertex3Ds pos, tangent, right, up;
      Vertex3Ds Local(float x, float z) const { return pos + right * x + up * z; }
   };

   struct Route
   {
      vector<Vertex3Ds> pts;
      vector<float> dist; // cumulative distance along the route
      vector<Frame> frames;
      float length = 0.f;

      void Build()
      {
         dist.assign(pts.size(), 0.f);
         for (size_t i = 1; i < pts.size(); i++)
            dist[i] = dist[i - 1] + Len(pts[i] - pts[i - 1]);
         length = dist.empty() ? 0.f : dist.back();
         frames.resize(pts.size());
         Vertex3Ds prevRight(1.f, 0.f, 0.f);
         for (size_t i = 0; i < pts.size(); i++)
         {
            const Vertex3Ds& a = pts[i > 0 ? i - 1 : 0];
            const Vertex3Ds& b = pts[i + 1 < pts.size() ? i + 1 : pts.size() - 1];
            Vertex3Ds t = Norm(b - a);
            if (Len(t) < 0.5f)
               t = i > 0 ? frames[i - 1].tangent : Vertex3Ds(0.f, -1.f, 0.f);
            Vertex3Ds up, right;
            if (std::abs(t.z) < 0.95f)
            {
               up = Norm(Vertex3Ds(0.f, 0.f, 1.f) - t * t.z);
               right = Norm(Cross(up, t));
            }
            else
            {
               right = Norm(prevRight - t * Dot(t, prevRight));
               if (Len(right) < 0.5f)
                  right = Vertex3Ds(1.f, 0.f, 0.f);
               up = Norm(Cross(t, right));
            }
            prevRight = right;
            frames[i] = Frame { pts[i], t, right, up };
         }
      }

      Frame At(float d) const
      {
         if (frames.empty())
            return Frame { Vertex3Ds(0.f, 0.f, 0.f), Vertex3Ds(0.f, -1.f, 0.f), Vertex3Ds(1.f, 0.f, 0.f), Vertex3Ds(0.f, 0.f, 1.f) };
         d = std::clamp(d, 0.f, length);
         size_t i = 0;
         while (i + 2 < dist.size() && dist[i + 1] < d)
            i++;
         if (i + 1 >= frames.size())
            return frames.back();
         const float span = dist[i + 1] - dist[i];
         const float t = span > 1e-6f ? (d - dist[i]) / span : 0.f;
         const Frame &a = frames[i], &b = frames[i + 1];
         Frame f;
         f.pos = a.pos + (b.pos - a.pos) * t;
         f.tangent = Norm(a.tangent + (b.tangent - a.tangent) * t);
         f.right = Norm(a.right + (b.right - a.right) * t);
         f.up = Norm(Cross(f.tangent, f.right));
         f.right = Norm(Cross(f.up, f.tangent));
         return f;
      }
   };

   struct TubeMesh
   {
      Mesh mesh;
      int sides = 10;

      // One continuous tube along a polyline, capped at both ends (a closed loop passes its first point again and gets no caps)
      void Add(const vector<Vertex3Ds>& path, float radius, bool closed = false)
      {
         vector<Vertex3Ds> pts;
         for (const Vertex3Ds& p : path)
            if (pts.empty() || Len(p - pts.back()) > 1e-3f)
               pts.push_back(p);
         if (pts.size() < 2)
            return;
         const unsigned int base = static_cast<unsigned int>(mesh.m_vertices.size());
         Vertex3Ds n0;
         float along = 0.f;
         for (size_t i = 0; i < pts.size(); i++)
         {
            const Vertex3Ds& a = pts[i > 0 ? i - 1 : (closed ? pts.size() - 2 : 0)];
            const Vertex3Ds& b = pts[i + 1 < pts.size() ? i + 1 : (closed ? 1 : pts.size() - 1)];
            const Vertex3Ds t = Norm(b - a);
            if (i == 0)
            {
               Vertex3Ds ref = std::abs(t.z) < 0.9f ? Vertex3Ds(0.f, 0.f, 1.f) : Vertex3Ds(1.f, 0.f, 0.f);
               n0 = Norm(Cross(t, ref));
            }
            else
            {
               n0 = Norm(n0 - t * Dot(t, n0));
               if (Len(n0) < 0.5f)
                  n0 = Norm(Cross(t, std::abs(t.z) < 0.9f ? Vertex3Ds(0.f, 0.f, 1.f) : Vertex3Ds(1.f, 0.f, 0.f)));
               along += Len(pts[i] - pts[i - 1]);
            }
            const Vertex3Ds n1 = Norm(Cross(t, n0));
            for (int j = 0; j <= sides; j++)
            {
               const float a0 = (float)(2.0 * M_PI) * static_cast<float>(j % sides) / static_cast<float>(sides);
               const Vertex3Ds n = n0 * cosf(a0) + n1 * sinf(a0);
               Vertex3D_NoTex2 v;
               v.x = pts[i].x + n.x * radius;
               v.y = pts[i].y + n.y * radius;
               v.z = pts[i].z + n.z * radius;
               v.nx = n.x;
               v.ny = n.y;
               v.nz = n.z;
               v.tu = static_cast<float>(j) / static_cast<float>(sides);
               v.tv = along / (radius * (float)(2.0 * M_PI));
               mesh.m_vertices.push_back(v);
            }
         }
         const unsigned int ring = static_cast<unsigned int>(sides + 1);
         for (size_t i = 0; i + 1 < pts.size(); i++)
            for (unsigned int j = 0; j < static_cast<unsigned int>(sides); j++)
            {
               const unsigned int a = base + static_cast<unsigned int>(i) * ring + j, b = a + 1, c = a + ring, d = c + 1;
               mesh.m_indices.insert(mesh.m_indices.end(), { a, c, b, b, c, d });
            }
         if (closed)
            return;
         for (const bool start : { true, false })
         {
            const size_t i = start ? 0 : pts.size() - 1;
            const Vertex3Ds t = Norm(start ? pts[0] - pts[1] : pts[i] - pts[i - 1]);
            Vertex3D_NoTex2 c;
            c.x = pts[i].x;
            c.y = pts[i].y;
            c.z = pts[i].z;
            c.nx = t.x;
            c.ny = t.y;
            c.nz = t.z;
            c.tu = 0.5f;
            c.tv = 0.5f;
            const unsigned int center = static_cast<unsigned int>(mesh.m_vertices.size());
            mesh.m_vertices.push_back(c);
            const unsigned int ringStart = base + static_cast<unsigned int>(i) * ring;
            for (unsigned int j = 0; j < static_cast<unsigned int>(sides); j++)
               if (start)
                  mesh.m_indices.insert(mesh.m_indices.end(), { center, ringStart + j, ringStart + j + 1 });
               else
                  mesh.m_indices.insert(mesh.m_indices.end(), { center, ringStart + j + 1, ringStart + j });
         }
      }
   };

   // Rounds every interior corner of a polyline with an arc of the given radius (shortened where the segments are too short for it)
   vector<Vertex3Ds> RoundCorners(const vector<Vertex3Ds>& path, float radius)
   {
      if (path.size() < 3 || radius <= 0.f)
         return path;
      vector<Vertex3Ds> out { path.front() };
      for (size_t i = 1; i + 1 < path.size(); i++)
      {
         const Vertex3Ds& p = path[i];
         const Vertex3Ds din = Norm(p - path[i - 1]), dout = Norm(path[i + 1] - p);
         const float cosA = std::clamp(Dot(din, dout), -1.f, 1.f);
         const float turn = acosf(cosA);
         if (turn < 0.01f || turn > (float)M_PI - 0.01f)
         {
            out.push_back(p);
            continue;
         }
         const float lenIn = Len(p - path[i - 1]) * (i == 1 ? 1.f : 0.5f), lenOut = Len(path[i + 1] - p) * (i + 2 == path.size() ? 1.f : 0.5f);
         float r = radius;
         const float tangentLength = r * tanf(turn * 0.5f);
         if (const float m = min(lenIn, lenOut); tangentLength > m)
            r = m / tanf(turn * 0.5f);
         const float tl = r * tanf(turn * 0.5f);
         const Vertex3Ds a = p - din * tl;
         const Vertex3Ds axis = Norm(Cross(din, dout));
         const Vertex3Ds inward = Norm(dout - din);
         const Vertex3Ds center = p + inward * (r / cosf(turn * 0.5f));
         const int steps = max(2, static_cast<int>(ceilf(turn / MAX_BEND_STEP)));
         for (int s = 0; s <= steps; s++)
            out.push_back(center + Rotate(a - center, axis, turn * static_cast<float>(s) / static_cast<float>(steps)));
      }
      out.push_back(path.back());
      return out;
   }

   struct Rail
   {
      Vertex2D offset; // x across (right positive), y up
      float trimStart = 0.f, trimEnd = 0.f;
      bool active = true;
   };

   // Cross-section presets, measured on the wireform meshes of real tables (~/tables survey, 2026-11-02):
   // williams: Theatre of Magic (Bally 1995): floor pair 33 apart, upper pair 67 apart and 28.5 up, open stirrup hoops, diagonal struts
   // gottlieb: Tee'd Off (Gottlieb 1993): four wires on a 50 x 54 box inside closed round rings
   // rail4 / rail3 / rail2: the medians of the 360 wireforms measured on their collision ramps in the ~/tables survey (2026-11-03): floor
   // pair 38 apart, guards 56 apart and 38 up, open top U hoops 65 wide every 85 to 95 hanging 12 below the floor wires, legs near vertical
   struct LayoutPreset
   {
      float bottom, middle, middleHeight;
      int wires;
      float hoopSpacing, hoopWidth, legSpacing;
      const char* exit; // the end fitting auto puts at the low end
   };
   LayoutPreset PresetOf(const string& name)
   {
      if (name == "gottlieb")
         return { 50.f, 50.f, 54.f, 4, 260.f, 0.f, 520.f, "none" };
      if (name == "williams")
         return { BOTTOM_RAIL_SPACING, MIDDLE_RAIL_SPACING, MIDDLE_RAIL_HEIGHT, 4, 300.f, 65.f, 450.f, "hairpin" };
      if (name == "rail2")
         return { 38.f, 56.f, 38.f, 2, 85.f, 65.f, 170.f, "auto" };
      if (name == "rail3")
         return { 38.f, 56.f, 38.f, 3, 95.f, 65.f, 190.f, "auto" };
      return { 38.f, 56.f, 38.f, 4, 95.f, 65.f, 190.f, "auto" };
   }
   bool IsRailStyle(const string& name) { return name != "williams" && name != "gottlieb"; }

   float SpreadGapAt(float s, float gap0, float gap1, float over)
   {
      if (s >= over)
         return gap1;
      return gap0 + (gap1 - gap0) * (0.5f - 0.5f * cosf(s / over * (float)M_PI));
   }

   vector<Vertex2D> DefaultOffsets(int rails, bool thirdRight, const LayoutPreset& preset)
   {
      vector<Vertex2D> o;
      if (rails <= 1)
         return { Vertex2D(0.f, 0.f) };
      o.emplace_back(-preset.bottom * 0.5f, 0.f);
      o.emplace_back(preset.bottom * 0.5f, 0.f);
      if (rails == 3)
      {
         o.emplace_back(thirdRight ? preset.middle * 0.5f : -preset.middle * 0.5f, preset.middleHeight);
         return o;
      }
      if (rails >= 4)
      {
         o.emplace_back(-preset.middle * 0.5f, preset.middleHeight);
         o.emplace_back(preset.middle * 0.5f, preset.middleHeight);
      }
      const int top = rails - 4;
      for (int i = 0; i < top; i++)
         o.emplace_back(top == 1 ? 0.f : -TOP_RAIL_SPACING * 0.5f + TOP_RAIL_SPACING * static_cast<float>(i) / static_cast<float>(top - 1), preset.middleHeight * 2.f);
      return o;
   }

   // Fitted ring: centered on the box around the active rails, its centerline just outside the outermost wire
   void FitRing(const vector<Rail>& rails, float wireRadius, Vertex2D& center, float& radius)
   {
      Vertex2D lo(FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX);
      for (const Rail& r : rails)
         if (r.active)
         {
            lo.x = min(lo.x, r.offset.x);
            lo.y = min(lo.y, r.offset.y);
            hi.x = max(hi.x, r.offset.x);
            hi.y = max(hi.y, r.offset.y);
         }
      center = Vertex2D((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
      radius = 0.f;
      for (const Rail& r : rails)
         if (r.active)
            radius = max(radius, Vertex2D(r.offset.x - center.x, r.offset.y - center.y).Length());
      radius += 2.f * wireRadius;
   }

   float NormAngle(float a)
   {
      while (a < 0.f)
         a += 360.f;
      while (a >= 360.f)
         a -= 360.f;
      return a;
   }

   // Ring outline in the cross-section (angles counter clockwise from the right: 0 right, 90 up, 180 left, 270 down), as one or two
   // open paths: a cutout removes an angular range, a straight range is replaced by its chord
   vector<vector<Vertex2D>> RingPaths(
      const Vertex2D& center, float radius, bool hasCutout, float cutStart, float cutEnd, bool hasStraight, float strStart, float strEnd, int density, bool& closed)
   {
      struct Piece
      {
         float from, to;
         bool chord;
      };
      vector<Piece> pieces;
      // Walk the circle from the cutout end (or 0) once around, splitting at the straight range
      const float startAngle = hasCutout ? NormAngle(cutEnd) : 0.f;
      const float sweep = hasCutout ? NormAngle(cutStart - cutEnd) : 360.f;
      closed = !hasCutout;
      if (hasStraight)
      {
         const float s0 = NormAngle(strStart - startAngle), s1 = NormAngle(strEnd - startAngle);
         if (s0 < sweep && s1 <= sweep && s0 < s1)
         {
            if (s0 > 0.f)
               pieces.push_back({ 0.f, s0, false });
            pieces.push_back({ s0, s1, true });
            if (s1 < sweep)
               pieces.push_back({ s1, sweep, false });
         }
         else
            pieces.push_back({ 0.f, sweep, false });
      }
      else
         pieces.push_back({ 0.f, sweep, false });
      vector<Vertex2D> path;
      const auto at = [&](float rel)
      {
         const float a = (startAngle + rel) * (float)(M_PI / 180.0);
         return Vertex2D(center.x + cosf(a) * radius, center.y + sinf(a) * radius);
      };
      for (const Piece& piece : pieces)
      {
         if (piece.chord)
         {
            path.push_back(at(piece.from));
            path.push_back(at(piece.to));
            continue;
         }
         const int steps = max(2, static_cast<int>(ceilf((piece.to - piece.from) / 360.f * static_cast<float>(density))));
         for (int s = 0; s <= steps; s++)
            path.push_back(at(piece.from + (piece.to - piece.from) * static_cast<float>(s) / static_cast<float>(steps)));
      }
      return { path };
   }

   vector<Vertex3Ds> ToWorld(const Frame& f, const vector<Vertex2D>& local)
   {
      vector<Vertex3Ds> out;
      for (const Vertex2D& p : local)
         out.push_back(f.Local(p.x, p.y));
      return out;
   }

   vector<float> ParseDistances(const json& v, float length)
   {
      vector<float> out;
      if (v.is_number())
      {
         const float spacing = v.get<float>();
         if (spacing > 1.f)
            for (float d = spacing * 0.5f; d < length - spacing * 0.25f; d += spacing)
               out.push_back(d);
      }
      else if (v.is_array())
         for (const json& e : v)
         {
            if (e.is_number())
               out.push_back(e.get<float>());
            else if (e.is_object() && e.contains("at"))
               out.push_back(e.value("at", 0.f));
         }
      return out;
   }

   json OptionAt(const json& v, size_t index)
   {
      if (v.is_array() && index < v.size() && v[index].is_object())
         return v[index];
      if (v.is_object())
         return v;
      return json::object();
   }
}

bool RampCenterline(Ramp* ramp, vector<Vertex3Ds>& center, string& error)
{
   int count = 0;
   float* height = nullptr;
   const Vertex2D* const outline = ramp->GetRampVertex(count, &height, nullptr, nullptr, nullptr, HIT_SHAPE_DETAIL_LEVEL, true);
   if (!outline || count < 2)
   {
      delete[] height;
      error = "the ramp has no usable centerline"s;
      return false;
   }
   for (int i = 0; i < count; i++)
   {
      const Vertex2D a = outline[i], b = outline[count * 2 - i - 1];
      center.emplace_back((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, height[i]);
   }
   delete[] outline;
   delete[] height;
   return true;
}

// Builds the wireform mesh in table coordinates. Returns the fixtures placed and the dimensions; the error says why nothing was built
float WireformSpreadFall(float floorSpacing, float wireDiameter, float spreadTo, float spreadOver)
{
   for (float s = 0.f; s <= spreadOver + 1.f; s += 1.f)
      if (SpreadGapAt(s, floorSpacing, spreadTo, spreadOver) >= 50.f + wireDiameter)
         return s;
   return spreadOver;
}

json BuildWireform(Player& p, const json& argsIn, Mesh& mesh, string& error)
{
   // auto (the default when no fixture is given): the supports, legs and exit of the chosen layout preset
   json args = argsIn;
   string layoutName = lowerCase(args.value("layout", "rail4"s));
   if (layoutName == "stern" || layoutName == "survey" || layoutName == "default")
      layoutName = "rail4";
   if (layoutName != "rail2" && layoutName != "rail3" && layoutName != "rail4" && layoutName != "williams" && layoutName != "gottlieb")
   {
      error = "layout is rail4 (default) | rail3 | rail2 | williams | gottlieb"s;
      return json();
   }
   const LayoutPreset preset = PresetOf(layoutName);
   if (args.contains("hoops") && !args.contains("stirrups"))
      args["stirrups"] = args["hoops"];
   const bool noFixtures = !args.contains("rings") && !args.contains("rungs") && !args.contains("cradles") && !args.contains("stands") && !args.contains("stirrups")
      && !args.contains("struts") && !args.contains("legs");
   const bool autoFixtures = args.value("auto", noFixtures);
   const bool autoEnds = autoFixtures && !args.contains("start") && !args.contains("end");
   Route route;
   const json points = args.value("points", json::array());
   if (!points.empty())
   {
      for (const json& pt : points)
         if (pt.is_array() && pt.size() >= 2)
            route.pts.emplace_back(pt[0].get<float>(), pt[1].get<float>(), pt.size() > 2 ? pt[2].get<float>() : 0.f);
      if (route.pts.size() < 2)
      {
         error = "points needs at least 2 [x, y, z] entries"s;
         return json();
      }
      // Smooth the hand drawn polyline the way the ramp editor does: a Catmull-Rom pass with ~10 unit steps keeps bends round
      vector<Vertex3Ds> smooth;
      const vector<Vertex3Ds>& pts = route.pts;
      for (size_t i = 0; i + 1 < pts.size(); i++)
      {
         const Vertex3Ds& p0 = pts[i > 0 ? i - 1 : i];
         const Vertex3Ds& p1 = pts[i];
         const Vertex3Ds& p2 = pts[i + 1];
         const Vertex3Ds& p3 = pts[i + 2 < pts.size() ? i + 2 : i + 1];
         const int steps = max(1, static_cast<int>(Len(p2 - p1) / 10.f));
         for (int s = 0; s < steps; s++)
         {
            const float t = static_cast<float>(s) / static_cast<float>(steps), t2 = t * t, t3 = t2 * t;
            smooth.push_back((p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) * 0.5f);
         }
      }
      smooth.push_back(pts.back());
      route.pts = smooth;
   }
   else
   {
      const string rampName = args.value("ramp", ""s);
      IEditable* part = rampName.empty() ? nullptr : FindPart(p, rampName);
      if (!part || part->GetItemType() != eItemRamp)
      {
         error = rampName.empty()
            ? "ramp (the collision ramp the ball rides, its centerline and heights are the route) or points ([[x, y, z], ...] along the floor the ball rolls on) is required"s
            : "No ramp named '"s + rampName + '\'';
         return json();
      }
      if (!RampCenterline(static_cast<Ramp*>(part), route.pts, error))
         return json();
      if (args.value("reverse", false))
         std::ranges::reverse(route.pts);
   }
   route.Build();
   if (route.length < 10.f)
   {
      error = "the route is shorter than 10 units"s;
      return json();
   }

   const float wireDiameter = std::clamp(args.value("wireDiameter", IsRailStyle(layoutName) ? 6.5f : DEFAULT_WIRE_DIAMETER), 1.f, 20.f);
   const float wireRadius = wireDiameter * 0.5f;
   const int railCount = std::clamp(args.value("wires", preset.wires), 1, 6);
   const bool thirdRight = lowerCase(args.value("thirdRailSide", "right"s)) != "left";
   // a resting 50 ball touches two 6.5 wires 38 apart about 4 above the collision floor: the floor wires sit that much above the route
   const float floorLift = args.value("floorLift", IsRailStyle(layoutName) ? 25.f - sqrtf(max(0.f, (25.f + wireRadius) * (25.f + wireRadius) - preset.bottom * preset.bottom * 0.25f)) : 0.f);
   vector<Rail> rails;
   for (const Vertex2D& o : DefaultOffsets(railCount, thirdRight, preset))
      rails.push_back(Rail { Vertex2D(o.x, o.y + floorLift) });
   // The exit, at the lower end of the route where the ball leaves. williams (Theatre of Magic): both pairs run on past the end and each
   // closes in its own long narrow U. The rail styles follow the survey's exits: spread (the floor pair opens from 38 to 68 over 100 of
   // wire so the ball falls through, then closes 180 past the end, level: the inlane feed) when the end is high enough to drop from,
   // else hairpin (a rounded tip 60 past the end); the guards close in their own U 60 past the end
   string exitChoice = lowerCase(args.value("exit", preset.exit));
   // the exit is where the ball leaves: the far end of a route that climbs then descends, else the lower end
   bool exitAtStart = route.pts.front().z < route.pts.back().z;
   {
      size_t top = 0;
      for (size_t i = 1; i < route.pts.size(); i++)
         if (route.pts[i].z > route.pts[top].z)
            top = i;
      if (route.dist[top] > 60.f && route.dist[top] < route.length - 60.f && route.pts[top].z > max(route.pts.front().z, route.pts.back().z) + 8.f)
         exitAtStart = false;
   }
   if (exitChoice == "auto")
      exitChoice = (exitAtStart ? route.pts.front().z : route.pts.back().z) >= 40.f ? "spread" : "hairpin";
   if (autoEnds && railCount >= 2 && exitChoice != "none")
   {
      json fits = json::array();
      if (layoutName == "williams")
      {
         fits.push_back(json { { "type", "hairpin" }, { "rails", { 1, 2 } }, { "loopDiameter", preset.bottom + wireDiameter * 4.f }, { "leadLength", 130.f }, { "tilt", 6.f } });
         if (railCount >= 4)
            fits.push_back(json { { "type", "hairpin" }, { "rails", { 3, 4 } }, { "loopDiameter", preset.middle + wireDiameter }, { "leadLength", 115.f }, { "tilt", 6.f } });
      }
      else
      {
         if (exitChoice == "spread")
            fits.push_back(json { { "type", "spread" }, { "rails", { 1, 2 } } });
         else
            fits.push_back(json { { "type", "hairpin" }, { "rails", { 1, 2 } }, { "loopDiameter", 50.f }, { "leadLength", 35.f } });
         if (railCount >= 4)
            fits.push_back(json { { "type", "hairpin" }, { "rails", { 3, 4 } }, { "loopDiameter", preset.middle + wireDiameter }, { "leadLength", 30.f } });
      }
      args[exitAtStart ? "start" : "end"] = fits;
      if (IsRailStyle(layoutName) && args.value("entry", "tab"s) == "tab")
      {
         args[exitAtStart ? "end" : "start"] = json::array({ json { { "type", "tab" } } });
         for (size_t i = 2; i < rails.size(); i++)
            (exitAtStart ? rails[i].trimEnd : rails[i].trimStart) += 15.f;
      }
   }
   if (const json offsets = args.value("offsets", json::array()); offsets.is_array())
      for (size_t i = 0; i < offsets.size() && i < rails.size(); i++)
         if (offsets[i].is_array() && offsets[i].size() == 2)
            rails[i].offset = Vertex2D(offsets[i][0].get<float>(), offsets[i][1].get<float>());
   if (const json trims = args.value("trimStart", json::array()); trims.is_array())
      for (size_t i = 0; i < trims.size() && i < rails.size(); i++)
         rails[i].trimStart = max(0.f, trims[i].get<float>());
   if (const json trims = args.value("trimEnd", json::array()); trims.is_array())
      for (size_t i = 0; i < trims.size() && i < rails.size(); i++)
         rails[i].trimEnd = max(0.f, trims[i].get<float>());

   TubeMesh tube;
   tube.sides = std::clamp(args.value("sides", 12), 6, 24);
   json placed = json::array();
   json warnings = json::array();

   // End fittings first: they decide where the attached rails stop
   struct EndFit
   {
      string type;
      int railA = 0, railB = 1;
      float loopDiameter = 60.f, lead = 40.f, tangentLength = -1.f, rotation = 0.f, tilt = 0.f, drop = 80.f, zAngle = 0.f;
      float bendRadius = 141.f, ringSpacing = 45.f, mouthScale = 1.3f;
      float spreadTo = 68.f, spreadOver = 100.f, apex = 180.f;
      json cutoffs;
   };
   const auto parseEnd = [&](const char* key, vector<EndFit>& fits)
   {
      json list = args.value(key, json());
      if (!list.is_array())
         list = json::array({ list });
      for (const json& e : list)
      {
         EndFit fit;
         if (e.is_string())
            fit.type = lowerCase(e.get<string>());
         else if (e.is_object())
         {
            fit.type = lowerCase(e.value("type", "hairpin"s));
            if (const json r = e.value("rails", json::array()); r.is_array() && r.size() == 2)
            {
               fit.railA = r[0].get<int>() - 1;
               fit.railB = r[1].get<int>() - 1;
            }
            fit.loopDiameter = e.value("loopDiameter", fit.loopDiameter);
            fit.lead = e.value("leadLength", fit.lead);
            fit.tangentLength = e.value("tangentLength", fit.tangentLength);
            fit.rotation = e.value("rotation", fit.rotation);
            fit.tilt = e.value("tilt", fit.tilt);
            fit.drop = e.value("dropLength", fit.drop);
            fit.zAngle = e.value("zAngle", fit.zAngle);
            fit.bendRadius = max(20.f, e.value("bendRadius", fit.bendRadius));
            fit.ringSpacing = max(10.f, e.value("rings", fit.ringSpacing));
            fit.mouthScale = e.value("mouthScale", fit.mouthScale);
            fit.spreadTo = std::clamp(e.value("spreadTo", fit.spreadTo), 30.f, 200.f);
            fit.spreadOver = std::clamp(e.value("spreadOver", fit.spreadOver), 20.f, 400.f);
            fit.apex = std::clamp(e.value("apex", fit.apex), 60.f, 600.f);
            fit.cutoffs = e.value("cutoffs", json::array());
         }
         else
            continue;
         if (fit.type.empty() || fit.type == "open" || fit.type == "none")
            continue;
         if (fit.type != "hairpin" && fit.type != "elbow" && fit.type != "drop" && fit.type != "spread" && fit.type != "tab")
         {
            warnings.push_back(string(key) + ": unknown fitting '" + fit.type + "' (spread | hairpin | tab | elbow | drop | open)");
            continue;
         }
         if (fit.type != "drop" && fit.type != "tab" && (fit.railA < 0 || fit.railB < 0 || fit.railA >= static_cast<int>(rails.size()) || fit.railB >= static_cast<int>(rails.size()) || fit.railA == fit.railB))
         {
            warnings.push_back(string(key) + ": rails must name two different rails of the layout (1 based)");
            continue;
         }
         if (fit.type == "elbow" && fit.cutoffs.is_array())
            for (size_t i = 0; i < fit.cutoffs.size() && i < rails.size(); i++)
               if (static_cast<int>(i) != fit.railA && static_cast<int>(i) != fit.railB && fit.cutoffs[i].is_number())
                  (string(key) == "start" ? rails[i].trimStart : rails[i].trimEnd) = max(0.f, fit.cutoffs[i].get<float>());
         fits.push_back(fit);
      }
   };
   vector<EndFit> startFits, endFits;
   parseEnd("start", startFits);
   parseEnd("end", endFits);
   for (const bool start : { true, false })
      for (const EndFit& fit : start ? startFits : endFits)
         if (fit.type == "spread")
         {
            const float spacing = rails.size() >= 2 ? std::abs(rails[fit.railB].offset.x - rails[fit.railA].offset.x) : preset.bottom;
            const float pull = args.value("spreadPullBack", WireformSpreadFall(spacing, wireDiameter, fit.spreadTo, fit.spreadOver));
            for (Rail& r : rails)
               (start ? r.trimStart : r.trimEnd) += pull;
            placed.push_back({ { "type", "pullBack" }, { "end", start ? "start" : "end" }, { "distance", Round(pull) },
               { "note", "the wires open this far before the collision ramp's end, where the ball falls through; vpx_ramp apply exit:\"spread\" extends the collision ramp by the same distance" } });
         }

   // Drop ending: the whole bundle bends down a quarter turn on a real bend radius (3 inches by default) and runs straight down, closed
   // rings around the drop and a larger ring at the mouth where the ball leaves (the end of a rail that drops into a lane or a scoop)
   float dropStartDist = FLT_MAX, dropEndDist = -1.f;
   for (const bool start : { false, true })
   {
      const vector<EndFit>& fits = start ? startFits : endFits;
      const auto it = std::ranges::find_if(fits, [](const EndFit& f) { return f.type == "drop"; });
      if (it == fits.end())
         continue;
      EndFit fit = *it;
      Frame f = route.frames[start ? 0 : route.frames.size() - 1];
      // The mouth must stay above the playfield: shorten the drop, then tighten the bend, and say so
      const float room = f.pos.z - wireDiameter;
      if (fit.bendRadius + fit.drop > room)
      {
         const float wantedRadius = fit.bendRadius, wantedDrop = fit.drop;
         fit.drop = max(0.f, room - fit.bendRadius);
         if (fit.bendRadius > room)
            fit.bendRadius = max(20.f, room);
         warnings.push_back(std::format("{} drop: the rail is only {:.0f} above the playfield there, bend radius {:.0f} and drop {:.0f} were reduced to {:.0f} and {:.0f}",
            start ? "start" : "end", f.pos.z, wantedRadius, wantedDrop, fit.bendRadius, fit.drop));
      }
      Vertex3Ds dir = start ? -f.tangent : f.tangent;
      dir.z = 0.f;
      dir = Norm(dir);
      if (Len(dir) < 0.5f)
         dir = f.tangent;
      const Vertex3Ds down(0.f, 0.f, -1.f);
      vector<Vertex3Ds> extension;
      const int steps = max(4, static_cast<int>(ceilf((float)(M_PI * 0.5) / MAX_BEND_STEP)));
      for (int s = 1; s <= steps; s++)
      {
         const float a = (float)(M_PI * 0.5) * static_cast<float>(s) / steps;
         extension.push_back(f.pos + dir * (fit.bendRadius * sinf(a)) + down * (fit.bendRadius * (1.f - cosf(a))));
      }
      const Vertex3Ds bendEnd = extension.back();
      const int dropSteps = max(1, static_cast<int>(fit.drop / 10.f));
      for (int s = 1; s <= dropSteps; s++)
         extension.push_back(bendEnd + down * (fit.drop * static_cast<float>(s) / dropSteps));
      const float extensionLength = fit.bendRadius * (float)(M_PI * 0.5) + fit.drop;
      if (start)
      {
         std::ranges::reverse(extension);
         route.pts.insert(route.pts.begin(), extension.begin(), extension.end());
         dropEndDist = extensionLength;
      }
      else
      {
         route.pts.insert(route.pts.end(), extension.begin(), extension.end());
         dropStartDist = route.length;
      }
      route.Build();
      placed.push_back({ { "type", "drop" }, { "end", start ? "start" : "end" }, { "bendRadius", fit.bendRadius }, { "dropLength", fit.drop },
         { "mouth", { Round(route.pts[start ? 0 : route.pts.size() - 1].x), Round(route.pts[start ? 0 : route.pts.size() - 1].y), Round(route.pts[start ? 0 : route.pts.size() - 1].z) } } });
   }
   const auto inDrop = [&](float d) { return d > dropStartDist - 20.f || d < dropEndDist + 20.f; };
   if (autoFixtures)
   {
      // Supports every ~300 units (6 to 8 inches on the real rails), never every few inches: williams uses open stirrups and diagonal
      // struts, gottlieb closed rings and short stands
      const float from = max(0.f, dropEndDist), to = min(route.length, dropStartDist);
      const float usable = to - from;
      if (layoutName == "gottlieb")
      {
         if (!args.contains("rings"))
            args["rings"] = json::array();
         if (!args.contains("stands"))
            args["stands"] = json::array();
         for (float d = from + 120.f; d < to - 60.f; d += 260.f)
            args["rings"].push_back(json { { "at", d }, { "style", "closed" } });
         for (float d = from + 180.f; d < to - 60.f; d += 520.f)
            args["stands"].push_back(json { { "at", d } });
      }
      else if (layoutName == "williams")
      {
         if (!args.contains("stirrups"))
         {
            args["stirrups"] = json::array();
            const int count = max(1, static_cast<int>(usable / 300.f));
            for (int i = 0; i < count; i++)
               args["stirrups"].push_back(json { { "at", from + usable * (static_cast<float>(i) + 0.5f) / static_cast<float>(count) } });
         }
         if (!args.contains("struts"))
         {
            args["struts"] = json::array();
            const int count = max(1, static_cast<int>(usable / 450.f));
            for (int i = 0; i < count; i++)
               args["struts"].push_back(json { { "at", from + usable * (static_cast<float>(i) + 0.5f) / static_cast<float>(count) + 60.f } });
         }
      }
      else
      {
         // U hoops every hoopSpacing (85 to 95 on the surveyed rails), a pair of vertical legs under every second hoop
         if (!args.contains("stirrups"))
         {
            args["stirrups"] = json::array();
            const int count = max(1, static_cast<int>(roundf(usable / preset.hoopSpacing)));
            for (int i = 0; i < count; i++)
               args["stirrups"].push_back(json { { "at", from + usable * (static_cast<float>(i) + 0.5f) / static_cast<float>(count) }, { "halfWidth", preset.hoopWidth * 0.5f } });
         }
         if (!args.contains("legs"))
         {
            args["legs"] = json::array();
            const int count = max(1, static_cast<int>(roundf(usable / preset.hoopSpacing)));
            for (int i = 0; i < count; i += 2)
               args["legs"].push_back(json { { "at", from + usable * (static_cast<float>(i) + 0.5f) / static_cast<float>(count) }, { "side", "both" }, { "halfWidth", preset.hoopWidth * 0.5f } });
         }
      }
   }
   // Rings around a drop: closed, every ringSpacing, and the larger mouth ring at the very end
   for (const bool start : { false, true })
   {
      const vector<EndFit>& fits = start ? startFits : endFits;
      const auto it = std::ranges::find_if(fits, [](const EndFit& f) { return f.type == "drop"; });
      if (it == fits.end())
         continue;
      json rings = args.value("rings", json::array());
      if (rings.is_number())
      {
         const float spacing = rings.get<float>();
         rings = json::array();
         for (float d = spacing * 0.5f; d < route.length - spacing * 0.25f; d += spacing)
            if (!inDrop(d))
               rings.push_back(json { { "at", d } });
      }
      const float from = start ? 0.f : dropStartDist, to = start ? dropEndDist : route.length;
      for (float d = (start ? to : from) + it->ringSpacing * 0.5f; start ? d > from + it->ringSpacing * 0.3f : d < to - it->ringSpacing * 0.3f;
         d += start ? -it->ringSpacing : it->ringSpacing)
         rings.push_back(json { { "at", d }, { "style", "closed" } });
      rings.push_back(json { { "at", start ? wireRadius + 2.f : route.length - wireRadius - 2.f }, { "style", "closed" }, { "scale", it->mouthScale } });
      args["rings"] = rings;
   }

   // Rails
   int railsBuilt = 0;
   for (size_t i = 0; i < rails.size(); i++)
   {
      const Rail& r = rails[i];
      if (!r.active)
         continue;
      const float from = r.trimStart, to = route.length - r.trimEnd;
      if (to - from < 5.f)
      {
         warnings.push_back("rail "s + std::to_string(i + 1) + " is trimmed away");
         continue;
      }
      vector<Vertex3Ds> path;
      path.push_back(route.At(from).Local(r.offset.x, r.offset.y));
      for (size_t k = 0; k < route.pts.size(); k++)
         if (route.dist[k] > from && route.dist[k] < to)
            path.push_back(route.frames[k].Local(r.offset.x, r.offset.y));
      path.push_back(route.At(to).Local(r.offset.x, r.offset.y));
      tube.Add(path, wireRadius);
      railsBuilt++;
   }

   // The surface under a point: the playfield or whatever collider the probe finds, for the feet of legs, stands and struts
   auto colliders = CollidersByPart(p);
   const string routeRampName = args.value("ramp", ""s);
   const auto surfaceUnder = [&](float x, float y, float fromZ, string& name)
   {
      name = "playfield"s;
      float best = 0.f;
      for (const auto& [part, partColliders] : colliders)
      {
         if (part == p.m_ptable || p.m_ptable->IsPlayerSessionObject(part) || (part->GetItemType() == eItemPrimitive && static_cast<Primitive*>(part)->IsPlayfield()))
            continue;
         if (!routeRampName.empty() && StrCompareNoCase(part->GetName(), routeRampName))
            continue;
         FRect3D box(FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX);
         for (const HitObject* ho : partColliders)
            box.Extend(ho->m_hitBBox);
         if (x < box.left - 2.f || x > box.right + 2.f || y < box.top - 2.f || y > box.bottom + 2.f || min(box.zlow, box.zhigh) > fromZ)
            continue;
         for (const HitObject* ho : partColliders)
         {
            if (ho->m_ObjType >= eTrigger)
               continue;
            const Vertex3Ds c = ClosestOnCollider(ho, Vertex3Ds(x, y, fromZ));
            if (Vertex2D(c.x - x, c.y - y).Length() <= 3.f && c.z < fromZ - 1.f && c.z > best)
            {
               best = c.z;
               name = part->GetName();
            }
         }
      }
      return best;
   };

   const auto endFrame = [&](bool start, float trim)
   {
      Frame f = route.At(start ? trim : route.length - trim);
      if (start)
      {
         f.tangent = -f.tangent;
         f.right = -f.right;
      }
      return f;
   };

   // Hairpin: the two rails join in a semicircle past the end, the leads blending into the loop with a cubic ease
   const auto buildHairpin = [&](bool start, const EndFit& fit)
   {
      const Rail &ra = rails[fit.railA], &rb = rails[fit.railB];
      const Frame f = endFrame(start, max(start ? ra.trimStart : ra.trimEnd, start ? rb.trimStart : rb.trimEnd));
      const Vertex3Ds a = f.Local(ra.offset.x, ra.offset.y), b = f.Local(rb.offset.x, rb.offset.y);
      // tilt bends the loop downward out of the route's plane (a drop-out loop dips toward the lane)
      const Vertex3Ds outward = Norm(Rotate(f.tangent, f.right, -fit.tilt * (float)(M_PI / 180.0)));
      Vertex3Ds pairAxis = Norm(b - a);
      if (Len(pairAxis) < 0.5f)
         pairAxis = f.right;
      pairAxis = Norm(Rotate(pairAxis, outward, fit.rotation * (float)(M_PI / 180.0)));
      const Vertex3Ds mid = (a + b) * 0.5f;
      const Vertex3Ds center = mid + outward * fit.lead;
      const float radius = fit.loopDiameter * 0.5f;
      Vertex3Ds arcA = center - pairAxis * radius, arcB = center + pairAxis * radius;
      if (Len(a - arcA) > Len(a - arcB))
      {
         pairAxis = -pairAxis;
         std::swap(arcA, arcB);
      }
      const auto lead = [&](const Vertex3Ds& from, const Vertex3Ds& to)
      {
         // Default handles of 45% of the lead: the rails swing out into the loop in one smooth S, short handles make a kink
         const float handle = min(fit.tangentLength < 0.f ? Len(to - from) * 0.45f : max(0.f, fit.tangentLength), Len(to - from) * 0.49f);
         const Vertex3Ds c1 = from + outward * handle, c2 = to - outward * handle;
         vector<Vertex3Ds> pts;
         const int steps = 14;
         for (int s = 0; s <= steps; s++)
         {
            const float t = static_cast<float>(s) / steps, u = 1.f - t;
            pts.push_back(from * (u * u * u) + c1 * (3.f * u * u * t) + c2 * (3.f * u * t * t) + to * (t * t * t));
         }
         return pts;
      };
      vector<Vertex3Ds> path = lead(a, arcA);
      const int arcSteps = 24;
      for (int s = 1; s <= arcSteps; s++)
      {
         const float ang = (float)M_PI * static_cast<float>(s) / arcSteps;
         path.push_back(center - pairAxis * (cosf(ang) * radius) + outward * (sinf(ang) * radius));
      }
      vector<Vertex3Ds> back = lead(b, arcB);
      for (size_t s = back.size() - 1; s-- > 0;)
         path.push_back(back[s]);
      tube.Add(path, wireRadius);
      placed.push_back({ { "type", "hairpin" }, { "end", start ? "start" : "end" }, { "rails", { fit.railA + 1, fit.railB + 1 } },
         { "loopCenter", { Round(center.x), Round(center.y), Round(center.z) } }, { "loopDiameter", fit.loopDiameter } });
   };

   // Tab: the floor wires hook down over the end of the sheet ramp they hand the ball to (a quarter bend of radius 10 and 12 straight down);
   // the guards stop a little short of the floor wires
   const auto buildTab = [&](bool start, const EndFit& fit)
   {
      for (size_t ri = 0; ri < rails.size(); ri++)
      {
         const Rail& r = rails[ri];
         if (!r.active)
            continue;
         const bool floorWire = ri < 2;
         if (!floorWire)
            continue;
         const Frame f = endFrame(start, start ? r.trimStart : r.trimEnd);
         const Vertex3Ds attach = f.Local(r.offset.x, r.offset.y);
         Vertex3Ds outward = Norm(Vertex3Ds(f.tangent.x, f.tangent.y, 0.f));
         if (Len(outward) < 0.5f)
            outward = f.tangent;
         const float bend = 10.f, drop = 12.f;
         vector<Vertex3Ds> path { attach, attach + outward * bend, attach + outward * bend + Vertex3Ds(0.f, 0.f, -drop) };
         tube.Add(RoundCorners(path, bend), wireRadius);
      }
      placed.push_back({ { "type", "tab" }, { "end", start ? "start" : "end" } });
   };

   // Elbow: the two rails bend straight down (bend radius = the wire diameter) and drop, for a rail feeding a hole or a subway
   const auto buildElbow = [&](bool start, const EndFit& fit)
   {
      for (const int ri : { fit.railA, fit.railB })
      {
         const Rail& r = rails[ri];
         const Frame f = endFrame(start, start ? r.trimStart : r.trimEnd);
         const Vertex3Ds attach = f.Local(r.offset.x, r.offset.y);
         Vertex3Ds outward = Norm(Rotate(f.tangent, Vertex3Ds(0.f, 0.f, 1.f), fit.zAngle * (float)(M_PI / 180.0)));
         const Vertex3Ds down(0.f, 0.f, -1.f);
         const float bend = min(max(0.5f, wireDiameter), fit.drop);
         vector<Vertex3Ds> path { attach, attach + outward * bend, attach + outward * bend + down * max(0.f, fit.drop) };
         tube.Add(RoundCorners(path, bend), wireRadius);
      }
      placed.push_back({ { "type", "elbow" }, { "end", start ? "start" : "end" }, { "rails", { fit.railA + 1, fit.railB + 1 } }, { "dropLength", fit.drop } });
   };
   // Spread: the floor pair runs on level past the end, opening from its spacing to spreadTo over spreadOver of wire (the ball falls
   // through once the gap passes 56), runs on at that width and closes in a half circle whose far point is apex past the end; one hoop
   // under the widest part. Measured on No Fear, Maverick, Big Bang Bar and Twilight Zone
   const auto buildSpread = [&](bool start, const EndFit& fit)
   {
      const Rail &ra = rails[fit.railA], &rb = rails[fit.railB];
      const Frame f = endFrame(start, max(start ? ra.trimStart : ra.trimEnd, start ? rb.trimStart : rb.trimEnd));
      Vertex3Ds outward = Norm(Vertex3Ds(f.tangent.x, f.tangent.y, 0.f));
      if (Len(outward) < 0.5f)
         outward = f.tangent;
      Vertex3Ds across = Norm(Cross(Vertex3Ds(0.f, 0.f, 1.f), outward));
      const Vertex3Ds a = f.Local(ra.offset.x, ra.offset.y), b = f.Local(rb.offset.x, rb.offset.y);
      if (Dot(b - a, across) < 0.f)
         across = -across;
      const Vertex3Ds mid = (a + b) * 0.5f;
      const float gap0 = Len(b - a), gap1 = max(fit.spreadTo, gap0 + 1.f);
      const float straightEnd = max(fit.spreadOver, fit.apex - gap1 * 0.5f);
      const auto gapAt = [&](float s) { return SpreadGapAt(s, gap0, gap1, fit.spreadOver); };
      vector<Vertex3Ds> path;
      for (float s = 0.f; s < straightEnd; s += 5.f)
         path.push_back(mid + outward * s - across * (gapAt(s) * 0.5f));
      const Vertex3Ds center = mid + outward * straightEnd;
      const float r = gap1 * 0.5f;
      const int arcSteps = 24;
      for (int k = 0; k <= arcSteps; k++)
      {
         const float ang = (float)M_PI * static_cast<float>(k) / arcSteps;
         path.push_back(center - across * (cosf(ang) * r) + outward * (sinf(ang) * r));
      }
      for (float s = straightEnd; s >= 0.f; s -= 5.f)
         path.push_back(mid + outward * s + across * (gapAt(s) * 0.5f));
      path.push_back(b);
      tube.Add(path, wireRadius);
      // the hoop under the widest part: half round bottom hanging 12 below the wires, arms up to 30 above them, just outside the wires
      {
         const float s = min(fit.spreadOver + 10.f, straightEnd);
         const Vertex3Ds c = mid + outward * s;
         const float half = gapAt(s) * 0.5f + wireDiameter;
         const float bottomZ = -wireDiameter - 6.f, topZ = 30.f;
         const float radius = min(half, (topZ - bottomZ) * 0.5f);
         const float centerZ = bottomZ + radius;
         const Vertex3Ds up(0.f, 0.f, 1.f);
         vector<Vertex3Ds> hoop { c - across * half + up * topZ, c - across * half + up * centerZ };
         for (int k = 0; k <= 24; k++)
         {
            const float ang = (float)M_PI + (float)M_PI * static_cast<float>(k) / 24.f;
            hoop.push_back(c + across * (cosf(ang) * half) + up * (centerZ + sinf(ang) * radius));
         }
         hoop.push_back(c + across * half + up * topZ);
         tube.Add(hoop, wireRadius);
         for (const float sign : { -1.f, 1.f })
         {
            const Vertex3Ds legTop = c + across * (sign * (half + wireDiameter)) + up * topZ;
            string surface;
            const float groundZ = surfaceUnder(legTop.x, legTop.y, legTop.z - 1.f, surface);
            if (legTop.z - (groundZ + wireRadius) < wireDiameter * 4.f)
               continue;
            const Vertex3Ds foot(legTop.x, legTop.y, groundZ + wireRadius);
            tube.Add(RoundCorners({ legTop, foot, foot + across * (sign * 22.f) }, wireDiameter * 1.5f), wireRadius);
         }
      }
      float fallAt = -1.f;
      for (float s = 0.f; s <= straightEnd; s += 1.f)
         if (gapAt(s) >= 50.f + wireDiameter)
         {
            fallAt = s;
            break;
         }
      placed.push_back({ { "type", "spread" }, { "end", start ? "start" : "end" }, { "rails", { fit.railA + 1, fit.railB + 1 } }, { "widensTo", Round(gap1) }, { "over", Round(fit.spreadOver) },
         { "apexPastEnd", Round(straightEnd + r) }, { "ballFallsAt", fallAt < 0.f ? json() : json(Round(fallAt)) }, { "apex", { Round(center.x + outward.x * r), Round(center.y + outward.y * r), Round(center.z) } } });
   };
   for (const bool start : { true, false })
      for (const EndFit& fit : start ? startFits : endFits)
      {
         if (fit.type == "hairpin")
            buildHairpin(start, fit);
         else if (fit.type == "spread")
            buildSpread(start, fit);
         else if (fit.type == "tab")
            buildTab(start, fit);
         else if (fit.type == "elbow")
            buildElbow(start, fit);
      }

   // Supports along the route
   // Rung: a bar from the inner surface of floor wire 1 to the inner surface of floor wire 2, at their height
   const auto rungAt = [&](const Frame& f, float lateral, float vertical, float lengthAdjust, float angle, Vertex3Ds& a, Vertex3Ds& b)
   {
      const Rail &r0 = rails[0], &r1 = rails[min<size_t>(1, rails.size() - 1)];
      Vertex2D c((r0.offset.x + r1.offset.x) * 0.5f + lateral, (r0.offset.y + r1.offset.y) * 0.5f + vertical);
      const float half = max(1.f, Vertex2D(r1.offset.x - r0.offset.x, r1.offset.y - r0.offset.y).Length() * 0.5f - wireRadius + lengthAdjust * 0.5f);
      const float ang = angle * (float)(M_PI / 180.0);
      const Vertex2D dir(cosf(ang), sinf(ang));
      a = f.Local(c.x - dir.x * half, c.y - dir.y * half);
      b = f.Local(c.x + dir.x * half, c.y + dir.y * half);
   };

   json rings = args.value("rings", json());
   for (const json& t : args.value("tunnels", json::array()))
   {
      if (!t.is_object())
         continue;
      if (!rings.is_array())
         rings = json::array();
      const float spacing = max(15.f, t.value("spacing", 45.f));
      for (float d = t.value("from", 0.f) + spacing * 0.5f; d < t.value("to", route.length); d += spacing)
         rings.push_back(json { { "at", d }, { "style", "closed" } });
   }
   for (size_t idx = 0; const float d : ParseDistances(rings, route.length))
   {
      const json o = OptionAt(rings, idx++);
      if (rings.is_number() && inDrop(d))
         continue;
      const Frame f = route.At(d);
      Vertex2D center;
      float radius;
      FitRing(rails, wireRadius, center, radius);
      radius *= o.value("scale", 1.f);
      center.x += o.value("offsetX", 0.f);
      center.y += o.value("offsetZ", 0.f);
      const string style = lowerCase(o.value("style", "open"s));
      const bool hasCutout = style == "open" || o.contains("cutout");
      const bool hasStraight = style == "open" || style == "flat" || o.contains("straight");
      float cutStart = 60.f, cutEnd = 120.f, strStart = 210.f, strEnd = 330.f;
      if (const json c = o.value("cutout", json::array()); c.is_array() && c.size() == 2)
         cutStart = c[0].get<float>(), cutEnd = c[1].get<float>();
      if (const json c = o.value("straight", json::array()); c.is_array() && c.size() == 2)
         strStart = c[0].get<float>(), strEnd = c[1].get<float>();
      bool closed = false;
      for (const vector<Vertex2D>& path : RingPaths(center, radius, hasCutout, cutStart, cutEnd, hasStraight, strStart, strEnd, o.value("density", 32), closed))
         tube.Add(ToWorld(f, path), wireRadius, closed);
      placed.push_back(
         { { "type", "ring" }, { "at", Round(d) }, { "style", hasCutout ? (hasStraight ? "open top, flat bottom" : "open top") : (hasStraight ? "closed, flat bottom" : "closed") },
            { "diameter", Round(radius * 2.f) }, { "pos", { Round(f.pos.x), Round(f.pos.y), Round(f.pos.z) } } });
   }

   const json rungs = args.value("rungs", json());
   for (size_t idx = 0; const float d : ParseDistances(rungs, route.length))
   {
      const json o = OptionAt(rungs, idx++);
      Vertex3Ds a, b;
      rungAt(route.At(d), o.value("offsetX", 0.f), o.value("offsetZ", 0.f), o.value("length", 0.f), o.value("angle", 0.f), a, b);
      tube.Add({ a, b }, wireRadius);
      placed.push_back({ { "type", "rung" }, { "at", Round(d) }, { "pos", { Round((a.x + b.x) * 0.5f), Round((a.y + b.y) * 0.5f), Round((a.z + b.z) * 0.5f) } } });
   }

   // Cradle: a U bracket whose flat bottom lies under the two floor wires, touching them, with an arm from each corner rising outward past
   // the raised rail on that side (53 degrees between the arms on the williams layout). An option; the measured rails use stirrups
   const json cradles = args.value("cradles", json());
   for (size_t idx = 0; const float d : ParseDistances(cradles, route.length))
   {
      const json o = OptionAt(cradles, idx++);
      if (inDrop(d))
         continue;
      const Frame f = route.At(d);
      const float cornerRadius = max(o.value("cornerRadius", wireDiameter * 1.25f), wireRadius);
      const Rail &r0 = rails[0], &r1 = rails[min<size_t>(1, rails.size() - 1)];
      const float bottomZ = min(r0.offset.y, r1.offset.y) - wireDiameter;
      float angle = o.value("angle", 0.f), bottom = o.value("bottomLength", 0.f), armDefault = o.value("armLength", 0.f);
      Vertex2D origin((r0.offset.x + r1.offset.x) * 0.5f, bottomZ);
      if (rails.size() >= 4 && angle <= 0.f)
      {
         // arms parallel to the line from each floor wire to the raised wire above it, one wire diameter outside both
         const Vertex2D bl = r0.offset, br = r1.offset, ul = rails[2].offset, ur = rails[3].offset;
         Vertex2D dl(ul.x - bl.x, ul.y - bl.y), dr(ur.x - br.x, ur.y - br.y);
         dl.Normalize();
         dr.Normalize();
         const Vertex2D nl(-dl.y, dl.x), nr(dr.y, -dr.x);
         const Vertex2D pl(bl.x + nl.x * wireDiameter, bl.y + nl.y * wireDiameter), pr(br.x + nr.x * wireDiameter, br.y + nr.y * wireDiameter);
         angle = acosf(std::clamp(dl.x * dr.x + dl.y * dr.y, -1.f, 1.f)) * (float)(180.0 / M_PI);
         if (std::abs(dl.y) > 1e-3f && std::abs(dr.y) > 1e-3f)
         {
            const float cornerL = pl.x + dl.x * (bottomZ - pl.y) / dl.y, cornerR = pr.x + dr.x * (bottomZ - pr.y) / dr.y;
            origin.x = (cornerL + cornerR) * 0.5f;
            if (bottom <= 0.f)
               bottom = cornerR - cornerL;
            if (armDefault <= 0.f)
               armDefault = max((ul.y + nl.y * wireDiameter - bottomZ) / dl.y, (ur.y + nr.y * wireDiameter - bottomZ) / dr.y) + wireDiameter;
         }
      }
      if (angle <= 0.f)
         angle = 53.130102f;
      const float half = angle * 0.5f * (float)(M_PI / 180.0);
      if (bottom <= 0.f)
         bottom = Vertex2D(r1.offset.x - r0.offset.x, r1.offset.y - r0.offset.y).Length() + wireDiameter * 1.2f;
      if (armDefault <= 0.f)
         armDefault = (preset.middleHeight + wireDiameter * 2.f) / cosf(half);
      const float leftArm = o.value("leftLength", armDefault), rightArm = o.value("rightLength", armDefault);
      origin.x += o.value("offsetX", 0.f);
      origin.y += o.value("offsetZ", 0.f);
      const Vertex2D dirL(-sinf(half), cosf(half)), dirR(sinf(half), cosf(half));
      vector<Vertex2D> raw;
      if (leftArm > 1e-3f)
         raw.emplace_back(-bottom * 0.5f + dirL.x * leftArm, dirL.y * leftArm);
      raw.emplace_back(-bottom * 0.5f, 0.f);
      raw.emplace_back(bottom * 0.5f, 0.f);
      if (rightArm > 1e-3f)
         raw.emplace_back(bottom * 0.5f + dirR.x * rightArm, dirR.y * rightArm);
      const float rot = o.value("rotation", 0.f) * (float)(M_PI / 180.0);
      vector<Vertex3Ds> path;
      for (const Vertex2D& q : raw)
         path.push_back(f.Local(origin.x + q.x * cosf(rot) - q.y * sinf(rot), origin.y + q.x * sinf(rot) + q.y * cosf(rot)));
      tube.Add(RoundCorners(path, cornerRadius), wireRadius);
      placed.push_back({ { "type", "cradle" }, { "at", Round(d) }, { "armAngle", Round(angle) }, { "bottomLength", Round(bottom) }, { "armLength", Round(armDefault) },
         { "pos", { Round(f.pos.x), Round(f.pos.y), Round(f.pos.z) } } });
   }

   // Stands: a rung under the bottom rails, a leg down from one end, and a U-hook foot flat on the surface below (the playfield, a
   // deck or whatever collider the probe finds under the foot), every bend rounded to the wire diameter
   const json stands = args.value("stands", json());
   for (size_t idx = 0; const float d : ParseDistances(stands, route.length))
   {
      const json o = OptionAt(stands, idx++);
      if (inDrop(d))
         continue;
      const Frame f = route.At(d);
      const bool rightSide = lowerCase(o.value("side", "right"s)) != "left";
      const float sign = rightSide ? 1.f : -1.f;
      Vertex3Ds a, b;
      rungAt(f, o.value("offsetX", 0.f), o.value("offsetZ", -wireDiameter), o.value("length", 0.f), 0.f, a, b);
      const Vertex3Ds legStart = rightSide ? b : a, other = rightSide ? a : b;
      const Vertex3Ds flatRight = Norm(Vertex3Ds(f.right.x, f.right.y, 0.f)), flatAlong = Norm(Vertex3Ds(f.tangent.x, f.tangent.y, 0.f));
      const float footWidth = o.value("footWidth", 30.f), footArm = o.value("footLength", 30.f), footConnected = o.value("footConnectionLength", 30.f);
      const float outX = o.value("footOut", 15.f), backY = o.value("footBack", -22.5f);
      const Vertex3Ds footAnchor2d = legStart + flatRight * (sign * outX) + flatAlong * backY;
      string surface;
      float groundZ = o.contains("groundZ") ? o.value("groundZ", 0.f) : surfaceUnder(footAnchor2d.x, footAnchor2d.y, legStart.z - 1.f, surface);
      const float footZ = groundZ + wireRadius;
      if (legStart.z - footZ < wireDiameter * 3.f)
      {
         warnings.push_back(std::format("stand at {:.0f}: the rail is only {:.0f} above {} there, no room for a leg", d, legStart.z - groundZ, surface));
         continue;
      }
      // Foot: a U in the surface plane, the leg joins the end of the connected arm, the U opens along the route
      const float r = max(0.05f, footWidth * 0.5f);
      const Vertex3Ds ux = flatRight * sign, uy = flatAlong;
      const Vertex3Ds base(footAnchor2d.x, footAnchor2d.y, footZ);
      const float arcY = -footArm * 0.5f + r * 0.5f, openY = footArm * 0.5f + r * 0.5f;
      vector<Vertex3Ds> foot { base + ux * (-r) + uy * (arcY + footConnected), base + ux * (-r) + uy * arcY };
      for (int s = 1; s <= 24; s++)
      {
         const float ang = (float)M_PI + (float)M_PI * static_cast<float>(s) / 24.f;
         foot.push_back(base + ux * (cosf(ang) * r) + uy * (arcY + sinf(ang) * r));
      }
      foot.push_back(base + ux * r + uy * openY);
      const float startLength = o.value("startLength", 40.f);
      vector<Vertex3Ds> path { other, legStart, legStart + Vertex3Ds(0.f, 0.f, -min(startLength, (legStart.z - footZ) * 0.5f)) };
      for (const Vertex3Ds& q : foot)
         path.push_back(q);
      tube.Add(RoundCorners(path, wireDiameter), wireRadius);
      placed.push_back({ { "type", "stand" }, { "at", Round(d) }, { "side", rightSide ? "right" : "left" }, { "foot", { Round(base.x), Round(base.y), Round(groundZ) } },
         { "on", surface.empty() ? "groundZ" : surface }, { "legHeight", Round(legStart.z - groundZ) } });
   }

   // Stirrups: the open hoop under a Williams rail (Theatre of Magic): a half round bottom hanging 12 below the floor wires and straight
   // arms rising just inside the upper rails up to their height, 65 wide and 41 tall on the measured rail, one every 6 to 8 inches
   const json stirrups = args.value("stirrups", json());
   for (size_t idx = 0; const float d : ParseDistances(stirrups, route.length))
   {
      const json o = OptionAt(stirrups, idx++);
      if (inDrop(d))
         continue;
      const Frame f = route.At(d);
      const Rail &r0 = rails[0], &r1 = rails[min<size_t>(1, rails.size() - 1)];
      const float floorZ = min(r0.offset.y, r1.offset.y), floorHalf = std::abs(r1.offset.x - r0.offset.x) * 0.5f;
      const bool hasUpper = rails.size() >= 4;
      const float upperHalf = hasUpper ? std::abs(rails[3].offset.x - rails[2].offset.x) * 0.5f : floorHalf + wireDiameter * 2.f;
      const float topZ = o.value("top", hasUpper ? max(rails[2].offset.y, rails[3].offset.y) : floorZ + 25.f);
      const float half = o.value("halfWidth", max(floorHalf + wireDiameter, upperHalf - wireDiameter)) + o.value("widen", 0.f);
      const float bottomZ = floorZ - wireDiameter - o.value("drop", 6.f);
      float radius = min(half, (topZ - bottomZ) * 0.5f);
      const float centerZ = bottomZ + radius;
      const Vertex2D center((r0.offset.x + r1.offset.x) * 0.5f + o.value("offsetX", 0.f), o.value("offsetZ", 0.f));
      vector<Vertex2D> local;
      local.emplace_back(center.x - half, center.y + topZ);
      local.emplace_back(center.x - half, center.y + centerZ);
      const int steps = 24;
      for (int s = 0; s <= steps; s++)
      {
         const float a = (float)M_PI + (float)M_PI * static_cast<float>(s) / steps;
         local.emplace_back(center.x + cosf(a) * half, center.y + centerZ + sinf(a) * radius);
      }
      local.emplace_back(center.x + half, center.y + topZ);
      tube.Add(ToWorld(f, local), wireRadius);
      placed.push_back(
         { { "type", "stirrup" }, { "at", Round(d) }, { "width", Round(half * 2.f) }, { "height", Round(topZ - bottomZ) }, { "pos", { Round(f.pos.x), Round(f.pos.y), Round(f.pos.z) } } });
   }

   // Legs: a vertical wire from a hoop's side (just outside the hoop at the floor wires' height) straight down to the surface under it,
   // ending in a short foot turned outward; the surveyed legs run within 13 degrees of vertical
   const json legs = args.value("legs", json());
   for (size_t idx = 0; const float d : ParseDistances(legs, route.length))
   {
      const json o = OptionAt(legs, idx++);
      if (inDrop(d))
         continue;
      const Frame f = route.At(d);
      const string sideName = lowerCase(o.value("side", "both"s));
      const Rail &r0 = rails[0], &r1 = rails[min<size_t>(1, rails.size() - 1)];
      const float floorZ = min(r0.offset.y, r1.offset.y);
      const float half = o.value("halfWidth", preset.hoopWidth > 0.f ? preset.hoopWidth * 0.5f : std::abs(r1.offset.x - r0.offset.x) * 0.5f + wireDiameter * 2.f) + wireDiameter;
      const bool hasUpper = rails.size() >= 4;
      const float topZ = o.value("top", hasUpper ? max(rails[2].offset.y, rails[3].offset.y) : floorZ + 25.f);
      const float footLength = o.value("footLength", 22.f);
      for (const bool rightSide : { true, false })
      {
         if ((rightSide && sideName == "left") || (!rightSide && sideName == "right"))
            continue;
         const float sign = rightSide ? 1.f : -1.f;
         const Vertex3Ds top = f.Local((r0.offset.x + r1.offset.x) * 0.5f + sign * half, topZ);
         const Vertex3Ds flatRight = Norm(Vertex3Ds(f.right.x, f.right.y, 0.f));
         string surface;
         const float groundZ = o.contains("groundZ") ? o.value("groundZ", 0.f) : surfaceUnder(top.x, top.y, top.z - 1.f, surface);
         const float footZ = groundZ + wireRadius;
         if (top.z - footZ < wireDiameter * 4.f)
         {
            warnings.push_back(std::format("leg at {:.0f} ({}): the rail is only {:.0f} above {} there, no room for a leg", d, rightSide ? "right" : "left", top.z - groundZ, surface));
            continue;
         }
         const Vertex3Ds foot(top.x, top.y, footZ);
         const Vertex3Ds flatAlong = Norm(Vertex3Ds(f.tangent.x, f.tangent.y, 0.f));
         const float fr = 7.f;
         vector<Vertex3Ds> path { top, foot, foot + flatRight * (sign * (footLength - fr)) };
         const Vertex3Ds arcCenter = foot + flatRight * (sign * (footLength - fr)) + flatAlong * fr;
         for (int k = 1; k <= 12; k++)
         {
            const float a = (float)M_PI * static_cast<float>(k) / 12.f;
            path.push_back(arcCenter + flatRight * (sign * sinf(a) * fr) - flatAlong * (cosf(a) * fr));
         }
         path.push_back(foot + flatAlong * (2.f * fr) + flatRight * (sign * 2.f));
         tube.Add(RoundCorners(path, wireDiameter * 1.5f), wireRadius);
         placed.push_back({ { "type", "leg" }, { "at", Round(d) }, { "side", rightSide ? "right" : "left" }, { "foot", { Round(foot.x), Round(foot.y), Round(groundZ) } }, { "on", surface.empty() ? "groundZ" : surface },
            { "height", Round(top.z - groundZ) } });
      }
   }

   // Struts: the long diagonal brace of a Williams rail: one straight wire from the outside of an upper rail down to a flat loop lying on
   // the surface, running back along the route about 2.6 times the height (20 degrees on Theatre of Magic), the loop 60 x 20 flat on the wood
   const json struts = args.value("struts", json());
   for (size_t idx = 0; const float d : ParseDistances(struts, route.length))
   {
      const json o = OptionAt(struts, idx++);
      if (inDrop(d))
         continue;
      const Frame f = route.At(d);
      const bool rightSide = lowerCase(o.value("side", "right"s)) != "left";
      const float sign = rightSide ? 1.f : -1.f;
      const Rail* rail = rails.size() >= 4 ? &rails[rightSide ? 3 : 2] : &rails[rightSide ? min<size_t>(1, rails.size() - 1) : 0];
      const Vertex3Ds attach = f.Local(rail->offset.x + sign * wireDiameter, rail->offset.y);
      const Vertex3Ds flatAlong = Norm(Vertex3Ds(f.tangent.x, f.tangent.y, 0.f)), flatRight = Norm(Vertex3Ds(f.right.x, f.right.y, 0.f));
      const Vertex3Ds back = flatAlong * (o.value("forward", false) ? 1.f : -1.f);
      string surface;
      float groundZ = o.contains("groundZ") ? o.value("groundZ", 0.f) : surfaceUnder(attach.x + back.x * attach.z * 2.6f, attach.y + back.y * attach.z * 2.6f, attach.z - 1.f, surface);
      const float height = attach.z - (groundZ + wireRadius);
      if (height < wireDiameter * 4.f)
      {
         warnings.push_back(std::format("strut at {:.0f}: the rail is only {:.0f} above {} there, no room for a strut", d, attach.z - groundZ, surface));
         continue;
      }
      const float run = o.value("run", height * o.value("runRatio", 2.6f));
      const Vertex3Ds foot(
         attach.x + back.x * run + flatRight.x * sign * o.value("footOut", 10.f), attach.y + back.y * run + flatRight.y * sign * o.value("footOut", 10.f), groundZ + wireRadius);
      // the flat loop: on along the strut direction, a half turn of radius footWidth/2 toward the rail, and back
      const float footLength = o.value("footLength", 60.f), footWidth = o.value("footWidth", 20.f);
      const Vertex3Ds lat = flatRight * (-sign);
      const float r = footWidth * 0.5f;
      vector<Vertex3Ds> path { attach, foot, foot + back * (footLength - r) };
      const Vertex3Ds arcCenter = foot + back * (footLength - r) + lat * r;
      for (int s = 1; s <= 16; s++)
      {
         const float a = (float)M_PI * static_cast<float>(s) / 16.f;
         path.push_back(arcCenter + back * (sinf(a) * r) - lat * (cosf(a) * r));
      }
      path.push_back(foot + lat * footWidth + back * 8.f);
      tube.Add(RoundCorners(path, wireDiameter * 2.f), wireRadius);
      placed.push_back({ { "type", "strut" }, { "at", Round(d) }, { "side", rightSide ? "right" : "left" }, { "foot", { Round(foot.x), Round(foot.y), Round(groundZ) } },
         { "on", surface.empty() ? "groundZ" : surface }, { "height", Round(height) }, { "run", Round(run) } });
   }

   mesh = std::move(tube.mesh);
   if (mesh.m_vertices.empty())
   {
      error = "nothing was built: no active rail"s;
      return json();
   }
   mesh.UpdateBounds();
   const Vertex3Ds& lo = mesh.m_minAABound;
   const Vertex3Ds& hi = mesh.m_maxAABound;
   json result { { "routeLength", Round(route.length) }, { "routeLengthInches", Round(route.length / UNITS_PER_INCH, 2) }, { "rails", railsBuilt }, { "wireDiameter", wireDiameter },
      { "wireDiameterInches", Round(wireDiameter / UNITS_PER_INCH, 3) }, { "routeStart", { Round(route.pts.front().x), Round(route.pts.front().y), Round(route.pts.front().z) } },
      { "routeEnd", { Round(route.pts.back().x), Round(route.pts.back().y), Round(route.pts.back().z) } },
      { "bounds", { { "min", { Round(lo.x), Round(lo.y), Round(lo.z) } }, { "max", { Round(hi.x), Round(hi.y), Round(hi.z) } } } }, { "fixtures", placed },
      { "vertices", mesh.NumVertices() }, { "triangles", mesh.NumIndices() / 3 } };
   json layout = json::array();
   for (const Rail& r : rails)
      layout.push_back({ Round(r.offset.x), Round(r.offset.y) });
   result["layout"] = layout;
   if (!warnings.empty())
      result["warnings"] = warnings;
   result["layoutPreset"] = layoutName;
   if (autoFixtures && IsRailStyle(layoutName))
      result["autoFixtures"] = std::format("no fixtures were given: U hoops every ~{:.0f} ({:.0f} wide, hanging 12 below the floor wires), a pair of vertical legs under every second hoop, "
         "and the {} exit at the low end{}; pass hoops, legs, rings, rungs, cradles, stirrups, struts, stands, start or end to lay them out yourself (auto:false for bare rails)",
         preset.hoopSpacing, preset.hoopWidth, exitChoice, railCount >= 4 ? " with the guard pair closed in its own U"s : ""s);
   else if (autoFixtures)
      result["autoFixtures"] = layoutName == "gottlieb"
         ? "no fixtures were given: closed rings every 260 and stands every 520 (Tee'd Off style); pass rings, rungs, cradles, stirrups, struts, stands, start or end to lay them out "
           "yourself (auto:false for bare rails)"
         : "no fixtures were given: stirrup hoops every ~300, diagonal struts every ~450, the floor pair ending in a long narrow U at the low end with the upper rails stopped 90 short "
           "(Theatre of Magic style); pass rings, rungs, cradles, stirrups, struts, stands, start or end to lay them out yourself (auto:false for bare rails)";
   return result;
}

void AddWireformTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_wireform",
      "Build the visible wireform (habitrail) for a ramp: bent wire rails around the route the ball rides, with the metalwork a real one has. The route is the collision ramp's centerline "
      "at the ball's floor height (ramp), or points. Rails sit in a cross-section around it (wires 1..6: two bottom rails 30 apart the ball rolls on, raised pair 30 out and 30 up, top "
      "wires 60 up; offsets overrides them). Supports at distances along the route, or every N units when given a number: rings (fitted around the wires; default open at the top with a "
      "flat bottom chord, style closed/flat/open, cutout and straight angle ranges, scale), rungs (under the bottom rails), cradles (the U bracket under the bundle whose arms hug the rails "
      "from below), stands (leg down from a bottom rail to the surface found under the foot, ending in a U-hook foot; side, startLength, footWidth, footLength, groundZ). End fittings "
      "start/end: hairpin (the loop past the end where the ball drops out: loopDiameter, leadLength, rotation 90 stands it up, rails [a, b]) or elbow (two rails bending straight down: "
      "dropLength, zAngle, cutoffs per other rail). trimStart/trimEnd per rail shorten rails. The mesh goes into the named Primitive (created if missing, non collidable, material), in "
      "table coordinates at position 0,0,0; name_only reports without building. Details: vpx_status guide:\"wireform\".",
      Schema({ { "name", Prop("string", "Primitive to hold the mesh (created when missing)") },
                { "ramp", Prop("string", "Collision ramp whose centerline and heights are the route (the invisible flat ramp the ball rides)") },
                { "reverse", Prop("boolean", "ramp: run the route from the ramp's last point to its first") },
                { "points",
                   json { { "type", "array" }, { "items", json { { "type", "array" } } },
                      { "description", "Route instead of ramp: [[x, y, z], ...] along the floor the ball rolls on (smoothed)" } } },
                { "wires", Prop("integer", "Rail count 1..6, default from the layout (rail4: 4, rail2: 2)") },
                { "layout",
                   Prop("string",
                      "Style, from the survey of 360 wireforms in real tables: rail4 (default: floor pair 38 apart, guards 56 apart and 38 up, U hoops every 95, vertical legs, spread or hairpin "
                      "exit) | rail3 (one guard) | rail2 (floor pair and hoops only, hoops every 85) | williams (Theatre of Magic: 33 / 67 / 28.5, stirrups every 300, diagonal struts, long "
                      "hairpins) | gottlieb (Tee'd Off: 50 x 54 box in closed rings)") },
                { "exit", Prop("string", "What auto puts at the low end: auto (default: spread when that end is 40 or more above the playfield, else hairpin) | spread | hairpin | none") },
                { "entry", Prop("string", "What auto puts at the other end on the rail styles: tab (default: the floor wires hook down over the ramp that hands the ball over, guards stopped 15 short) | none") },
                { "spreadPullBack", Prop("number", "spread: how far before the collision end the wires start opening (default: where the gap passes the ball, about 58); vpx_ramp apply exit:\"spread\" extends the collision ramp by the same amount") },
                { "tunnels", json { { "type", "array" }, { "items", json { { "type", "object" } } }, { "description", "[{from, to, spacing}]: closed rings every spacing (45) along a span, for a tunnel or a drop" } } },
                { "floorLift", Prop("number", "Height of the floor wires above the route (the collision floor); default: where a resting ball touches the pair (about 4), 0 for williams and gottlieb") },
                { "hoops", json { { "type", json::array({ "number", "array" }) }, { "description", "Spacing, or [{at, halfWidth, drop, top}]: the open top U hoops (same as stirrups)" } } },
                { "legs",
                   json { { "type", json::array({ "number", "array" }) },
                      { "description", "Spacing, or [{at, side: both|right|left, halfWidth, footLength, groundZ}]: vertical legs from the hoop's side down to the surface under it, with a short foot" } } },
                { "thirdRailSide", Prop("string", "3 wires: right (default) | left") },
                { "stirrups",
                   json { { "type", json::array({ "number", "array" }) },
                      { "description",
                         "Spacing, or [{at, halfWidth, drop, top, offsetX, offsetZ}]: the open hoop of a Williams rail, half round bottom 12 below the floor wires, arms just inside the "
                         "upper rails up to their height" } } },
                { "struts",
                   json { { "type", json::array({ "number", "array" }) },
                      { "description",
                         "Spacing, or [{at, side: right|left, runRatio (run per unit of height, default 2.6), run, forward, footLength, footWidth, footOut, groundZ}]: a straight diagonal "
                         "brace from the upper rail down to a flat loop on the surface" } } },
                { "offsets", json { { "type", "array" }, { "description", "Per rail [across, up] offsets from the route, overriding the defaults" } } },
                { "wireDiameter", Prop("number", "Default 6.5 (survey median; 6.3 for williams)") }, { "sides", Prop("integer", "Tube polygon sides 6..24, default 12") },
                { "material", Prop("string", "Material name for the primitive (default: Metal when the table has it)") },
                { "rings",
                   json { { "type", json::array({ "number", "array" }) },
                      { "description", "Spacing, or [{at, style: open|flat|closed, scale, cutout: [a, b], straight: [a, b], offsetX, offsetZ, density}]" } } },
                { "rungs", json { { "type", json::array({ "number", "array" }) }, { "description", "Spacing, or [{at, angle, length, offsetX, offsetZ}]" } } },
                { "cradles",
                   json { { "type", json::array({ "number", "array" }) },
                      { "description",
                         "Spacing, or [{at, bottomLength, armLength, leftLength, rightLength, angle, rotation, cornerRadius, offsetX, offsetZ}]: a U bracket whose flat bottom touches under "
                         "the floor wires, arms rising past the raised wires" } } },
                { "stands",
                   json { { "type", json::array({ "number", "array" }) },
                      { "description", "Spacing, or [{at, side: right|left, startLength, footWidth, footLength, footConnectionLength, footOut, footBack, groundZ}]" } } },
                { "start",
                   json { { "type", json::array({ "string", "object" }) },
                      { "description",
                         "End fitting at the route start: spread | hairpin | elbow | drop, or {type, rails: [a, b], spreadTo (68), spreadOver (100), apex (180), loopDiameter, leadLength, tangentLength, rotation, dropLength, zAngle, cutoffs}" } } },
                { "end", json { { "type", json::array({ "string", "object" }) }, { "description", "End fitting at the route end, same as start" } } },
                { "trimStart", json { { "type", "array" }, { "description", "Per rail: shorten the rail by this much at the start" } } },
                { "trimEnd", json { { "type", "array" }, { "description", "Per rail: shorten at the end" } } },
                { "preview", Prop("boolean", "Report the fixtures and dimensions without writing the mesh") } },
         { "name" }),
      [](const json& args)
      {
         return WithPlayer(
            [args](Player& p)
            {
               if (const string err = RequireEditor(p); !err.empty())
                  return ErrorOutput(err);
               const string name = args.value("name", ""s);
               if (name.empty() && !args.value("preview", false))
                  return ErrorOutput("name is required: the Primitive that holds the wireform mesh (created when missing)"s);
               Mesh mesh;
               string error;
               json result = BuildWireform(p, args, mesh, error);
               if (!error.empty())
                  return ErrorOutput(error);
               if (args.value("preview", false))
               {
                  result["preview"] = true;
                  return TextOutput(result);
               }
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
               prim->m_d.m_meshFileName = "generated:wireform"s;
               prim->m_d.m_collidable = false;
               prim->m_d.m_toy = true;
               prim->m_d.m_visible = true;
               prim->m_d.m_staticRendering = true;
               string material = args.value("material", ""s);
               if (material.empty() && created)
               {
                  // a metal type material with a light base, else our own: a material merely named Metal can be anything
                  for (const char* candidate : { "Ramp Wire", "Chrome", "chrome", "Metal", "metal", "Wire", "wire" })
                  {
                     const Material* mat = p.m_ptable->GetMaterial(candidate);
                     if (material.empty() && !p.m_ptable->IsDummyMaterial(mat) && mat->m_type == Material::MaterialType::METAL && (mat->m_cBase & 0xFF) + ((mat->m_cBase >> 8) & 0xFF) + ((mat->m_cBase >> 16) & 0xFF) > 300)
                        material = candidate;
                  }
                  if (material.empty())
                  {
                     Material* mat = new Material();
                     mat->m_name = "Ramp Wire"s;
                     mat->m_type = Material::MaterialType::METAL;
                     mat->m_cBase = 0xD0D0D0;
                     mat->m_fRoughness = 0.9f;
                     PinTable* table = p.m_ptable;
                     table->AddMaterial(mat);
                     table->SetupLookUpTables(true);
                     RecordAction(p, table, [table, mat]()
                     {
                        table->RemoveMaterial(mat);
                        table->SetupLookUpTables(true);
                     });
                     material = mat->m_name;
                     result["materialCreated"] = true;
                  }
               }
               if (!material.empty())
               {
                  if (p.m_ptable->IsDummyMaterial(p.m_ptable->GetMaterial(material)))
                     result["materialWarning"] = "no material named '"s + material + "' in the table, the primitive keeps its material";
                  else
                     prim->m_d.m_szMaterial = material;
               }
               RefreshPart(p, part);
               p.m_liveUI->m_editorUI.EndPartEdit(part);
               McpServer::Get().NoteEdit();
               result["name"] = part->GetName();
               result["created"] = created;
               result["material"] = prim->m_d.m_szMaterial;
               result["note"] = "a non collidable toy primitive at (0, 0, 0) with the mesh in table coordinates; the collision stays with the ramp. Run it again after moving the ramp's "
                                "points to rebuild the mesh";
               return WithLiveCopyNote(p, result);
            });
      } });
}

}
