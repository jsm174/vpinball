#pragma once

#include "plugins/MsgPlugin.h"
#include "plugins/VPXPlugin.h"
#include "plugins/ScriptablePlugin.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>

namespace Scorbit {

struct ScorbitConfig
{
   std::string provider = "vpin";
   std::string environment = "production";
   std::string encryptedKey;
   std::string deviceKeyFile = "scorbit_device.key";
   std::string installUuid;
   int logLevel = 1;
   int machineIdOverride = 0; // testing: non-zero wins over the table's DoInit id
};

// Implemented in ScorbitPlugin.cpp - sourced from vpinball.ini [Plugin.Scorbit].
ScorbitConfig GetPluginConfig();

class Scorbit final
{
public:
   Scorbit(MsgPluginAPI* msgApi, uint32_t endpointId, VPXPluginAPI* vpxApi);
   ~Scorbit();

   PSC_IMPLEMENT_REFCOUNT()

   // ---- Legacy ScorbitIF surface (drop-in compatible) -------------------
   bool DoInit(int machineId, const std::string& dirQrCode, const std::string& version, const std::string& opdb);
   void StartSession();
   void ForceAsynch(bool enabled);
   void StopSession(double s1, double s2, double s3, double s4, int numPlayers);
   void StopSession2(double s1, double s2, double s3, double s4, int numPlayers, bool cancel);
   void SetGameMode(const std::string& mode);
   void SendUpdate(double s1, double s2, double s3, double s4, int ball, int player, int numPlayers);
   void SendUpdateAsynch(double s1, double s2, double s3, double s4, int ball, int player, int numPlayers, bool async);
   void DoTimer(int interval);
   void Callback();
   std::string GetName(int playerNum);
   void SetUploadLog(bool v) { m_uploadLog = v; }

   bool GetSessionActive() const { return m_sessionActive; }
   bool GetNeedsPairing() const { return m_needsPairing; }
   bool GetEnabled() const { return m_enabled; }

   // ---- New clean additions --------------------------------------------
   std::string GetPairDeeplink();
   std::string GetPairCode();
   std::string GetClaimDeeplink(int playerNum);
   bool GenerateQRCode(const std::string& text, const std::string& file);

   // Edge-triggered, read-once. Lets the table fire its own
   // Scorbit_Paired / Scorbit_PlayerClaimed subs from a timer poll
   // instead of the plugin reaching back into VBScript by name.
   bool TakeNewlyPaired();
   int TakeNewlyClaimedPlayer();

   // SDK event entry point (called from the C trampoline, not from script).
   void HandleSdkEvent(const void* event);

private:
   void ApplyScores(double s1, double s2, double s3, double s4, int numPlayers);

   MsgPluginAPI* m_msgApi;
   uint32_t m_endpointId;
   VPXPluginAPI* m_vpxApi;

   ScorbitConfig m_config;
   void* m_handle = nullptr; // sb_game_handle_t

   std::atomic<bool> m_enabled { false };
   std::atomic<bool> m_sessionActive { false };
   std::atomic<bool> m_needsPairing { false };
   std::atomic<bool> m_forceAsync { false };
   bool m_uploadLog = false;
   int m_machineId = 0;
   std::string m_version;
   std::string m_qrDir;
   std::string m_curMode;

   std::mutex m_mutex;
   std::array<std::string, 4> m_playerNames;
   std::array<std::string, 4> m_claimDeeplinks;
   std::atomic<bool> m_newlyPaired { false };
   std::atomic<int> m_newlyClaimed { 0 };
};

}
