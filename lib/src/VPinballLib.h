#pragma once

#include <string>
#include <functional>
#include <queue>
#include <mutex>
#include "../include/vpinball/VPinballLib_C.h"
#include "WebServer.h"
#include "TableManager.h"
#include "core/vpversion.h"

namespace VPinballLib {

using std::string;
using std::vector;

struct ProgressData {
   int progress;
};

struct RumbleData {
   uint16_t lowFrequencyRumble;
   uint16_t highFrequencyRumble;
   uint32_t durationMs;
};

struct ScriptErrorData {
   VPINBALL_SCRIPT_ERROR_TYPE error;
   int line;
   int position;
   const char* description;
};

struct WebServerData {
   const char* url;
};

struct CaptureScreenshotData {
   bool success;
};

class VPinballLib
{
public:
   static VPinballLib& Instance()
   {
      static VPinballLib inst;
      return inst;
   }

   int AppInit(int argc, char** argv);
   void AppIterate();
   void AppEvent(SDL_Event* event);

   SDL_Window* GetWindow() { return m_pWindow; }
#ifdef __APPLE__
   void* GetMetalLayer() { return m_pMetalLayer; }
#endif
   bool PollEvent(SDL_Event& event);
   void* SendEvent(VPINBALL_EVENT event, void* data);
   void SetEventCallback(VPinballEventCallback callback);

   string GetVersionStringFull() { return VP_VERSION_STRING_FULL_LITERAL; };

   void Log(VPINBALL_LOG_LEVEL level, const string& message);
   void ResetLog();

   int LoadValueInt(const string& sectionName, const string& key, int defaultValue);
   float LoadValueFloat(const string& sectionName, const string& key, float defaultValue);
   string LoadValueString(const string& sectionName, const string& key, const string& defaultValue);
   void SaveValueInt(const string& sectionName, const string& key, int value);
   void SaveValueFloat(const string& sectionName, const string& key, float value);
   void SaveValueString(const string& sectionName, const string& key, const string& value);
   VPINBALL_STATUS ResetIni();

   void StartWebServer();
   void StopWebServer();

   VPINBALL_STATUS Load(const string& uuid);
   VPINBALL_STATUS ExtractScript();
   VPINBALL_STATUS Play();
   VPINBALL_STATUS Stop();

   // File utilities
   bool FileExists(const string& path);
   string PrepareFileForViewing(const string& path);

   // SAF table cache management
   void CleanupSAFCache();
   string PrepareSAFTableForLoading(const string& safPath);

   VPINBALL_STATUS RefreshTables();
   VPINBALL_STATUS RefreshTables(const string& focusUuid);
   VPINBALL_STATUS ReloadTablesPath();
   const char* GetTables();
   const char* GetTablesPath();
   VPINBALL_STATUS RenameTable(const string& uuid, const string& newName);
   VPINBALL_STATUS DeleteTable(const string& uuid);
   VPINBALL_STATUS ImportTable(const string& sourceFile);
   VPINBALL_STATUS SetTableArtwork(const string& uuid, const string& artworkPath);
   string ExportTable(const string& uuid);

   void SetGameLoop(std::function<void()> gameLoop) { m_gameLoop = gameLoop; }

   void GetVPXTables(VPXTablesData& tablesData);
   void FreeVPXTablesData(VPXTablesData& tablesData);


   void UpdateWebServer();
   void SetWebServerUpdated();
   void NotifyTableEvent(VPINBALL_EVENT event, void* data);
   void CleanupVPXTable(VPXTable& table);
   void Cleanup();

private:
   VPinballLib();
   ~VPinballLib();
   VPinballLib(const VPinballLib&) = delete;
   VPinballLib& operator=(const VPinballLib&) = delete;

   void LoadPlugins();
   void UnloadPlugins();

   SDL_Window* m_pWindow = nullptr;
#ifdef __APPLE__
   void* m_pMetalLayer = nullptr;
#endif

   WebServer m_webServer;
   TableManager* m_pTableManager;

   vector<std::shared_ptr<MsgPI::MsgPlugin>> m_plugins;
   std::function<void*(VPINBALL_EVENT, void*)> m_eventCallback = nullptr;
   std::function<void()> m_gameLoop = nullptr;
   bool m_scanningTables;
   std::queue<SDL_Event> m_eventQueue;
   std::mutex m_eventMutex;
   string m_loadedTableSafPath;
};

}

#ifdef __cplusplus
extern "C" {
#endif
void VPinball_SetBridgeCallback(void (*callback)(void*));
#ifdef __cplusplus
}
#endif
