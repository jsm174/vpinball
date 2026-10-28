// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "mcp/McpBookmarks.h"

#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace VPX::MCP::Bookmarks
{

namespace
{

constexpr uint32_t RING_MS = 8000; // history kept while playing
constexpr uint32_t BOOKMARK_MS = 6000; // what a bookmark keeps
constexpr size_t MAX_BOOKMARKS = 30;

std::deque<Frame>& Ring()
{
   static std::deque<Frame> ring;
   return ring;
}

// The flippers and the plunger of the table being played, found once per session (read every frame)
struct PartCache
{
   const Player* player = nullptr;
   size_t partCount = 0;
   std::vector<Flipper*> flippers;
   Plunger* plunger = nullptr;
};

PartCache& Parts(Player& p)
{
   static PartCache cache;
   const size_t count = p.m_ptable->GetParts().size();
   if (cache.player != &p || cache.partCount != count)
   {
      cache.player = &p;
      cache.partCount = count;
      cache.flippers.clear();
      cache.plunger = nullptr;
      for (IEditable* part : p.m_ptable->GetParts())
      {
         if (part->GetItemType() == eItemFlipper)
            cache.flippers.push_back(static_cast<Flipper*>(part));
         else if (part->GetItemType() == eItemPlunger && cache.plunger == nullptr)
            cache.plunger = static_cast<Plunger*>(part);
      }
   }
   return cache;
}

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

double R(float f, int decimals) { return std::strtod(std::format("{:.{}f}", f, decimals).c_str(), nullptr); }

json FrameToJson(const Frame& f)
{
   json balls = json::array();
   for (const BallSample& b : f.balls)
      balls.push_back(json::array({ b.id, R(b.x, 1), R(b.y, 1), R(b.z, 1), R(b.vx, 2), R(b.vy, 2), R(b.vz, 2) }));
   json angles = json::array();
   for (const float a : f.flipperAngles)
      angles.push_back(R(a, 1));
   return json::array({ f.t, f.flags, balls, angles, R(f.plunger, 3) });
}

bool FrameFromJson(const json& j, Frame& f)
{
   // [t, flags, balls] (older bookmarks), then the flipper angles and the plunger pull
   if (!j.is_array() || j.size() < 3 || j.size() > 5 || !j[0].is_number() || !j[1].is_number() || !j[2].is_array())
      return false;
   f.t = j[0].get<uint32_t>();
   f.flags = static_cast<uint8_t>(j[1].get<int>());
   if (j.size() >= 4 && j[3].is_array())
      for (const json& a : j[3])
         f.flipperAngles.push_back(a.is_number() ? a.get<float>() : 0.f);
   if (j.size() >= 5 && j[4].is_number())
      f.plunger = j[4].get<float>();
   for (const json& b : j[2])
   {
      if (!b.is_array() || b.size() != 7)
         return false;
      f.balls.push_back({ b[0].get<int>(), b[1].get<float>(), b[2].get<float>(), b[3].get<float>(), b[4].get<float>(), b[5].get<float>(), b[6].get<float>() });
   }
   return true;
}

}

Store& Store::For(const PinTable* table)
{
   static std::map<std::string, std::unique_ptr<Store>> stores;
   std::filesystem::path path;
   const std::string key = Sketch::SidecarKey(table, ".bookmarks.json", path);
   auto it = stores.find(key);
   if (it == stores.end())
   {
      it = stores.emplace(key, std::make_unique<Store>()).first;
      it->second->m_path = path;
      it->second->Load();
   }
   return *it->second;
}

std::filesystem::path Store::ScreenshotFolder(const PinTable* table) const
{
   if (table != nullptr && table->m_liveBaseTable != nullptr)
      table = table->m_liveBaseTable;
   if (table != nullptr && !table->m_filename.empty())
      return table->m_filename.parent_path() / (table->m_filename.stem().string() + ".bookmarks");
   return std::filesystem::temp_directory_path() / "vpx_bookmarks";
}

void Store::Load()
{
   if (m_path.empty() || !std::filesystem::exists(m_path))
      return;
   try
   {
      std::ifstream in(m_path, std::ios::binary);
      std::stringstream ss;
      ss << in.rdbuf();
      const json doc = json::parse(ss.str(), nullptr, false);
      if (!doc.is_object() || !doc.contains("bookmarks") || !doc["bookmarks"].is_array())
         return;
      m_seq = doc.value("seq", static_cast<uint64_t>(0));
      for (const json& e : doc["bookmarks"])
      {
         Bookmark b;
         b.id = e.value("id", m_nextId);
         b.created = e.value("created", std::string());
         b.note = e.value("note", std::string());
         b.gameTimeMs = e.value("gameTimeMs", 0u);
         b.screenshot = e.value("screenshot", std::string());
         if (e.contains("flippers") && e["flippers"].is_array())
            for (const json& fj : e["flippers"])
               b.flippers.push_back({ fj.value("name", std::string()), fj.value("start", 0.f), fj.value("end", 0.f), fj.value("x", 0.f) });
         if (e.contains("frames") && e["frames"].is_array())
            for (const json& fj : e["frames"])
            {
               Frame f;
               if (FrameFromJson(fj, f))
                  b.frames.push_back(std::move(f));
            }
         m_nextId = std::max(m_nextId, b.id + 1);
         m_items.push_back(std::move(b));
      }
   }
   catch (const std::exception& e)
   {
      PLOGE << "Bookmarks could not be read: " << e.what();
   }
}

void Store::Save()
{
   ++m_seq;
   if (m_path.empty())
      return;
   try
   {
      if (m_items.empty())
      {
         std::error_code ec;
         std::filesystem::remove(m_path, ec);
         return;
      }
      json items = json::array();
      for (const Bookmark& b : m_items)
      {
         json frames = json::array();
         for (const Frame& f : b.frames)
            frames.push_back(FrameToJson(f));
         json flippers = json::array();
         for (const FlipperInfo& fi : b.flippers)
            flippers.push_back(json { { "name", fi.name }, { "start", fi.startAngle }, { "end", fi.endAngle }, { "x", fi.x } });
         items.push_back(json { { "id", b.id }, { "created", b.created }, { "note", b.note }, { "gameTimeMs", b.gameTimeMs }, { "screenshot", b.screenshot }, { "flippers", flippers }, { "frames", frames } });
      }
      const json doc { { "version", 1 }, { "seq", m_seq }, { "bookmarks", items } };
      const std::filesystem::path tmp = m_path.string() + ".tmp";
      {
         std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
         out << doc.dump();
         if (!out)
         {
            PLOGE << "Bookmarks could not be written: " << tmp.string();
            return;
         }
      }
      std::filesystem::rename(tmp, m_path);
   }
   catch (const std::exception& e)
   {
      PLOGE << "Bookmarks could not be saved: " << e.what();
   }
}

Bookmark* Store::Find(int id)
{
   for (Bookmark& b : m_items)
      if (b.id == id)
         return &b;
   return nullptr;
}

int Store::Add(Bookmark bookmark)
{
   bookmark.id = m_nextId++;
   const int id = bookmark.id;
   m_items.push_back(std::move(bookmark));
   if (m_items.size() > MAX_BOOKMARKS)
      m_items.erase(m_items.begin());
   Save();
   return id;
}

bool Store::Remove(int id)
{
   const auto it = std::ranges::find_if(m_items, [id](const Bookmark& b) { return b.id == id; });
   if (it == m_items.end())
      return false;
   if (!it->screenshot.empty())
   {
      std::error_code ec;
      std::filesystem::remove(PathFromUTF8(it->screenshot), ec);
   }
   m_items.erase(it);
   Save();
   return true;
}

void Store::Clear()
{
   for (const Bookmark& b : m_items)
      if (!b.screenshot.empty())
      {
         std::error_code ec;
         std::filesystem::remove(PathFromUTF8(b.screenshot), ec);
      }
   m_items.clear();
   Save();
}

void Store::Changed(Bookmark&)
{
   Save();
}

void OnFrame(Player& p)
{
   static const void* lastPlayer = nullptr;
   static uint32_t lastTime = 0;
   std::deque<Frame>& ring = Ring();
   if (&p != lastPlayer || p.m_time_msec < lastTime)
   {
      ring.clear();
      lastPlayer = &p;
   }
   // Nothing is recorded in the editor, nor while the game is paused (no new game time)
   if (p.IsEditorMode() || (p.m_time_msec == lastTime && !ring.empty()))
   {
      lastTime = p.m_time_msec;
      return;
   }
   lastTime = p.m_time_msec;
   Frame f;
   f.t = p.m_time_msec;
   if (p.m_pininput.IsPressed(static_cast<int>(p.m_pininput.GetLeftFlipperActionId())))
      f.flags |= FLAG_LEFT;
   if (p.m_pininput.IsPressed(static_cast<int>(p.m_pininput.GetRightFlipperActionId())))
      f.flags |= FLAG_RIGHT;
   for (Ball* b : p.m_vball)
   {
      int id = -1;
      b->get_ID(&id);
      const vec3& pos = b->GetPosition();
      const vec3& vel = b->GetVelocity();
      f.balls.push_back({ id, pos.x, pos.y, pos.z, vel.x, vel.y, vel.z });
   }
   if (f.balls.empty())
      return; // no ball in play: nothing to look at
   // What the flippers and the plunger actually did (the button state alone misses the bot and the plunger)
   const PartCache& cache = Parts(p);
   for (Flipper* flipper : cache.flippers)
   {
      float angle = 0.f;
      flipper->get_CurrentAngle(&angle);
      f.flipperAngles.push_back(angle);
   }
   if (cache.plunger != nullptr)
      f.plunger = cache.plunger->GetPullFraction();
   ring.push_back(std::move(f));
   while (ring.size() > 1 && ring.back().t - ring.front().t > RING_MS)
      ring.pop_front();
}

std::string Create(Player& p)
{
   const std::deque<Frame>& ring = Ring();
   if (ring.empty())
      return "Nothing to bookmark yet: no ball has been in play";
   Bookmark b;
   b.created = NowString();
   b.gameTimeMs = p.m_time_msec;
   for (Flipper* flipper : Parts(p).flippers)
   {
      FlipperInfo info;
      info.name = flipper->GetName();
      flipper->get_StartAngle(&info.startAngle);
      flipper->get_EndAngle(&info.endAngle);
      info.x = flipper->GetCenter().x;
      b.flippers.push_back(info);
   }
   const uint32_t end = ring.back().t;
   for (const Frame& f : ring)
      if (end - f.t <= BOOKMARK_MS)
         b.frames.push_back(f);
   Store& store = Store::For(p.m_ptable);
   const uint32_t span = b.frames.back().t - b.frames.front().t;
   const int id = store.Add(std::move(b));
   // A screenshot of the moment (written when the next frames have rendered)
   try
   {
      const std::filesystem::path folder = store.ScreenshotFolder(p.m_ptable);
      std::filesystem::create_directories(folder);
      const std::filesystem::path file = folder / std::format("bookmark-{}.png", id);
      if (Bookmark* saved = store.Find(id))
         saved->screenshot = PathToUTF8(file);
      p.m_renderer->m_renderDevice->CaptureScreenshot({ p.m_playfieldWnd }, { file }, [](bool) { }, 1);
   }
   catch (const std::exception& e)
   {
      PLOGW << "Bookmark screenshot failed: " << e.what();
   }
   return std::format("Bookmark #{} saved: the last {:.0f} s", id, static_cast<float>(span) / 1000.f);
}

ImU32 BookmarkColor(int id, int alpha)
{
   static constexpr uint32_t palette[] = { 0xFF5C8A, 0x4DA3FF, 0xFFB02E, 0x2FC4B2, 0xA06BFF, 0x8BD43F };
   const uint32_t c = palette[static_cast<size_t>(std::max(id - 1, 0)) % std::size(palette)];
   return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, alpha);
}

void DrawPaths(ImDrawList* dl, const Store& store, const std::function<ImVec2(Sketch::Point)>& project, float dpi)
{
   for (const Bookmark& b : store.All())
   {
      if (!b.visible || b.frames.empty())
         continue;
      const ImU32 col = BookmarkColor(b.id), dark = IM_COL32(0, 0, 0, 150);
      // One path per ball
      std::map<int, std::vector<std::pair<uint32_t, ImVec2>>> paths;
      for (const Frame& f : b.frames)
         for (const BallSample& s : f.balls)
         {
            const ImVec2 p = project({ s.x, s.y });
            if (p.x != FLT_MAX)
               paths[s.id].emplace_back(f.t, p);
         }
      for (const auto& [id, pts] : paths)
      {
         if (pts.size() < 2)
            continue;
         std::vector<ImVec2> line;
         for (const auto& pt : pts)
            line.push_back(pt.second);
         dl->AddPolyline(line.data(), static_cast<int>(line.size()), dark, ImDrawFlags_None, 5.f * dpi);
         dl->AddPolyline(line.data(), static_cast<int>(line.size()), col, ImDrawFlags_None, 2.5f * dpi);
         // A dot every 100 ms: close dots mean a slow ball
         uint32_t nextDot = pts.front().first;
         for (const auto& pt : pts)
            if (pt.first >= nextDot)
            {
               dl->AddCircleFilled(pt.second, 2.8f * dpi, col, 8);
               nextDot = pt.first + 100;
            }
         dl->AddCircleFilled(pts.front().second, 6.f * dpi, IM_COL32(20, 20, 24, 255), 14);
         dl->AddCircleFilled(pts.front().second, 4.5f * dpi, IM_COL32(60, 220, 100, 255), 14);
         dl->AddCircleFilled(pts.back().second, 6.f * dpi, IM_COL32(20, 20, 24, 255), 14);
         dl->AddCircleFilled(pts.back().second, 4.5f * dpi, IM_COL32(255, 70, 70, 255), 14);
         const std::string label = std::format("#{}", b.id);
         const ImVec2 at(pts.back().second.x + 9.f * dpi, pts.back().second.y - 8.f * dpi);
         const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
         dl->AddRectFilled(ImVec2(at.x - 3.f * dpi, at.y - 2.f * dpi), ImVec2(at.x + ts.x + 3.f * dpi, at.y + ts.y + 2.f * dpi), IM_COL32(20, 20, 24, 210), 3.f * dpi);
         dl->AddText(at, col, label.c_str());
      }
   }
}

}
