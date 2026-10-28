// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "ui/live/editor/HeatStore.h"
#include "utils/objloader.h"
#include "math/MeshUtils.h"

namespace VPX::MCP
{

static void PrimitiveWorldVertices(Primitive* prim, vector<Vertex3Ds>& out);

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
static bool GateWireQuad(const HitObject* ho, Vertex3Ds quad[4])
{
   const HitGate* const gate = dynamic_cast<const HitGate*>(ho);
   if (!gate || gate->GetPhysicProfile() != PhysicProfile::V10_9)
      return false;
   const GateMoverObject& mv = gate->m_gateMover;
   const Vertex2D a = mv.m_center - mv.m_tangent * mv.m_wireHalfSpan, b = mv.m_center + mv.m_tangent * mv.m_wireHalfSpan;
   quad[0] = Vertex3Ds(a.x, a.y, mv.m_pivotZ);
   quad[1] = Vertex3Ds(b.x, b.y, mv.m_pivotZ);
   quad[2] = Vertex3Ds(b.x, b.y, mv.m_pivotZ - mv.m_wireDepth);
   quad[3] = Vertex3Ds(a.x, a.y, mv.m_pivotZ - mv.m_wireDepth);
   return true;
}

Vertex3Ds ClosestOnCollider(const HitObject* ho, const Vertex3Ds& p)
{
   if (Vertex3Ds quad[4]; GateWireQuad(ho, quad))
   {
      const Vertex3Ds c1 = ClosestOnTriangle(p, quad[0], quad[1], quad[2]), c2 = ClosestOnTriangle(p, quad[0], quad[2], quad[3]);
      return (c1 - p).LengthSquared() <= (c2 - p).LengthSquared() ? c1 : c2;
   }
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
   json j { { "name", d.part->GetName() }, { "type", ItemTypeName(d.part->GetItemType()) }, { "distance", Round(d.distance) }, { "gapForBall", Round(d.distance - ballRadius) },
      { "closest", json { { "x", Round(d.closest.x) }, { "y", Round(d.closest.y) }, { "z", Round(d.closest.z) } } } };
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
   if (Vertex3Ds quad[4]; GateWireQuad(ho, quad))
   {
      for (int i = 0; i < 4; i++)
         line(quad[i], quad[(i + 1) % 4], 8);
      return;
   }
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

static void PrimitiveWorldVertices(Primitive* prim, vector<Vertex3Ds>& out)
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
      for (const auto& [part, partColliders] : colliders)
      {
         FRect3D box(FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX);
         for (const HitObject* ho : partColliders)
            box.Extend(ho->m_hitBBox);
         partBoxes[part] = box;
      }
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
      // Heatmap: the game time since the last frame spread along the move of the ball (resting or moving), and its hits
      {
         const vec3& v = b->GetVelocity();
         VPX::Heat::Store::For(p.m_ptable).TrackBall(p.m_ptable, id, pos.x, pos.y, sqrtf(v.x * v.x + v.y * v.y + v.z * v.z), p.m_time_msec);
      }
      string under;
      float underDistance = FLT_MAX;
      vector<string> touchingNow;
      for (const auto& [part, partColliders] : colliders)
      {
         if (part == p.m_ptable)
            continue;
         const FRect3D& box = partBoxes[part];
         const float reach = radius + 2.f;
         if (pos.x < box.left - reach || pos.x > box.right + reach || pos.y < box.top - reach || pos.y > box.bottom + reach || pos.z < min(box.zlow, box.zhigh) - reach || pos.z > max(box.zlow, box.zhigh) + reach)
            continue;
         const PartDistance d = DistanceToPart(part, partColliders, point, reach);
         if (d.distance > radius + 2.f)
            continue;
         const bool playfield = p.m_ptable->IsPlayerSessionObject(part) || (part->GetItemType() == eItemPrimitive && static_cast<Primitive*>(part)->IsPlayfield());
         const string name = playfield ? "playfield"s : part->GetName();
         vector<string>& seen = contacts[id];
         if (!playfield && std::ranges::find(seen, name) == seen.end())
            seen.push_back(name);
         if (!d.volume)
            touchingNow.push_back(name);
         if (!d.volume && d.closest.z < pos.z - radius * 0.5f && d.distance < underDistance)
         {
            under = name;
            underDistance = d.distance;
         }
      }
      if (under.empty() && pos.z - radius < 5.f)
         under = "playfield"s;
      if (!under.empty() && support[id] != under)
      {
         support[id] = under;
         rode[id].push_back({ { "on", under }, { "t", p.m_time_msec - startMs }, { "x", Round(pos.x) }, { "y", Round(pos.y) }, { "z", Round(pos.z) } });
      }
      // A speed loss of at least 4 and a fifth of the speed within one frame is a hit, not gravity or rolling friction: attribute it to the part
      // touched now or on the previous frame (preferring one that is not the surface the ball rides), else the nearest collider
      {
         const vec3& vel = b->GetVelocity();
         const float speed = sqrtf(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
         if (const auto it = lastSpeed.find(id); it != lastSpeed.end())
         {
            const float before = it->second;
            const float loss = before - speed;
            if (loss >= 4.f && loss >= 0.2f * before)
            {
               string part;
               const vector<string>& previous = lastTouching[id];
               for (const vector<string>* list : { static_cast<const vector<string>*>(&touchingNow), &previous })
                  for (const string& n : *list)
                     if (part.empty() && n != under)
                        part = n;
               if (part.empty())
                  part = !touchingNow.empty() ? touchingNow.front() : !previous.empty() ? previous.front() : ""s;
               if (part.empty())
               {
                  float bestDistance = FLT_MAX;
                  for (const auto& [other, otherColliders] : colliders)
                  {
                     if (other == p.m_ptable)
                        continue;
                     const FRect3D& box = partBoxes[other];
                     const float reach = radius + 40.f;
                     if (pos.x < box.left - reach || pos.x > box.right + reach || pos.y < box.top - reach || pos.y > box.bottom + reach || pos.z < min(box.zlow, box.zhigh) - reach || pos.z > max(box.zlow, box.zhigh) + reach)
                        continue;
                     const PartDistance d = DistanceToPart(other, otherColliders, point, reach);
                     if (d.distance < bestDistance && !d.volume)
                     {
                        bestDistance = d.distance;
                        const bool playfield = p.m_ptable->IsPlayerSessionObject(other) || (other->GetItemType() == eItemPrimitive && static_cast<Primitive*>(other)->IsPlayfield());
                        part = playfield ? "playfield"s : other->GetName();
                     }
                  }
               }
               if (part.empty())
                  part = "unknown"s;
               const uint32_t t = p.m_time_msec - startMs;
               json& list = impacts[id];
               if (!list.is_array())
                  list = json::array();
               if (!list.empty() && list.back().value("part", ""s) == part && t - list.back().value("tEnd", 0u) <= 40)
               {
                  json& last = list.back();
                  last["speedOut"] = Round(speed, 2);
                  last["loss"] = Round(last.value("speedIn", 0.f) - speed, 2);
                  last["tEnd"] = t;
               }
               else
                  list.push_back({ { "part", part }, { "t", t }, { "tEnd", t }, { "x", Round(pos.x) }, { "y", Round(pos.y) }, { "z", Round(pos.z) }, { "speedIn", Round(before, 2) }, { "speedOut", Round(speed, 2) }, { "loss", Round(loss, 2) } });
            }
         }
         lastSpeed[id] = speed;
         lastTouching[id] = touchingNow;
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
               crossings[id].push_back({ { "t", p.m_time_msec - startMs }, { "x", Round(at.x) }, { "y", Round(at.y) }, { "z", Round(at.z) }, { "direction", crossAxis == 0 ? (c > a ? "+x" : "-x") : (c > a ? "down the table (+y)" : "up the table (-y)") },
                  { "speed", Round(sqrtf(vel.x * vel.x + vel.y * vel.y)) }, { "angle", Round(atan2f(vel.x, -vel.y) * (float)(180.0 / M_PI)) } });
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
   if (const auto it = impacts.find(id); it != impacts.end() && it->second.is_array() && !it->second.empty())
   {
      vector<json> sorted(it->second.begin(), it->second.end());
      std::ranges::stable_sort(sorted, [](const json& a, const json& b) { return a.value("loss", 0.f) > b.value("loss", 0.f); });
      json list = json::array();
      for (const json& e : sorted)
      {
         json entry = e;
         if (entry.value("tEnd", 0u) == entry.value("t", 0u))
            entry.erase("tEnd");
         list.push_back(entry);
         if (list.size() >= 10)
            break;
      }
      j["impacts"] = list;
      j["impactCount"] = sorted.size();
   }
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

json RampCheckJson(Player& p, const vector<Ramp*>& ramps, float ballRadius, float joinDistance, bool includePaths)
{
   vector<RampGeom> geoms;
   for (Ramp* ramp : ramps)
      if (RampGeom g = BuildRampGeom(ramp); g.center.size() >= 2)
         geoms.push_back(std::move(g));
   json joins = json::array(), wiresInPath = json::array(), wireCrossings = json::array(), warnings = json::array(), list = json::array();
   for (const RampGeom& g : geoms)
   {
      json entry { { "name", g.name }, { "kind", g.wire ? "wire" : "flat" }, { "ballPath", g.path }, { "start", { Round(g.center.front().x), Round(g.center.front().y), Round(g.center.front().z) } },
         { "end", { Round(g.center.back().x), Round(g.center.back().y), Round(g.center.back().z) } }, { "collisionWidth", { Round(g.half.front() * 2.f), Round(g.half.back() * 2.f) } } };
      if (includePaths)
      {
         // The floor centerline (z = floor height) with the collision half width, one point every ~10 units
         json path = json::array();
         float since = FLT_MAX;
         for (size_t i = 0; i < g.center.size(); i++)
         {
            if (i > 0)
               since += Vertex2D(g.center[i].x - g.center[i - 1].x, g.center[i].y - g.center[i - 1].y).Length();
            if (since >= 10.f || i + 1 == g.center.size())
            {
               path.push_back({ Round(g.center[i].x), Round(g.center[i].y), Round(g.center[i].z), Round(g.half[i]) });
               since = 0.f;
            }
         }
         entry["path"] = path;
      }
      list.push_back(entry);
   }

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
      json j { { "from", a.g->name + (a.start ? " start" : " end") }, { "to", b.g->name + (b.start ? " start" : " end") }, { "at", { Round(a.at.x), Round(a.at.y) } },
         { "gap", Round(gap) }, { "lateralOffset", Round(lateral) }, { "heightStep", Round(heightStep) }, { "angle", Round(angle) }, { "collisionWidths", { Round(a.half * 2.f), Round(b.half * 2.f) } } };
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
               mismatch.push_back({ { "wire", wa.label }, { "nearest", match->label }, { "distance", Round(best) } });
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
               json entry { { "wire", x.name + ' ' + w.label }, { "path", y.name }, { "at", { Round(worstAt.x), Round(worstAt.y), Round(worstAt.z) } }, { "clearance", Round(worst) }, { "wireEnd", worstIsEnd } };
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
                  wireCrossings.push_back({ { "a", x.name + ' ' + wa.label }, { "b", y.name + ' ' + wb.label }, { "at", { Round(at.x), Round(at.y), Round(at.z) } }, { "clearance", Round(clearance) } });
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
         json j { { "end", geoms[i].name + (start ? " start" : " end") }, { "at", { Round(e.at.x), Round(e.at.y), Round(e.at.z) } }, { "meets", surface }, { "surfaceHeight", Round(surfaceZ) }, { "step", Round(step) } };
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

   // Path clearance: a ball riding the ramp (on the centerline and against each wall) swept along it must not intersect any other collider.
   // Underside: where the floor runs about a ball's height above the surface beside the ramp with nothing under its edge, a ball wedges under it
   json pathClearance = json::array(), underside = json::array();
   {
      ankerl::unordered_dense::map<IEditable*, FRect3D> boxes;
      for (const auto& [part, partColliders] : colliders)
      {
         FRect3D box(FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX);
         for (const HitObject* ho : partColliders)
            box.Extend(ho->m_hitBBox);
         boxes[part] = box;
      }
      const auto isPlayfield = [&p](IEditable* part) { return p.m_ptable->IsPlayerSessionObject(part) || (part->GetItemType() == eItemPrimitive && static_cast<Primitive*>(part)->IsPlayfield()); };
      const auto geomOf = [&geoms](IEditable* part) -> const RampGeom*
      {
         for (const RampGeom& g : geoms)
            if (g.ramp == part)
               return &g;
         return nullptr;
      };
      const auto nearPoint = [](const FRect3D& box, const Vertex3Ds& pt, float reach)
      {
         return !(pt.x < box.left - reach || pt.x > box.right + reach || pt.y < box.top - reach || pt.y > box.bottom + reach || pt.z < min(box.zlow, box.zhigh) - reach || pt.z > max(box.zlow, box.zhigh) + reach);
      };
      // The surface a ball resting at (x, y) would sit on below z: probes down with a ball, the way surfaceEnds does
      const auto surfaceBelow = [&](const RampGeom& g, float x, float y, float fromZ, float& surfaceZ) -> string
      {
         surfaceZ = 0.f;
         for (float z = fromZ; z >= ballRadius - 1.f; z -= 2.f)
         {
            const Vertex3Ds point(x, y, z);
            const IEditable* found = nullptr;
            float foundZ = 0.f, foundDistance = FLT_MAX;
            for (const auto& [part, partColliders] : colliders)
            {
               if (part == p.m_ptable || part == g.ramp || !nearPoint(boxes[part], point, ballRadius + 0.5f))
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
               surfaceZ = foundZ;
               return isPlayfield(const_cast<IEditable*>(found)) ? "playfield"s : found->GetName();
            }
         }
         return "playfield"s;
      };
      struct Worst { float penetration = 0.f; Vertex3Ds at; float along = 0.f; string side; };
      struct Gap { string side; Vertex3Ds from, to; float minClear = FLT_MAX, maxClear = 0.f, length = 0.f; string surface; bool open = false; };
      for (const RampGeom& g : geoms)
      {
         if (!g.path)
            continue;
         std::map<IEditable*, Worst> worst;
         vector<Gap> gaps;
         Gap current[2];
         const auto closeGap = [&gaps](Gap& gap)
         {
            if (gap.open && gap.length >= 15.f)
               gaps.push_back(gap);
            gap = Gap();
         };
         const float total = PolylineLength(g.center);
         float walked = 0.f;
         for (size_t i = 0; i + 1 < g.center.size(); i++)
         {
            const Vertex3Ds &a = g.center[i], &b = g.center[i + 1];
            const float segLen = Vertex2D(b.x - a.x, b.y - a.y).Length();
            if (segLen < 0.01f)
               continue;
            const Vertex2D dir((b.x - a.x) / segLen, (b.y - a.y) / segLen);
            const Vertex2D left(dir.y, -dir.x); // left of the direction of travel (y grows toward the player)
            for (float s = 0.f; s < segLen + 5.f; s += 10.f)
            {
               const float t = min(s, segLen) / segLen;
               const Vertex3Ds c(a.x + dir.x * min(s, segLen), a.y + dir.y * min(s, segLen), a.z + t * (b.z - a.z));
               const float half = g.half[i] + t * (g.half[i + 1] - g.half[i]);
               const float along = walked + min(s, segLen);
               const float offset = max(0.f, half - ballRadius);
               for (const auto& [side, lat] : { std::pair<const char*, float> { "center", 0.f }, std::pair<const char*, float> { "left", offset }, std::pair<const char*, float> { "right", -offset } })
               {
                  if (offset < 2.f && lat != 0.f)
                     continue;
                  const Vertex3Ds ballCenter(c.x + left.x * lat, c.y + left.y * lat, c.z + ballRadius);
                  for (const auto& [part, partColliders] : colliders)
                  {
                     if (part == p.m_ptable || part == g.ramp || isPlayfield(part) || !nearPoint(boxes[part], ballCenter, ballRadius))
                        continue;
                     const PartDistance d = DistanceToPart(part, partColliders, ballCenter, ballRadius - 0.5f);
                     if (d.distance > ballRadius - 0.5f || d.volume)
                        continue;
                     if (const RampGeom* other = geomOf(part); other && nearJoin(g, *other, ballCenter))
                        continue;
                     const float penetration = ballRadius - d.distance;
                     if (Worst& w = worst[part]; penetration > w.penetration)
                        w = Worst { penetration, ballCenter, along, side };
                  }
               }
               for (int sideIdx = 0; sideIdx < 2; sideIdx++)
               {
                  const float sign = sideIdx == 0 ? 1.f : -1.f;
                  Gap& gap = current[sideIdx];
                  bool wedge = false;
                  float clearance = 0.f;
                  string surface;
                  if (c.z > 28.f)
                  {
                     const float out = half + ballRadius + 1.f;
                     float surfaceZ = 0.f;
                     surface = surfaceBelow(g, c.x + left.x * sign * out, c.y + left.y * sign * out, c.z - 1.f, surfaceZ);
                     clearance = c.z - surfaceZ;
                     if (clearance > 28.f && clearance < 2.f * ballRadius + 2.f)
                     {
                        const Vertex3Ds edge(c.x + left.x * sign * half, c.y + left.y * sign * half, surfaceZ + clearance * 0.5f);
                        bool blocked = false;
                        for (const auto& [part, partColliders] : colliders)
                        {
                           if (part == p.m_ptable || part == g.ramp || isPlayfield(part) || !nearPoint(boxes[part], edge, ballRadius))
                              continue;
                           if (const PartDistance d = DistanceToPart(part, partColliders, edge, ballRadius); d.distance <= ballRadius && !d.volume)
                           {
                              blocked = true;
                              break;
                           }
                        }
                        wedge = !blocked;
                     }
                  }
                  if (wedge)
                  {
                     if (!gap.open)
                        gap = Gap { sideIdx == 0 ? "left" : "right", c, c, clearance, clearance, 0.f, surface, true };
                     else
                        gap.length += (c - gap.to).Length();
                     gap.to = c;
                     gap.minClear = min(gap.minClear, clearance);
                     gap.maxClear = max(gap.maxClear, clearance);
                     if (gap.surface != surface)
                        gap.surface = "several surfaces"s;
                  }
                  else
                     closeGap(gap);
               }
            }
            walked += segLen;
         }
         closeGap(current[0]);
         closeGap(current[1]);
         vector<std::pair<IEditable*, Worst>> hits(worst.begin(), worst.end());
         std::ranges::sort(hits, [](const auto& x, const auto& y) { return x.second.penetration > y.second.penetration; });
         for (const auto& [part, w] : hits)
         {
            pathClearance.push_back({ { "ramp", g.name }, { "part", part->GetName() }, { "penetration", Round(w.penetration) }, { "at", { Round(w.at.x), Round(w.at.y), Round(w.at.z) } }, { "along", Round(w.along) }, { "ofLength", Round(total) }, { "side", w.side } });
            warnings.push_back(std::format("{}: {} cuts {:.1f} into a ball riding the ramp ({} of the path) {:.0f} units in of {:.0f}, at ({:.0f}, {:.0f}, {:.0f})", g.name, part->GetName(), w.penetration, w.side, w.along, total, w.at.x, w.at.y, w.at.z));
         }
         for (const Gap& gap : gaps)
         {
            underside.push_back({ { "ramp", g.name }, { "side", gap.side }, { "from", { Round(gap.from.x), Round(gap.from.y), Round(gap.from.z) } }, { "to", { Round(gap.to.x), Round(gap.to.y), Round(gap.to.z) } }, { "length", Round(gap.length) },
               { "floorAbove", { Round(gap.minClear), Round(gap.maxClear) } }, { "over", gap.surface } });
            warnings.push_back(std::format("{} {} edge: the floor runs {:.0f} to {:.0f} above {} for {:.0f} units from ({:.0f}, {:.0f}) with nothing under the edge: a ball can wedge under it (add an invisible skirt wall below the edge)", g.name, gap.side,
               gap.minClear, gap.maxClear, gap.surface, gap.length, gap.from.x, gap.from.y));
         }
      }
   }
   return json { { "ramps", list }, { "joins", joins }, { "surfaceEnds", surfaceEnds }, { "pathClearance", pathClearance }, { "underside", underside }, { "wiresInBallPath", wiresInPath }, { "wireCrossings", wireCrossings }, { "warnings", warnings },
      { "note", "Wire ramps collide as flat walls at the collision width (wire_distance_x + 20), the drawn wires sit at wire_distance_x; floor and upper wires are what you see. pathClearance: a ball swept along each ramp (centerline and against each wall, every 10 units) intersecting another part's collision shape (walls, posts, other ramps, primitives: penetration in units, side left/right seen in the direction of travel). underside: stretches where the ramp floor is 28 to 52 above the surface beside it with nothing under its edge, so a ball rolling against the edge wedges under the ramp. wiresInBallPath: a wire of one ramp within a ball's reach while it rides another (clearance < 0: drawn through the ball), measured from the whole collision width of the other ramp. Only ballPath ramps (collidable, playfield space, not a single wire guide rail) count as paths for joins, surfaceEnds, pathClearance, underside and wiresInBallPath; joins compare the ramp centerline ends; surfaceEnds probes down just past every other ramp end for the surface there (collision shapes, the playfield at its mesh or 0) and reports the height step; parts near a join are excluded from the wire and clearance checks." } };
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

static void PartOutlinePoints(IEditable* part, vector<Vertex3Ds>& points)
{
   if (part->GetItemType() != eItemFlasher)
   {
      part->GetBoundingVertices(points, nullptr);
      return;
   }
   const Flasher* const flasher = static_cast<Flasher*>(part);
   vector<RenderVertex> outline;
   flasher->m_curve.GetRgVertex(outline);
   const Vertex2D& center = flasher->m_curve.GetCenter();
   const Matrix3D transform = Matrix3D::MatrixTranslate(-center.x, -center.y, 0.f)
      * (((Matrix3D::MatrixRotateZ(ANGTORAD(flasher->m_d.m_rotZ)) * Matrix3D::MatrixRotateY(ANGTORAD(flasher->m_d.m_rotY))) * Matrix3D::MatrixRotateX(ANGTORAD(flasher->m_d.m_rotX)))
         * Matrix3D::MatrixTranslate(center.x, center.y, flasher->m_d.m_height));
   const bool cabinetSpace = part->GetPartGroup() != nullptr && part->GetPartGroup()->GetReferenceSpace() != PartGroupData::SpaceReference::SR_PLAYFIELD;
   const Matrix3D cab = cabinetSpace ? part->GetPTable()->GetDefaultPlayfieldToCabMatrix() : Matrix3D::MatrixIdentity();
   for (const RenderVertex& v : outline)
      points.push_back(cab * (transform * Vertex3Ds(v.x, v.y, 0.f)));
}

json PartBoundsJson(IEditable* part)
{
   vector<Vertex3Ds> points;
   const char* source = "mesh";
   vector<unsigned int> indices;
   if (string error; !PartWorldMesh(part, points, indices, error))
   {
      PartOutlinePoints(part, points);
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
   return json { { "min", { Round(lo.x), Round(lo.y), Round(lo.z) } }, { "max", { Round(hi.x), Round(hi.y), Round(hi.z) } }, { "size", { Round(hi.x - lo.x), Round(hi.y - lo.y), Round(hi.z - lo.z) } }, { "from", source } };
}

static int FlasherTriangleCount(const Flasher* flasher, const vector<RenderVertex>& outline)
{
   const size_t count = flasher->m_d.m_renderMode != FlasherData::RenderMode::FLASHER ? min(static_cast<size_t>(4), outline.size()) : outline.size();
   if (count < 3)
      return 0;
   vector<unsigned int> poly(count);
   for (unsigned int i = 0; i < count; i++)
      poly[i] = i;
   vector<WORD> tris;
   PolygonToTriangles(outline, poly, tris, false);
   return static_cast<int>(tris.size() / 3);
}

json FlasherMeshJson(const Flasher* flasher)
{
   vector<RenderVertex> outline;
   flasher->m_curve.GetRgVertex(outline);
   float area = 0.f;
   for (size_t i = 0; i < outline.size(); i++)
   {
      const RenderVertex& a = outline[i];
      const RenderVertex& b = outline[(i + 1) % outline.size()];
      area += a.x * b.y - b.x * a.y;
   }
   const int triangles = FlasherTriangleCount(flasher, outline);
   json j { { "triangles", triangles }, { "winding", area < 0.f ? "ccw" : "cw" } };
   if (triangles == 0)
   {
      std::ranges::reverse(outline);
      j["warning"] = FlasherTriangleCount(flasher, outline) > 0
         ? "draws nothing: the points run clockwise. A flasher needs them counter-clockwise seen from above (top-left, bottom-left, bottom-right, top-right); vpx_part points with the same points fixes the order"s
         : "draws nothing: the outline does not triangulate (fewer than 3 points, zero area or self-intersecting)"s;
   }
   return j;
}

bool FixFlasherWinding(IEditable* part)
{
   if (part->GetItemType() != eItemFlasher)
      return false;
   Flasher* const flasher = static_cast<Flasher*>(part);
   vector<RenderVertex> outline;
   flasher->m_curve.GetRgVertex(outline);
   if (FlasherTriangleCount(flasher, outline) > 0)
      return false;
   std::ranges::reverse(outline);
   if (FlasherTriangleCount(flasher, outline) == 0)
      return false;
   flasher->m_curve.ReverseOrder();
   return true;
}

static const char* PlayModeName(unsigned int mask)
{
   switch (mask)
   {
   case PartGroupData::PlayerModeVisibilityMask::PMVM_DESKTOP: return "desktop";
   case PartGroupData::PlayerModeVisibilityMask::PMVM_FSS: return "fss";
   case PartGroupData::PlayerModeVisibilityMask::PMVM_CABINET: return "cabinet";
   case PartGroupData::PlayerModeVisibilityMask::PMVM_MIXED_REALITY: return "mr";
   case PartGroupData::PlayerModeVisibilityMask::PMVM_VIRTUAL_REALITY: return "vr";
   default: return "unknown";
   }
}

json PartVisibilityJson(Player& p, IEditable* part)
{
   json j { { "name", part->GetName() }, { "type", ItemTypeName(part->GetItemType()) } };
   json reasons = json::array();
   string error;
   const nlohmann::ordered_json doc = PartDoc(part, error);
   if (doc.contains("visible") && doc["visible"].is_boolean())
   {
      j["visible"] = doc["visible"].get<bool>();
      if (!doc["visible"].get<bool>())
         reasons.push_back("visible is false");
   }
   if (part->GetIRenderable() == nullptr)
      reasons.push_back("this part type is not rendered");

   const ViewSetupID viewMode = p.m_ptable->GetViewMode();
   const unsigned int modeMask = p.m_vrDevice ? (p.m_renderer->m_vrApplyColorKey ? PartGroupData::PlayerModeVisibilityMask::PMVM_MIXED_REALITY : PartGroupData::PlayerModeVisibilityMask::PMVM_VIRTUAL_REALITY)
      : viewMode == BG_FULLSCREEN ? PartGroupData::PlayerModeVisibilityMask::PMVM_CABINET
      : viewMode == BG_FSS        ? PartGroupData::PlayerModeVisibilityMask::PMVM_FSS
                                  : PartGroupData::PlayerModeVisibilityMask::PMVM_DESKTOP;
   j["playMode"] = PlayModeName(modeMask);
   const PartGroup* const group = part->GetPartGroup();
   if (group)
   {
      const unsigned int mask = group->GetPlayerModeVisibilityMask();
      json modes = json::array();
      for (const unsigned int m : { PartGroupData::PlayerModeVisibilityMask::PMVM_DESKTOP, PartGroupData::PlayerModeVisibilityMask::PMVM_FSS, PartGroupData::PlayerModeVisibilityMask::PMVM_CABINET,
              PartGroupData::PlayerModeVisibilityMask::PMVM_MIXED_REALITY, PartGroupData::PlayerModeVisibilityMask::PMVM_VIRTUAL_REALITY })
         if (mask & m)
            modes.push_back(PlayModeName(m));
      static const char* const spaces[] = { "playfield", "cabinet", "cabinet feet", "room", "inherit" };
      const int space = static_cast<int>(group->GetReferenceSpace());
      j["group"] = json { { "name", group->GetName() }, { "shownInModes", modes }, { "shownInPlayMode", (mask & modeMask) != 0 }, { "space", space >= 0 && space < 5 ? spaces[space] : "unknown" } };
      if ((mask & modeMask) == 0)
         reasons.push_back("its group '"s + group->GetName() + "' is hidden in " + PlayModeName(modeMask) + " mode (player_mode_visibility_mask of the group or one of its parents)");
   }
   if (part->m_desktopBackdrop)
   {
      j["desktopBackdrop"] = true;
      if (modeMask != PartGroupData::PlayerModeVisibilityMask::PMVM_DESKTOP && modeMask != PartGroupData::PlayerModeVisibilityMask::PMVM_FSS)
         reasons.push_back("it is a desktop backdrop part: those are not drawn in cabinet, VR and MR modes");
   }
   if (p.m_liveUI && p.m_liveUI->IsEditorUIOpened() && !part->IsUIVisible(true))
   {
      j["editorVisible"] = false;
      reasons.push_back("hidden in the editor (ui_visible of the part or its group): the editor views do not draw it, play does");
   }

   vector<Vertex3Ds> points;
   vector<unsigned int> indices;
   if (string meshError; !PartWorldMesh(part, points, indices, meshError))
      PartOutlinePoints(part, points);
   if (json bounds = PartBoundsJson(part); !bounds.is_null())
      j["bounds"] = bounds;
   else
      reasons.push_back("it has no geometry");

   const RenderView* const view = p.m_renderer->GetPrimaryView();
   if (view && !points.empty() && !part->m_desktopBackdrop)
   {
      const Matrix3D& m = view->GetMVP().GetModelViewProj(0);
      const float width = static_cast<float>(view->GetDisplayWidth()), height = static_cast<float>(view->GetDisplayHeight());
      float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
      size_t behind = 0;
      for (const Vertex3Ds& v : points)
      {
         const float xp = m._11 * v.x + m._21 * v.y + m._31 * v.z + m._41;
         const float yp = m._12 * v.x + m._22 * v.y + m._32 * v.z + m._42;
         const float wp = m._14 * v.x + m._24 * v.y + m._34 * v.z + m._44;
         if (wp <= 1e-4f)
         {
            behind++;
            continue;
         }
         const float sx = (xp / wp * 0.5f + 0.5f) * width, sy = (0.5f - yp / wp * 0.5f) * height;
         x0 = min(x0, sx);
         y0 = min(y0, sy);
         x1 = max(x1, sx);
         y1 = max(y1, sy);
      }
      json screen { { "viewport", { Round(width, 0), Round(height, 0) } }, { "camera", "the player camera of this session (not the editor panes)" } };
      if (behind == points.size())
      {
         screen["onScreen"] = "off";
         screen["cropped"] = json::array({ "behind the camera" });
         reasons.push_back("it is behind the player camera");
      }
      else
      {
         screen["rect"] = { Round(x0, 0), Round(y0, 0), Round(x1, 0), Round(y1, 0) };
         json cropped = json::array();
         if (x0 < 0.f)
            cropped.push_back("left");
         if (y0 < 0.f)
            cropped.push_back("top");
         if (x1 > width)
            cropped.push_back("right");
         if (y1 > height)
            cropped.push_back("bottom");
         if (behind > 0)
            cropped.push_back("partly behind the camera");
         const bool off = x1 < 0.f || y1 < 0.f || x0 > width || y0 > height;
         screen["onScreen"] = off ? "off" : cropped.empty() ? "full" : "partial";
         if (!cropped.empty())
            screen["cropped"] = cropped;
         if (off)
            reasons.push_back("it is outside the player camera's frame");
         else if (!cropped.empty())
            reasons.push_back("the player camera crops it ("s + cropped.dump() + ")");
      }
      j["screen"] = screen;
   }

   if (part->GetItemType() == eItemFlasher)
   {
      const Flasher* const flasher = static_cast<Flasher*>(part);
      const FlasherData& d = flasher->m_d;
      json mesh = FlasherMeshJson(flasher);
      if (mesh.contains("warning"))
         reasons.push_back(mesh["warning"]);
      j["flasher"] = mesh;
      if (d.m_color == 0)
         reasons.push_back("color is black: the flasher is skipped");
      if (d.m_alpha == 0)
         reasons.push_back("alpha is 0: the flasher is skipped");
      if (d.m_intensity_scale == 0.f)
         reasons.push_back("intensity_scale is 0: the flasher is skipped");
      if (!d.m_szLightmap.empty())
         j["lightmap"] = json { { "light", d.m_szLightmap }, { "note", "the flasher's opacity follows this light: it is dark while the light is off" } };
      if (!d.m_stateSrcLink.empty())
      {
         float state = 0.f;
         const bool resolved = p.m_resURIResolver.TryGetFloatState(d.m_stateSrcLink, state);
         j["stateLink"] = json { { "link", d.m_stateSrcLink }, { "resolves", resolved }, { "value", Round(state, 3) } };
         if (resolved && state == 0.f)
            reasons.push_back("its state link reads 0: the flasher is dark");
      }
      const Matrix3D rotation = (Matrix3D::MatrixRotateZ(ANGTORAD(d.m_rotZ)) * Matrix3D::MatrixRotateY(ANGTORAD(d.m_rotY))) * Matrix3D::MatrixRotateX(ANGTORAD(d.m_rotX));
      const Vertex3Ds normal = rotation.MultiplyVectorNoTranslate(Vertex3Ds(0.f, 0.f, 1.f));
      if (view)
      {
         const vec4& eye = view->GetMVP().GetCameraPos(0);
         const Vertex2D& center = flasher->m_curve.GetCenter();
         const float facing = normal.x * (eye.x - center.x) + normal.y * (eye.y - center.y) + normal.z * (eye.z - d.m_height);
         j["facing"] = json { { "normal", { Round(normal.x, 2), Round(normal.y, 2), Round(normal.z, 2) } }, { "side", facing >= 0.f ? "front" : "back" },
            { "note", "flashers are drawn two-sided: the back shows too, with the image mirrored" } };
      }
      const bool playing = !p.IsEditorMode();
      switch (d.m_renderMode)
      {
      case FlasherData::RenderMode::DMD:
      case FlasherData::RenderMode::DISPLAY:
      {
         const bool dmd = d.m_renderMode == FlasherData::RenderMode::DMD;
         PinballPlugin::ResURIResolver::DisplayState display { nullptr };
         if (!d.m_imageSrcLink.empty())
            display = p.m_resURIResolver.GetDisplayState(d.m_imageSrcLink);
         bool usedDefault = false;
         if (dmd && display.state.frame == nullptr)
         {
            display = p.m_resURIResolver.GetDisplayState(PinballPlugin::ResURIResolver::DefaultDmdUri);
            usedDefault = true;
         }
         json info { { "mode", dmd ? "dmd" : "display" }, { "link", d.m_imageSrcLink }, { "resolves", display.source != nullptr }, { "hasFrame", display.state.frame != nullptr } };
         if (usedDefault)
            info["usedDefaultDmd"] = true;
         if (display.source)
            info["source"] = json { { "endpointId", display.source->id.endpointId }, { "resId", display.source->id.resId }, { "width", display.source->width }, { "height", display.source->height } };
         if (display.state.frame)
            info["frameId"] = display.state.frameId;
         else
         {
            info["sources"] = p.m_resURIResolver.DumpDisplaySources();
            reasons.push_back(!playing ? "display sources only exist while a game runs: check again during a playtest"s
                  : display.source ? "the display source resolves but has not produced a frame yet"s
                                   : "image_src_link does not resolve to a display source (see display.sources: is the plugin enabled, was the display created by the script?)"s);
         }
         j["display"] = info;
         break;
      }
      case FlasherData::RenderMode::ALPHASEG:
      {
         const PinballPlugin::ResURIResolver::SegDisplayState segs = p.m_resURIResolver.GetSegDisplayState(d.m_imageSrcLink);
         const bool ok = segs.state.frame != nullptr && segs.source != nullptr && segs.source->nElements != 0;
         j["display"] = json { { "mode", "alphaseg" }, { "link", d.m_imageSrcLink }, { "resolves", segs.source != nullptr }, { "hasFrame", segs.state.frame != nullptr }, { "elements", segs.source ? segs.source->nElements : 0 } };
         if (!ok)
            reasons.push_back(playing ? "image_src_link does not resolve to a segment display with a frame" : "segment displays only exist while a game runs: check again during a playtest");
         break;
      }
      case FlasherData::RenderMode::EXT_RENDER:
      {
         static const char* const windows[] = { "playfield", "backglass", "score view", "topper" };
         const int wnd = clamp(d.m_renderStyle, 1, 3);
         const size_t renderers = p.m_ancillaryWndRenderers[wnd].size();
         j["extRender"] = json { { "window", windows[wnd] }, { "renderStyle", d.m_renderStyle }, { "renderers", renderers } };
         if (renderers == 0)
            reasons.push_back("no renderer is registered for the "s + windows[wnd] + " window: nothing draws into this flasher (a plugin such as B2S, PUP, ScoreView or WebView.ShowScreen has to)");
         break;
      }
      default: break;
      }
   }
   j["likelyHidden"] = !reasons.empty();
   j["reasons"] = reasons;
   return j;
}

}
