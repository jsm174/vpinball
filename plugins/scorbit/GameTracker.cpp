// license:GPLv3+

#include "GameTracker.h"
#include "Scorbit.h"

#include "pinmame/PinMAMEPlugin.h"

namespace Scorbit
{

static constexpr double POLL_INTERVAL = 0.25;

GameTracker::GameTracker(const MsgPluginAPI* msgApi, uint32_t endpointId)
   : m_msgApi(msgApi)
   , m_endpointId(endpointId)
   , m_stateSources(
        msgApi, endpointId, CTLPI_STATE_GET_SRC_MSG, CTLPI_STATE_ON_SRC_CHG_MSG, [this](std::vector<StateSrcId>& items) { FilterStateSources(items); },
        [this]() { m_states = { }; }, // The resolved state indexes point into the source list, so drop them before it turns over
        [this]() { m_stateSources.With([this](const std::vector<StateSrcId>& items) { ResolveStates(items); }); })
{
   m_stateSources.Subscribe();
}

GameTracker::~GameTracker()
{
   Stop();
   m_stateSources.Unsubscribe();
}

void GameTracker::SetController(uint32_t controllerEndpointId)
{
   if (m_controllerEndpointId == controllerEndpointId)
      return;
   m_controllerEndpointId = controllerEndpointId;
   m_stateSources.Refresh();
}

void GameTracker::Start(Scorbit* scorbit)
{
   m_scorbit = scorbit;
   m_wasGameOver = true;
   for (int64_t& v : m_lastScores)
      v = 0;
   m_lastPlayers = 1;
   m_lastBall = -1;
   m_lastPlayer = -1;
   if (!m_pollActive.exchange(true))
      m_msgApi->RunOnMainThread(m_endpointId, POLL_INTERVAL, OnPoll, this);
}

void GameTracker::Stop()
{
   if (!m_pollActive.exchange(false))
      return;
   if (m_scorbit && !m_wasGameOver)
      m_scorbit->StopSession(
         static_cast<double>(m_lastScores[0]), static_cast<double>(m_lastScores[1]), static_cast<double>(m_lastScores[2]), static_cast<double>(m_lastScores[3]), m_lastPlayers);
   m_wasGameOver = true;
   m_scorbit = nullptr;
}

void GameTracker::FilterStateSources(std::vector<StateSrcId>& items) const
{
   std::erase_if(items, [this](const StateSrcId& src) { return src.id.endpointId != m_controllerEndpointId || src.id.resId != PMPI_GROUP_GAMESTATE; });
}

void GameTracker::ResolveStates(const std::vector<StateSrcId>& items)
{
   m_states = { };

   for (const StateSrcId& src : items)
   {
      GameStates resolved;
      resolved.id = src.id;
      resolved.nStates = src.nStates;
      unsigned int nScores = 0;
      for (unsigned int i = 0; i < src.nStates; i++)
      {
         const StateDef& def = src.stateDefs[i];
         if (def.dataFormat != CTLPI_STATE_FORMAT_INT64 || def.GetState == nullptr)
            continue;
         const std::string_view path = def.desc ? def.desc : "";
         if (path.starts_with("game_state\\scores\\"sv) && nScores < 4)
            resolved.scores[nScores++] = i;
         else if (path.starts_with("game_state\\current_ball"sv))
            resolved.ball = i;
         else if (path.starts_with("game_state\\game_over"sv))
            resolved.gameOver = i;
         else if (path.starts_with("game_state\\player_count"sv))
            resolved.playerCount = i;
         else if (path.starts_with("game_state\\current_player"sv))
            resolved.currentPlayer = i;
      }
      if (resolved.scores[0] != UINT_MAX && resolved.gameOver != UINT_MAX)
      {
         m_states = resolved;
         LOGI("Game states resolved: scores="s + std::to_string(nScores) + " ball=" + (m_states.ball != UINT_MAX ? "yes" : "no")
            + " playerCount=" + (m_states.playerCount != UINT_MAX ? "yes" : "no") + " currentPlayer=" + (m_states.currentPlayer != UINT_MAX ? "yes" : "no"));
         return;
      }
   }
}

// libpinmame leaves pResult untouched when the machine is not running or the
// memory region is unmapped, so the default value must be preset by the caller.
int64_t GameTracker::ReadState(const StateSrcId& src, unsigned int index, int64_t defValue)
{
   if (index >= src.nStates)
      return defValue;
   int64_t value = defValue;
   const StateDef& def = src.stateDefs[index];
   def.GetState(def.callContext, &value);
   return value;
}

void GameTracker::OnPoll(void* userData) { static_cast<GameTracker*>(userData)->Poll(); }

void GameTracker::Poll()
{
   if (!m_pollActive || m_scorbit == nullptr)
      return;

   if (m_states.nStates != 0)
   {
      m_stateSources.With(
         [this](const std::vector<StateSrcId>& items)
         {
            for (const StateSrcId& src : items)
            {
               if (src.id.id == m_states.id.id && src.nStates == m_states.nStates)
               {
                  UpdateSession(src);
                  break;
               }
            }
         });
   }

   m_msgApi->RunOnMainThread(m_endpointId, POLL_INTERVAL, OnPoll, this);
}

void GameTracker::UpdateSession(const StateSrcId& src)
{
   const bool gameOver = ReadState(src, m_states.gameOver, 1) != 0;
   const int ball = static_cast<int>(ReadState(src, m_states.ball, 0));
   const int64_t playerCount = ReadState(src, m_states.playerCount, 1);
   const int players = (playerCount < 1 || playerCount > 4) ? 1 : static_cast<int>(playerCount);
   const int64_t currentPlayer = ReadState(src, m_states.currentPlayer, 1);
   const int player = (currentPlayer < 1 || currentPlayer > 4) ? 1 : static_cast<int>(currentPlayer);

   int64_t s[4];
   for (int i = 0; i < 4; i++)
      s[i] = ReadState(src, m_states.scores[i], 0);

   const bool sessionStarted = !gameOver && m_wasGameOver;
   if (sessionStarted)
   {
      LOGI("Game started ("s + std::to_string(players) + " players)");
      m_scorbit->StartSession();
   }

   if (!gameOver)
   {
      // Ball and player changes must be pushed as well: a ball may be drained, or the active player
      // may rotate, without any score change, and a starting session carries neither until first sent
      const bool changed
         = sessionStarted || ball != m_lastBall || player != m_lastPlayer || s[0] != m_lastScores[0] || s[1] != m_lastScores[1] || s[2] != m_lastScores[2] || s[3] != m_lastScores[3];
      if (changed)
         m_scorbit->SendUpdate(static_cast<double>(s[0]), static_cast<double>(s[1]), static_cast<double>(s[2]), static_cast<double>(s[3]), ball, player, players);
      for (int i = 0; i < 4; i++)
         m_lastScores[i] = s[i];
      m_lastPlayers = players;
      m_lastBall = ball;
      m_lastPlayer = player;
   }

   if (gameOver && !m_wasGameOver)
   {
      LOGI("Game over, p1="s + std::to_string(s[0]));
      m_scorbit->StopSession(static_cast<double>(s[0]), static_cast<double>(s[1]), static_cast<double>(s[2]), static_cast<double>(s[3]), players);
   }

   m_wasGameOver = gameOver;
}

}
