// license:GPLv3+

#include "core/stdafx.h"
#include "ui/live/editor/SketchStore.h"
#include "parts/pintable.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace VPX::Sketch
{

using json = nlohmann::json;

namespace
{

constexpr float PI_F = 3.14159265358979f;

constexpr const char* TYPE_NAMES[] = { "stroke", "arrow", "circle", "box", "text", "arc" };
constexpr const char* KIND_NAMES[KIND_COUNT] = { "note", "path", "ramp", "wireform", "wall", "target", "hole", "problem" };
constexpr const char* KIND_DESCRIPTIONS[KIND_COUNT] = {
   "general comment (grey)",
   "playfield path: where the ball rolls on the playfield, in drawing order (green)",
   "ramp: plastic ramp off the playfield, in drawing order (blue)",
   "wireform: wire ramp or habitrail, in drawing order (purple)",
   "wall / guide: a solid edge, rail or post (white)",
   "target: something to hit, e.g. drop bank, standups, spinner, captive ball (yellow)",
   "hole: scoop, saucer, kicker, sinkhole, VUK, anywhere the ball goes in (orange)",
   "problem: something wrong or to remove (red)" };
constexpr uint32_t KIND_COLORS[KIND_COUNT] = { 0x9A9A9A, 0x3CCB4E, 0x3C8CFF, 0xB066FF, 0xF2F2F2, 0xFFD23C, 0xFF9A2E, 0xFF4040 };

float Round2(float f) { return static_cast<float>(std::strtod(std::format("{:.2f}", f).c_str(), nullptr)); }

std::string NowString()
{
   const std::time_t t = std::time(nullptr);
   std::tm tm {};
#ifdef _WIN32
   localtime_s(&tm, &t);
#else
   localtime_r(&t, &tm);
#endif
   char buf[32];
   std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
   return buf;
}

float Dist(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }

float DistToSegment(Point p, Point a, Point b)
{
   const float dx = b.x - a.x, dy = b.y - a.y;
   const float len2 = dx * dx + dy * dy;
   float t = len2 > 0.f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.f;
   t = std::clamp(t, 0.f, 1.f);
   return Dist(p, Point { a.x + t * dx, a.y + t * dy });
}

float NormalizeDeg(float d)
{
   while (d < 0.f)
      d += 360.f;
   while (d >= 360.f)
      d -= 360.f;
   return d;
}

json PointJson(Point p) { return json::array({ Round2(p.x), Round2(p.y) }); }

bool ReadPoint(const json& j, Point& p)
{
   if (j.is_array() && j.size() >= 2 && j[0].is_number() && j[1].is_number())
   {
      p = { j[0].get<float>(), j[1].get<float>() };
      return true;
   }
   if (j.is_object() && j.contains("x") && j.contains("y") && j["x"].is_number() && j["y"].is_number())
   {
      p = { j["x"].get<float>(), j["y"].get<float>() };
      return true;
   }
   return false;
}

}

const char* TypeName(MarkType type) { return TYPE_NAMES[static_cast<int>(type)]; }

bool TypeFromName(const std::string& name, MarkType& type)
{
   for (int i = 0; i < 6; i++)
      if (name == TYPE_NAMES[i])
      {
         type = static_cast<MarkType>(i);
         return true;
      }
   return false;
}

const char* KindName(MarkKind kind) { return KIND_NAMES[static_cast<int>(kind)]; }

bool KindFromName(const std::string& name, MarkKind& kind)
{
   for (int i = 0; i < KIND_COUNT; i++)
      if (name == KIND_NAMES[i])
      {
         kind = static_cast<MarkKind>(i);
         return true;
      }
   return false;
}

const char* KindDescription(MarkKind kind) { return KIND_DESCRIPTIONS[static_cast<int>(kind)]; }

uint32_t KindColor(MarkKind kind) { return KIND_COLORS[static_cast<int>(kind)]; }

bool PointCountValid(MarkType type, size_t count)
{
   switch (type)
   {
   case MarkType::Stroke: return count >= 2;
   case MarkType::Arrow:
   case MarkType::Circle:
   case MarkType::Box: return count == 2;
   case MarkType::Text: return count == 1;
   case MarkType::Arc: return count >= 3 && (count % 2) == 1;
   }
   return false;
}

ArcInfo ComputeArc(Point a, Point b, Point c)
{
   ArcInfo arc;
   const float d = 2.f * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
   if (std::abs(d) < 1e-3f)
      return arc;
   const float a2 = a.x * a.x + a.y * a.y, b2 = b.x * b.x + b.y * b.y, c2 = c.x * c.x + c.y * c.y;
   arc.center.x = (a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d;
   arc.center.y = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d;
   arc.radius = Dist(arc.center, a);
   if (!(arc.radius < 1e6f))
      return arc;
   const float sa = std::atan2(a.y - arc.center.y, a.x - arc.center.x) * 180.f / PI_F;
   const float ec = std::atan2(c.y - arc.center.y, c.x - arc.center.x) * 180.f / PI_F;
   // Direction a -> b -> c: sign of the turn
   const float cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
   float sweep = NormalizeDeg(ec - sa);
   if (cross < 0.f)
      sweep = sweep > 0.f ? sweep - 360.f : 0.f;
   arc.startDeg = sa;
   arc.endDeg = ec;
   arc.sweepDeg = sweep;
   arc.length = arc.radius * std::abs(sweep) * PI_F / 180.f;
   arc.valid = true;
   return arc;
}

std::vector<Point> Flatten(const Mark& mark)
{
   std::vector<Point> out;
   const auto& pts = mark.points;
   switch (mark.type)
   {
   case MarkType::Stroke:
   case MarkType::Arrow:
   case MarkType::Text: out = pts; break;
   case MarkType::Box:
      if (pts.size() == 2)
         out = { pts[0], { pts[1].x, pts[0].y }, pts[1], { pts[0].x, pts[1].y }, pts[0] };
      break;
   case MarkType::Circle:
      if (pts.size() == 2)
      {
         const float r = Dist(pts[0], pts[1]);
         for (int i = 0; i <= 48; i++)
         {
            const float a = 2.f * PI_F * static_cast<float>(i) / 48.f;
            out.push_back({ pts[0].x + r * std::cos(a), pts[0].y + r * std::sin(a) });
         }
      }
      break;
   case MarkType::Arc:
      if (pts.size() >= 3)
      {
         out.push_back(pts[0]);
         for (size_t i = 0; i + 2 < pts.size(); i += 2)
         {
            const ArcInfo arc = ComputeArc(pts[i], pts[i + 1], pts[i + 2]);
            if (!arc.valid)
            {
               out.push_back(pts[i + 2]);
               continue;
            }
            const int n = std::max(2, static_cast<int>(std::ceil(std::abs(arc.sweepDeg) / 5.f)));
            for (int k = 1; k < n; k++)
            {
               const float a = (arc.startDeg + arc.sweepDeg * static_cast<float>(k) / static_cast<float>(n)) * PI_F / 180.f;
               out.push_back({ arc.center.x + arc.radius * std::cos(a), arc.center.y + arc.radius * std::sin(a) });
            }
            out.push_back(pts[i + 2]);
         }
      }
      break;
   }
   return out;
}

void Bounds(const Mark& mark, Point& min, Point& max)
{
   min = { FLT_MAX, FLT_MAX };
   max = { -FLT_MAX, -FLT_MAX };
   for (const Point& p : Flatten(mark))
   {
      min = { std::min(min.x, p.x), std::min(min.y, p.y) };
      max = { std::max(max.x, p.x), std::max(max.y, p.y) };
   }
}

float DistanceTo(const Mark& mark, Point p)
{
   const auto& pts = mark.points;
   if (mark.type == MarkType::Box && pts.size() == 2)
   {
      const float x0 = std::min(pts[0].x, pts[1].x), x1 = std::max(pts[0].x, pts[1].x);
      const float y0 = std::min(pts[0].y, pts[1].y), y1 = std::max(pts[0].y, pts[1].y);
      const float dx = std::max({ x0 - p.x, 0.f, p.x - x1 }), dy = std::max({ y0 - p.y, 0.f, p.y - y1 });
      return std::hypot(dx, dy);
   }
   if (mark.type == MarkType::Circle && pts.size() == 2)
      return std::max(0.f, Dist(p, pts[0]) - Dist(pts[0], pts[1]));
   const std::vector<Point> poly = Flatten(mark);
   if (poly.empty())
      return FLT_MAX;
   if (poly.size() == 1)
      return Dist(p, poly[0]);
   float best = FLT_MAX;
   for (size_t i = 0; i + 1 < poly.size(); i++)
      best = std::min(best, DistToSegment(p, poly[i], poly[i + 1]));
   return best;
}

json MarkToJson(const Mark& mark, bool derived)
{
   json pts = json::array();
   for (const Point& p : mark.points)
      pts.push_back(PointJson(p));
   json j { { "id", mark.id }, { "type", TypeName(mark.type) }, { "kind", KindName(mark.kind) }, { "points", pts }, { "label", mark.label },
      { "author", mark.author }, { "status", mark.done ? "done" : "open" }, { "created", mark.created }, { "seq", mark.seq } };
   if (mark.request)
      j["request"] = true;
   if (!mark.part.empty())
      j["part"] = mark.part;
   if (!mark.view.is_null())
      j["view"] = mark.view;
   if (!std::isnan(mark.heightStart))
      j["heightStart"] = Round2(mark.heightStart);
   if (!std::isnan(mark.heightEnd))
      j["heightEnd"] = Round2(mark.heightEnd);
   if (derived)
   {
      Point mn, mx;
      Bounds(mark, mn, mx);
      if (mn.x <= mx.x)
         j["bounds"] = json { { "min", PointJson(mn) }, { "max", PointJson(mx) } };
      if (mark.type == MarkType::Circle && mark.points.size() == 2)
         j["radius"] = Round2(Dist(mark.points[0], mark.points[1]));
      if (mark.type == MarkType::Box && mark.points.size() == 2)
         j["size"] = PointJson({ std::abs(mark.points[1].x - mark.points[0].x), std::abs(mark.points[1].y - mark.points[0].y) });
      if (mark.type == MarkType::Arc && mark.points.size() >= 3)
      {
         json arcs = json::array();
         float total = 0.f;
         for (size_t i = 0; i + 2 < mark.points.size(); i += 2)
         {
            const ArcInfo a = ComputeArc(mark.points[i], mark.points[i + 1], mark.points[i + 2]);
            if (!a.valid)
            {
               arcs.push_back(json { { "straight", true }, { "from", PointJson(mark.points[i]) }, { "to", PointJson(mark.points[i + 2]) } });
               total += Dist(mark.points[i], mark.points[i + 2]);
               continue;
            }
            total += a.length;
            arcs.push_back(json { { "center", PointJson(a.center) }, { "radius", Round2(a.radius) }, { "startDeg", Round2(a.startDeg) }, { "endDeg", Round2(a.endDeg) },
               { "sweepDeg", Round2(a.sweepDeg) }, { "length", Round2(a.length) } });
         }
         j["arcs"] = arcs;
         j["length"] = Round2(total);
      }
      else if (mark.type == MarkType::Stroke || mark.type == MarkType::Arrow)
      {
         float total = 0.f;
         for (size_t i = 0; i + 1 < mark.points.size(); i++)
            total += Dist(mark.points[i], mark.points[i + 1]);
         j["length"] = Round2(total);
      }
   }
   return j;
}

std::string MarkFromJson(const json& j, Mark& mark)
{
   if (!j.is_object())
      return "A mark must be an object";
   if (j.contains("type"))
   {
      if (!j["type"].is_string() || !TypeFromName(j["type"].get<std::string>(), mark.type))
         return "Unknown type, use stroke | arrow | circle | box | text | arc";
   }
   if (j.contains("kind"))
   {
      if (!j["kind"].is_string() || !KindFromName(j["kind"].get<std::string>(), mark.kind))
         return "Unknown kind, use note | path | ramp | wireform | wall | target | hole | problem";
   }
   if (j.contains("points"))
   {
      if (!j["points"].is_array())
         return "points must be an array of [x, y]";
      std::vector<Point> pts;
      for (const json& e : j["points"])
      {
         Point p;
         if (!ReadPoint(e, p))
            return "Each point must be [x, y] or {x, y}";
         pts.push_back(p);
      }
      mark.points = std::move(pts);
   }
   if (!PointCountValid(mark.type, mark.points.size()))
   {
      switch (mark.type)
      {
      case MarkType::Stroke: return "A stroke needs at least 2 points";
      case MarkType::Arrow: return "An arrow needs exactly 2 points (tail, head)";
      case MarkType::Circle: return "A circle needs exactly 2 points (center, a point on the rim)";
      case MarkType::Box: return "A box needs exactly 2 points (opposite corners)";
      case MarkType::Text: return "A text pin needs exactly 1 point";
      case MarkType::Arc: return "An arc needs 2n+1 points: start, then the middle and end point of each arc (3 points for one arc, 5 for two chained arcs...)";
      }
   }
   if (j.contains("label") && j["label"].is_string())
      mark.label = j["label"].get<std::string>();
   if (j.contains("author") && j["author"].is_string())
      mark.author = j["author"].get<std::string>();
   if (j.contains("status") && j["status"].is_string())
      mark.done = j["status"].get<std::string>() == "done";
   if (j.contains("done") && j["done"].is_boolean())
      mark.done = j["done"].get<bool>();
   if (j.contains("request") && j["request"].is_boolean())
      mark.request = j["request"].get<bool>();
   if (j.contains("part") && j["part"].is_string())
      mark.part = j["part"].get<std::string>();
   if (j.contains("view") && (j["view"].is_object() || j["view"].is_null()))
      mark.view = j["view"];
   if (j.contains("heightStart"))
      mark.heightStart = j["heightStart"].is_number() ? j["heightStart"].get<float>() : NAN;
   if (j.contains("heightEnd"))
      mark.heightEnd = j["heightEnd"].is_number() ? j["heightEnd"].get<float>() : NAN;
   return {};
}

///////////////////////////////////////////////////////////////////////////////

std::string SidecarKey(const PinTable*& table, const char* extension, std::filesystem::path& path)
{
   if (table != nullptr && table->m_liveBaseTable != nullptr)
      table = table->m_liveBaseTable;
   if (table == nullptr || table->m_filename.empty())
      return std::format("memory:{}", static_cast<const void*>(table));
   path = table->m_filename;
   path.replace_extension(extension);
   return path.string();
}

Store& Store::For(const PinTable* table)
{
   static std::map<std::string, std::unique_ptr<Store>> stores;
   std::filesystem::path path;
   const std::string key = SidecarKey(table, ".notes.json", path);
   auto it = stores.find(key);
   if (it == stores.end())
   {
      it = stores.emplace(key, std::make_unique<Store>()).first;
      it->second->m_path = path;
      it->second->Load();
   }
   return *it->second;
}

void Store::Load()
{
   m_marks.clear();
   m_seq = 0;
   m_nextId = 1;
   if (m_path.empty() || !std::filesystem::exists(m_path))
      return;
   try
   {
      std::ifstream in(m_path, std::ios::binary);
      std::stringstream ss;
      ss << in.rdbuf();
      const json doc = json::parse(ss.str(), nullptr, false);
      if (!doc.is_object())
      {
         PLOGE << "Sketch notes file is not valid JSON: " << m_path.string();
         return;
      }
      if (doc.contains("seq") && doc["seq"].is_number_unsigned())
         m_seq = doc["seq"].get<uint64_t>();
      if (doc.contains("marks") && doc["marks"].is_array())
         for (const json& e : doc["marks"])
         {
            Mark mark;
            const std::string err = MarkFromJson(e, mark);
            if (!err.empty())
            {
               PLOGW << "Sketch notes: skipped a mark (" << err << ")";
               continue;
            }
            mark.id = e.value("id", m_nextId);
            mark.created = e.value("created", std::string());
            mark.seq = e.value("seq", static_cast<uint64_t>(0));
            m_seq = std::max(m_seq, mark.seq);
            m_nextId = std::max(m_nextId, mark.id + 1);
            m_marks.push_back(std::move(mark));
         }
   }
   catch (const std::exception& e)
   {
      PLOGE << "Sketch notes could not be read: " << e.what();
   }
}

void Store::Save()
{
   if (m_path.empty())
      return;
   try
   {
      const std::filesystem::path tmp = m_path.string() + ".tmp";
      {
         std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
         out << ToJson().dump(1, '\t');
         if (!out)
         {
            PLOGE << "Sketch notes could not be written: " << tmp.string();
            return;
         }
      }
      std::filesystem::rename(tmp, m_path);
   }
   catch (const std::exception& e)
   {
      PLOGE << "Sketch notes could not be saved: " << e.what();
   }
}

json Store::ToJson() const
{
   json marks = json::array();
   for (const Mark& m : m_marks)
      marks.push_back(MarkToJson(m));
   return json { { "version", 1 }, { "seq", m_seq }, { "marks", marks } };
}

const Mark* Store::Find(int id) const
{
   for (const Mark& m : m_marks)
      if (m.id == id)
         return &m;
   return nullptr;
}

int Store::Add(Mark mark)
{
   mark.id = m_nextId++;
   if (mark.created.empty())
      mark.created = NowString();
   Touch(mark);
   const int id = mark.id;
   m_marks.push_back(std::move(mark));
   Save();
   return id;
}

bool Store::Replace(const Mark& mark, bool save)
{
   for (Mark& m : m_marks)
      if (m.id == mark.id)
      {
         const std::string created = m.created;
         m = mark;
         m.created = created;
         Touch(m);
         if (save)
            Save();
         return true;
      }
   return false;
}

bool Store::Remove(int id)
{
   const auto it = std::ranges::find_if(m_marks, [id](const Mark& m) { return m.id == id; });
   if (it == m_marks.end())
      return false;
   m_marks.erase(it);
   ++m_seq;
   Save();
   return true;
}

size_t Store::Clear(const std::string& author, bool onlyDone)
{
   const size_t before = m_marks.size();
   std::erase_if(m_marks, [&](const Mark& m) { return (author.empty() || m.author == author) && (!onlyDone || m.done); });
   const size_t removed = before - m_marks.size();
   if (removed > 0)
   {
      ++m_seq;
      Save();
   }
   return removed;
}

void Store::Restore(std::vector<Mark> marks)
{
   m_marks = std::move(marks);
   for (const Mark& m : m_marks)
      m_nextId = std::max(m_nextId, m.id + 1);
   ++m_seq;
   Save();
}

}

namespace VPX::Sketch
{

bool EndTangent(const Mark& mark, Point& end, Point& dir)
{
   if (mark.type != MarkType::Arc || mark.points.size() < 3)
      return false;
   const size_t n = mark.points.size();
   const Point a = mark.points[n - 3], b = mark.points[n - 2], c = mark.points[n - 1];
   const ArcInfo arc = ComputeArc(a, b, c);
   end = c;
   if (arc.valid)
   {
      const float rx = c.x - arc.center.x, ry = c.y - arc.center.y;
      dir = arc.sweepDeg >= 0.f ? Point { -ry, rx } : Point { ry, -rx };
   }
   else
      dir = { c.x - b.x, c.y - b.y };
   const float len = std::hypot(dir.x, dir.y);
   if (len < 1e-6f)
      return false;
   dir = { dir.x / len, dir.y / len };
   return true;
}

void TangentArcMid(Point p, Point dir, Point e, Point& mid)
{
   const Point n { -dir.y, dir.x };
   const float dx = e.x - p.x, dy = e.y - p.y;
   const float dlen2 = dx * dx + dy * dy;
   const float nd = n.x * dx + n.y * dy;
   if (dlen2 < 1e-6f || std::abs(nd) < 1e-3f * std::sqrt(dlen2))
   {
      mid = { (p.x + e.x) * 0.5f, (p.y + e.y) * 0.5f };
      return;
   }
   const float s = dlen2 / (2.f * nd); // signed: the center is p + n * s
   const Point c { p.x + n.x * s, p.y + n.y * s };
   const float r = std::abs(s);
   const float angP = std::atan2(p.y - c.y, p.x - c.x), angE = std::atan2(e.y - c.y, e.x - c.x);
   constexpr float TWO_PI = 2.f * PI_F;
   auto norm = [](float a) { while (a < 0.f) a += TWO_PI; while (a >= TWO_PI) a -= TWO_PI; return a; };
   const float sweep = s > 0.f ? norm(angE - angP) : -norm(angP - angE);
   const float am = angP + sweep * 0.5f;
   mid = { c.x + r * std::cos(am), c.y + r * std::sin(am) };
}

}
