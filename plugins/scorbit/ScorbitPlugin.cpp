// license:GPLv3+

#include "common.h"
#include "GameTracker.h"
#include "Machines.h"
#include "Scorbit.h"

#include "plugins/ControllerPlugin.h"
#include "pinmame/PinMAMEPlugin.h"

#include <memory>

using namespace PinballPlugin::Controller;

namespace Scorbit
{

LPI_IMPLEMENT_CPP

MSGPI_STRING_VAL_SETTING(setProvider, "provider", "Provider", "Scorbit provider id", false, "vpxplugin", 64);
MSGPI_STRING_VAL_SETTING(setEnv, "environment", "Environment", "production or staging", true, "staging", 32);
MSGPI_STRING_VAL_SETTING(
   setKey, "providerKey", "Provider key", "Scorbit provider key", true, "5b2eC5SbmkMN4HhJGjC1o4EDqJ8865oIcP4IUNW7BC5H9gTcQ6N98Zk1qATGRNH1WT2pFJVTQXc8LyBF9vjTeRY0JOyx2zvERwXpcg==", 2048);
MSGPI_STRING_VAL_SETTING(setKeyFile, "deviceKeyFile", "Device key file", "Per-machine key filename, relative to the pref dir", false, "scorbit_device.key", 256);
MSGPI_INT_VAL_SETTING(setLog, "logLevel", "Log level", "0 quiet, 1 info, 2 debug", true, 0, 2, 2);

static const MsgPluginAPI* msgApi = nullptr;
static VPXPluginAPI* vpxApi = nullptr;
static uint32_t endpointId = 0;
static unsigned int getVpxApiId = 0;
static unsigned int getMachineStateId = 0;

static std::unique_ptr<CtrlItemConsumer<ControllerDef>> controllers;
static std::unique_ptr<GameTracker> tracker;

// Raw pointer on purpose: a static unique_ptr would run ~Scorbit from
// __cxa_finalize when the host exits without unloading plugins (e.g. the
// signal handler exit path), spawning threads and calling into the already
// finalized SDK. Abnormal exits leak instead; normal unload deletes below.
static Scorbit* scorbit = nullptr;

static string currentGameId;
static uint32_t pinmameEndpointId = 0;

static ScorbitConfig GetPluginConfig()
{
   ScorbitConfig c;
   c.provider = setProvider_Get();
   c.environment = setEnv_Get();
   c.providerKey = setKey_Get();
   c.deviceKeyFile = setKeyFile_Get();
   c.logLevel = setLog_Get();
   return c;
}

static void FilterControllers(std::vector<ControllerDef>& items)
{
   std::erase_if(items, [](const ControllerDef& def) { return !std::string_view(def.gameId).starts_with(PMPI_GAMEID_PREFIX); });
}

static void OnControllersChanged()
{
   string gameId;
   uint32_t controllerEndpointId = 0;
   controllers->With(
      [&gameId, &controllerEndpointId](const std::vector<ControllerDef>& items)
      {
         if (!items.empty())
         {
            const ControllerDef& controller = items.front();
            gameId = string(CtrlGetGameKey(controller.gameId));
            controllerEndpointId = controller.endpointId;
         }
      });

   if (gameId == currentGameId && controllerEndpointId == pinmameEndpointId)
      return;

   tracker->Stop();
   delete scorbit;
   scorbit = nullptr;
   currentGameId = gameId;
   pinmameEndpointId = controllerEndpointId;
   tracker->SetController(controllerEndpointId);

   if (gameId.empty())
   {
      LOGI("Game ended"s);
      return;
   }

   PinMAMEMachineStateMsg machineState { .version = 1 };
   msgApi->BroadcastMsg(endpointId, getMachineStateId, &machineState);
   const string romId = machineState.rom ? machineState.rom : gameId;

   MachineInfo machine;
   if (!LookupMachine(gameId, romId, machine))
   {
      LOGI("game="s + gameId + " rom=" + romId + " has no Scorbit machine mapping - Scorbit idle");
      return;
   }

   LOGI("New game: game="s + gameId + " rom=" + romId + " machineId=" + std::to_string(machine.machineId));

   scorbit = new Scorbit(vpxApi, GetPluginConfig());
   if (scorbit->DoInit(machine.machineId, romId, machine.uuid))
      tracker->Start(scorbit);
}

}

using namespace Scorbit;

MSGPI_EXPORT void MSGPIAPI ScorbitPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
   msgApi = api;
   endpointId = sessionId;
   LPISetup(endpointId, msgApi);
   LOGI("Scorbit plugin loading"s);

   msgApi->BroadcastMsg(endpointId, getVpxApiId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API), &vpxApi);
   getMachineStateId = msgApi->GetMsgID(PMPI_NAMESPACE, PMPI_GET_MACHINE_STATE);

   msgApi->RegisterSetting(endpointId, &setProvider);
   msgApi->RegisterSetting(endpointId, &setEnv);
   msgApi->RegisterSetting(endpointId, &setKey);
   msgApi->RegisterSetting(endpointId, &setKeyFile);
   msgApi->RegisterSetting(endpointId, &setLog);

   const ScorbitConfig c = GetPluginConfig();
   LOGI("Scorbit plugin loaded. provider="s + c.provider + " env=" + c.environment
      + " key=" + (c.providerKey.empty() ? "MISSING (Scorbit will stay disabled)"s : ("present (" + std::to_string(c.providerKey.size()) + " chars)")));

   tracker = std::make_unique<GameTracker>(msgApi, endpointId);
   controllers = std::make_unique<CtrlItemConsumer<ControllerDef>>(
      msgApi, endpointId, CTLPI_CONTROLLERS_GET_MSG, CTLPI_CONTROLLERS_ON_CHG_MSG, [](std::vector<ControllerDef>& items) { FilterControllers(items); }, nullptr,
      []() { OnControllersChanged(); });
   controllers->Subscribe();
}

MSGPI_EXPORT void MSGPIAPI ScorbitPluginUnload()
{
   LOGI("Scorbit plugin unloading"s);

   tracker->Stop();
   msgApi->FlushPendingCallbacks(endpointId);

   controllers->Unsubscribe();
   controllers = nullptr;
   tracker = nullptr;

   delete scorbit;
   scorbit = nullptr;
   currentGameId.clear();
   pinmameEndpointId = 0;

   msgApi->ReleaseMsgID(getMachineStateId);
   msgApi->ReleaseMsgID(getVpxApiId);
   vpxApi = nullptr;
   msgApi = nullptr;
}
