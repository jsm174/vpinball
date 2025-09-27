#pragma once

#include <string>
#include <functional>
#include <queue>
#include <mutex>
#include "VPinballTableManager.h"

class WebServer;

namespace VPinballLib {

using std::string;
using std::vector;

enum class LogLevel {
   Debug,
   Info,
   Warn,
   Error
};

enum class VPinballStatus {
   Success,
   Failure
};

enum class ScriptErrorType {
   Compile,
   Runtime
};


enum class Event {
   ArchiveUncompressing,
   ArchiveCompressing,
   LoadingItems,
   LoadingSounds,
   LoadingImages,
   LoadingFonts,
   LoadingCollections,
   Play,
   CreatingPlayer,
   Prerendering,
   PlayerStarted, 
   Rumble,
   ScriptError,
   PlayerClosing,
   PlayerClosed, 
   Stopped,
   WebServer,
   CaptureScreenshot,
   TableList,
   TableImport,
   TableRename,
   TableDelete,
   TableScan,
   TableScanComplete,
   RefreshingTableList,
   TableListRefreshComplete,
   TableAdded,
   TableUpdated,
   TableRemoved,
   TablesJsonGenerated
};

struct ProgressData {
   int progress;
};

struct RumbleData {
   uint16_t lowFrequencyRumble;
   uint16_t highFrequencyRumble;
   uint32_t durationMs;
};

struct ScriptErrorData {
   ScriptErrorType error;
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

struct VPXTable {
   char* uuid;
   char* name;
   char* fileName;
   char* fullPath;
   char* path;
   char* artwork;
   int64_t createdAt;
   int64_t modifiedAt;
};

struct TableInfo {
   char* tableId;
   char* name;
};

struct TablesData {
   TableInfo* tables;
   int tableCount;
   bool success;
};

struct TableEventData {
   const char* tableId;
   const char* newName;
   const char* path;
   bool success;
};

struct VPXTablesData {
   VPXTable* tables;
   int tableCount;
   bool success;
};

struct TableScanData {
   int tablesFound;
   int tablesProcessed;
   bool scanComplete;
   const char* currentTable;
};

struct VPXTableEventData {
   const VPXTable* table;
   bool success;
   const char* errorMessage;
};


enum class TableEvent {
   TablesChanged,
   RefreshStarted, 
   RefreshCompleted 
};

class VPinballTableManager;

class VPinball {
public:
   VPinball(SDL_Window* pWindow, int argc, char** argv);
   ~VPinball();

   void AppIterate();
   void AppEvent(SDL_Event* event);

   void LoadPlugins();
   void UnloadPlugins();
  
   void Init(std::function<void*(Event, void*)> callback);
   void SetupEventCallback();
   string GetVersionStringFull();
   void Log(LogLevel level, const string& message);
   void ResetLog();
   int LoadValueInt(const string& sectionName, const string& key, int defaultValue);
   float LoadValueFloat(const string& sectionName, const string& key, float defaultValue);
   string LoadValueString(const string& sectionName, const string& key, const string& defaultValue);
   void SaveValueInt(const string& sectionName, const string& key, int value);
   void SaveValueFloat(const string& sectionName, const string& key, float value);
   void SaveValueString(const string& sectionName, const string& key, const string& value);
   void UpdateWebServer();
   VPinballStatus ResetIni();
   VPinballStatus Load(const string& source);
   VPinballStatus ExtractScript(const string& source);
   VPinballStatus Play();
   VPinballStatus Stop();
   void ToggleFPS();
   void CaptureScreenshot(const string& filename);
   void SetWebServerUpdated();
   
   VPinballStatus RefreshTables();
   void GetVPXTables(VPXTablesData& tablesData);
   char* GetTablesJson();
   char* GetTableJson(const string& uuid);
   VPinballStatus GetVPXTable(const string& uuid, VPXTable& table);
   VPinballStatus AddVPXTable(const string& filePath);
   VPinballStatus RemoveVPXTable(const string& uuid);
   VPinballStatus RenameVPXTable(const string& uuid, const string& newName);
   VPinballStatus ImportVPXZ(const string& vpxzPath);
   void FreeVPXTablesData(VPXTablesData& tablesData);
   void FreeVPXTable(VPXTable& table);
   VPinballStatus ImportTableFile(const string& sourceFile);
   VPinballStatus SetTableArtwork(const string& uuid, const string& artworkPath);
   const char* GetTablesPath();
   string ExportTable(const string& uuid);
   void NotifyTableEvent(Event event, void* data);   
   void CleanupVPXTable(VPXTable& table);

   static VPinball& GetInstance();

   SDL_Window* GetWindow() { return m_pWindow; }
   void* GetMetalLayer() { return m_pMetalLayer; }
   void* SendEvent(Event event, void* data);
   void SetGameLoop(std::function<void()> gameLoop) { m_gameLoop = gameLoop; }
   bool GetQueuedEvent(SDL_Event& event);

private:
   SDL_Window* m_pWindow = nullptr;
   void* m_pMetalLayer = nullptr;
   void Cleanup();
   vector<std::shared_ptr<MsgPlugin>> m_plugins;
   std::function<void*(Event, void*)> m_eventCallback = nullptr;
   std::function<void()> m_gameLoop = nullptr;
   WebServer* m_pWebServer = nullptr;
   bool m_scanningTables;
   VPinballTableManager* m_tableManager = nullptr;
   std::queue<SDL_Event> m_eventQueue;
   std::mutex m_eventMutex;
};

}

#ifdef __cplusplus
extern "C" {
#endif
void VPinball_SetBridgeCallback(void (*callback)(void*));
#ifdef __cplusplus
}
#endif