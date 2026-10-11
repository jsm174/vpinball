// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "utils/objloader.h"

namespace VPX::MCP
{

FlipperGeom GetFlipperGeom(Player& p, IEditable* part)
{
   FlipperGeom g;
   if (!part || part->GetItemType() != eItemFlipper)
      return g;
   const FlipperData& d = static_cast<Flipper*>(part)->m_d;
   g.name = part->GetName();
   g.base = d.m_Center;
   g.length = d.m_FlipperRadiusMax;
   const float a = ANGTORAD(d.m_StartAngle);
   g.tip = Vertex2D(g.base.x + g.length * sinf(a), g.base.y - g.length * cosf(a));
   g.left = g.tip.x > g.base.x;
   g.valid = true;
   return g;
}

vector<FlipperGeom> TableFlippers(Player& p)
{
   vector<FlipperGeom> flippers;
   for (IEditable* part : p.m_ptable->GetParts())
      if (part->GetItemType() == eItemFlipper)
         if (FlipperGeom g = GetFlipperGeom(p, part); g.valid)
            flippers.push_back(g);
   return flippers;
}

// The flipper a ball is heading to: above the flipper, moving down, within its horizontal span (with a margin)
const FlipperGeom* ApproachingFlipper(const vector<FlipperGeom>& flippers, Ball* ball, float& along)
{
   const vec3& pos = ball->GetPosition();
   const vec3& vel = ball->GetVelocity();
   if (vel.y <= 0.f)
      return nullptr;
   for (const FlipperGeom& f : flippers)
   {
      const float minX = std::min(f.base.x, f.tip.x) - 30.f, maxX = std::max(f.base.x, f.tip.x) + 30.f;
      const float topY = std::min(f.base.y, f.tip.y);
      if (pos.x >= minX && pos.x <= maxX && pos.y >= topY - 250.f && pos.y <= f.base.y + 20.f)
      {
         along = f.Along(Vertex2D(pos.x, pos.y));
         return &f;
      }
   }
   return nullptr;
}

// Outline of a band of the given thickness around a polyline (walls, guides, rims): left side forward, right side back, mitered joints
json BandOutline(const json& centerline, float thickness)
{
   vector<Vertex2D> pts;
   vector<bool> smooth;
   for (const json& pt : centerline)
   {
      pts.emplace_back(pt.value("x", 0.f), pt.value("y", 0.f));
      smooth.push_back(pt.value("smooth", false));
   }
   const size_t n = pts.size();
   const float half = 0.5f * thickness;
   vector<Vertex2D> left(n), right(n);
   for (size_t i = 0; i < n; i++)
   {
      const auto normal = [&pts](size_t a, size_t b)
      {
         Vertex2D d(pts[b].x - pts[a].x, pts[b].y - pts[a].y);
         const float len = sqrtf(d.x * d.x + d.y * d.y);
         return len > 0.f ? Vertex2D(-d.y / len, d.x / len) : Vertex2D(0.f, 0.f);
      };
      Vertex2D nrm;
      float scale = 1.f;
      if (i == 0)
         nrm = normal(0, 1);
      else if (i == n - 1)
         nrm = normal(n - 2, n - 1);
      else
      {
         const Vertex2D n1 = normal(i - 1, i), n2 = normal(i, i + 1);
         nrm = Vertex2D(n1.x + n2.x, n1.y + n2.y);
         const float len = sqrtf(nrm.x * nrm.x + nrm.y * nrm.y);
         nrm = len > 0.f ? Vertex2D(nrm.x / len, nrm.y / len) : n1;
         const float cosHalf = nrm.x * n1.x + nrm.y * n1.y;
         scale = cosHalf > 0.25f ? 1.f / cosHalf : 4.f;
      }
      left[i] = Vertex2D(pts[i].x + nrm.x * half * scale, pts[i].y + nrm.y * half * scale);
      right[i] = Vertex2D(pts[i].x - nrm.x * half * scale, pts[i].y - nrm.y * half * scale);
   }
   json outline = json::array();
   for (size_t i = 0; i < n; i++)
      outline.push_back({ { "x", left[i].x }, { "y", left[i].y }, { "smooth", static_cast<bool>(smooth[i]) } });
   for (size_t i = n; i-- > 0;)
      outline.push_back({ { "x", right[i].x }, { "y", right[i].y }, { "smooth", static_cast<bool>(smooth[i]) } });
   return outline;
}

json CircleOutline(float x, float y, float radius, int segments)
{
   json outline = json::array();
   segments = std::clamp(segments, 3, 128);
   for (int i = 0; i < segments; i++)
   {
      const float a = static_cast<float>(2.0 * M_PI * i / segments);
      outline.push_back({ { "x", x + radius * sinf(a) }, { "y", y - radius * cosf(a) }, { "smooth", true } });
   }
   return outline;
}

Vertex3Ds ClosestOnSegment(const Vertex3Ds& p, const Vertex3Ds& a, const Vertex3Ds& b)
{
   const Vertex3Ds ab = b - a;
   const float len2 = ab.Dot(ab);
   const float t = len2 > 0.f ? std::clamp((p - a).Dot(ab) / len2, 0.f, 1.f) : 0.f;
   return a + ab * t;
}

Vertex3Ds ClosestOnTriangle(const Vertex3Ds& p, const Vertex3Ds& a, const Vertex3Ds& b, const Vertex3Ds& c)
{
   const Vertex3Ds ab = b - a, ac = c - a, ap = p - a;
   const float d1 = ab.Dot(ap), d2 = ac.Dot(ap);
   if (d1 <= 0.f && d2 <= 0.f)
      return a;
   const Vertex3Ds bp = p - b;
   const float d3 = ab.Dot(bp), d4 = ac.Dot(bp);
   if (d3 >= 0.f && d4 <= d3)
      return b;
   const float vc = d1 * d4 - d3 * d2;
   if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
      return a + ab * (d1 / (d1 - d3));
   const Vertex3Ds cp = p - c;
   const float d5 = ab.Dot(cp), d6 = ac.Dot(cp);
   if (d6 >= 0.f && d5 <= d6)
      return c;
   const float vb = d5 * d2 - d1 * d6;
   if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f)
      return a + ac * (d2 / (d2 - d6));
   const float va = d3 * d6 - d5 * d4;
   if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f)
      return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
   const float denom = 1.f / (va + vb + vc);
   return a + ab * (vb * denom) + ac * (vc * denom);
}

// Closest point of a collider to p
Vertex3Ds ClosestOnCollider(const HitObject* ho, const Vertex3Ds& p)
{
   const FRect3D& bb = ho->m_hitBBox;
   const auto clampZ = [&bb](float z) { return std::clamp(z, min(bb.zlow, bb.zhigh), max(bb.zlow, bb.zhigh)); };
   if (ho->GetType() != e3DLine)
   {
      if (const LineSeg* seg = dynamic_cast<const LineSeg*>(ho))
      {
         const Vertex3Ds c = ClosestOnSegment(Vertex3Ds(p.x, p.y, 0.f), Vertex3Ds(seg->v1.x, seg->v1.y, 0.f), Vertex3Ds(seg->v2.x, seg->v2.y, 0.f));
         return Vertex3Ds(c.x, c.y, clampZ(p.z));
      }
      if (const HitCircle* circle = dynamic_cast<const HitCircle*>(ho))
      {
         Vertex2D d(p.x - circle->center.x, p.y - circle->center.y);
         const float len = sqrtf(d.x * d.x + d.y * d.y);
         if (ho->m_ObjType >= eTrigger && len <= circle->radius)
            return Vertex3Ds(p.x, p.y, clampZ(p.z));
         if (len > 0.f)
            d = Vertex2D(d.x / len, d.y / len);
         else
            d = Vertex2D(1.f, 0.f);
         return Vertex3Ds(circle->center.x + d.x * circle->radius, circle->center.y + d.y * circle->radius, clampZ(p.z));
      }
      if (const HitLineZ* line = dynamic_cast<const HitLineZ*>(ho))
         return Vertex3Ds(line->m_xy.x, line->m_xy.y, clampZ(p.z));
   }
   if (const HitPoint* point = dynamic_cast<const HitPoint*>(ho))
      return point->m_p;
   if (const HitTriangle* tri = dynamic_cast<const HitTriangle*>(ho))
      return ClosestOnTriangle(p, tri->m_rgv[0], tri->m_rgv[1], tri->m_rgv[2]);
   if (const Hit3DPoly* poly = dynamic_cast<const Hit3DPoly*>(ho); poly && poly->GetVertexCount() >= 3)
   {
      Vertex3Ds best = poly->GetVertices()[0];
      float bestD = FLT_MAX;
      for (int i = 1; i + 1 < poly->GetVertexCount(); i++)
      {
         const Vertex3Ds c = ClosestOnTriangle(p, poly->GetVertices()[0], poly->GetVertices()[i], poly->GetVertices()[i + 1]);
         if (const float d = (c - p).Length(); d < bestD)
         {
            bestD = d;
            best = c;
         }
      }
      return best;
   }
   return Vertex3Ds(std::clamp(p.x, bb.left, bb.right), std::clamp(p.y, bb.top, bb.bottom), clampZ(p.z));
}

ankerl::unordered_dense::map<IEditable*, vector<const HitObject*>> CollidersByPart(Player& p)
{
   ankerl::unordered_dense::map<IEditable*, vector<const HitObject*>> parts;
   if (p.m_physics)
      for (const HitObject* ho : p.m_physics->GetHitObjects())
         if (ho && ho->m_editable && ho->GetType() != eBall && ho->m_enabled)
            parts[ho->m_editable].push_back(ho);
   return parts;
}

PartDistance DistanceToPart(IEditable* part, const vector<const HitObject*>& colliders, const Vertex3Ds& p, float maxDistance)
{
   PartDistance best;
   best.part = part;
   for (const HitObject* ho : colliders)
   {
      // Quick reject on the bounding box: a collider can not be closer than its box
      const FRect3D& bb = ho->m_hitBBox;
      const float dx = max(max(bb.left - p.x, p.x - bb.right), 0.f), dy = max(max(bb.top - p.y, p.y - bb.bottom), 0.f);
      const float dz = max(max(min(bb.zlow, bb.zhigh) - p.z, p.z - max(bb.zlow, bb.zhigh)), 0.f);
      const float boxDistance = sqrtf(dx * dx + dy * dy + dz * dz);
      if (boxDistance > min(best.distance, maxDistance))
         continue;
      const Vertex3Ds c = ClosestOnCollider(ho, p);
      if (const float d = (c - p).Length(); d < best.distance)
      {
         best.distance = d;
         best.closest = c;
         best.volume = ho->m_ObjType >= eTrigger;
      }
   }
   return best;
}

// Parts whose colliders come within radius of p, nearest first
vector<PartDistance> PartsNear(Player& p, const Vertex3Ds& point, float radius)
{
   vector<PartDistance> found;
   for (const auto& [part, colliders] : CollidersByPart(p))
      if (part != p.m_ptable)
         if (PartDistance d = DistanceToPart(part, colliders, point, radius); d.distance <= radius)
            found.push_back(d);
   std::ranges::sort(found, [](const PartDistance& a, const PartDistance& b) { return a.distance < b.distance; });
   return found;
}

json PartDistanceJson(const PartDistance& d, float ballRadius)
{
   json j { { "name", d.part->GetName() }, { "type", ItemTypeName(d.part->GetItemType()) }, { "distance", d.distance }, { "gapForBall", d.distance - ballRadius },
      { "closest", json { { "x", d.closest.x }, { "y", d.closest.y }, { "z", d.closest.z } } } };
   if (d.volume)
      j["volume"] = true;
   return j;
}

// Solid parts (and other balls) a ball is resting against
json TouchingJson(Player& p, Ball* ball, bool* onPlunger)
{
   const vec3& pos = ball->GetPosition();
   json touching = json::array();
   if (onPlunger)
      *onPlunger = false;
   for (const PartDistance& d : PartsNear(p, Vertex3Ds(pos.x, pos.y, pos.z), ball->GetRadius() + 2.f))
      if (!d.volume)
      {
         touching.push_back(d.part->GetName());
         if (onPlunger && d.part->GetItemType() == eItemPlunger)
            *onPlunger = true;
      }
   for (Ball* other : p.m_vball)
      if (other != ball && (other->GetPosition() - pos).Length() < ball->GetRadius() + other->GetRadius() + 2.f)
         touching.push_back("another ball");
   return touching;
}

// Sample points along a collider's shape, used to measure the gap between two parts
void SampleCollider(const HitObject* ho, vector<Vertex3Ds>& out)
{
   const FRect3D& bb = ho->m_hitBBox;
   const float zmid = 0.5f * (bb.zlow + bb.zhigh);
   const auto line = [&out](const Vertex3Ds& a, const Vertex3Ds& b, int n)
   {
      for (int i = 0; i <= n; i++)
         out.push_back(a + (b - a) * (static_cast<float>(i) / n));
   };
   if (ho->GetType() != e3DLine)
   {
      if (const LineSeg* seg = dynamic_cast<const LineSeg*>(ho))
      {
         line(Vertex3Ds(seg->v1.x, seg->v1.y, zmid), Vertex3Ds(seg->v2.x, seg->v2.y, zmid), 24);
         return;
      }
      if (const HitCircle* circle = dynamic_cast<const HitCircle*>(ho))
      {
         for (int i = 0; i < 48; i++)
         {
            const float a = static_cast<float>(2.0 * M_PI * i / 48);
            out.emplace_back(circle->center.x + circle->radius * sinf(a), circle->center.y + circle->radius * cosf(a), zmid);
         }
         return;
      }
      if (const HitLineZ* lz = dynamic_cast<const HitLineZ*>(ho))
      {
         line(Vertex3Ds(lz->m_xy.x, lz->m_xy.y, bb.zlow), Vertex3Ds(lz->m_xy.x, lz->m_xy.y, bb.zhigh), 4);
         return;
      }
   }
   if (const HitPoint* point = dynamic_cast<const HitPoint*>(ho))
   {
      out.push_back(point->m_p);
      return;
   }
   if (const HitTriangle* tri = dynamic_cast<const HitTriangle*>(ho))
   {
      for (int i = 0; i < 3; i++)
         line(tri->m_rgv[i], tri->m_rgv[(i + 1) % 3], 6);
      out.push_back((tri->m_rgv[0] + tri->m_rgv[1] + tri->m_rgv[2]) * (1.f / 3.f));
      return;
   }
   if (const Hit3DPoly* poly = dynamic_cast<const Hit3DPoly*>(ho))
   {
      for (int i = 0; i < poly->GetVertexCount(); i++)
         line(poly->GetVertices()[i], poly->GetVertices()[(i + 1) % poly->GetVertexCount()], 6);
      return;
   }
   for (const float z : { bb.zlow, zmid, bb.zhigh })
   {
      line(Vertex3Ds(bb.left, bb.top, z), Vertex3Ds(bb.right, bb.top, z), 6);
      line(Vertex3Ds(bb.right, bb.top, z), Vertex3Ds(bb.right, bb.bottom, z), 6);
      line(Vertex3Ds(bb.right, bb.bottom, z), Vertex3Ds(bb.left, bb.bottom, z), 6);
      line(Vertex3Ds(bb.left, bb.bottom, z), Vertex3Ds(bb.left, bb.top, z), 6);
   }
}

// Flat textured shapes in VPX units, z up, bottom at z 0, the top face mapped to the whole image
bool GenerateMesh(Mesh& mesh, const string& shape, float radius, float height, float width, float depth, int segments, string& error)
{
   mesh.Clear();
   auto vert = [&mesh](float x, float y, float z, float nx, float ny, float nz, float u, float v)
   {
      Vertex3D_NoTex2 vt {};
      vt.x = x; vt.y = y; vt.z = z;
      vt.nx = nx; vt.ny = ny; vt.nz = nz;
      vt.tu = u; vt.tv = v;
      mesh.m_vertices.push_back(vt);
      return static_cast<unsigned int>(mesh.m_vertices.size() - 1);
   };
   auto tri = [&mesh](unsigned int a, unsigned int b, unsigned int c) { mesh.m_indices.insert(mesh.m_indices.end(), { a, c, b }); };
   if (height <= 0.f)
   {
      error = "height must be positive"s;
      return false;
   }
   if (shape == "disc" || shape == "cylinder" || shape == "post")
   {
      if (radius <= 0.f)
      {
         error = "radius must be positive"s;
         return false;
      }
      segments = std::clamp(segments, 3, 256);
      const unsigned int top = vert(0.f, 0.f, height, 0.f, 0.f, 1.f, 0.5f, 0.5f);
      for (int i = 0; i < segments; i++)
      {
         const float a = static_cast<float>(2.0 * M_PI * i / segments);
         vert(radius * sinf(a), radius * cosf(a), height, 0.f, 0.f, 1.f, 0.5f + 0.5f * sinf(a), 0.5f + 0.5f * cosf(a));
      }
      for (int i = 0; i < segments; i++)
         tri(top, top + 1 + i, top + 1 + (i + 1) % segments);
      const unsigned int bottom = vert(0.f, 0.f, 0.f, 0.f, 0.f, -1.f, 0.5f, 0.5f);
      for (int i = 0; i < segments; i++)
      {
         const float a = static_cast<float>(2.0 * M_PI * i / segments);
         vert(radius * sinf(a), radius * cosf(a), 0.f, 0.f, 0.f, -1.f, 0.5f + 0.5f * sinf(a), 0.5f + 0.5f * cosf(a));
      }
      for (int i = 0; i < segments; i++)
         tri(bottom, bottom + 1 + (i + 1) % segments, bottom + 1 + i);
      const unsigned int side = static_cast<unsigned int>(mesh.m_vertices.size());
      for (int i = 0; i <= segments; i++)
      {
         const float a = static_cast<float>(2.0 * M_PI * i / segments);
         const float u = static_cast<float>(i) / segments;
         vert(radius * sinf(a), radius * cosf(a), height, sinf(a), cosf(a), 0.f, u, 0.f);
         vert(radius * sinf(a), radius * cosf(a), 0.f, sinf(a), cosf(a), 0.f, u, 1.f);
      }
      for (int i = 0; i < segments; i++)
      {
         const unsigned int t0 = side + 2 * i, b0 = t0 + 1, t1 = t0 + 2, b1 = t0 + 3;
         tri(t1, t0, b0);
         tri(t1, b0, b1);
      }
   }
   else if (shape == "box")
   {
      if (width <= 0.f || depth <= 0.f)
      {
         error = "width and depth must be positive"s;
         return false;
      }
      const float hw = width * 0.5f, hd = depth * 0.5f;
      const float cx[4] = { hw, hw, -hw, -hw }, cy[4] = { hd, -hd, -hd, hd };
      const unsigned int top = static_cast<unsigned int>(mesh.m_vertices.size());
      for (int i = 0; i < 4; i++)
         vert(cx[i], cy[i], height, 0.f, 0.f, 1.f, (cx[i] + hw) / width, (cy[i] + hd) / depth);
      tri(top, top + 1, top + 2);
      tri(top, top + 2, top + 3);
      const unsigned int bottom = static_cast<unsigned int>(mesh.m_vertices.size());
      for (int i = 0; i < 4; i++)
         vert(cx[i], cy[i], 0.f, 0.f, 0.f, -1.f, (cx[i] + hw) / width, (cy[i] + hd) / depth);
      tri(bottom, bottom + 2, bottom + 1);
      tri(bottom, bottom + 3, bottom + 2);
      for (int i = 0; i < 4; i++)
      {
         const int n = (i + 1) % 4;
         const float ex = cx[n] - cx[i], ey = cy[n] - cy[i];
         const float len = sqrtf(ex * ex + ey * ey);
         const float nx = ey / len, ny = -ex / len;
         const unsigned int t0 = vert(cx[i], cy[i], height, nx, ny, 0.f, i * 0.25f, 0.f);
         const unsigned int b0 = vert(cx[i], cy[i], 0.f, nx, ny, 0.f, i * 0.25f, 1.f);
         const unsigned int t1 = vert(cx[n], cy[n], height, nx, ny, 0.f, (i + 1) * 0.25f, 0.f);
         const unsigned int b1 = vert(cx[n], cy[n], 0.f, nx, ny, 0.f, (i + 1) * 0.25f, 1.f);
         tri(t1, t0, b0);
         tri(t1, b0, b1);
      }
   }
   else
   {
      error = "shape must be disc, cylinder or box"s;
      return false;
   }
   mesh.UpdateBounds();
   return true;
}

void PrimitiveWorldVertices(Primitive* prim, vector<Vertex3Ds>& out)
{
   Matrix3D full = prim->RecalculateMatrices();
   if (prim->GetPartGroup() != nullptr && prim->GetPartGroup()->GetReferenceSpace() != PartGroupData::SpaceReference::SR_PLAYFIELD)
      full = full * prim->GetPTable()->GetDefaultPlayfieldToCabMatrix();
   out.clear();
   out.reserve(prim->m_mesh.NumVertices());
   for (const Vertex3D_NoTex2& v : prim->m_mesh.m_vertices)
      out.push_back(full * v);
}

void SimRun::Track(Player& p, const std::function<bool(Ball*)>& filter)
{
   if (!collidersReady)
   {
      colliders = CollidersByPart(p);
      collidersReady = true;
   }
   for (Ball* b : p.m_vball)
   {
      if (!filter(b))
         continue;
      int id = -1;
      b->get_ID(&id);
      const vec3& pos = b->GetPosition();
      const float radius = b->GetRadius();
      const Vertex3Ds point(pos.x, pos.y, pos.z);
      string under;
      float underDistance = FLT_MAX;
      for (const auto& [part, partColliders] : colliders)
      {
         if (part == p.m_ptable)
            continue;
         const PartDistance d = DistanceToPart(part, partColliders, point, radius + 2.f);
         if (d.distance > radius + 2.f)
            continue;
         const bool playfield = p.m_ptable->IsPlayerSessionObject(part) || (part->GetItemType() == eItemPrimitive && static_cast<Primitive*>(part)->IsPlayfield());
         const string name = playfield ? "playfield"s : part->GetName();
         vector<string>& seen = contacts[id];
         if (!playfield && std::ranges::find(seen, name) == seen.end())
            seen.push_back(name);
         if (!d.volume && d.closest.z < pos.z - radius * 0.5f && d.distance < underDistance)
         {
            under = name;
            underDistance = d.distance;
         }
      }
      if (under.empty() && pos.z - radius < 5.f)
         under = "playfield"s;
      const auto r = [](float f) { return std::strtod(std::format("{:.1f}", f).c_str(), nullptr); };
      if (!under.empty() && support[id] != under)
      {
         support[id] = under;
         rode[id].push_back({ { "on", under }, { "t", p.m_time_msec - startMs }, { "x", r(pos.x) }, { "y", r(pos.y) }, { "z", r(pos.z) } });
      }
      if (crossAxis >= 0)
      {
         if (const auto it = lastPos.find(id); it != lastPos.end() && crossings[id].size() < 10)
         {
            const vec3& prev = it->second;
            const float a = (crossAxis == 0 ? prev.x : prev.y) - crossValue, c = (crossAxis == 0 ? pos.x : pos.y) - crossValue;
            if ((a < 0.f) != (c < 0.f) && a != c)
            {
               const float f = a / (a - c);
               const vec3 at = prev + (pos - prev) * f;
               const vec3& vel = b->GetVelocity();
               crossings[id].push_back({ { "t", p.m_time_msec - startMs }, { "x", r(at.x) }, { "y", r(at.y) }, { "z", r(at.z) }, { "direction", crossAxis == 0 ? (c > a ? "+x" : "-x") : (c > a ? "down the table (+y)" : "up the table (-y)") },
                  { "speed", r(sqrtf(vel.x * vel.x + vel.y * vel.y)) }, { "angle", r(atan2f(vel.x, -vel.y) * (float)(180.0 / M_PI)) } });
            }
         }
         lastPos[id] = pos;
      }
   }
}

json SimRun::BallPathJson(int id)
{
   json j { { "rode", rode.contains(id) ? rode[id] : json::array() }, { "contacts", contacts.contains(id) ? json(contacts[id]) : json::array() } };
   if (crossAxis >= 0)
      j["crossings"] = crossings.contains(id) && crossings[id].is_array() ? crossings[id] : json::array();
   return j;
}

namespace
{
   struct RampWire
   {
      string label;
      vector<Vertex3Ds> pts;
   };

   struct RampGeom
   {
      Ramp* ramp = nullptr;
      string name;
      bool wire = false;
      bool path = false; // a surface a ball rides: collidable, in playfield space, not a single wire guide rail
      vector<Vertex3Ds> center; // floor centerline (z = floor height)
      vector<float> half; // collision half width
      vector<RampWire> wires;
      float wireRadius = 0.f;
      Vertex3Ds lo { FLT_MAX, FLT_MAX, FLT_MAX }, hi { -FLT_MAX, -FLT_MAX, -FLT_MAX };
   };

   RampGeom BuildRampGeom(Ramp* ramp)
   {
      RampGeom g;
      g.ramp = ramp;
      g.name = ramp->GetName();
      g.wire = ramp->IsHabitrail();
      g.path = ramp->m_d.m_collidable && ramp->m_d.m_type != RampType1Wire && (ramp->GetPartGroup() == nullptr || ramp->GetPartGroup()->GetReferenceSpace() == PartGroupData::SpaceReference::SR_PLAYFIELD);
      int count = 0;
      float* height = nullptr;
      if (const Vertex2D* const outline = ramp->GetRampVertex(count, &height, nullptr, nullptr, nullptr, HIT_SHAPE_DETAIL_LEVEL, true))
      {
         for (int i = 0; i < count; i++)
         {
            const Vertex2D a = outline[i], b = outline[count * 2 - i - 1];
            g.center.emplace_back((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, height[i]);
            g.half.push_back(sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)) * 0.5f);
         }
         delete[] outline;
      }
      delete[] height;
      if (g.wire)
      {
         const RampData& d = ramp->m_d;
         g.wireRadius = d.m_wireDiameter * 0.5f;
         count = 0;
         height = nullptr;
         Vertex2D* middle = nullptr;
         if (const Vertex2D* const rails = ramp->GetRampVertex(count, &height, nullptr, nullptr, d.m_type == RampType1Wire ? &middle : nullptr, -1.f, false))
         {
            RampWire a { "wire A (floor)"s, {} }, b { "wire B (floor)"s, {} }, upperA { "wire A (upper)"s, {} }, upperB { "wire B (upper)"s, {} }, single { "wire"s, {} };
            for (int i = 0; i < count; i++)
            {
               if (d.m_type == RampType1Wire)
               {
                  single.pts.emplace_back(middle[i].x, middle[i].y, height[i]);
                  continue;
               }
               const Vertex2D ra = rails[i], rb = rails[count * 2 - i - 1];
               a.pts.emplace_back(ra.x, ra.y, height[i] + 3.f);
               b.pts.emplace_back(rb.x, rb.y, height[i] + 3.f);
               upperA.pts.emplace_back(ra.x, ra.y, height[i] + d.m_wireDistanceY * 0.5f);
               upperB.pts.emplace_back(rb.x, rb.y, height[i] + d.m_wireDistanceY * 0.5f);
            }
            delete[] rails;
            if (d.m_type == RampType1Wire)
               g.wires.push_back(single);
            else
            {
               g.wires.push_back(a);
               g.wires.push_back(b);
               if (d.m_type == RampType4Wire || d.m_type == RampType3WireRight)
                  g.wires.push_back(upperA);
               if (d.m_type == RampType4Wire || d.m_type == RampType3WireLeft)
                  g.wires.push_back(upperB);
            }
         }
         delete[] height;
         delete[] middle;
      }
      for (size_t i = 0; i < g.center.size(); i++)
      {
         const float h = g.half[i] + 30.f;
         g.lo = Vertex3Ds(min(g.lo.x, g.center[i].x - h), min(g.lo.y, g.center[i].y - h), min(g.lo.z, g.center[i].z - 10.f));
         g.hi = Vertex3Ds(max(g.hi.x, g.center[i].x + h), max(g.hi.y, g.center[i].y + h), max(g.hi.z, g.center[i].z + 120.f));
      }
      return g;
   }

   // Closest point of a 2D polyline to p: segment index, parameter, lateral distance (signed, positive on the left of the travel direction in table coordinates)
   bool ClosestOnCenter(const vector<Vertex3Ds>& line, const Vertex3Ds& p, size_t& seg, float& t, float& lateral, float& along)
   {
      float best = FLT_MAX, walked = 0.f;
      for (size_t i = 0; i + 1 < line.size(); i++)
      {
         const float dx = line[i + 1].x - line[i].x, dy = line[i + 1].y - line[i].y;
         const float len2 = dx * dx + dy * dy;
         const float u = len2 > 0.f ? std::clamp(((p.x - line[i].x) * dx + (p.y - line[i].y) * dy) / len2, 0.f, 1.f) : 0.f;
         const float cx = line[i].x + u * dx - p.x, cy = line[i].y + u * dy - p.y;
         if (const float d2 = cx * cx + cy * cy; d2 < best)
         {
            best = d2;
            seg = i;
            t = u;
            const float len = sqrtf(len2);
            lateral = len > 0.f ? (dx * (p.y - line[i].y) - dy * (p.x - line[i].x)) / len : 0.f;
            along = walked + u * len;
         }
         walked += sqrtf(len2);
      }
      return best != FLT_MAX;
   }

   float PolylineLength(const vector<Vertex3Ds>& line)
   {
      float len = 0.f;
      for (size_t i = 0; i + 1 < line.size(); i++)
         len += sqrtf((line[i + 1].x - line[i].x) * (line[i + 1].x - line[i].x) + (line[i + 1].y - line[i].y) * (line[i + 1].y - line[i].y));
      return len;
   }

   bool BoxesOverlap(const RampGeom& a, const RampGeom& b)
   {
      return a.lo.x <= b.hi.x && b.lo.x <= a.hi.x && a.lo.y <= b.hi.y && b.lo.y <= a.hi.y && a.lo.z <= b.hi.z && b.lo.z <= a.hi.z;
   }
}

json RampCheckJson(Player& p, const vector<Ramp*>& ramps, float ballRadius, float joinDistance)
{
   const auto r = [](float f) { return std::strtod(std::format("{:.1f}", f).c_str(), nullptr); };
   vector<RampGeom> geoms;
   for (Ramp* ramp : ramps)
      if (RampGeom g = BuildRampGeom(ramp); g.center.size() >= 2)
         geoms.push_back(std::move(g));
   json joins = json::array(), wiresInPath = json::array(), wireCrossings = json::array(), warnings = json::array(), list = json::array();
   for (const RampGeom& g : geoms)
      list.push_back({ { "name", g.name }, { "kind", g.wire ? "wire" : "flat" }, { "ballPath", g.path }, { "start", { r(g.center.front().x), r(g.center.front().y), r(g.center.front().z) } },
         { "end", { r(g.center.back().x), r(g.center.back().y), r(g.center.back().z) } }, { "collisionWidth", { r(g.half.front() * 2.f), r(g.half.back() * 2.f) } } });

   // Joins: an end of one ramp close to an end of another
   struct End { const RampGeom* g; bool start; Vertex3Ds at; Vertex2D outDir; float half; };
   const auto endOf = [](const RampGeom& g, bool start)
   {
      const Vertex3Ds& a = start ? g.center[0] : g.center.back();
      const Vertex3Ds& b = start ? g.center[1] : g.center[g.center.size() - 2];
      Vertex2D dir(a.x - b.x, a.y - b.y); // pointing out of the ramp
      dir.Normalize();
      return End { &g, start, a, dir, start ? g.half.front() : g.half.back() };
   };
   vector<std::pair<const RampGeom*, Vertex3Ds>> joinPoints;
   // Candidate joins, then each ramp end keeps its best match: stacked ramps (rails drawn as separate ramps above a floor ramp) end at the same points
   struct Candidate { size_t i, j; bool si, sj; float score; };
   vector<Candidate> candidates;
   for (size_t i = 0; i < geoms.size(); i++)
      for (size_t j = i + 1; j < geoms.size(); j++)
      {
         if (!geoms[i].path || !geoms[j].path)
            continue;
         for (const bool si : { false, true })
            for (const bool sj : { true, false })
            {
               const End a = endOf(geoms[i], si), b = endOf(geoms[j], sj);
               const float gap = Vertex2D(b.at.x - a.at.x, b.at.y - a.at.y).Length();
               const float angle = acosf(std::clamp(-(a.outDir.x * b.outDir.x + a.outDir.y * b.outDir.y), -1.f, 1.f)) * (float)(180.0 / M_PI);
               if (gap > joinDistance + min(a.half, b.half) * 0.5f || angle > 120.f)
                  continue;
               candidates.push_back({ i, j, si, sj, std::abs(b.at.z - a.at.z) + gap + angle * 0.5f });
            }
      }
   std::ranges::sort(candidates, [](const Candidate& a, const Candidate& b) { return a.score < b.score; });
   std::set<std::pair<size_t, bool>> usedEnds;
   for (const Candidate& cand : candidates)
   {
      if (usedEnds.contains({ cand.i, cand.si }) || usedEnds.contains({ cand.j, cand.sj }))
         continue;
      usedEnds.insert({ cand.i, cand.si });
      usedEnds.insert({ cand.j, cand.sj });
      const End a = endOf(geoms[cand.i], cand.si), b = endOf(geoms[cand.j], cand.sj);
      const float dx = b.at.x - a.at.x, dy = b.at.y - a.at.y;
      const float gap = sqrtf(dx * dx + dy * dy);
      // a's out direction faces against b's out direction when the ramps continue each other
      const float cosAngle = std::clamp(-(a.outDir.x * b.outDir.x + a.outDir.y * b.outDir.y), -1.f, 1.f);
      const float angle = acosf(cosAngle) * (float)(180.0 / M_PI);
      const float lateral = a.outDir.x * dy - a.outDir.y * dx;
      const float heightStep = b.at.z - a.at.z;
      json j { { "from", a.g->name + (a.start ? " start" : " end") }, { "to", b.g->name + (b.start ? " start" : " end") }, { "at", { r(a.at.x), r(a.at.y) } },
         { "gap", r(gap) }, { "lateralOffset", r(lateral) }, { "heightStep", r(heightStep) }, { "angle", r(angle) }, { "collisionWidths", { r(a.half * 2.f), r(b.half * 2.f) } } };
      json issues = json::array();
      if (std::abs(heightStep) > 1.f)
         issues.push_back(std::format("height step of {:.1f} at the join ({} {:.1f}, {} {:.1f})", heightStep, a.g->name, a.at.z, b.g->name, b.at.z));
      if (gap > 3.f)
         issues.push_back(std::format("the ends are {:.1f} apart", gap));
      if (angle > 10.f)
         issues.push_back(std::format("direction changes by {:.0f} degrees at the join", angle));
      // Edges of the narrower side sticking into the wider one's path
      const float leftClear = (b.half + lateral) - a.half, rightClear = (b.half - lateral) - a.half;
      if (leftClear < -1.f || rightClear < -1.f)
         issues.push_back(std::format("{} is narrower or offset: its edge sits {:.1f} inside the path of {} on one side (collision walls the ball can catch)", b.g->name, -min(leftClear, rightClear), a.g->name));
      if (a.g->wire && b.g->wire)
      {
         json mismatch = json::array();
         for (const RampWire& wa : a.g->wires)
         {
            const Vertex3Ds& pa = a.start ? wa.pts.front() : wa.pts.back();
            float best = FLT_MAX;
            const RampWire* match = nullptr;
            for (const RampWire& wb : b.g->wires)
               if (const float d = (pa - (b.start ? wb.pts.front() : wb.pts.back())).Length(); d < best)
               {
                  best = d;
                  match = &wb;
               }
            if (match && best > 2.f)
               mismatch.push_back({ { "wire", wa.label }, { "nearest", match->label }, { "distance", r(best) } });
         }
         if (!mismatch.empty())
         {
            j["wireEndMismatch"] = mismatch;
            issues.push_back("visible wire ends do not line up (see wireEndMismatch)"s);
         }
      }
      if (!issues.empty())
         j["issues"] = issues;
      joins.push_back(j);
      joinPoints.emplace_back(a.g, a.at);
      joinPoints.emplace_back(b.g, b.at);
   }
   const auto nearJoin = [&](const RampGeom& x, const RampGeom& y, const Vertex3Ds& p)
   {
      return std::ranges::any_of(joinPoints, [&](const auto& jp) { return (jp.first == &x || jp.first == &y) && Vertex2D(jp.second.x - p.x, jp.second.y - p.y).Length() < 60.f; });
   };

   // Visible wires of one ramp through the path of a ball riding another
   for (const RampGeom& x : geoms)
      for (const RampWire& w : x.wires)
         for (const RampGeom& y : geoms)
         {
            if (&x == &y || !y.path || !BoxesOverlap(x, y))
               continue;
            const float yLength = PolylineLength(y.center);
            float worst = FLT_MAX;
            Vertex3Ds worstAt;
            bool worstIsEnd = false;
            for (size_t k = 0; k < w.pts.size(); k++)
            {
               const Vertex3Ds& pt = w.pts[k];
               size_t seg = 0;
               float t = 0.f, lateral = 0.f, along = 0.f;
               if (!ClosestOnCenter(y.center, pt, seg, t, lateral, along) || along <= 0.5f || along >= yLength - 0.5f)
                  continue;
               const float half = y.half[seg] + t * (y.half[seg + 1] - y.half[seg]);
               const float floorZ = y.center[seg].z + t * (y.center[seg + 1].z - y.center[seg].z);
               const float lateralGap = max(0.f, std::abs(lateral) - max(0.f, half - ballRadius));
               const float verticalGap = pt.z - (floorZ + ballRadius);
               const float clearance = sqrtf(lateralGap * lateralGap + verticalGap * verticalGap) - ballRadius - x.wireRadius;
               if (clearance < worst && !nearJoin(x, y, pt))
               {
                  worst = clearance;
                  worstAt = pt;
                  worstIsEnd = k == 0 || k + 1 == w.pts.size();
               }
            }
            if (worst < 5.f)
            {
               json entry { { "wire", x.name + ' ' + w.label }, { "path", y.name }, { "at", { r(worstAt.x), r(worstAt.y), r(worstAt.z) } }, { "clearance", r(worst) }, { "wireEnd", worstIsEnd } };
               if (!y.ramp->m_d.m_visible)
                  entry["pathInvisible"] = true;
               wiresInPath.push_back(entry);
               if (worst < 0.f)
                  warnings.push_back(std::format("{} {} {} {:.1f} into the path of a ball riding {}{} at ({:.0f}, {:.0f}, {:.0f}): the ball is drawn through it (it collides with the flat walls of each ramp, not with wires)",
                     x.name, w.label, worstIsEnd ? "ends" : "cuts", -worst, y.name, y.ramp->m_d.m_visible ? "" : " (invisible collision ramp)", worstAt.x, worstAt.y, worstAt.z));
            }
         }

   // Visible wires of two ramps touching or crossing
   for (size_t i = 0; i < geoms.size(); i++)
      for (size_t j = i + 1; j < geoms.size(); j++)
      {
         const RampGeom &x = geoms[i], &y = geoms[j];
         if (x.wires.empty() || y.wires.empty() || !BoxesOverlap(x, y))
            continue;
         for (const RampWire& wa : x.wires)
            for (const RampWire& wb : y.wires)
            {
               float best = FLT_MAX;
               Vertex3Ds at;
               for (const Vertex3Ds& pa : wa.pts)
                  for (size_t k = 0; k + 1 < wb.pts.size(); k++)
                  {
                     const Vertex3Ds c = ClosestOnSegment(pa, wb.pts[k], wb.pts[k + 1]);
                     if (const float d = (c - pa).Length(); d < best)
                     {
                        best = d;
                        at = pa;
                     }
                  }
               const float clearance = best - x.wireRadius - y.wireRadius;
               if (clearance < 1.f && !nearJoin(x, y, at))
               {
                  wireCrossings.push_back({ { "a", x.name + ' ' + wa.label }, { "b", y.name + ' ' + wb.label }, { "at", { r(at.x), r(at.y), r(at.z) } }, { "clearance", r(clearance) } });
                  warnings.push_back(std::format("{} {} and {} {} {} at ({:.0f}, {:.0f}, {:.0f})", x.name, wa.label, y.name, wb.label, clearance < 0.f ? "intersect" : "touch", at.x, at.y, at.z));
               }
            }
      }
   // Ends that meet no other ramp: probe down just past the end for the surface the ball comes from or lands on (playfield, wall top, primitive...)
   json surfaceEnds = json::array();
   const auto colliders = CollidersByPart(p);
   for (size_t i = 0; i < geoms.size(); i++)
   {
      if (!geoms[i].path)
         continue;
      for (const bool start : { true, false })
      {
         if (usedEnds.contains({ i, start }))
            continue;
         const End e = endOf(geoms[i], start);
         const Vertex3Ds probe(e.at.x + e.outDir.x * (ballRadius + 5.f), e.at.y + e.outDir.y * (ballRadius + 5.f), 0.f);
         string surface = "playfield"s;
         float surfaceZ = 0.f;
         for (float z = e.at.z + ballRadius + 15.f; z >= ballRadius - 1.f; z -= 2.f)
         {
            const Vertex3Ds point(probe.x, probe.y, z);
            const IEditable* found = nullptr;
            float foundZ = 0.f, foundDistance = FLT_MAX;
            for (const auto& [part, partColliders] : colliders)
            {
               if (part == p.m_ptable || part == geoms[i].ramp)
                  continue;
               const PartDistance d = DistanceToPart(part, partColliders, point, ballRadius + 0.5f);
               if (d.distance <= ballRadius + 0.5f && !d.volume && d.closest.z < z - ballRadius * 0.5f && d.distance < foundDistance)
               {
                  found = part;
                  foundZ = d.closest.z;
                  foundDistance = d.distance;
               }
            }
            if (found)
            {
               const bool playfield = p.m_ptable->IsPlayerSessionObject(found) || (found->GetItemType() == eItemPrimitive && static_cast<const Primitive*>(found)->IsPlayfield());
               surface = playfield ? "playfield"s : found->GetName();
               surfaceZ = foundZ;
               break;
            }
         }
         const float step = e.at.z - surfaceZ;
         json j { { "end", geoms[i].name + (start ? " start" : " end") }, { "at", { r(e.at.x), r(e.at.y), r(e.at.z) } }, { "meets", surface }, { "surfaceHeight", r(surfaceZ) }, { "step", r(step) } };
         if (std::abs(step) > 2.f)
         {
            const string issue = step > 0.f
               ? std::format("the ramp end sits {:.1f} above {} (a lip the ball hits when entering here, a drop when leaving)", step, surface)
               : std::format("the ramp end sits {:.1f} below {} (the ball drops onto the ramp when entering here, hits the edge of {} when leaving)", -step, surface, surface);
            j["issue"] = issue;
            warnings.push_back(j["end"].get<string>() + ": " + issue);
         }
         surfaceEnds.push_back(j);
      }
   }
   for (const json& j : joins)
      for (const json& issue : j.value("issues", json::array()))
         warnings.push_back(j["from"].get<string>() + " -> " + j["to"].get<string>() + ": " + issue.get<string>());
   return json { { "ramps", list }, { "joins", joins }, { "surfaceEnds", surfaceEnds }, { "wiresInBallPath", wiresInPath }, { "wireCrossings", wireCrossings }, { "warnings", warnings },
      { "note", "Wire ramps collide as flat walls at the collision width (wire_distance_x + 20), the drawn wires sit at wire_distance_x; floor and upper wires are what you see. wiresInBallPath: a wire of one ramp within a ball's reach while it rides another (clearance < 0: drawn through the ball), measured from the whole collision width of the other ramp. Only ballPath ramps (collidable, playfield space, not a single wire guide rail) count as paths for joins, surfaceEnds and wiresInBallPath; joins compare the ramp centerline ends; surfaceEnds probes down just past every other ramp end for the surface there (collision shapes, the playfield at its mesh or 0) and reports the height step; parts near a join are excluded from the wire checks." } };
}

bool PartWorldMesh(IEditable* part, vector<Vertex3Ds>& vertices, vector<unsigned int>& indices, string& error)
{
   vertices.clear();
   indices.clear();
   const bool cabinetSpace = part->GetPartGroup() != nullptr && part->GetPartGroup()->GetReferenceSpace() != PartGroupData::SpaceReference::SR_PLAYFIELD;
   if (part->GetItemType() == eItemPrimitive)
   {
      Primitive* prim = static_cast<Primitive*>(part);
      PrimitiveWorldVertices(prim, vertices);
      indices = prim->m_mesh.m_indices;
      return true;
   }
   switch (part->GetItemType())
   {
   case eItemRamp: case eItemRubber: case eItemSurface: case eItemFlipper: case eItemBumper: case eItemGate: case eItemSpinner: case eItemHitTarget: case eItemKicker: case eItemTrigger: break;
   default: error = "this part type has no mesh export (lights, flashers, decals, text boxes, timers...); get reports its bounds"s; return false;
   }
   const std::filesystem::path tmp = std::filesystem::temp_directory_path() / ("vpx-mcp-mesh-"s + std::to_string(usec()) + ".obj");
   {
      ObjLoader loader;
      if (!loader.ExportStart(tmp))
      {
         error = "could not write a temporary .obj"s;
         return false;
      }
      part->ExportMesh(loader);
   }
   Mesh mesh;
   const bool loaded = std::filesystem::file_size(tmp) > 0 && mesh.LoadWavefrontObj(tmp, MeshUnits::VPUnits);
   std::error_code ec;
   std::filesystem::remove(tmp, ec);
   std::filesystem::remove(std::filesystem::path(tmp).replace_extension(".mtl"), ec);
   if (!loaded || mesh.m_vertices.empty())
   {
      error = "nothing to export: the part is invisible or has no visible mesh"s;
      return false;
   }
   const Matrix3D cab = cabinetSpace ? part->GetPTable()->GetDefaultPlayfieldToCabMatrix() : Matrix3D::MatrixIdentity();
   vertices.reserve(mesh.NumVertices());
   for (const Vertex3D_NoTex2& v : mesh.m_vertices)
      vertices.push_back(cab * Vertex3Ds(v.x, v.y, v.z));
   indices = mesh.m_indices;
   return true;
}

json PartBoundsJson(IEditable* part)
{
   vector<Vertex3Ds> points;
   const char* source = "mesh";
   vector<unsigned int> indices;
   if (string error; !PartWorldMesh(part, points, indices, error))
   {
      part->GetBoundingVertices(points, nullptr);
      source = "outline";
   }
   if (points.empty() && g_pplayer && g_pplayer->m_physics)
   {
      for (const HitObject* ho : g_pplayer->m_physics->GetHitObjects())
         if (ho && ho->m_editable == part)
            SampleCollider(ho, points);
      source = "collision shape";
   }
   if (points.empty())
      return json();
   Vertex3Ds lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
   for (const Vertex3Ds& v : points)
   {
      lo = Vertex3Ds(min(lo.x, v.x), min(lo.y, v.y), min(lo.z, v.z));
      hi = Vertex3Ds(max(hi.x, v.x), max(hi.y, v.y), max(hi.z, v.z));
   }
   const auto r = [](float f) { return std::strtod(std::format("{:.1f}", f).c_str(), nullptr); };
   return json { { "min", { r(lo.x), r(lo.y), r(lo.z) } }, { "max", { r(hi.x), r(hi.y), r(hi.z) } }, { "size", { r(hi.x - lo.x), r(hi.y - lo.y), r(hi.z - lo.z) } }, { "from", source } };
}

}
