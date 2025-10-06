#include "core/stdafx.h"

#include "core/TableDB.h"
#include "core/VPXPluginAPIImpl.h"
#include "core/extern.h"
#include "VPinballLib.h"
#include "TableManager.h"
#include "FileSystem.h"
#include "VPXProgress.h"
#include "WebServer.h"

#include <zip.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <set>
#include <map>
#include <queue>
#include <mutex>
#include <stduuid/uuid.h>
#include <nlohmann/json.hpp>

#if defined(__ANDROID__)
#define COMMAND_APP_INIT_COMPLETED 0x8001
#include "SAFFileSystemProvider.h"
#endif

MSGPI_EXPORT void MSGPIAPI AlphaDMDPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api);
MSGPI_EXPORT void MSGPIAPI AlphaDMDPluginUnload();
MSGPI_EXPORT void MSGPIAPI B2SPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api);
MSGPI_EXPORT void MSGPIAPI B2SPluginUnload();
MSGPI_EXPORT void MSGPIAPI B2SLegacyPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api);
MSGPI_EXPORT void MSGPIAPI B2SLegacyPluginUnload();
MSGPI_EXPORT void MSGPIAPI DOFPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api);
MSGPI_EXPORT void MSGPIAPI DOFPluginUnload();
MSGPI_EXPORT void MSGPIAPI DMDUtilPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api);
MSGPI_EXPORT void MSGPIAPI DMDUtilPluginUnload();
MSGPI_EXPORT void MSGPIAPI FlexDMDPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api);
MSGPI_EXPORT void MSGPIAPI FlexDMDPluginUnload();
MSGPI_EXPORT void MSGPIAPI PinMAMEPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api);
MSGPI_EXPORT void MSGPIAPI PinMAMEPluginUnload();
MSGPI_EXPORT void MSGPIAPI PUPPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api);
MSGPI_EXPORT void MSGPIAPI PUPPluginUnload();
MSGPI_EXPORT void MSGPIAPI RemoteControlPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api);
MSGPI_EXPORT void MSGPIAPI RemoteControlPluginUnload();
MSGPI_EXPORT void MSGPIAPI ScoreViewPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api);
MSGPI_EXPORT void MSGPIAPI ScoreViewPluginUnload();
MSGPI_EXPORT void MSGPIAPI SerumPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api);
MSGPI_EXPORT void MSGPIAPI SerumPluginUnload();
MSGPI_EXPORT void MSGPIAPI WMPPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api);
MSGPI_EXPORT void MSGPIAPI WMPPluginUnload();

static void (*s_pBridgeCallback)(void*) = nullptr;

// ObjC Bridge

extern "C" void VPinball_SetBridgeCallback(void (*callback)(void*))
{
   s_pBridgeCallback = callback;
}

// Main Library

namespace VPinballLib {

VPinballLib::VPinballLib()
{
   EditableRegistry::RegisterEditable<Ball>();
   EditableRegistry::RegisterEditable<Bumper>();
   EditableRegistry::RegisterEditable<Decal>();
   EditableRegistry::RegisterEditable<DispReel>();
   EditableRegistry::RegisterEditable<Flasher>();
   EditableRegistry::RegisterEditable<Flipper>();
   EditableRegistry::RegisterEditable<Gate>();
   EditableRegistry::RegisterEditable<Kicker>();
   EditableRegistry::RegisterEditable<Light>();
   EditableRegistry::RegisterEditable<LightSeq>();
   EditableRegistry::RegisterEditable<Plunger>();
   EditableRegistry::RegisterEditable<Primitive>();
   EditableRegistry::RegisterEditable<Ramp>();
   EditableRegistry::RegisterEditable<Rubber>();
   EditableRegistry::RegisterEditable<Spinner>();
   EditableRegistry::RegisterEditable<Surface>();
   EditableRegistry::RegisterEditable<Textbox>();
   EditableRegistry::RegisterEditable<Timer>();
   EditableRegistry::RegisterEditable<Trigger>();
   EditableRegistry::RegisterEditable<HitTarget>();
   EditableRegistry::RegisterEditable<PartGroup>();
}

VPinballLib::~VPinballLib()
{
}

int VPinballLib::AppInit(int argc, char** argv)
{
   if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
      return 0;

   SDL_PropertiesID props = SDL_CreateProperties();
   SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Visual Pinball Player");
   #if defined(__ANDROID__)
      SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
   #endif
   SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
   m_pWindow = SDL_CreateWindowWithProperties(props);
   SDL_DestroyProperties(props);

   if (!m_pWindow)
      return 0;

#ifdef __APPLE__
   m_pMetalLayer = SDL_GetRenderMetalLayer(SDL_CreateRenderer(m_pWindow, "Metal"));
   if (!m_pMetalLayer)
      return 0;

   props = SDL_GetWindowProperties(m_pWindow);
   s_pBridgeCallback((void*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL));
#endif

#ifdef __ANDROID__
   SDL_SendAndroidMessage(COMMAND_APP_INIT_COMPLETED, 0);
#endif

   return 1;
}

void VPinballLib::AppIterate()
{
   if (m_gameLoop) {
      m_gameLoop();

      if (g_pplayer && (g_pplayer->GetCloseState() == Player::CS_PLAYING
         || g_pplayer->GetCloseState() == Player::CS_USER_INPUT
         || g_pplayer->GetCloseState() == Player::CS_CLOSE_CAPTURE_SCREENSHOT))
         return;

      if (m_tableImageCaptureReady)
         ProcessTableImageCapture();
      else if (!m_loadedTableUuid.empty()) {
         if (m_tableImageCachePath.empty()) {
            RefreshTables(m_loadedTableUuid);
            m_loadedTableUuid.clear();
            m_loadedTableSafPath.clear();
         } 
      }

      PLOGI.printf("Game Loop stopping");

      m_gameLoop = nullptr;

      delete g_pplayer;
      g_pplayer = nullptr;

      CComObject<PinTable>* pActiveTable = g_pvp->GetActiveTable();
      if (pActiveTable)
         g_pvp->CloseTable(pActiveTable);
   }

   if (m_tableImageCaptureReady)
      ProcessTableImageCapture();
}

void VPinballLib::AppEvent(SDL_Event* event)
{
   std::lock_guard<std::mutex> lock(m_eventMutex);
   if (m_gameLoop)
      m_eventQueue.push(*event);
   else {
      while (!m_eventQueue.empty())
         m_eventQueue.pop();
   }
}

bool VPinballLib::PollAppEvent(SDL_Event& event)
{
   std::lock_guard<std::mutex> lock(m_eventMutex);
   if (m_eventQueue.empty())
      return false;

   event = m_eventQueue.front();
   m_eventQueue.pop();
   return true;
}

void VPinballLib::Startup(VPinballEventCallback callback)
{
   SetEventCallback(callback);

   SDL_RunOnMainThread([](void* userdata) {
      VPinballLib* lib = (VPinballLib*)userdata;

      g_pvp = new ::VPinball();
      g_pvp->SetLogicalNumberOfProcessors(SDL_GetNumLogicalCPUCores());
      g_pvp->m_settings.LoadFromFile(g_pvp->m_myPrefPath + "VPinballX.ini", true);
      g_pvp->m_settings.Save();

      Logger::GetInstance()->Init();
      Logger::GetInstance()->SetupLogger(true);

      PLOGI << "VPX - " << VP_VERSION_STRING_FULL_LITERAL;
      PLOGI << "m_logicalNumberOfProcessors=" << g_pvp->GetLogicalNumberOfProcessors();
      PLOGI << "m_myPath=" << g_pvp->m_myPath;
      PLOGI << "m_myPrefPath=" << g_pvp->m_myPrefPath;

      if (!DirExists(PATH_USER)) {
         std::error_code ec;
         if (std::filesystem::create_directory(PATH_USER, ec)) {
            PLOGI.printf("User path created: %s", PATH_USER.c_str());
         }
         else {
            PLOGE.printf("Unable to create user path: %s", PATH_USER.c_str());
         }
      }

      lib->LoadPlugins();

#ifdef __ANDROID__
      SAFFileSystemProvider::Init();
#endif

      lib->m_tableManager.Start();

#ifdef __ANDROID__
      lib->CleanupSAFCache();
#endif
   }, this, true);
}

void VPinballLib::SetEventCallback(VPinballEventCallback callback)
{
   m_eventCallback = [callback](VPINBALL_EVENT event, void* data) -> void* {
      thread_local string jsonString;
      const char* jsonData = nullptr;

      if (data != nullptr) {
         nlohmann::json j;

         switch(event) {
            case VPINBALL_EVENT_ARCHIVE_UNCOMPRESSING:
            case VPINBALL_EVENT_ARCHIVE_COMPRESSING:
            case VPINBALL_EVENT_LOADING_ITEMS:
            case VPINBALL_EVENT_LOADING_SOUNDS:
            case VPINBALL_EVENT_LOADING_IMAGES:
            case VPINBALL_EVENT_LOADING_FONTS:
            case VPINBALL_EVENT_LOADING_COLLECTIONS:
            case VPINBALL_EVENT_PRERENDERING: {
               ProgressData* progressData = (ProgressData*)data;
               j["progress"] = progressData->progress;
               jsonString = j.dump();
               jsonData = jsonString.c_str();
               break;
            }
            case VPINBALL_EVENT_RUMBLE: {
               RumbleData* rumbleData = (RumbleData*)data;
               j["lowFrequencyRumble"] = rumbleData->lowFrequencyRumble;
               j["highFrequencyRumble"] = rumbleData->highFrequencyRumble;
               j["durationMs"] = rumbleData->durationMs;
               jsonString = j.dump();
               jsonData = jsonString.c_str();
               break;
            }
            case VPINBALL_EVENT_SCRIPT_ERROR: {
               ScriptErrorData* scriptErrorData = (ScriptErrorData*)data;
               j["error"] = (int)scriptErrorData->error;
               j["line"] = scriptErrorData->line;
               j["position"] = scriptErrorData->position;
               j["description"] = scriptErrorData->description ? scriptErrorData->description : "";
               jsonString = j.dump();
               jsonData = jsonString.c_str();
               break;
            }
            case VPINBALL_EVENT_WEB_SERVER: {
               WebServerData* webServerData = (WebServerData*)data;
               j["url"] = webServerData->url ? webServerData->url : "";
               jsonString = j.dump();
               jsonData = jsonString.c_str();
               break;
            }
            case VPINBALL_EVENT_TABLE_LIST_UPDATED: {
               jsonData = (const char*)data;
               break;
            }
            default:
               break;
         }
      }

      callback(event, jsonData);
      return nullptr;
   };
}

void VPinballLib::SendEvent(VPINBALL_EVENT event, void* data)
{
   auto callback = Instance().m_eventCallback;
   if (callback)
      callback(event, data);
}

void VPinballLib::LoadPlugins()
{
   static constexpr struct {
      const char* id;
      void (*load)(uint32_t, const MsgPluginAPI*);
      void (*unload)();
   } plugins[] = {
      { "PinMAME",       &PinMAMEPluginLoad,       &PinMAMEPluginUnload       },
      { "AlphaDMD",      &AlphaDMDPluginLoad,      &AlphaDMDPluginUnload      },
      { "B2S",           &B2SPluginLoad,           &B2SPluginUnload           },
      { "B2SLegacy",     &B2SLegacyPluginLoad,     &B2SLegacyPluginUnload     },
      { "DOF",           &DOFPluginLoad,           &DOFPluginUnload           },
      { "DMDUtil",       &DMDUtilPluginLoad,       &DMDUtilPluginUnload       },
      { "FlexDMD",       &FlexDMDPluginLoad,       &FlexDMDPluginUnload       },
      { "PUP",           &PUPPluginLoad,           &PUPPluginUnload           },
      { "RemoteControl", &RemoteControlPluginLoad, &RemoteControlPluginUnload },
      { "ScoreView",     &ScoreViewPluginLoad,     &ScoreViewPluginUnload     },
      { "Serum",         &SerumPluginLoad,         &SerumPluginUnload         },
      { "WMP",           &WMPPluginLoad,           &WMPPluginUnload           }
   };

   for (size_t i = 0; i < std::size(plugins); ++i) {
      if (VPXPluginAPIImpl::GetInstance().getAPI().GetOption(
             plugins[i].id, "Enable",
             VPX_OPT_SHOW_UI, "Enable plugin",
             0.f, 1.f, 1.f, 0.f,
             VPXPluginAPI::NONE,
             nullptr
         )) {
         auto& p = plugins[i];
         auto plugin = MsgPI::MsgPluginManager::GetInstance().RegisterPlugin(
            p.id, p.id, p.id,
            "", "", "",
            p.load, p.unload
         );
         plugin->Load(&MsgPI::MsgPluginManager::GetInstance().GetMsgAPI());
         m_plugins.push_back(plugin);
      }
   }
}

void VPinballLib::UnloadPlugins()
{
   for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it)
      (*it)->Unload();
   m_plugins.clear();
}

void VPinballLib::Log(VPINBALL_LOG_LEVEL level, const string& message)
{
   switch (level) {
      case VPINBALL_LOG_LEVEL_DEBUG:
         PLOGD << message;
         break;
      case VPINBALL_LOG_LEVEL_INFO:
         PLOGI << message;
         break;
      case VPINBALL_LOG_LEVEL_WARN:
         PLOGW << message;
         break;
      case VPINBALL_LOG_LEVEL_ERROR:
         PLOGE << message;
         break;
   }
}

void VPinballLib::ResetLog()
{
   Logger::GetInstance()->Truncate();
}

int VPinballLib::LoadValueInt(const string& sectionName, const string& key, int defaultValue)
{
   return g_pvp->m_settings.LoadValueWithDefault(Settings::GetSection(sectionName), key, defaultValue);
}

float VPinballLib::LoadValueFloat(const string& sectionName, const string& key, float defaultValue)
{
   return g_pvp->m_settings.LoadValueWithDefault(Settings::GetSection(sectionName), key, defaultValue);
}

string VPinballLib::LoadValueString(const string& sectionName, const string& key, const string& defaultValue)
{
   return g_pvp->m_settings.LoadValueWithDefault(Settings::GetSection(sectionName), key, defaultValue);
}

void VPinballLib::SaveValueInt(const string& sectionName, const string& key, int value)
{
   g_pvp->m_settings.SaveValue(Settings::GetSection(sectionName), key, value);
   g_pvp->m_settings.Save();
}

void VPinballLib::SaveValueFloat(const string& sectionName, const string& key, float value)
{
   g_pvp->m_settings.SaveValue(Settings::GetSection(sectionName), key, value);
   g_pvp->m_settings.Save();
}

void VPinballLib::SaveValueString(const string& sectionName, const string& key, const string& value)
{
   g_pvp->m_settings.SaveValue(Settings::GetSection(sectionName), key, value);
   g_pvp->m_settings.Save();
}

VPINBALL_STATUS VPinballLib::ResetIni()
{
   string iniFilePath = g_pvp->m_myPrefPath + "VPinballX.ini";
   if (std::remove(iniFilePath.c_str()) != 0)
      return VPINBALL_STATUS_FAILURE;

   g_pvp->m_settings.LoadFromFile(iniFilePath, true);
   g_pvp->m_settings.Save();
   return VPINBALL_STATUS_SUCCESS;
}

void VPinballLib::UpdateWebServer()
{
   bool enabled = g_pvp->m_settings.LoadValueWithDefault(Settings::Standalone, "WebServer"s, false);
   PLOGI.printf("VPinballLib::UpdateWebServer: WebServer setting = %d, IsRunning = %d", enabled, m_webServer.IsRunning());

   if (m_webServer.IsRunning())
      m_webServer.Stop();

   if (enabled)
      m_webServer.Start();
}

#ifdef __ANDROID__
void VPinballLib::CleanupSAFCache()
{
   string cachePath = string(g_pvp->m_myPrefPath) + "saf_cache";
   PLOGI.printf("VPinballLib::CleanupSAFCache: Cleaning up SAF cache at: %s", cachePath.c_str());

   if (std::filesystem::exists(cachePath)) {
      try {
         std::filesystem::remove_all(cachePath);
         PLOGI.printf("VPinballLib::CleanupSAFCache: Successfully cleaned up cache");
      } catch (const std::filesystem::filesystem_error& ex) {
         PLOGE.printf("VPinballLib::CleanupSAFCache: Error: %s", ex.what());
      }
   }
}

string VPinballLib::PrepareSAFTableForLoading(const string& safPath)
{
   PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: START - Preparing SAF table: %s", safPath.c_str());

   try {
      // Extract relative path from the content:// URI
      // e.g., "content://.../tree/XXX/blankTable/blankTable.vpx" -> "blankTable/blankTable.vpx"
      string relativePath = FileSystem::ExtractRelativePath(safPath);

      std::filesystem::path pathObj(relativePath);
      string fileName = pathObj.filename().string();
      string tableDir = pathObj.parent_path().string();

      PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: Relative path: %s, fileName: %s, tableDir: %s",
                   relativePath.c_str(), fileName.c_str(), tableDir.c_str());

      // Create cache directory
      string cachePath = string(g_pvp->m_myPrefPath) + "saf_cache";
      string tableCachePath = cachePath + "/" + tableDir;

      PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: Cache path: %s", cachePath.c_str());
      PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: Table cache path: %s", tableCachePath.c_str());

      if (!std::filesystem::exists(cachePath)) {
         PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: Creating cache directory");
         std::filesystem::create_directories(cachePath);
      }

      // Clean up any existing cache for this table
      if (std::filesystem::exists(tableCachePath)) {
         PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: Cleaning up existing cache: %s", tableCachePath.c_str());
         std::filesystem::remove_all(tableCachePath);
      }

      // Create table cache directory
      PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: Creating table cache directory");
      std::filesystem::create_directories(tableCachePath);

      // Build the content URI for the table directory
      string tablesPath = m_tableManager.GetTablesPath();
      string safTableDir = tablesPath + "/" + tableDir;
      PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: Starting copy from SAF: %s to cache: %s", safTableDir.c_str(), tableCachePath.c_str());

      if (!FileSystem::CopyDirectory(safTableDir, tableCachePath)) {
         PLOGE.printf("VPinballLib::PrepareSAFTableForLoading: FAILED to copy table directory from SAF");
         return "";
      }

      PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: Copy completed successfully");

      // Save paths for sync-back later
      m_safTableDir = safTableDir;
      m_safCachePath = tableCachePath;
      PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: Saved SAF paths for sync-back - SAF: %s, Cache: %s",
                   m_safTableDir.c_str(), m_safCachePath.c_str());

      // Return the cache path to the VPX file
      string cachedVPXPath = tableCachePath + "/" + fileName;
      PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: SUCCESS - Table ready at: %s", cachedVPXPath.c_str());

      return cachedVPXPath;
   } catch (const std::exception& ex) {
      PLOGE.printf("VPinballLib::PrepareSAFTableForLoading: EXCEPTION: %s", ex.what());
      return "";
   }
}

void VPinballLib::SyncSAFCacheBack()
{
   if (m_safTableDir.empty() || m_safCachePath.empty()) {
      PLOGI.printf("VPinballLib::SyncSAFCacheBack: No SAF table loaded, skipping sync");
      return;
   }

   PLOGI.printf("VPinballLib::SyncSAFCacheBack: START - Syncing cache back to SAF");
   PLOGI.printf("VPinballLib::SyncSAFCacheBack: Cache path: %s", m_safCachePath.c_str());
   PLOGI.printf("VPinballLib::SyncSAFCacheBack: SAF path: %s", m_safTableDir.c_str());

   try {
      // Copy entire cache directory back to SAF (recursive, overwrites existing files)
      // This captures:
      // - Modified .ini files
      // - New/modified nvram files (PinMAME)
      // - Any other plugin-generated files in subdirectories
      if (!FileSystem::CopyDirectory(m_safCachePath, m_safTableDir)) {
         PLOGE.printf("VPinballLib::SyncSAFCacheBack: FAILED to sync cache back to SAF");
      } else {
         PLOGI.printf("VPinballLib::SyncSAFCacheBack: SUCCESS - All changes synced back to SAF");
      }
   } catch (const std::exception& ex) {
      PLOGE.printf("VPinballLib::SyncSAFCacheBack: EXCEPTION: %s", ex.what());
   }

   // Clear the saved paths
   m_safTableDir.clear();
   m_safCachePath.clear();
}
#endif  // __ANDROID__

VPINBALL_STATUS VPinballLib::LoadTable(const string& uuid)
{
   PLOGI.printf("VPinballLib::Load: Loading table with UUID: %s", uuid.c_str());

   // Clear previous table state and store new UUID
   m_loadedTableUuid = uuid;
   m_loadedTableSafPath.clear();
   m_tableImageCaptureReady = false;
   m_tableImageCaptureSuccess = false;
   m_tableImageCachePath.clear();

   Table* table = m_tableManager.GetTable(uuid);
   if (!table) {
      PLOGE.printf("VPinballLib::Load: Table not found in registry");
      m_loadedTableUuid.clear();
      return VPINBALL_STATUS_FAILURE;
   }

   if (!table->fullPath) {
      PLOGE.printf("VPinballLib::Load: Table fullPath is NULL");
      return VPINBALL_STATUS_FAILURE;
   }

   string loadPath = table->fullPath;
   PLOGI.printf("VPinballLib::Load: Table fullPath: %s", loadPath.c_str());

#ifdef __ANDROID__
   // If this is a SAF table, copy it to cache first
   if (FileSystem::IsSAFUri(loadPath)) {
      PLOGI.printf("VPinballLib::Load: SAF table detected, preparing for loading");
      m_loadedTableSafPath = table->fullPath;
      loadPath = PrepareSAFTableForLoading(table->fullPath);
      if (loadPath.empty()) {
         PLOGE.printf("VPinballLib::Load: Failed to prepare SAF table");
         m_loadedTableSafPath.clear();
         return VPINBALL_STATUS_FAILURE;
      }
      PLOGI.printf("VPinballLib::Load: Loading from cache: %s", loadPath.c_str());
   } else {
      m_loadedTableSafPath.clear();
   }
#else
   m_loadedTableSafPath.clear();
#endif

   PLOGI.printf("VPinballLib::Load: Calling LoadFileName with: %s", loadPath.c_str());
   VPXProgress progress;
   g_pvp->LoadFileName(loadPath, true, &progress);

   bool success = g_pvp->GetActiveTable() != nullptr;
   PLOGI.printf("VPinballLib::Load: LoadFileName result: %s", success ? "SUCCESS" : "FAILURE");

   return success ? VPINBALL_STATUS_SUCCESS : VPINBALL_STATUS_FAILURE;
}

VPINBALL_STATUS VPinballLib::ExtractTableScript()
{
   CComObject<PinTable>* const pActiveTable = g_pvp->GetActiveTable();
   if (!pActiveTable)
      return VPINBALL_STATUS_FAILURE;

   string filename = pActiveTable->m_filename;
   if (!ReplaceExtensionFromFilename(filename, "vbs"))
      return VPINBALL_STATUS_FAILURE;

   pActiveTable->m_pcv->SaveToFile(filename);
   g_pvp->CloseTable(pActiveTable);

#ifdef __ANDROID__
   // If this was a SAF table, copy the VBS back to SAF
   if (!m_loadedTableSafPath.empty()) {
      PLOGI.printf("VPinballLib::ExtractScript: SAF table detected, copying VBS back to SAF");

      std::filesystem::path safPath(m_loadedTableSafPath);
      string safVbsPath = safPath.parent_path().string() + "/" + safPath.stem().string() + ".vbs";

      PLOGI.printf("VPinballLib::ExtractScript: Copying %s to %s", filename.c_str(), safVbsPath.c_str());

      if (!FileSystem::CopyFile(filename, safVbsPath)) {
         PLOGE.printf("VPinballLib::ExtractScript: Failed to copy VBS to SAF");
         return VPINBALL_STATUS_FAILURE;
      }

      PLOGI.printf("VPinballLib::ExtractScript: Successfully copied VBS to SAF");
   }
#endif

   return VPINBALL_STATUS_SUCCESS;
}

VPINBALL_STATUS VPinballLib::Play()
{
   if (m_gameLoop)
      return VPINBALL_STATUS_FAILURE;

   CComObject<PinTable>* const pActiveTable = g_pvp->GetActiveTable();
   if (!pActiveTable)
      return VPINBALL_STATUS_FAILURE;

   if (!SDL_RunOnMainThread([](void*) {
      g_pvp->DoPlay(0);
   }, nullptr, true))
      return VPINBALL_STATUS_FAILURE;

   return VPINBALL_STATUS_SUCCESS;
}

VPINBALL_STATUS VPinballLib::Stop()
{
   CComObject<PinTable>* const pActiveTable = g_pvp->GetActiveTable();
   if (!pActiveTable)
      return VPINBALL_STATUS_FAILURE;

   pActiveTable->QuitPlayer(Player::CS_CLOSE_APP);

   return VPINBALL_STATUS_SUCCESS;
}

bool VPinballLib::ShouldCaptureTableImage()
{
   if (m_loadedTableUuid.empty())
      return false;

   Table* table = m_tableManager.GetTable(m_loadedTableUuid);
   if (!table)
      return false;

   // Check if table already has image
   if (table->image && table->image[0] != '\0')
      return false;

   return true;
}

void VPinballLib::RequestTableImageCapture()
{
   Table* table = m_tableManager.GetTable(m_loadedTableUuid);
   if (!table) {
      PLOGE.printf("VPinballLib::RequestTableImageCapture: Table not found");
      return;
   }

   // Determine image path based on whether it's SAF or regular filesystem
   string imagePath;

   if (!m_loadedTableSafPath.empty()) {
      // SAF table - screenshot to cache, we'll copy to SAF in callback
      string cacheDir = string(g_pvp->m_myPrefPath) + "image_temp";
      FileSystem::CreateDirectories(cacheDir);

      imagePath = cacheDir + "/" + m_loadedTableUuid + ".jpg";
      m_tableImageCachePath = imagePath;

      PLOGI.printf("VPinballLib::RequestTableImageCapture: SAF table, saving to cache: %s", imagePath.c_str());
   } else {
      // Regular filesystem - screenshot directly to final location
      string fullPath = table->fullPath;
      size_t lastDot = fullPath.find_last_of('.');
      imagePath = (lastDot != string::npos) ? fullPath.substr(0, lastDot) + ".jpg" : fullPath + ".jpg";
      m_tableImageCachePath = imagePath;

      PLOGI.printf("VPinballLib::RequestTableImageCapture: Regular table, saving to: %s", imagePath.c_str());
   }

   // Request the screenshot
   g_pplayer->m_renderer->m_renderDevice->CaptureScreenshot(imagePath,
      [this](bool success) {
         OnTableImageCaptured(success);
      });
}

void VPinballLib::OnTableImageCaptured(bool success)
{
   // Called from render thread - just set flags, don't do JNI/SAF work here
   PLOGI.printf("VPinballLib::OnTableImageCaptured: success=%d (render thread)", success);
   m_tableImageCaptureSuccess = success;
   m_tableImageCaptureReady = true;
}

void VPinballLib::ProcessTableImageCapture()
{
   // Called from main thread - safe to do JNI/SAF operations here
   PLOGI.printf("VPinballLib::ProcessTableImageCapture: Processing table image capture on main thread");

   if (!m_tableImageCaptureSuccess) {
      PLOGE.printf("VPinballLib::ProcessTableImageCapture: Screenshot capture failed");
      m_tableImageCaptureReady = false;
      m_tableImageCachePath.clear();
      m_loadedTableUuid.clear();
      m_loadedTableSafPath.clear();
      return;
   }

   Table* table = m_tableManager.GetTable(m_loadedTableUuid);
   if (!table) {
      PLOGE.printf("VPinballLib::ProcessTableImageCapture: Table not found");
      m_tableImageCaptureReady = false;
      m_tableImageCachePath.clear();
      m_loadedTableUuid.clear();
      m_loadedTableSafPath.clear();
      return;
   }

   string finalTableImageRelativePath;

   // If this was a SAF table, copy from cache to SAF
   if (!m_loadedTableSafPath.empty() && !m_tableImageCachePath.empty()) {
      PLOGI.printf("VPinballLib::ProcessTableImageCapture: SAF table, copying from cache to SAF");

      // Build SAF table image path
      size_t lastSlash = m_loadedTableSafPath.find_last_of('/');
      size_t lastDot = m_loadedTableSafPath.find_last_of('.');
      string parentPath = (lastSlash != string::npos) ? m_loadedTableSafPath.substr(0, lastSlash) : "";
      string stem = (lastSlash != string::npos && lastDot != string::npos && lastDot > lastSlash)
         ? m_loadedTableSafPath.substr(lastSlash + 1, lastDot - lastSlash - 1)
         : m_loadedTableSafPath;
      string safImagePath = parentPath + "/" + stem + ".jpg";

      PLOGI.printf("VPinballLib::ProcessTableImageCapture: Copying %s to %s", m_tableImageCachePath.c_str(), safImagePath.c_str());

      if (!FileSystem::CopyFile(m_tableImageCachePath, safImagePath)) {
         PLOGE.printf("VPinballLib::ProcessTableImageCapture: Failed to copy table image to SAF");
         m_tableImageCaptureReady = false;
         m_tableImageCachePath.clear();
         m_loadedTableUuid.clear();
         m_loadedTableSafPath.clear();
         return;
      }

      // Clean up cache file
      if (!FileSystem::Delete(m_tableImageCachePath)) {
         PLOGW.printf("VPinballLib::ProcessTableImageCapture: Failed to remove cache file");
      }

      // Extract relative path for table metadata
      string relativePath = FileSystem::ExtractRelativePath(safImagePath);
      finalTableImageRelativePath = relativePath;
   } else {
      // Regular filesystem - table image is already at final location
      // Build table image path (same logic as in RequestTableImageCapture)
      string fullPath = table->fullPath;
      size_t lastDot = fullPath.find_last_of('.');
      string imagePath = (lastDot != string::npos) ? fullPath.substr(0, lastDot) + ".jpg" : fullPath + ".jpg";

      // Make relative to tables path
      string tablesPath = m_tableManager.GetTablesPath();
      if (imagePath.find(tablesPath) == 0) {
         finalTableImageRelativePath = imagePath.substr(tablesPath.length());
         // Remove leading slash if present
         if (!finalTableImageRelativePath.empty() && finalTableImageRelativePath[0] == '/')
            finalTableImageRelativePath = finalTableImageRelativePath.substr(1);
      } else {
         finalTableImageRelativePath = imagePath;
      }
   }

   PLOGI.printf("VPinballLib::ProcessTableImageCapture: Setting table image path: %s", finalTableImageRelativePath.c_str());

   // Update table metadata
   SetTableImage(m_loadedTableUuid, finalTableImageRelativePath);

   // Trigger table list refresh with focus on this table
   RefreshTables(m_loadedTableUuid);

   // Clear table image capture state and table info
   m_tableImageCaptureReady = false;
   m_tableImageCachePath.clear();
   m_loadedTableUuid.clear();
   m_loadedTableSafPath.clear();
}

bool VPinballLib::FileExists(const string& path)
{
   bool exists = FileSystem::Exists(path);
   PLOGI.printf("VPinballLib::FileExists: path='%s' exists=%d", path.c_str(), exists);
   return exists;
}

bool VPinballLib::DeleteFile(const string& path)
{
   bool deleted = FileSystem::Delete(path);
   PLOGI.printf("VPinballLib::DeleteFile: path='%s' deleted=%d", path.c_str(), deleted);
   return deleted;
}

string VPinballLib::PrepareFileForViewing(const string& path)
{
   PLOGI.printf("VPinballLib::PrepareFileForViewing: Called with path: %s", path.c_str());

   // If not a SAF path, return as-is
   if (!FileSystem::IsSAFUri(path)) {
      PLOGI.printf("VPinballLib::PrepareFileForViewing: Not SAF, returning as-is: %s", path.c_str());
      return path;
   }

   PLOGI.printf("VPinballLib::PrepareFileForViewing: SAF file detected, copying to cache: %s", path.c_str());

   // Extract relative path from content:// URI
   string relativePath = FileSystem::ExtractRelativePath(path);

   // Create cache path with full directory structure
   string cachePath = string(g_pvp->m_myPrefPath) + "saf_cache";
   string cachedFilePath = cachePath + "/" + relativePath;

   // Create parent directories if needed
   std::filesystem::path cachedFilePathObj(cachedFilePath);
   std::filesystem::path parentDir = cachedFilePathObj.parent_path();
   if (!std::filesystem::exists(parentDir)) {
      std::filesystem::create_directories(parentDir);
   }

   // Copy file from SAF to cache
   if (!FileSystem::CopyFile(path, cachedFilePath)) {
      PLOGE.printf("VPinballLib::PrepareFileForViewing: Failed to copy SAF file to cache");
      return "";
   }

   PLOGI.printf("VPinballLib::PrepareFileForViewing: File ready at: %s", cachedFilePath.c_str());
   return cachedFilePath;
}

void VPinballLib::Cleanup()
{
#ifdef __ANDROID__
   SyncSAFCacheBack();
#endif

   CComObject<PinTable>* pActiveTable = g_pvp->GetActiveTable();
   if (pActiveTable)
      g_pvp->CloseTable(pActiveTable);

   m_tableManager.ClearAll();
}



// Table Management Functions Implementation

VPINBALL_STATUS VPinballLib::RefreshTables(const string& focusUuid)
{
   PLOGI.printf("VPinball::RefreshTables: Starting table refresh via VPinballTableManager");

   VPINBALL_STATUS refreshStatus = m_tableManager.RefreshTables();
   if (refreshStatus != VPINBALL_STATUS_SUCCESS) {
      PLOGE.printf("VPinball::RefreshTables: Failed to refresh tables");
      return refreshStatus;
   }

   // Send event to mobile apps with optional focusUuid
   PLOGI.printf("VPinball::RefreshTables: Sending TABLE_LIST_UPDATED event to mobile apps (focusUuid='%s')", focusUuid.c_str());

   if (!focusUuid.empty()) {
      // Send with focus UUID in JSON
      thread_local string jsonData;
      jsonData = "{\"focusUuid\":\"" + focusUuid + "\"}";
      PLOGI.printf("VPinball::RefreshTables: Sending with JSON: %s", jsonData.c_str());
      SendEvent(VPINBALL_EVENT_TABLE_LIST_UPDATED, (void*)jsonData.c_str());
   } else {
      PLOGI.printf("VPinball::RefreshTables: Sending with nullptr (no focusUuid)");
      SendEvent(VPINBALL_EVENT_TABLE_LIST_UPDATED, nullptr);
   }

   PLOGI.printf("VPinball::RefreshTables: Table refresh completed successfully");
   return VPINBALL_STATUS_SUCCESS;
}

VPINBALL_STATUS VPinballLib::RefreshTables()
{
   return RefreshTables("");
}

VPINBALL_STATUS VPinballLib::ReloadTablesPath()
{
   PLOGI.printf("VPinball::ReloadTablesPath: Reloading tables path");

   VPINBALL_STATUS status = m_tableManager.ReloadTablesPath();
   if (status == VPINBALL_STATUS_SUCCESS) {
      PLOGI.printf("VPinball::ReloadTablesPath: Successfully reloaded tables path");
      // Send TABLE_LIST_UPDATED event to notify mobile apps
      // Note: Don't call RefreshTables() here as it would do another reconciliation
      PLOGI.printf("VPinball::ReloadTablesPath: Sending TABLE_LIST_UPDATED event");
      SendEvent(VPINBALL_EVENT_TABLE_LIST_UPDATED, nullptr);
   } else {
      PLOGE.printf("VPinball::ReloadTablesPath: Failed to reload tables path");
   }

   return status;
}

VPINBALL_STATUS VPinballLib::ImportTable(const string& sourceFile)
{
   VPINBALL_STATUS status = m_tableManager.ImportTable(sourceFile);

   if (status == VPINBALL_STATUS_SUCCESS)
      RefreshTables();

   return status;
}

VPINBALL_STATUS VPinballLib::SetTableImage(const string& uuid, const string& imagePath)
{
   VPINBALL_STATUS status = m_tableManager.SetTableImage(uuid, imagePath);

   if (status == VPINBALL_STATUS_SUCCESS)
      RefreshTables(uuid);

   return status;
}

string VPinballLib::ExportTable(const string& uuid)
{
   return m_tableManager.ExportTable(uuid);
}

void VPinballLib::CleanupTable(Table& table)
{
   delete[] table.uuid;
   delete[] table.name;
   delete[] table.fileName;
   delete[] table.fullPath;
   delete[] table.path;
   delete[] table.image;
}

void VPinballLib::GetTables(TablesData& tablesData)
{
   m_tableManager.GetAllTables(tablesData);
}

void VPinballLib::FreeTablesData(TablesData& tablesData)
{
   if (tablesData.tables) {
      for (int i = 0; i < tablesData.tableCount; i++) {
         CleanupTable(tablesData.tables[i]);
      }
      delete[] tablesData.tables;
      tablesData.tables = nullptr;
   }
   tablesData.tableCount = 0;
}

const char* VPinballLib::GetTables()
{
   return m_tableManager.GetTablesJsonCopy();
}

const char* VPinballLib::GetTablesPath()
{
   return m_tableManager.GetTablesPath().c_str();
}

VPINBALL_STATUS VPinballLib::RenameTable(const string& uuid, const string& newName)
{
   VPINBALL_STATUS status = m_tableManager.RenameTable(uuid, newName);

   if (status == VPINBALL_STATUS_SUCCESS)
      RefreshTables(uuid);

   return status;
}

VPINBALL_STATUS VPinballLib::DeleteTable(const string& uuid)
{
   VPINBALL_STATUS status = m_tableManager.DeleteTable(uuid);

   if (status == VPINBALL_STATUS_SUCCESS)
      RefreshTables();

   return status;
}

}
