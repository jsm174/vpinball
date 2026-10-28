// license:GPLv3+

#pragma once

#include "ui/live/editor/SketchStore.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "imgui/imgui.h"

class Player;
class PinTable;

// "That felt wrong" bookmarks: while a table is played (by hand or in a playtest) the last seconds are recorded (every ball, and the flipper buttons); one key (F8)
// keeps them as a bookmark with a screenshot of that moment. The assistant reads the bookmarks with vpx_bookmarks, replays them in the simulator, and measures what happened.
// Bookmarks are saved next to the table (<table>.bookmarks.json, the screenshots in <table>.bookmarks/).
namespace VPX::MCP::Bookmarks
{

constexpr uint8_t FLAG_LEFT = 1; // the left flipper button is pressed
constexpr uint8_t FLAG_RIGHT = 2; // the right flipper button is pressed

struct BallSample
{
   int id = 0;
   float x = 0.f, y = 0.f, z = 0.f;
   float vx = 0.f, vy = 0.f, vz = 0.f;
};

struct Frame
{
   uint32_t t = 0; // game time, ms
   uint8_t flags = 0;
   std::vector<BallSample> balls;
   std::vector<float> flipperAngles; // current angle of each flipper of the table (degrees), in the order of Bookmark::flippers
   float plunger = 0.f; // pull of the plunger: 0 at rest, 1 fully pulled back
};

// A flipper of the table: where it rests and where it goes (degrees), and its place on the table
struct FlipperInfo
{
   std::string name;
   float startAngle = 0.f, endAngle = 0.f;
   float x = 0.f;
};

struct Bookmark
{
   int id = 0;
   std::string created; // local time
   std::string note;
   uint32_t gameTimeMs = 0; // game time when the key was pressed
   std::vector<FlipperInfo> flippers;
   std::vector<Frame> frames;
   std::string screenshot; // image file of the moment ("" until captured)
   uint64_t seq = 0;
   bool visible = false; // the path is drawn in the editor views and screenshots (not saved)
};

class Store
{
public:
   // The store of a table (the live copy of a playtest shares the store of its base table). Loaded from the sidecar file on first use.
   static Store& For(const PinTable* table);

   const std::vector<Bookmark>& All() const { return m_items; }
   Bookmark* Find(int id);
   int Add(Bookmark bookmark);
   bool Remove(int id);
   void Clear();
   // Saves after a change of the note
   void Changed(Bookmark& bookmark);
   uint64_t Seq() const { return m_seq; }
   const std::filesystem::path& Path() const { return m_path; }
   // Where the screenshots go
   std::filesystem::path ScreenshotFolder(const PinTable* table) const;

private:
   void Load();
   void Save();

   std::filesystem::path m_path;
   std::vector<Bookmark> m_items;
   uint64_t m_seq = 0;
   int m_nextId = 1;
};

// Called by the player every frame: records the balls and the flipper buttons of the last seconds
void OnFrame(Player& player);
// The bookmark key: keeps the last 6 seconds. Returns what to tell the user.
std::string Create(Player& player);
// The paths of the visible bookmarks: a line per ball with a dot every 100 ms, green at the start and red at the end
void DrawPaths(ImDrawList* drawList, const Store& store, const std::function<ImVec2(Sketch::Point)>& project, float dpi);
ImU32 BookmarkColor(int id, int alpha = 255);

}
