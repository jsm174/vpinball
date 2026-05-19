#include "common.h"
#include "Scorbit.h"

#include "scorbit_sdk/scorbit_sdk_c.h"
#include "qrcodegen.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <memory>
#include <thread>

namespace Scorbit {

static std::atomic<int> g_logLevel { 2 };

static void SdkLogTrampoline(const char* msg, sb_log_level_t lvl, const char*, int, int64_t, void*)
{
   if (!msg)
      return;
   const std::string m = "[sdk] "s + msg;
   switch (lvl) {
      case SB_DEBUG: if (g_logLevel >= 2) LOGI(m); break;
      case SB_INFO:  if (g_logLevel >= 1) LOGI(m); break;
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

static std::string QrToAscii(const std::string& text)
{
   uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
   uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
   if (!qrcodegen_encodeText(text.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM,
          qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
      return {};

   const int size = qrcodegen_getSize(qr);
   constexpr int quiet = 4;
   auto dark = [&](int x, int y) -> bool {
      if (x < 0 || y < 0 || x >= size || y >= size)
         return false;
      return qrcodegen_getModule(qr, x, y);
   };

   std::string out;
   for (int y = -quiet; y < size + quiet; y += 2) {
      for (int x = -quiet; x < size + quiet; ++x) {
         const bool top = dark(x, y);
         const bool bot = dark(x, y + 1);
         out += top ? (bot ? "\xE2\x96\x88" : "\xE2\x96\x80")
                    : (bot ? "\xE2\x96\x84" : " ");
      }
      out += '\n';
   }
   return out;
}

static void EventTrampoline(const sb_event_t* event, void* userData)
{
   if (userData)
      static_cast<Scorbit*>(userData)->HandleEvent(event);
}

Scorbit::Scorbit(VPXPluginAPI* vpxApi)
   : m_vpxApi(vpxApi)
{
}

Scorbit::~Scorbit()
{
   if (!m_handle)
      return;

   auto h = static_cast<sb_game_handle_t>(m_handle);
   m_handle = nullptr;

   if (m_sessionActive)
      sb_set_game_finished(h);

   sb_reset_logger();

   auto done = std::make_shared<std::promise<void>>();
   auto ready = done->get_future();
   std::thread([h, done]() {
      sb_destroy_game_state(h);
      done->set_value();
   }).detach();
   ready.wait_for(std::chrono::seconds(2));
}

bool Scorbit::DoInit(int machineId, const std::string& version, const std::string& uuidOverride)
{
   LOGI("DoInit(machineId="s + std::to_string(machineId) + ", version=" + version + ")");
   if (m_handle) {
      LOGI("DoInit: already initialized"s);
      return true;
   }

   VPXInfo info {};
   m_vpxApi->GetVpxInfo(&info);
   const std::filesystem::path prefDir = info.prefPath ? info.prefPath : ".";

   m_config = GetPluginConfig();
   g_logLevel = m_config.logLevel;

   const std::string uuid = uuidOverride.empty() ? m_config.installUuid : uuidOverride;

   if (m_config.providerKey.empty()) {
      LOGW("DoInit: scorbit.providerKey is empty in vpinball.ini [Plugin.Scorbit] - Scorbit disabled."s);
      return false;
   }

   m_machineId = machineId;
   g_keyPath = (prefDir / m_config.deviceKeyFile).string();
   LOGI("DoInit: provider="s + m_config.provider + " env=" + m_config.environment
      + " machineId=" + std::to_string(m_machineId)
      + " deviceKey=" + g_keyPath);

   sb_add_logger_callback(&SdkLogTrampoline, this, 512);

   sb_config_t cf = sb_config_create();
   sb_config_set_provider(cf, m_config.provider.c_str());
   sb_config_set_machine_id(cf, m_machineId);
   sb_config_set_game_code_version(cf, version.c_str());
   sb_config_set_hostname(cf, m_config.environment.c_str());
   sb_config_set_encrypted_key(cf, m_config.providerKey.c_str());
   if (!uuid.empty())
      sb_config_set_uuid(cf, uuid.c_str());
   sb_config_set_save_key_callback(cf, &SaveKeyCb, this);
   sb_config_set_load_key_callback(cf, &LoadKeyCb, this);
   sb_config_set_event_callback(cf, &EventTrampoline, this);

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

void Scorbit::StartSession()
{
   if (!m_enabled) {
      LOGW("StartSession ignored: Scorbit not enabled (DoInit did not succeed)"s);
      return;
   }
   LOGI("StartSession"s);
   auto h = static_cast<sb_game_handle_t>(m_handle);
   sb_set_game_started(h, SB_GAME_STARTED_BY_BUTTON);
   sb_commit(h);
   m_sessionActive = true;
}

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

void Scorbit::StopSession(double s1, double s2, double s3, double s4, int numPlayers)
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

void Scorbit::HandleEvent(const void* eventPtr)
{
   const sb_event_t* event = static_cast<const sb_event_t*>(eventPtr);
   const sb_event_type_t type = sb_event_type(event);

   if (type == SB_EVT_PAIRING_STATUS_CHANGED) {
      bool paired = false;
      sb_event_pairing_status_changed(event, &paired);
      m_paired = paired;
      if (paired) {
         LOGI("Scorbit paired - scores will upload."s);
         return;
      }
   }
   else if (type != SB_EVT_CONFIG_RECEIVED) {
      return;
   }

   MaybeShowPairing();
}

void Scorbit::MaybeShowPairing()
{
   if (m_paired || m_qrShown)
      return;

   auto h = static_cast<sb_game_handle_t>(m_handle);
   const char* link = sb_get_pair_deeplink(h);
   if (!link || !*link)
      return;

   m_qrShown = true;
   LOGI("Scorbit not paired. Scan this QR with the Scorbit app to pair:\n"s
      + QrToAscii(link) + "\nOr open: " + link);

   sb_request_pair_code(h, [](sb_error_t error, const char* code, void*) {
      if (error == SB_EC_SUCCESS && code && *code)
         LOGI("Scorbit pairing code: "s + code);
   }, this);
}

}
