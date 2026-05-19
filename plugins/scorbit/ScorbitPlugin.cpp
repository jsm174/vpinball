// license:GPLv3+

#include "plugins/MsgPlugin.h"
#include "plugins/VPXPlugin.h"
#include "plugins/ControllerPlugin.h"
#include "pinmame/PinMAMEPlugin.h"

#include "nlohmann/json.hpp"

#include "common.h"
#include "Scorbit.h"

#include <atomic>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <tchar.h>
#else
#include <dlfcn.h>
#endif

using std::string;
using json = nlohmann::json;

namespace Scorbit {

LPI_IMPLEMENT_CPP

static MsgPluginAPI* msgApi = nullptr;
static VPXPluginAPI* vpxApi = nullptr;
static uint32_t endpointId = 0;
static unsigned int getVpxApiId = 0;
static unsigned int onControllersChangedId = 0;
static unsigned int getControllersId = 0;
static unsigned int onStateSrcChangedId = 0;
static unsigned int getStateSrcId = 0;

// Raw pointer on purpose: a static unique_ptr would run ~Scorbit from
// __cxa_finalize when the host exits without unloading plugins (e.g. the
// signal handler exit path), spawning threads and calling into the already
// finalized SDK. Abnormal exits leak instead; normal unload deletes below.
static Scorbit* scorbit = nullptr;
static string currentRomId;

static std::atomic<bool> pollActive { false };
static std::atomic<bool> statesDirty { true };
static bool wasGameOver = true;
static int64_t lastScores[4] = { 0, 0, 0, 0 };
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

// Game states published by libpinmame from a tomlogic memory map (see
// https://github.com/tomlogic/pinmame-nvram-maps): each map entry becomes an
// INT64 state in the PMPI_GROUP_GAMESTATE group whose desc holds the JSON
// group path (e.g. "game_state\scores\Player 1"), so states are matched on
// the standardized game_state keys, not on the per-map display labels.
struct GameStates
{
   int(MSGPIAPI* GetState)(unsigned int index, int type, void* pResult) = nullptr;
   unsigned int scores[4] = { UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX };
   unsigned int ball = UINT_MAX;
   unsigned int gameOver = UINT_MAX;
   unsigned int playerCount = UINT_MAX;
   unsigned int currentPlayer = UINT_MAX;
};

static GameStates states;

static bool ResolveStates()
{
   states = {};

   GetStateSrcMsg msg = { 0, 0, nullptr };
   msgApi->BroadcastMsg(endpointId, getStateSrcId, &msg);
   if (msg.count == 0)
      return false;
   std::vector<StateSrcId> sources(msg.count);
   msg = { msg.count, 0, sources.data() };
   msgApi->BroadcastMsg(endpointId, getStateSrcId, &msg);

   for (unsigned int i = 0; i < msg.count && i < msg.maxEntryCount; i++)
   {
      const StateSrcId& src = sources[i];
      GameStates resolved;
      resolved.GetState = src.GetState;
      unsigned int nScores = 0;
      for (unsigned int j = 0; j < src.nStates; j++)
      {
         const StateDef& def = src.stateDefs[j];
         if ((def.id.groupId & PMPI_GROUP_MASK) != PMPI_GROUP_GAMESTATE)
            continue;
         if ((def.typeMask & CTLPI_STATE_TYPE_INT64) == 0)
            continue;
         const string path = def.desc ? def.desc : "";
         if (path.starts_with("game_state\\scores\\") && nScores < 4)
            resolved.scores[nScores++] = j;
         else if (path.starts_with("game_state\\current_ball"))
            resolved.ball = j;
         else if (path.starts_with("game_state\\game_over"))
            resolved.gameOver = j;
         else if (path.starts_with("game_state\\player_count"))
            resolved.playerCount = j;
         else if (path.starts_with("game_state\\current_player"))
            resolved.currentPlayer = j;
      }
      if (resolved.GetState && resolved.scores[0] != UINT_MAX && resolved.gameOver != UINT_MAX)
      {
         states = resolved;
         LOGI("Game states resolved: scores="s + std::to_string(nScores)
            + " ball=" + (states.ball != UINT_MAX ? "yes" : "no")
            + " playerCount=" + (states.playerCount != UINT_MAX ? "yes" : "no")
            + " currentPlayer=" + (states.currentPlayer != UINT_MAX ? "yes" : "no"));
         return true;
      }
   }
   return false;
}

static int64_t ReadState(unsigned int index, int64_t defValue)
{
   if (index == UINT_MAX)
      return defValue;
   int64_t v = 0;
   if (states.GetState(index, CTLPI_STATE_TYPE_INT64, &v) != 0)
      return defValue;
   return v;
}

static void PollStates(void*)
{
   if (!pollActive || !scorbit)
      return;

   if (statesDirty.exchange(false))
      ResolveStates();

   if (states.GetState)
   {
      const bool gameOver = ReadState(states.gameOver, 1) != 0;
      const int ball = static_cast<int>(ReadState(states.ball, 0));
      const int64_t playerCount = ReadState(states.playerCount, 1);
      const int players = (playerCount < 1 || playerCount > 4) ? 1 : static_cast<int>(playerCount);
      const int64_t currentPlayer = ReadState(states.currentPlayer, 1);
      const int player = (currentPlayer < 1 || currentPlayer > 4) ? 1 : static_cast<int>(currentPlayer);

      int64_t s[4];
      for (int i = 0; i < 4; ++i)
         s[i] = ReadState(states.scores[i], 0);

      if (!gameOver && wasGameOver)
      {
         LOGI("Game started ("s + std::to_string(players) + " players)");
         scorbit->StartSession();
      }

      if (!gameOver)
      {
         const bool changed = s[0] != lastScores[0] || s[1] != lastScores[1]
            || s[2] != lastScores[2] || s[3] != lastScores[3];
         if (changed)
            scorbit->SendUpdate(static_cast<double>(s[0]), static_cast<double>(s[1]),
               static_cast<double>(s[2]), static_cast<double>(s[3]), ball, player, players);
         for (int i = 0; i < 4; ++i)
            lastScores[i] = s[i];
         lastPlayers = players;
      }

      if (gameOver && !wasGameOver)
      {
         LOGI("Game over, p1="s + std::to_string(s[0]));
         scorbit->StopSession(static_cast<double>(s[0]), static_cast<double>(s[1]),
            static_cast<double>(s[2]), static_cast<double>(s[3]), players);
      }

      wasGameOver = gameOver;
   }

   msgApi->RunOnMainThread(endpointId, 0.25, PollStates, nullptr);
}

static void StopPoll()
{
   if (!pollActive.exchange(false))
      return;
   if (scorbit && !wasGameOver)
      scorbit->StopSession(static_cast<double>(lastScores[0]), static_cast<double>(lastScores[1]),
         static_cast<double>(lastScores[2]), static_cast<double>(lastScores[3]), lastPlayers);
   wasGameOver = true;
}

struct MachineInfo
{
   int machineId = 0;
   string uuid;
};

static std::filesystem::path GetPluginPath()
{
#ifdef _WIN32
   HMODULE hm = nullptr;
   if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, _T("ScorbitPluginLoad"), &hm) == 0)
      return std::filesystem::path();

   std::wstring pathBuf;
   DWORD size = MAX_PATH;
   while (true)
   {
      pathBuf.resize(size);
      const DWORD length = ::GetModuleFileNameW(hm, pathBuf.data(), size);
      if (length == 0)
         return std::filesystem::path();
      if (length < size)
      {
         pathBuf.resize(length);
         break;
      }
      size *= 2;
   }
   std::filesystem::path path(pathBuf);
#else
   Dl_info info {};
   if (dladdr((void*)&GetPluginPath, &info) == 0 || !info.dli_fname)
      return std::filesystem::path();

   char pathBuf[PATH_MAX];
   if (!realpath(info.dli_fname, pathBuf))
      return std::filesystem::path();
   std::filesystem::path path(pathBuf);
#endif
   return path.empty() ? path : path.parent_path();
}

static bool LookupMachine(const string& romId, MachineInfo& info)
{
   const std::filesystem::path file = GetPluginPath() / "assets" / "scorbit_machines.json";

   std::ifstream f(file);
   if (!f.is_open())
   {
      LOGI("No machines file at "s + file.string()
         + " - expected JSON like { \"" + romId + "\": { \"machineId\": 1582, \"uuid\": \"...\" } }");
      return false;
   }

   try
   {
      json machines;
      f >> machines;
      if (machines.is_object() && machines.contains(romId) && machines[romId].is_object())
      {
         info.machineId = machines[romId].value("machineId", 0);
         info.uuid = machines[romId].value("uuid", "");
         return info.machineId != 0;
      }
   }
   catch (const json::exception& e)
   {
      LOGE("Failed to parse "s + file.string() + ": " + e.what());
   }
   return false;
}

static void OnStateSrcChanged(const unsigned int, void*, void*)
{
   statesDirty = true;
}

static void OnControllersChanged(const unsigned int, void*, void*)
{
   string romId;
   GetControllersMsg msg = { 0, 0, nullptr };
   msgApi->BroadcastMsg(endpointId, getControllersId, &msg);
   if (msg.count > 0)
   {
      const string pinmamePrefix(PMPI_GAMEID_PREFIX);
      std::vector<ControllerDef> controllers(msg.count);
      msg = { msg.count, 0, controllers.data() };
      msgApi->BroadcastMsg(endpointId, getControllersId, &msg);
      for (const auto& controller : controllers)
      {
         const string gameId = controller.gameId ? controller.gameId : "";
         if (gameId.starts_with(pinmamePrefix) && gameId.length() > pinmamePrefix.length())
         {
            romId = gameId.substr(pinmamePrefix.length());
            break;
         }
      }
   }

   if (romId == currentRomId)
      return;

   StopPoll();
   delete scorbit;
   scorbit = nullptr;
   currentRomId = romId;
   if (romId.empty())
   {
      LOGI("Game ended"s);
      return;
   }

   MachineInfo machine;
   if (!LookupMachine(romId, machine))
   {
      LOGI("romId="s + romId + " has no Scorbit machine mapping - Scorbit idle");
      return;
   }

   LOGI("New game: romId="s + romId + " machineId=" + std::to_string(machine.machineId));

   scorbit = new Scorbit(vpxApi);
   if (scorbit->DoInit(machine.machineId, romId, machine.uuid))
   {
      wasGameOver = true;
      for (int64_t& v : lastScores)
         v = 0;
      lastPlayers = 1;
      statesDirty = true;
      pollActive = true;
      msgApi->RunOnMainThread(endpointId, 0.25, PollStates, nullptr);
   }
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

   onControllersChangedId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_CONTROLLERS_ON_CHG_MSG);
   getControllersId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_CONTROLLERS_GET_MSG);
   onStateSrcChangedId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_STATE_ON_SRC_CHG_MSG);
   getStateSrcId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_STATE_GET_SRC_MSG);
   msgApi->SubscribeMsg(endpointId, onControllersChangedId, OnControllersChanged, nullptr);
   msgApi->SubscribeMsg(endpointId, onStateSrcChangedId, OnStateSrcChanged, nullptr);

   const ScorbitConfig c = GetPluginConfig();
   LOGI("Scorbit plugin loaded. provider="s + c.provider
      + " env=" + c.environment
      + " key=" + (c.providerKey.empty() ? "MISSING (Scorbit will stay disabled)"s
                                          : ("present (" + std::to_string(c.providerKey.size()) + " chars)")));

   OnControllersChanged(onControllersChangedId, nullptr, nullptr);
}

MSGPI_EXPORT void MSGPIAPI ScorbitPluginUnload()
{
   LOGI("Scorbit plugin unloading"s);

   if (msgApi) {
      StopPoll();
      msgApi->FlushPendingCallbacks(endpointId);
      msgApi->UnsubscribeMsg(onControllersChangedId, OnControllersChanged, nullptr);
      msgApi->UnsubscribeMsg(onStateSrcChangedId, OnStateSrcChanged, nullptr);
      msgApi->ReleaseMsgID(onControllersChangedId);
      msgApi->ReleaseMsgID(getControllersId);
      msgApi->ReleaseMsgID(onStateSrcChangedId);
      msgApi->ReleaseMsgID(getStateSrcId);
      msgApi->ReleaseMsgID(getVpxApiId);
   }

   delete scorbit;
   scorbit = nullptr;
   currentRomId.clear();
   states = {};
   vpxApi = nullptr;
   msgApi = nullptr;
}

}
