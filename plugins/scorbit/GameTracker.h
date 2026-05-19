// license:GPLv3+

#pragma once

#include "common.h"
#include "plugins/ControllerPlugin.h"

#include <atomic>
#include <climits>

namespace Scorbit
{

class Scorbit;

// Follows the live game states published by the emulating controller and drives the
// Scorbit session from them: start on the game-over flag clearing, score/ball/player
// updates while a game runs, stop when the flag is raised again
class GameTracker final
{
public:
   GameTracker(const MsgPluginAPI* msgApi, uint32_t endpointId);
   ~GameTracker();

   void SetController(uint32_t controllerEndpointId);
   void Start(Scorbit* scorbit);
   void Stop();

private:
   // Game states published by libpinmame from a tomlogic memory map (see
   // https://github.com/tomlogic/pinmame-nvram-maps): each map entry becomes an
   // INT64 state of the PMPI_GROUP_GAMESTATE source whose desc holds the JSON
   // group path (e.g. "game_state\scores\Player 1"), so states are matched on
   // the standardized game_state keys, not on the per-map display labels.
   struct GameStates
   {
      CtlResId id { };
      unsigned int nStates = 0;
      unsigned int scores[4] = { UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX };
      unsigned int ball = UINT_MAX;
      unsigned int gameOver = UINT_MAX;
      unsigned int playerCount = UINT_MAX;
      unsigned int currentPlayer = UINT_MAX;
   };

   void FilterStateSources(std::vector<StateSrcId>& items) const;
   void ResolveStates(const std::vector<StateSrcId>& items);
   static int64_t ReadState(const StateSrcId& src, unsigned int index, int64_t defValue);
   static void OnPoll(void* userData);
   void Poll();
   void UpdateSession(const StateSrcId& src);

   const MsgPluginAPI* const m_msgApi;
   const uint32_t m_endpointId;
   uint32_t m_controllerEndpointId = 0;
   PinballPlugin::Controller::CtrlItemConsumer<StateSrcId> m_stateSources;
   GameStates m_states;

   Scorbit* m_scorbit = nullptr;
   std::atomic<bool> m_pollActive { false };
   bool m_wasGameOver = true;
   int64_t m_lastScores[4] = { 0, 0, 0, 0 };
   int m_lastPlayers = 1;
   int m_lastBall = -1;
   int m_lastPlayer = -1;
};

}
