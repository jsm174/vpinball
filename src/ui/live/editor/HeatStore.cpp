// license:GPLv3+

#include "core/stdafx.h"
#include "ui/live/editor/HeatStore.h"
#include "parts/pintable.h"

#include <cmath>
#include <map>
#include <memory>

namespace VPX::Heat
{

Store& Store::For(const PinTable* table)
{
   static std::map<std::string, std::unique_ptr<Store>> stores;
   std::filesystem::path path;
   const std::string key = Sketch::SidecarKey(table, ".heat", path);
   auto it = stores.find(key);
   if (it == stores.end())
      it = stores.emplace(key, std::make_unique<Store>()).first;
   return *it->second;
}

void Store::Clear()
{
   m_any = false;
   m_cols = m_rows = 0;
   m_time.clear();
   m_impacts.clear();
   m_rest.clear();
   m_last.clear();
   ++m_seq;
}

void Store::EnsureGrid(const PinTable* table)
{
   if (m_cols > 0 || table == nullptr)
      return;
   m_left = table->m_left;
   m_top = table->m_top;
   m_cols = std::max(1, static_cast<int>(std::ceil((table->m_right - table->m_left) / CELL)));
   m_rows = std::max(1, static_cast<int>(std::ceil((table->m_bottom - table->m_top) / CELL)));
   m_time.assign(static_cast<size_t>(m_cols) * m_rows, 0.f);
   m_impacts.assign(static_cast<size_t>(m_cols) * m_rows, 0.f);
   m_rest.assign(static_cast<size_t>(m_cols) * m_rows, 0.f);
}

void Store::AddSegment(const PinTable* table, float x0, float y0, float x1, float y1, float ms, bool resting)
{
   EnsureGrid(table);
   if (m_cols == 0 || ms <= 0.f)
      return;
   // Samples every half cell along the segment, so a fast ball does not skip cells
   const float len = std::hypot(x1 - x0, y1 - y0);
   const int n = std::max(1, static_cast<int>(std::ceil(len / (CELL * 0.5f))));
   const float w = ms / static_cast<float>(n);
   for (int i = 0; i < n; i++)
   {
      const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
      const int col = static_cast<int>(std::floor((x0 + (x1 - x0) * t - m_left) / CELL));
      const int row = static_cast<int>(std::floor((y0 + (y1 - y0) * t - m_top) / CELL));
      if (col >= 0 && col < m_cols && row >= 0 && row < m_rows)
         (resting ? m_rest : m_time)[static_cast<size_t>(row) * m_cols + col] += w;
   }
   m_any = true;
   ++m_seq;
}

void Store::TrackBall(const PinTable* table, int ballId, float x, float y, float speed, uint32_t nowMs)
{
   constexpr float REST_SPEED = 0.5f; // below this a ball is resting (waiting in the plunger lane, stuck, cradled)
   // A long gap (the game was paused, another run started) is not a move of the ball
   if (const auto it = m_last.find(ballId); it != m_last.end() && nowMs > it->second.ms && nowMs - it->second.ms < 1000)
   {
      const Last& last = it->second;
      AddSegment(table, last.x, last.y, x, y, static_cast<float>(nowMs - last.ms), speed < REST_SPEED && last.speed < REST_SPEED);
      // A hit: a speed loss of at least 4 and a fifth of the speed from one frame to the next (the rule of the simulator's impact reports)
      const float loss = last.speed - speed;
      if (loss >= 4.f && loss >= 0.2f * last.speed)
         AddImpact(table, x, y, loss);
   }
   m_last[ballId] = { x, y, speed, nowMs };
}

void Store::AddImpact(const PinTable* table, float x, float y, float loss)
{
   EnsureGrid(table);
   if (m_cols == 0 || loss <= 0.f)
      return;
   const int col = static_cast<int>(std::floor((x - m_left) / CELL)), row = static_cast<int>(std::floor((y - m_top) / CELL));
   if (col >= 0 && col < m_cols && row >= 0 && row < m_rows)
   {
      m_impacts[static_cast<size_t>(row) * m_cols + col] += loss;
      m_any = true;
      ++m_seq;
   }
}

float Store::Value(Layer layer, int col, int row) const
{
   if (col < 0 || col >= m_cols || row < 0 || row >= m_rows)
      return 0.f;
   return Cells(layer)[static_cast<size_t>(row) * m_cols + col];
}

float Store::Max(Layer layer) const
{
   float m = 0.f;
   for (const float v : Cells(layer))
      m = std::max(m, v);
   return m;
}

float Store::Total(Layer layer) const
{
   float t = 0.f;
   for (const float v : Cells(layer))
      t += v;
   return t;
}

void Draw(ImDrawList* dl, const Store& store, const std::function<ImVec2(Sketch::Point)>& project, float dpi)
{
   (void)dpi;
   if (store.Empty() || !store.m_visible)
      return;
   const float maxValue = store.Max(store.m_layer);
   if (maxValue <= 0.f)
      return;
   for (int row = 0; row < store.Rows(); row++)
      for (int col = 0; col < store.Cols(); col++)
      {
         const float v = store.Value(store.m_layer, col, row);
         if (v <= 0.f)
            continue;
         // The square root keeps the rarely visited cells visible next to the hot ones
         const float t = std::sqrt(v / maxValue);
         if (t < 0.06f)
            continue;
         const float x0 = store.Left() + static_cast<float>(col) * Store::CELL, y0 = store.Top() + static_cast<float>(row) * Store::CELL;
         const ImVec2 a = project({ x0, y0 }), b = project({ x0 + Store::CELL, y0 }), c = project({ x0 + Store::CELL, y0 + Store::CELL }), d = project({ x0, y0 + Store::CELL });
         if (a.x == FLT_MAX || b.x == FLT_MAX || c.x == FLT_MAX || d.x == FLT_MAX)
            continue;
         // Blue, green, yellow, red
         float r, g, bl;
         if (t < 0.33f)
         {
            const float k = t / 0.33f;
            r = 0.f, g = 0.4f + 0.5f * k, bl = 1.f - 0.6f * k;
         }
         else if (t < 0.66f)
         {
            const float k = (t - 0.33f) / 0.33f;
            r = k, g = 0.9f, bl = 0.4f - 0.4f * k;
         }
         else
         {
            const float k = (t - 0.66f) / 0.34f;
            r = 1.f, g = 0.9f - 0.8f * k, bl = 0.f;
         }
         const ImU32 col32 = IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(bl * 255), static_cast<int>(60 + 130 * t));
         const ImVec2 quad[4] = { a, b, c, d };
         dl->AddConvexPolyFilled(quad, 4, col32);
      }
}

}
