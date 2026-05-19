#pragma once

#include "plugins/MsgPlugin.h"
#include "plugins/VPXPlugin.h"

#include <atomic>
#include <string>

namespace Scorbit {

struct ScorbitConfig
{
   std::string provider = "vpxplugin";
   std::string environment = "staging";
   std::string providerKey;
   std::string deviceKeyFile = "scorbit_device.key";
   std::string installUuid;
   int logLevel = 2;
};

ScorbitConfig GetPluginConfig();

class Scorbit final
{
public:
   explicit Scorbit(VPXPluginAPI* vpxApi);
   ~Scorbit();

   bool DoInit(int machineId, const std::string& version, const std::string& uuidOverride = "");
   void StartSession();
   void SendUpdate(double s1, double s2, double s3, double s4, int ball, int player, int numPlayers);
   void StopSession(double s1, double s2, double s3, double s4, int numPlayers);

   void HandleEvent(const void* event);

private:
   void ApplyScores(double s1, double s2, double s3, double s4, int numPlayers);
   void MaybeShowPairing();

   VPXPluginAPI* m_vpxApi;

   ScorbitConfig m_config;
   void* m_handle = nullptr;

   std::atomic<bool> m_enabled { false };
   std::atomic<bool> m_sessionActive { false };
   std::atomic<bool> m_paired { false };
   bool m_qrShown = false;
   int m_machineId = 0;
};

}
