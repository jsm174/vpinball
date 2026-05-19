#include "common.h"
#include "Scorbit.h"

#include "scorbit_sdk/scorbit_sdk_c.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace Scorbit {

bool WriteQrPng(const std::string& text, const std::string& file);

static void SdkLogTrampoline(const char* msg, sb_log_level_t lvl, const char*, int, int64_t, void*)
{
   if (!msg)
      return;
   const std::string m = "[sdk] "s + msg;
   switch (lvl) {
      case SB_DEBUG: LOGD(m); break;
      case SB_INFO:  LOGI(m); break;
      case SB_WARN:  LOGW(m); break;
      default:       LOGE(m); break;
   }
}

static const char* AuthStr(sb_auth_status_t s)
{
   switch (s) {
      case SB_NET_NOT_AUTHENTICATED:              return "NotAuthenticated";
      case SB_NET_AUTHENTICATING:                 return "Authenticating";
      case SB_NET_AUTHENTICATED_CHECKING_PAIRING: return "Authenticated/CheckingPairing";
      case SB_NET_AUTHENTICATED_UNPAIRED:         return "Authenticated/Unpaired";
      case SB_NET_AUTHENTICATED_PAIRED:           return "Authenticated/Paired";
      case SB_NET_AUTHENTICATION_FAILED:          return "AuthenticationFailed";
      default:                                    return "?";
   }
}

static int64_t ToScore(double v) { return v < 0.0 ? -1 : static_cast<int64_t>(v); }

static void SdkEventTrampoline(const sb_event_t* ev, void* userData)
{
   static_cast<Scorbit*>(userData)->HandleSdkEvent(ev);
}

// ---- key persistence: replaces sToken.exe + ScorbitUUID.dat ------------
static std::string g_keyPath;

static void SaveKeyCb(const char* key, void*)
{
   if (FILE* f = std::fopen(g_keyPath.c_str(), "wb"); f) {
      std::fwrite(key, 1, std::strlen(key), f);
      std::fclose(f);
   }
}

static int LoadKeyCb(char* buffer, size_t bufferSize, void*)
{
   FILE* f = std::fopen(g_keyPath.c_str(), "rb");
   if (!f)
      return 0;
   const size_t n = std::fread(buffer, 1, bufferSize - 1, f);
   std::fclose(f);
   buffer[n] = '\0';
   return static_cast<int>(n);
}

Scorbit::Scorbit(MsgPluginAPI* msgApi, uint32_t endpointId, VPXPluginAPI* vpxApi)
   : m_msgApi(msgApi)
   , m_endpointId(endpointId)
   , m_vpxApi(vpxApi)
{
}

Scorbit::~Scorbit()
{
   if (m_handle) {
      if (m_sessionActive)
         sb_set_game_finished(static_cast<sb_game_handle_t>(m_handle));
      sb_destroy_game_state(static_cast<sb_game_handle_t>(m_handle));
      m_handle = nullptr;
   }
}

bool Scorbit::DoInit(int machineId, const std::string& dirQrCode, const std::string& version, const std::string& /*opdb*/)
{
   LOGI("DoInit(machineId="s + std::to_string(machineId) + ", qrDir=" + dirQrCode + ", version=" + version + ")");
   if (m_handle) {
      LOGI("DoInit: already initialized"s);
      return true;
   }

   VPXInfo info {};
   m_vpxApi->GetVpxInfo(&info);
   const std::filesystem::path prefDir = info.prefPath ? info.prefPath : ".";

   m_config = GetPluginConfig();

   if (m_config.encryptedKey.empty()) {
      LOGW("DoInit: scorbit.encryptedKey is empty in vpinball.ini [Plugin.Scorbit] - Scorbit disabled. See plugins/scorbit/README.md"s);
      return false;
   }

   m_machineId = m_config.machineIdOverride != 0 ? m_config.machineIdOverride : machineId;
   m_version = version;
   m_qrDir = dirQrCode;
   g_keyPath = (prefDir / m_config.deviceKeyFile).string();
   LOGI("DoInit: provider="s + m_config.provider + " env=" + m_config.environment
      + " machineId=" + std::to_string(m_machineId)
      + (m_config.machineIdOverride ? " (overridden)" : "")
      + " deviceKey=" + g_keyPath);

   sb_add_logger_callback(&SdkLogTrampoline, this, 512);

   sb_config_t cf = sb_config_create();
   sb_config_set_provider(cf, m_config.provider.c_str());
   sb_config_set_machine_id(cf, m_machineId);
   sb_config_set_game_code_version(cf, version.c_str());
   sb_config_set_hostname(cf, m_config.environment.c_str());
   sb_config_set_encrypted_key(cf, m_config.encryptedKey.c_str());
   if (!m_config.installUuid.empty())
      sb_config_set_uuid(cf, m_config.installUuid.c_str());
   sb_config_set_event_callback(cf, &SdkEventTrampoline, this);
   sb_config_set_save_key_callback(cf, &SaveKeyCb, this);
   sb_config_set_load_key_callback(cf, &LoadKeyCb, this);

   m_handle = sb_create_game_state(cf);
   sb_config_destroy(cf);

   if (!m_handle) {
      LOGE("DoInit: sb_create_game_state failed"s);
      return false;
   }

   m_enabled = true;
   LOGI("DoInit: ok, status="s + AuthStr(sb_get_status(static_cast<sb_game_handle_t>(m_handle))));
   return true;
}

void Scorbit::HandleSdkEvent(const void* event)
{
   const sb_event_t* ev = static_cast<const sb_event_t*>(event);

   bool paired = false;
   if (sb_event_pairing_status_changed(ev, &paired)) {
      m_needsPairing = !paired;
      if (paired)
         m_newlyPaired = true;
      LOGI("event: pairing status -> "s + (paired ? "PAIRED" : "UNPAIRED (needs pairing; deeplink="s + GetPairDeeplink() + ")"));
      return;
   }

   int count = 0;
   if (sb_event_players_updated(ev, &count)) {
      std::lock_guard<std::mutex> lk(m_mutex);
      for (int p = 1; p <= 4 && p <= count; ++p) {
         const char* name = nullptr;
         const char* url = nullptr;
         if (sb_event_player_preferred_name(ev, static_cast<sb_player_t>(p), &name) && name && *name) {
            if (m_playerNames[p - 1].empty()) {
               m_newlyClaimed = p;
               LOGI("event: player "s + std::to_string(p) + " claimed by " + name);
            }
            m_playerNames[p - 1] = name;
            m_claimDeeplinks[p - 1].clear();
         }
         else if (sb_event_player_claim_deeplink(ev, static_cast<sb_player_t>(p), &url) && url) {
            m_playerNames[p - 1].clear();
            m_claimDeeplinks[p - 1] = url;
         }
      }
   }
}

void Scorbit::StartSession()
{
   if (!m_enabled) {
      LOGW("StartSession ignored: Scorbit not enabled (DoInit did not succeed)"s);
      return;
   }
   LOGI("StartSession"s);
   {
      std::lock_guard<std::mutex> lk(m_mutex);
      for (auto& n : m_playerNames) n.clear();
      for (auto& d : m_claimDeeplinks) d.clear();
   }
   sb_set_game_started(static_cast<sb_game_handle_t>(m_handle), SB_GAME_STARTED_BY_BUTTON);
   sb_commit(static_cast<sb_game_handle_t>(m_handle));
   m_sessionActive = true;
}

void Scorbit::ForceAsynch(bool enabled) { m_forceAsync = enabled; }

void Scorbit::ApplyScores(double s1, double s2, double s3, double s4, int numPlayers)
{
   auto h = static_cast<sb_game_handle_t>(m_handle);
   const double s[4] = { s1, s2, s3, s4 };
   for (int p = 1; p <= numPlayers && p <= 4; ++p)
      sb_set_score(h, p, ToScore(s[p - 1]), 0);
}

void Scorbit::SendUpdate(double s1, double s2, double s3, double s4, int ball, int player, int numPlayers)
{
   if (!m_enabled || !m_sessionActive)
      return;
   auto h = static_cast<sb_game_handle_t>(m_handle);
   ApplyScores(s1, s2, s3, s4, numPlayers);
   if (ball >= 0)
      sb_set_current_ball(h, static_cast<sb_ball_t>(ball));
   if (player >= 1)
      sb_set_active_player(h, static_cast<sb_player_t>(player));
   sb_commit(h);
}

void Scorbit::SendUpdateAsynch(double s1, double s2, double s3, double s4, int ball, int player, int numPlayers, bool)
{
   SendUpdate(s1, s2, s3, s4, ball, player, numPlayers);
}

void Scorbit::SetGameMode(const std::string& mode)
{
   if (!m_enabled)
      return;
   auto h = static_cast<sb_game_handle_t>(m_handle);
   if (m_curMode == mode)
      return;
   m_curMode = mode;
   sb_clear_modes(h);
   size_t start = 0;
   while (start <= mode.size()) {
      size_t comma = mode.find(',', start);
      std::string one = mode.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      if (!one.empty())
         sb_add_mode(h, one.c_str());
      if (comma == std::string::npos)
         break;
      start = comma + 1;
   }
}

void Scorbit::StopSession(double s1, double s2, double s3, double s4, int numPlayers)
{
   StopSession2(s1, s2, s3, s4, numPlayers, false);
}

void Scorbit::StopSession2(double s1, double s2, double s3, double s4, int numPlayers, bool)
{
   if (!m_enabled || !m_sessionActive)
      return;
   LOGI("StopSession (players="s + std::to_string(numPlayers) + ")");
   auto h = static_cast<sb_game_handle_t>(m_handle);
   ApplyScores(s1, s2, s3, s4, numPlayers);
   sb_set_game_finished(h);
   sb_commit(h);
   m_sessionActive = false;
}

void Scorbit::DoTimer(int) { /* SDK owns its own network/heartbeat threads */ }
void Scorbit::Callback() { /* SDK is async internally; nothing to pump */ }

std::string Scorbit::GetName(int playerNum)
{
   if (playerNum < 1 || playerNum > 4)
      return {};
   std::lock_guard<std::mutex> lk(m_mutex);
   return m_playerNames[playerNum - 1];
}

std::string Scorbit::GetPairDeeplink()
{
   if (!m_handle)
      return {};
   const char* d = sb_get_pair_deeplink(static_cast<sb_game_handle_t>(m_handle));
   return d ? d : std::string {};
}

static std::string g_pairCode;
static void PairCodeCb(sb_error_t err, const char* reply, void*)
{
   g_pairCode = (err == SB_EC_SUCCESS && reply) ? reply : "";
}

std::string Scorbit::GetPairCode()
{
   if (!m_handle)
      return {};
   sb_request_pair_code(static_cast<sb_game_handle_t>(m_handle), &PairCodeCb, this);
   return g_pairCode;
}

std::string Scorbit::GetClaimDeeplink(int playerNum)
{
   if (playerNum < 1 || playerNum > 4)
      return {};
   std::lock_guard<std::mutex> lk(m_mutex);
   return m_claimDeeplinks[playerNum - 1];
}

bool Scorbit::GenerateQRCode(const std::string& text, const std::string& file)
{
   if (text.empty() || file.empty())
      return false;
   return WriteQrPng(text, file);
}

bool Scorbit::TakeNewlyPaired() { return m_newlyPaired.exchange(false); }
int Scorbit::TakeNewlyClaimedPlayer() { return m_newlyClaimed.exchange(0); }

}
