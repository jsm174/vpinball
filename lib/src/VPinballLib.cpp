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

VPinballLib::VPinballLib() : m_pTableManager(nullptr)
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
   delete m_pTableManager;
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
#endif

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

   LoadPlugins();

   m_pTableManager = new TableManager();
   m_pTableManager->SetSystemEventCallback([this](VPINBALL_EVENT event, void* data) -> void* {
      return this->SendEvent(event, data);
   });

   // Complete deferred initialization for content:// URIs (must be after m_pTableManager is assigned)
   m_pTableManager->CompleteDeferredInitialization();

   // Clean up any SAF cache from previous sessions
   CleanupSAFCache();

#ifdef __APPLE__
   props = SDL_GetWindowProperties(m_pWindow);
   s_pBridgeCallback((void*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL));
#endif

#ifdef __ANDROID__
   SDL_SendAndroidMessage(0x8000 + 1, 0);
#endif

   return 1;
}

void VPinballLib::AppIterate()
{
   if (m_gameLoop) {
      m_gameLoop();

      if (g_pplayer && (g_pplayer->GetCloseState() == Player::CS_PLAYING || g_pplayer->GetCloseState() == Player::CS_USER_INPUT))
         return;

      PLOGI.printf("Game Loop stopping");

      m_gameLoop = nullptr;

      delete g_pplayer;
      g_pplayer = nullptr;

      CComObject<PinTable>* pActiveTable = g_pvp->GetActiveTable();
      if (pActiveTable)
         g_pvp->CloseTable(pActiveTable);
   }
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
            case VPINBALL_EVENT_CAPTURE_SCREENSHOT: {
               CaptureScreenshotData* screenshotData = (CaptureScreenshotData*)data;
               j["success"] = screenshotData->success;
               jsonString = j.dump();
               jsonData = jsonString.c_str();
               break;
            }
            default:
               break;
         }
      }

      return callback(event, jsonData, data);
   };
}

bool VPinballLib::PollEvent(SDL_Event& event)
{
   std::lock_guard<std::mutex> lock(m_eventMutex);
   if (m_eventQueue.empty())
      return false;

   event = m_eventQueue.front();
   m_eventQueue.pop();
   return true;
}

void* VPinballLib::SendEvent(VPINBALL_EVENT event, void* data)
{
   if (event == VPINBALL_EVENT_PLAYER_STARTED)
      SetWebServerUpdated();
   else if (event == VPINBALL_EVENT_STOPPED)
      Cleanup();

   return m_eventCallback ? m_eventCallback(event, data) : nullptr;
}

void VPinballLib::UpdateWebServer()
{
   if (m_webServer.IsRunning())
      m_webServer.Stop();

   if (g_pvp->m_settings.LoadValueWithDefault(Settings::Standalone, "WebServer"s, false))
      m_webServer.Start();
}

void VPinballLib::StartWebServer()
{
   if (!m_webServer.IsRunning())
      m_webServer.Start();
}

void VPinballLib::StopWebServer()
{
   if (m_webServer.IsRunning())
      m_webServer.Stop();
}

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
      string tablesPath = m_pTableManager->GetTablesPath();
      string safTableDir = tablesPath + "/" + tableDir;
      PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: Starting copy from SAF: %s to cache: %s", safTableDir.c_str(), tableCachePath.c_str());

      if (!FileSystem::CopyDirectory(safTableDir, tableCachePath)) {
         PLOGE.printf("VPinballLib::PrepareSAFTableForLoading: FAILED to copy table directory from SAF");
         return "";
      }

      PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: Copy completed successfully");

      // Return the cache path to the VPX file
      string cachedVPXPath = tableCachePath + "/" + fileName;
      PLOGI.printf("VPinballLib::PrepareSAFTableForLoading: SUCCESS - Table ready at: %s", cachedVPXPath.c_str());

      return cachedVPXPath;
   } catch (const std::exception& ex) {
      PLOGE.printf("VPinballLib::PrepareSAFTableForLoading: EXCEPTION: %s", ex.what());
      return "";
   }
}

VPINBALL_STATUS VPinballLib::Load(const string& uuid)
{
   PLOGI.printf("VPinballLib::Load: Loading table with UUID: %s", uuid.c_str());

   VPXTable* table = m_pTableManager->GetTable(uuid);
   if (!table) {
      PLOGE.printf("VPinballLib::Load: Table not found in registry");
      return VPINBALL_STATUS_FAILURE;
   }

   if (!table->fullPath) {
      PLOGE.printf("VPinballLib::Load: Table fullPath is NULL");
      return VPINBALL_STATUS_FAILURE;
   }

   string loadPath = table->fullPath;
   PLOGI.printf("VPinballLib::Load: Table fullPath: %s", loadPath.c_str());

   // If this is a SAF table, copy it to cache first
   if (FileSystem::IsSpecialUri(loadPath)) {
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

   PLOGI.printf("VPinballLib::Load: Calling LoadFileName with: %s", loadPath.c_str());
   VPXProgress progress;
   g_pvp->LoadFileName(loadPath, true, &progress);

   bool success = g_pvp->GetActiveTable() != nullptr;
   PLOGI.printf("VPinballLib::Load: LoadFileName result: %s", success ? "SUCCESS" : "FAILURE");

   return success ? VPINBALL_STATUS_SUCCESS : VPINBALL_STATUS_FAILURE;
}

VPINBALL_STATUS VPinballLib::ExtractScript()
{
   CComObject<PinTable>* const pActiveTable = g_pvp->GetActiveTable();
   if (!pActiveTable)
      return VPINBALL_STATUS_FAILURE;

   string filename = pActiveTable->m_filename;
   if (!ReplaceExtensionFromFilename(filename, "vbs"))
      return VPINBALL_STATUS_FAILURE;

   pActiveTable->m_pcv->SaveToFile(filename);
   g_pvp->CloseTable(pActiveTable);

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

   return VPINBALL_STATUS_SUCCESS;
}

VPINBALL_STATUS VPinballLib::Play()
{
   if (m_gameLoop)
      return VPINBALL_STATUS_FAILURE;

   CComObject<PinTable>* const pActiveTable = g_pvp->GetActiveTable();
   if (!pActiveTable)
      return VPINBALL_STATUS_FAILURE;

   if (SDL_IsMainThread())
      g_pvp->DoPlay(0);
   else {
        SDL_MainThreadCallback cb = [](void*) {
            g_pvp->DoPlay(0);
        };
        if (!SDL_RunOnMainThread(cb, nullptr, true))
           return VPINBALL_STATUS_FAILURE;
   }

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

bool VPinballLib::FileExists(const string& path)
{
   bool exists = FileSystem::Exists(path);
   PLOGI.printf("VPinballLib::FileExists: path='%s' exists=%d", path.c_str(), exists);
   return exists;
}

string VPinballLib::PrepareFileForViewing(const string& path)
{
   PLOGI.printf("VPinballLib::PrepareFileForViewing: Called with path: %s", path.c_str());

   // If not a SAF path, return as-is
   if (!FileSystem::IsSpecialUri(path)) {
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

void VPinballLib::SetWebServerUpdated()
{
    m_webServer.SetLastUpdate();
}

void VPinballLib::Cleanup()
{
   CComObject<PinTable>* pActiveTable = g_pvp->GetActiveTable();
   if (pActiveTable)
      g_pvp->CloseTable(pActiveTable);

   m_pTableManager->ClearAll();
}



// VPXTable Management Functions Implementation

VPINBALL_STATUS VPinballLib::RefreshTables(const string& focusUuid)
{
   PLOGI.printf("VPinball::RefreshTables: Starting table refresh via VPinballTableManager");

   VPINBALL_STATUS refreshStatus = m_pTableManager->RefreshTables();
   if (refreshStatus != VPINBALL_STATUS_SUCCESS) {
      PLOGE.printf("VPinball::RefreshTables: Failed to refresh tables");
      return refreshStatus;
   }

   // Send event to mobile apps with optional focusUuid
   PLOGI.printf("VPinball::RefreshTables: Sending TABLE_LIST_UPDATED event to mobile apps");

   if (!focusUuid.empty()) {
      // Send with focus UUID in JSON
      thread_local string jsonData;
      jsonData = "{\"focusUuid\":\"" + focusUuid + "\"}";
      SendEvent(VPINBALL_EVENT_TABLE_LIST_UPDATED, (void*)jsonData.c_str());
   } else {
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

   VPINBALL_STATUS status = m_pTableManager->ReloadTablesPath();
   if (status == VPINBALL_STATUS_SUCCESS) {
      PLOGI.printf("VPinball::ReloadTablesPath: Successfully reloaded tables path");
   } else {
      PLOGE.printf("VPinball::ReloadTablesPath: Failed to reload tables path");
   }

   return status;
}

VPINBALL_STATUS VPinballLib::ImportTable(const string& sourceFile)
{
   return m_pTableManager->ImportTable(sourceFile);
}

VPINBALL_STATUS VPinballLib::SetTableArtwork(const string& uuid, const string& artworkPath)
{
   return m_pTableManager->SetTableArtwork(uuid, artworkPath);
}

string VPinballLib::ExportTable(const string& uuid)
{
   return m_pTableManager->ExportTable(uuid);
}

void VPinballLib::CleanupVPXTable(VPXTable& table)
{
   delete[] table.uuid;
   delete[] table.name;
   delete[] table.fileName;
   delete[] table.fullPath;
   delete[] table.path;
   delete[] table.artwork;
}

void VPinballLib::NotifyTableEvent(VPINBALL_EVENT event, void* data)
{
   if (m_eventCallback) {
      m_eventCallback(event, data);
   }
}

void VPinballLib::GetVPXTables(VPXTablesData& tablesData)
{
   m_pTableManager->GetAllTables(tablesData);
}

void VPinballLib::FreeVPXTablesData(VPXTablesData& tablesData)
{
   if (tablesData.tables) {
      for (int i = 0; i < tablesData.tableCount; i++) {
         CleanupVPXTable(tablesData.tables[i]);
      }
      delete[] tablesData.tables;
      tablesData.tables = nullptr;
   }
   tablesData.tableCount = 0;
}

const char* VPinballLib::GetTables()
{
   return m_pTableManager->GetTablesJsonCopy();
}

const char* VPinballLib::GetTablesPath()
{
   return m_pTableManager->GetTablesPath().c_str();
}

VPINBALL_STATUS VPinballLib::RenameTable(const string& uuid, const string& newName)
{
   return m_pTableManager->RenameTable(uuid, newName);
}

VPINBALL_STATUS VPinballLib::DeleteTable(const string& uuid)
{
   return m_pTableManager->DeleteTable(uuid);
}

}
