// license:GPLv3+

#include "plugins/MsgPlugin.h"
#include "plugins/VPXPlugin.h"
#include "plugins/ControllerPlugin.h"
#include "plugins/PinMAMEPlugin.h"

#include "common.h"
#include "Scorbit.h"

#include <cstdint>
#include <memory>
#include <string>

using std::string;

namespace Scorbit {

LPI_IMPLEMENT_CPP

static MsgPluginAPI* msgApi = nullptr;
static VPXPluginAPI* vpxApi = nullptr;
static uint32_t endpointId = 0;
static unsigned int getVpxApiId = 0;
static unsigned int onGameStartId = 0;
static unsigned int onGameEndId = 0;
static unsigned int getNvramMsgId = 0;

static std::unique_ptr<Scorbit> scorbit;
static bool sessionRunning = false;

static bool nvramPollActive = false;
static bool nvramWasGameOver = true;
static double lastScores[4] = { 0, 0, 0, 0 };
static int lastPlayers = 1;

MSGPI_STRING_VAL_SETTING(setProvider, "provider", "Provider",
   "Scorbit provider id", false, "vpxplugin", 64);
MSGPI_STRING_VAL_SETTING(setEnv, "environment", "Environment",
   "production or staging", true, "staging", 32);
MSGPI_STRING_VAL_SETTING(setKey, "providerKey", "Provider key",
   "Scorbit provider key", true,
   "5b2eC5SbmkMN4HhJGjC1o4EDqJ8865oIcP4IUNW7BC5H9gTcQ6N98Zk1qATGRNH1WT2pFJVTQXc8LyBF9vjTeRY0JOyx2zvERwXpcg==", 2048);
MSGPI_STRING_VAL_SETTING(setKeyFile, "deviceKeyFile", "Device key file",
   "Per-machine key filename, relative to the pref dir", false, "scorbit_device.key", 256);
MSGPI_INT_VAL_SETTING(setLog, "logLevel", "Log level",
   "0 quiet, 1 info, 2 debug", true, 0, 2, 2);

ScorbitConfig GetPluginConfig()
{
   ScorbitConfig c;
   c.provider = setProvider_Get();
   c.environment = setEnv_Get();
   c.providerKey = setKey_Get();
   c.deviceKeyFile = setKeyFile_Get();
   c.logLevel = setLog_Get();
   return c;
}

struct NvramMap
{
   const char* gameId;
   int machineId;
   const char* uuid;
   int scoreStart[4];
   int scoreLen;
   int ball;
   int gameOver;
   int playerCount;
   int currentPlayer;
   int minSize;
};

static const NvramMap kNvramMaps[] = {
   { "bk2k_l4", 1582, "3f61877a-dc49-4311-826a-0c1bb6e2c7bb",
     { 512, 516, 520, 524 }, 4, 56, 175, 178, 179, 528 },
};

static const NvramMap* g_map = nullptr;

static uint64_t Bcd(const uint8_t* d, int start, int len)
{
   uint64_t v = 0;
   for (int i = 0; i < len; ++i) {
      const uint8_t b = d[start + i];
      v = v * 100 + (b >> 4) * 10 + (b & 0x0F);
   }
   return v;
}

static void PollNvram(void*)
{
   if (!nvramPollActive || !g_map || !scorbit)
      return;

   PinMAMEGetNvramMsg msg { 0, nullptr };
   msgApi->BroadcastMsg(endpointId, getNvramMsgId, &msg);

   if (msg.data && msg.size >= static_cast<uint32_t>(g_map->minSize)) {
      const uint8_t* d = msg.data;
      const bool gameOver = d[g_map->gameOver] != 0;
      const int ball = d[g_map->ball] & 0x0F;
      int players = (d[g_map->playerCount] == 0xFF) ? 1 : (d[g_map->playerCount] + 1);
      players = players < 1 ? 1 : (players > 4 ? 4 : players);
      const int player = (d[g_map->currentPlayer] == 0xFF) ? 1 : (d[g_map->currentPlayer] + 1);

      double s[4];
      for (int i = 0; i < 4; ++i)
         s[i] = static_cast<double>(Bcd(d, g_map->scoreStart[i], g_map->scoreLen));

      if (!gameOver && nvramWasGameOver) {
         LOGI("NVRAM: game started ("s + std::to_string(players) + " players)");
         scorbit->StartSession();
      }

      if (!gameOver) {
         const bool changed = s[0] != lastScores[0] || s[1] != lastScores[1]
            || s[2] != lastScores[2] || s[3] != lastScores[3];
         if (changed)
            scorbit->SendUpdate(s[0], s[1], s[2], s[3], ball, player, players);
         for (int i = 0; i < 4; ++i)
            lastScores[i] = s[i];
         lastPlayers = players;
      }

      if (gameOver && !nvramWasGameOver) {
         LOGI("NVRAM: game over, p1="s + std::to_string((long long)s[0]));
         scorbit->StopSession(s[0], s[1], s[2], s[3], players);
      }

      nvramWasGameOver = gameOver;
   }

   msgApi->RunOnMainThread(endpointId, 0.25, PollNvram, nullptr);
}

static void StopNvramPoll()
{
   if (!nvramPollActive)
      return;
   nvramPollActive = false;
   if (scorbit && !nvramWasGameOver)
      scorbit->StopSession(lastScores[0], lastScores[1], lastScores[2], lastScores[3], lastPlayers);
   nvramWasGameOver = true;
}

static void OnGameStart(const unsigned int, void*, void* msgData)
{
   const CtlOnGameStateChgMsg* msg = static_cast<const CtlOnGameStateChgMsg*>(msgData);
   const string gameId = (msg && msg->gameId) ? msg->gameId : "";

   g_map = nullptr;
   for (const NvramMap& m : kNvramMaps) {
      if (gameId == m.gameId) {
         g_map = &m;
         break;
      }
   }
   if (!g_map) {
      LOGI("OnGameStart: gameId="s + gameId + " has no NVRAM map - Scorbit idle");
      return;
   }

   LOGI("OnGameStart: gameId="s + gameId + " machineId=" + std::to_string(g_map->machineId));

   if (!scorbit)
      scorbit = std::make_unique<Scorbit>(vpxApi);

   if (scorbit->DoInit(g_map->machineId, gameId, g_map->uuid)) {
      StopNvramPoll();
      sessionRunning = true;
      nvramWasGameOver = true;
      for (double& v : lastScores)
         v = 0.0;
      nvramPollActive = true;
      msgApi->RunOnMainThread(endpointId, 0.25, PollNvram, nullptr);
   }
}

static void OnGameEnd(const unsigned int, void*, void*)
{
   LOGI("OnGameEnd"s);
   StopNvramPoll();
   sessionRunning = false;
}

MSGPI_EXPORT void MSGPIAPI ScorbitPluginLoad(const uint32_t sessionId, MsgPluginAPI* api)
{
   msgApi = api;
   endpointId = sessionId;
   LPISetup(endpointId, msgApi);
   LOGI("Scorbit plugin loading"s);

   getVpxApiId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API);
   msgApi->BroadcastMsg(endpointId, getVpxApiId, &vpxApi);

   msgApi->RegisterSetting(endpointId, &setProvider);
   msgApi->RegisterSetting(endpointId, &setEnv);
   msgApi->RegisterSetting(endpointId, &setKey);
   msgApi->RegisterSetting(endpointId, &setKeyFile);
   msgApi->RegisterSetting(endpointId, &setLog);

   onGameStartId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_EVT_ON_GAME_START);
   onGameEndId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_EVT_ON_GAME_END);
   msgApi->SubscribeMsg(endpointId, onGameStartId, OnGameStart, nullptr);
   msgApi->SubscribeMsg(endpointId, onGameEndId, OnGameEnd, nullptr);

   getNvramMsgId = msgApi->GetMsgID(PINMAMEPI_NAMESPACE, PINMAMEPI_GET_NVRAM_MSG);

   const ScorbitConfig c = GetPluginConfig();
   LOGI("Scorbit plugin loaded. provider="s + c.provider
      + " env=" + c.environment
      + " key=" + (c.providerKey.empty() ? "MISSING (Scorbit will stay disabled)"s
                                          : ("present (" + std::to_string(c.providerKey.size()) + " chars)")));
}

MSGPI_EXPORT void MSGPIAPI ScorbitPluginUnload()
{
   LOGI("Scorbit plugin unloading"s);

   if (msgApi) {
      StopNvramPoll();
      msgApi->FlushPendingCallbacks(endpointId);
      msgApi->UnsubscribeMsg(onGameStartId, OnGameStart, nullptr);
      msgApi->UnsubscribeMsg(onGameEndId, OnGameEnd, nullptr);
      msgApi->ReleaseMsgID(onGameStartId);
      msgApi->ReleaseMsgID(onGameEndId);
      msgApi->ReleaseMsgID(getNvramMsgId);
      msgApi->ReleaseMsgID(getVpxApiId);
   }

   scorbit.reset();
   sessionRunning = false;
   vpxApi = nullptr;
   msgApi = nullptr;
}

}
