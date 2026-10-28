// license:GPLv3+

#pragma once

#include "SketchStore.h"

#include <functional>
#include <map>
#include <vector>

#include "imgui/imgui.h"

class PinTable;

// Heatmap of the ball: where the balls of the simulator runs (vpx_sim shot, flip, trace, autoplay...) spent their time, and where they lost energy hitting
// something. Filled while a sim tool runs, drawn over the table in the editor views and the screenshots, read by the assistant with vpx_heatmap. Not saved.
namespace VPX::Heat
{

// Time: ball time spent moving; Impacts: speed lost hitting something; Resting: ball time spent (almost) at rest, e.g. waiting in the plunger lane
enum class Layer { Time, Impacts, Resting };

class Store
{
public:
   static constexpr float CELL = 20.f; // table units

   // The store of a table (the live copy of a playtest shares the store of its base table)
   static Store& For(const PinTable* table);

   bool Empty() const { return !m_any; }
   void Clear();

   // A ball moved from (x0, y0) to (x1, y1) in ms of game time: the time is spread along the segment
   void AddSegment(const PinTable* table, float x0, float y0, float x1, float y1, float ms, bool resting = false);
   // A ball seen at (x, y) at game time nowMs: the time since the previous call for the same ball is spread along its move (the sim loops call this every frame)
   // The speed (the game units of the ball speed) tells a ball at rest (counted in the Resting layer) from a ball moving, and a sudden loss of speed is a hit (Impacts layer)
   void TrackBall(const PinTable* table, int ballId, float x, float y, float speed, uint32_t nowMs);
   // The ball lost 'loss' of speed hitting something at (x, y)
   void AddImpact(const PinTable* table, float x, float y, float loss);

   // Display options
   bool m_visible = true;
   Layer m_layer = Layer::Time;

   int Cols() const { return m_cols; }
   int Rows() const { return m_rows; }
   float Left() const { return m_left; }
   float Top() const { return m_top; }
   float Value(Layer layer, int col, int row) const;
   float Max(Layer layer) const;
   float Total(Layer layer) const;
   uint64_t Seq() const { return m_seq; }

private:
   void EnsureGrid(const PinTable* table);

   bool m_any = false;
   int m_cols = 0, m_rows = 0;
   float m_left = 0.f, m_top = 0.f;
   std::vector<float> m_time, m_impacts, m_rest;
   const std::vector<float>& Cells(Layer layer) const { return layer == Layer::Time ? m_time : layer == Layer::Impacts ? m_impacts : m_rest; }
   struct Last { float x = 0.f, y = 0.f, speed = 0.f; uint32_t ms = 0; };
   std::map<int, Last> m_last;
   uint64_t m_seq = 0;
};

// Draws the active layer as coloured cells (cold blue to hot red), nothing when empty or hidden. project maps a playfield point to screen pixels.
void Draw(ImDrawList* drawList, const Store& store, const std::function<ImVec2(Sketch::Point)>& project, float dpi);

}
