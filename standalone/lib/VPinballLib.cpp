#include "core/stdafx.h"

#include "core/vpversion.h"
#include "core/TableDB.h"
#include "core/VPXPluginAPIImpl.h"
#include "core/extern.h"
#include "VPinballLib.h"
#include "VPinballTableManager.h"
#include "VPXProgress.h"
#include "standalone/inc/webserver/WebServer.h"

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

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

using json = nlohmann::json;

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
static VPinballLib::VPinball* s_vpinball = nullptr;

// ObjC Bridge

extern "C" void VPinball_SetBridgeCallback(void (*callback)(void*))
{
   s_pBridgeCallback = callback;
}

// SDL Main Callbacks

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
   if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
       return SDL_APP_FAILURE;

   SDL_Window* pWindow = SDL_CreateWindow("Visual Pinball Player", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
   if (!pWindow)
       return SDL_APP_FAILURE;
    
   s_vpinball = new VPinballLib::VPinball(pWindow, argc, argv);

   return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
   s_vpinball->AppIterate();
   return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
   s_vpinball->AppEvent(event);
   return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
}

// Main Library

namespace VPinballLib {

VPinball::VPinball(SDL_Window* pWindow, int argc, char **argv) 
   : m_pWindow(pWindow)
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

#ifdef __APPLE__
   m_pMetalLayer = SDL_GetRenderMetalLayer(SDL_CreateRenderer(m_pWindow, "Metal"));
   SDL_PropertiesID props = SDL_GetWindowProperties(m_pWindow);
   s_pBridgeCallback((void*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL));

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
}

VPinball::~VPinball()
{
}

VPinball& VPinball::GetInstance()
{
   return *s_vpinball;
}

void VPinball::AppIterate()
{
   if (m_gameLoop) {
      m_gameLoop();

      if (g_pplayer && (g_pplayer->GetCloseState() == Player::CS_PLAYING || g_pplayer->GetCloseState() == Player::CS_USER_INPUT))
         return;

      PLOGI.printf("Game Loop stopping");

      m_gameLoop = nullptr;

      delete g_pplayer;
      g_pplayer = nullptr;

      string stoppedJson = "{}";
      SendEvent(Event::Stopped, (void*)stoppedJson.c_str());

      SetWebServerUpdated();
   }
}

void VPinball::AppEvent(SDL_Event* event)
{
   std::lock_guard<std::mutex> lock(m_eventMutex);
      
   if (m_gameLoop)
      m_eventQueue.push(*event);
   else {
      while (!m_eventQueue.empty())
         m_eventQueue.pop();
   }
}

void VPinball::Init(std::function<void*(Event, void*)> callback)
{
   m_pWebServer = new WebServer();
   m_eventCallback = callback;
   
   m_scanningTables = false;
   m_tableManager = new VPinballTableManager();
}

void VPinball::SetupEventCallback()
{
   PLOGI.printf("VPinball::SetupEventCallback: Setting up table management event callback");
   
   m_tableManager->SetEventCallback([this](TableEvent event, const VPXTable* table) {
      switch (event) {
         case TableEvent::RefreshStarted:
            PLOGI.printf("VPinball: Table refresh started - notifying mobile apps");
            SendEvent(Event::RefreshingTableList, nullptr);
            break;
         case TableEvent::RefreshCompleted:
            PLOGI.printf("VPinball: Table refresh completed - notifying mobile apps");
            SendEvent(Event::TableListRefreshComplete, nullptr);
            UpdateWebServer();
            break;
         case TableEvent::TablesChanged:
            PLOGI.printf("VPinball: Tables changed (import/delete/etc) - notifying mobile apps");
            SendEvent(Event::TableListRefreshComplete, nullptr);
            UpdateWebServer();
            break;
      }
   });
}

void VPinball::LoadPlugins()
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
         auto plugin = MsgPluginManager::GetInstance().RegisterPlugin(
            p.id, p.id, p.id,
            "", "", "",
            p.load, p.unload
         );
         plugin->Load(&MsgPluginManager::GetInstance().GetMsgAPI());
         m_plugins.push_back(plugin);
      }
   }
}

void VPinball::UnloadPlugins()
{
   for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it)
      (*it)->Unload();
   m_plugins.clear();
}

string VPinball::GetVersionStringFull()
{
   return VP_VERSION_STRING_FULL_LITERAL;
}

void VPinball::Log(LogLevel level, const string& message)
{
   switch (level) {
      case LogLevel::Debug: PLOGD << message; break;
      case LogLevel::Info: PLOGI << message; break;
      case LogLevel::Warn: PLOGW << message; break;
      case LogLevel::Error: PLOGE << message; break;
    }
}

void* VPinball::SendEvent(Event event, void* data)
{
   if (event == Event::PlayerStarted)
      SetWebServerUpdated();
   else if (event == Event::Stopped)
      Cleanup();

   return m_eventCallback ? m_eventCallback(event, data) : nullptr;
}

void VPinball::ResetLog()
{
   Logger::GetInstance()->Truncate();

   PLOGI << "VPX - " << VP_VERSION_STRING_FULL_LITERAL;
   PLOGI << "m_logicalNumberOfProcessors=" << g_pvp->GetLogicalNumberOfProcessors();
   PLOGI << "m_myPath=" << g_pvp->m_myPath;
   PLOGI << "m_myPrefPath=" << g_pvp->m_myPrefPath;
}

int VPinball::LoadValueInt(const string& sectionName, const string& key, int defaultValue)
{
   return g_pvp->m_settings.LoadValueWithDefault(Settings::GetSection(sectionName), key, defaultValue);
}

float VPinball::LoadValueFloat(const string& sectionName, const string& key, float defaultValue)
{
   return g_pvp->m_settings.LoadValueWithDefault(Settings::GetSection(sectionName), key, defaultValue);
}

string VPinball::LoadValueString(const string& sectionName, const string& key, const string& defaultValue)
{
   return g_pvp->m_settings.LoadValueWithDefault(Settings::GetSection(sectionName), key, defaultValue);
}

void VPinball::SaveValueInt(const string& sectionName, const string& key, int value)
{
   g_pvp->m_settings.SaveValue(Settings::GetSection(sectionName), key, value);
   g_pvp->m_settings.Save();
}

void VPinball::SaveValueFloat(const string& sectionName, const string& key, float value)
{
   g_pvp->m_settings.SaveValue(Settings::GetSection(sectionName), key, value);
   g_pvp->m_settings.Save();
}

void VPinball::SaveValueString(const string& sectionName, const string& key, const string& value)
{
   g_pvp->m_settings.SaveValue(Settings::GetSection(sectionName), key, value);
   g_pvp->m_settings.Save();
}

void VPinball::UpdateWebServer()
{
   if (m_pWebServer->IsRunning())
      m_pWebServer->Stop();

   if (g_pvp->m_settings.LoadValueWithDefault(Settings::Standalone, "WebServer"s, false))
      m_pWebServer->Start();
}

VPinballStatus VPinball::ResetIni()
{
   string iniFilePath = g_pvp->m_myPrefPath + "VPinballX.ini";
   if (std::remove(iniFilePath.c_str()) != 0)
      return VPinballStatus::Failure;

   g_pvp->m_settings.LoadFromFile(iniFilePath, true);
   g_pvp->m_settings.Save();
   return VPinballStatus::Success;
}

VPinballStatus VPinball::Load(const string& source)
{
   VPXProgress progress;
   g_pvp->LoadFileName(source, true, &progress);
   if (g_pvp->GetActiveTable())
      return VPinballStatus::Success;

   Cleanup();
   return VPinballStatus::Failure;
}

VPinballStatus VPinball::ExtractScript(const string& source)
{
   VPXProgress progress;
   bool success = false;
   g_pvp->LoadFileName(source, false, &progress);

   CComObject<PinTable>* const pActiveTable = g_pvp->GetActiveTable();
   if (pActiveTable) {
      string scriptFilename = source;
      if (ReplaceExtensionFromFilename(scriptFilename, "vbs"))
         pActiveTable->m_pcv->SaveToFile(scriptFilename);
      success = true;
   }

   Cleanup();
   return success ? VPinballStatus::Success : VPinballStatus::Failure;
}

VPinballStatus VPinball::Play()
{
   m_gameLoop = nullptr;
   CComObject<PinTable>* const pActiveTable = g_pvp->GetActiveTable();

   if (!pActiveTable)
      return VPinballStatus::Failure;

   g_pvp->DoPlay(0);

   return VPinballStatus::Success;
}

VPinballStatus VPinball::Stop()
{
   CComObject<PinTable>* const pActiveTable = g_pvp->GetActiveTable();

   if (!pActiveTable)
      return VPinballStatus::Failure;

   pActiveTable->QuitPlayer(Player::CS_CLOSE_APP);

   return VPinballStatus::Success;
}

void VPinball::ToggleFPS()
{
   if (!g_pplayer)
      return;

   g_pplayer->m_liveUI->ToggleFPS();
}

void VPinball::CaptureScreenshot(const string& filename)
{
   if (!g_pplayer)
      return;

   g_pplayer->m_renderer->m_renderDevice->CaptureScreenshot(filename.c_str(),
      [](bool success) {
         // Create JSON for screenshot event
         json screenshotJson;
         screenshotJson["success"] = success;
         string screenshotStr = screenshotJson.dump();
         GetInstance().SendEvent(Event::CaptureScreenshot, (void*)screenshotStr.c_str());
      });
}

void VPinball::SetWebServerUpdated()
{
   if (m_pWebServer)
      m_pWebServer->SetLastUpdate();
}

void VPinball::Cleanup()
{
   CComObject<PinTable>* pActiveTable = g_pvp->GetActiveTable();
   if (pActiveTable)
      g_pvp->CloseTable(pActiveTable);

   m_tableManager->ClearAll();
}

bool VPinball::GetQueuedEvent(SDL_Event& event)
{
   std::lock_guard<std::mutex> lock(m_eventMutex);
   if (m_eventQueue.empty())
      return false;

   event = m_eventQueue.front();
   m_eventQueue.pop();
   return true;
}

// VPXTable Management Functions Implementation

VPinballStatus VPinball::RefreshTables()
{
   PLOGI.printf("VPinball::RefreshTables: Starting table refresh via VPinballTableManager");
   
   VPinballStatus refreshStatus = m_tableManager->RefreshTables();
   if (refreshStatus != VPinballStatus::Success) {
      PLOGE.printf("VPinball::RefreshTables: Failed to refresh tables");
      return refreshStatus;
   }
   
   // Also directly send event to mobile apps to ensure they get notified
   PLOGI.printf("VPinball::RefreshTables: Sending TableListRefreshComplete event to mobile apps");
   SendEvent(Event::TableListRefreshComplete, nullptr);
   
   PLOGI.printf("VPinball::RefreshTables: Table refresh completed successfully");
   return VPinballStatus::Success;
}

VPinballStatus VPinball::ImportTableFile(const string& sourceFile)
{
   return m_tableManager->ImportTable(sourceFile);
}

VPinballStatus VPinball::SetTableArtwork(const string& uuid, const string& artworkPath)
{
   return m_tableManager->SetTableArtwork(uuid, artworkPath);
}

const char* VPinball::GetTablesPath()
{
   return m_tableManager->GetTablesPath().c_str();
}

string VPinball::ExportTable(const string& uuid)
{
   return m_tableManager->ExportTable(uuid);
}

void VPinball::CleanupVPXTable(VPXTable& table)
{
   delete[] table.uuid;
   delete[] table.name;
   delete[] table.fileName;
   delete[] table.fullPath;
   delete[] table.path;
   delete[] table.artwork;
}

void VPinball::NotifyTableEvent(Event event, void* data)
{
   if (m_eventCallback) {
      m_eventCallback(event, data);
   }
}

void VPinball::GetVPXTables(VPXTablesData& tablesData)
{
   PLOGI.printf("GetVPXTables: Delegating to VPinballTableManager");
   m_tableManager->GetAllTables(tablesData);
}

char* VPinball::GetTablesJson()
{
   return m_tableManager->GetTablesJsonCopy();
}

char* VPinball::GetTableJson(const string& uuid)
{
   return m_tableManager->GetTableJsonCopy(uuid);
}


void VPinball::FreeVPXTablesData(VPXTablesData& tablesData)
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

void VPinball::FreeVPXTable(VPXTable& table)
{
   CleanupVPXTable(table);
}


VPinballStatus VPinball::ImportVPXZ(const string& vpxzPath)
{
   return m_tableManager->ImportTable(vpxzPath);
}

VPinballStatus VPinball::GetVPXTable(const string& uuid, VPXTable& table)
{
   VPXTable* foundTable = m_tableManager->GetTable(uuid);
   if (foundTable) {
      table = *foundTable;
      return VPinballStatus::Success;
   }
   return VPinballStatus::Failure;
}

VPinballStatus VPinball::AddVPXTable(const string& filePath)
{
   if (!std::filesystem::exists(filePath)) {
      return VPinballStatus::Failure;
   }
   
   return m_tableManager->AddTable(filePath);
}

VPinballStatus VPinball::RemoveVPXTable(const string& uuid)
{
   // Delegate to VPinballTableManager for smart deletion logic
   return m_tableManager->DeleteTable(uuid);
}

VPinballStatus VPinball::RenameVPXTable(const string& uuid, const string& newName)
{
   return m_tableManager->RenameTable(uuid, newName);
}

}
