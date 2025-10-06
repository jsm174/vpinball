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
   bool PollAppEvent(SDL_Event& event);

   SDL_Window* GetWindow() { return m_pWindow; }
#ifdef __APPLE__
   void* GetMetalLayer() { return m_pMetalLayer; }
#endif

   string GetVersionStringFull() { return VP_VERSION_STRING_FULL_LITERAL; };
   void Startup(VPinballEventCallback callback);
   void SetEventCallback(VPinballEventCallback callback);
   static void SendEvent(VPINBALL_EVENT event, void* data);
   void Log(VPINBALL_LOG_LEVEL level, const string& message);
   void ResetLog();

   int LoadValueInt(const string& sectionName, const string& key, int defaultValue);
   float LoadValueFloat(const string& sectionName, const string& key, float defaultValue);
   string LoadValueString(const string& sectionName, const string& key, const string& defaultValue);
   void SaveValueInt(const string& sectionName, const string& key, int value);
   void SaveValueFloat(const string& sectionName, const string& key, float value);
   void SaveValueString(const string& sectionName, const string& key, const string& value);
   VPINBALL_STATUS ResetIni();

   void UpdateWebServer();

   const char* GetTables();
   const char* GetTablesPath();

   VPINBALL_STATUS RefreshTables();
   VPINBALL_STATUS RefreshTables(const string& focusUuid);
   VPINBALL_STATUS ReloadTablesPath();

   VPINBALL_STATUS RenameTable(const string& uuid, const string& newName);
   VPINBALL_STATUS DeleteTable(const string& uuid);
   VPINBALL_STATUS ImportTable(const string& sourceFile);
   VPINBALL_STATUS SetTableImage(const string& uuid, const string& imagePath);
   string ExportTable(const string& uuid);

   bool FileExists(const string& path);
   bool DeleteFile(const string& path);
   string PrepareFileForViewing(const string& path);

   // SAF table cache management (Android only)
#ifdef __ANDROID__
   void CleanupSAFCache();
   string PrepareSAFTableForLoading(const string& safPath);
   void SyncSAFCacheBack();
#endif

   VPINBALL_STATUS LoadTable(const string& uuid);
   VPINBALL_STATUS ExtractTableScript();
   VPINBALL_STATUS Play();
   VPINBALL_STATUS Stop();



  
   void SetGameLoop(std::function<void()> gameLoop) { m_gameLoop = gameLoop; }

   void GetTables(TablesData& tablesData);
   void FreeTablesData(TablesData& tablesData);

   void CleanupTable(Table& table);
   void Cleanup();

   bool ShouldCaptureTableImage();
   void RequestTableImageCapture();

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
   TableManager m_tableManager;

   vector<std::shared_ptr<MsgPI::MsgPlugin>> m_plugins;
   std::function<void*(VPINBALL_EVENT, void*)> m_eventCallback = nullptr;
   std::function<void()> m_gameLoop = nullptr;
   bool m_scanningTables;
   std::queue<SDL_Event> m_eventQueue;
   std::mutex m_eventMutex;

#ifdef __ANDROID__
   // SAF cache tracking for sync-back
   string m_safTableDir;       // SAF path to table directory (e.g., "content://.../tables/myTable")
   string m_safCachePath;      // Cache path where table is loaded from (e.g., "/data/saf_cache/myTable")
#endif

   // Table loading state (used for table image capture and SAF sync)
   string m_loadedTableUuid;
   string m_loadedTableSafPath;  // SAF path if loaded table is from SAF storage (Android only)

   // Table image capture state
   bool m_tableImageCaptureReady = false;
   bool m_tableImageCaptureSuccess = false;
   string m_tableImageCachePath;

   void OnTableImageCaptured(bool success);
   void ProcessTableImageCapture();
};

}

#ifdef __cplusplus
extern "C" {
#endif
void VPinball_SetBridgeCallback(void (*callback)(void*));
#ifdef __cplusplus
}
#endif
