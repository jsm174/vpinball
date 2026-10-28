// license:GPLv3+

#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

class PinTable;
class IEditable;

// Sketch / annotation layer: marks drawn on the playfield (by the user in the live editor, or by the assistant through the vpx_notes MCP tool).
// Marks live in a sidecar file next to the table (<table>.notes.json), in table units, independent of the .vpx file.
namespace VPX::Sketch
{
// The collaboration stores live beside the table file (<table>.<extension>, in memory for an unsaved table) and are shared by a
// playtest copy and its base table: resolves table to the base table and returns the store key, with the sidecar path when there is one
std::string SidecarKey(const PinTable*& table, const char* extension, std::filesystem::path& path);


enum class MarkType { Stroke, Arrow, Circle, Box, Text, Arc };

// What a mark means; the colour of a mark comes from its kind
enum class MarkKind { Note, Path, Ramp, Wireform, Wall, Target, Hole, Problem };

constexpr int KIND_COUNT = 8;

const char* TypeName(MarkType type);
bool TypeFromName(const std::string& name, MarkType& type);
const char* KindName(MarkKind kind);
bool KindFromName(const std::string& name, MarkKind& kind);
// What the kind means, for tool descriptions and the legend
const char* KindDescription(MarkKind kind);
// Display colour (0xRRGGBB)
uint32_t KindColor(MarkKind kind);
// Number of points a mark of this type needs: Stroke >= 2, Arrow 2, Circle 2 (center, point on the rim), Box 2 (corners), Text 1, Arc 2n+1 (start, then mid and end of every arc, a chain of arcs is continuous)
bool PointCountValid(MarkType type, size_t count);

struct Point
{
   float x = 0.f;
   float y = 0.f;
};

struct Mark
{
   int id = 0;
   MarkType type = MarkType::Stroke;
   MarkKind kind = MarkKind::Note;
   std::vector<Point> points;
   std::string label;
   std::string author = "user"; // user | assistant
   bool done = false;
   uint64_t seq = 0; // sequence number of the last change
   std::string created;
   // A request for the assistant (the user asked something here): the part it refers to (name) and the editor view when it was made
   bool request = false;
   std::string part;
   nlohmann::json view;
   // Optional heights for ramp / wireform marks (start, end); NaN when not set
   float heightStart = NAN;
   float heightEnd = NAN;
};

// A circular arc through three points
struct ArcInfo
{
   bool valid = false; // false when the three points are (almost) aligned
   Point center;
   float radius = 0.f;
   // Angles in degrees, atan2(dy, dx) in table coordinates (x right, y down the playfield towards the player): 0 = +x, 90 = towards the player
   float startDeg = 0.f;
   float endDeg = 0.f;
   float sweepDeg = 0.f; // signed, positive = increasing angle (clockwise as seen from above)
   float length = 0.f;
};

ArcInfo ComputeArc(Point a, Point b, Point c);
// Points of a mark flattened to a polyline (arcs sampled, circles as a ring), for hit tests and proximity
std::vector<Point> Flatten(const Mark& mark);
// Axis aligned bounds of a mark (arcs and circles included in full)
void Bounds(const Mark& mark, Point& min, Point& max);
// Distance from a point to a mark's drawn shape (0 inside a box or circle)
float DistanceTo(const Mark& mark, Point p);

class Store
{
public:
   // The store of a table (the live copy used in a playtest shares the store of its base table). Loaded from the sidecar file on first use.
   static Store& For(const PinTable* table);

   const std::vector<Mark>& Marks() const { return m_marks; }
   uint64_t Seq() const { return m_seq; }
   // Where the marks are saved; empty when the table is not saved to disk yet (marks then only live in this session)
   const std::filesystem::path& Path() const { return m_path; }

   const Mark* Find(int id) const;
   // Adds a mark (id and seq are assigned); returns its id
   int Add(Mark mark);
   // Replaces the mark with the same id (keeps its created time); false when unknown
   // save: false while a drag is in progress (the file is written when it ends)
   bool Replace(const Mark& mark, bool save = true);
   bool Remove(int id);
   // Removes all marks (optionally only the ones matching: author non-empty, or only done marks); returns how many
   size_t Clear(const std::string& author, bool onlyDone);
   // Undo support: restores a whole list of marks
   std::vector<Mark> Snapshot() const { return m_marks; }
   void Restore(std::vector<Mark> marks);

   nlohmann::json ToJson() const;

   // Display options (not saved)
   bool m_visible = true;
   bool m_hideDone = false;

private:
   void Load();
   void Save();
   void Touch(Mark& mark) { mark.seq = ++m_seq; }

   std::filesystem::path m_path;
   std::vector<Mark> m_marks;
   uint64_t m_seq = 0;
   int m_nextId = 1;
};

// derived: also adds what is computed from the points (arc center / radius / angles, circle radius, box size, bounds)
nlohmann::json MarkToJson(const Mark& mark, bool derived = false);
// Throws nothing: returns an error text, empty when OK. Missing fields keep the values of 'mark' (so it can be used to update).
std::string MarkFromJson(const nlohmann::json& j, Mark& mark);

}

namespace VPX::Sketch
{

// End point and unit direction of travel at the end of an arc mark (to continue it with a tangent arc); false when the mark is no arc
bool EndTangent(const Mark& mark, Point& end, Point& dir);
// Middle point of the circular arc that starts at p going along dir (unit) and ends at e (so a chain of arcs has no kink); a straight line when e is straight ahead
void TangentArcMid(Point p, Point dir, Point e, Point& mid);

}
