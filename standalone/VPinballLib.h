#pragma once

#include <string>
#include <functional>
#include <queue>
#include <mutex>
#include "VPinball.h"

class WebServer;

namespace VPinballLib {

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

enum class OptionUnit {
   NoUnit,
   Percent
};

enum class Event {
   ArchiveUncompressing,
   ArchiveCompressing,
   LoadingItems,
   LoadingSounds,
   LoadingImages,
   LoadingFonts,
   LoadingCollections,
   PlayerStarting,
   WindowCreated,
   Prerendering,
   PlayerStarted, 
   Rumble,
   ScriptError,
   LiveUIToggle,
   LiveUIUpdate,
   PlayerClosing,
   PlayerClosed, 
   Stopped,
   WebServer,
   CaptureScreenshot,
   TableList,
   TableImport,
   TableRename,
   TableDelete,
   SetPlayState,
   ToggleFPS,
   Stop,
   ResetIni,
   UpdateWebServer
};

struct ProgressData {
   int progress;
};

struct WindowCreatedData {
   void* pWindow;
   const char* pTitle;
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

// Use C structs from VPinball.h directly (zero duplication)
using TableOptions = VPinballTableOptions;
using CustomTableOption = VPinballCustomTableOption;
using ViewSetup = VPinballViewSetup;

// Command-based queue system for thread-safe UI operations
enum class UICommandType {
   SetPlayState,
   ToggleFPS,
   Stop,
   SetTableOptions,
   SetDefaultTableOptions,
   ResetTableOptions,
   SaveTableOptions,
   SetCustomTableOption,
   SetDefaultCustomTableOptions,
   ResetCustomTableOptions,
   SaveCustomTableOptions,
   SetViewSetup,
   SetDefaultViewSetup,
   ResetViewSetup,
   SaveViewSetup,
   CaptureScreenshot
};

struct UICommand {
   UICommandType type;
   union {
      struct {
         int enable;
      } setPlayState;
      struct {
         TableOptions options;
      } setTableOptions;
      struct {
         CustomTableOption option;
      } setCustomTableOption;
      struct {
         ViewSetup setup;
      } setViewSetup;
      struct {
         char filename[256];
      } captureScreenshot;
   } data;
};

class VPinball {
public:
   void LoadPlugins();
   void UnloadPlugins();
   static VPinball& GetInstance() { return s_instance; }
   void SetGameLoop(std::function<void()> gameLoop) { m_gameLoop = gameLoop; }
   static void* SendEvent(Event event, void* data);
   void Init(std::function<void*(Event, void*)> callback);
   string GetVersionStringFull();
   void Log(LogLevel level, const string& message);
   void ResetLog();
   int LoadValueInt(const string& sectionName, const string& key, int defaultValue);
   float LoadValueFloat(const string& sectionName, const string& key, float defaultValue);
   string LoadValueString(const string& sectionName, const string& key, const string& defaultValue);
   void SaveValueInt(const string& sectionName, const string& key, int value);
   void SaveValueFloat(const string& sectionName, const string& key, float value);
   void SaveValueString(const string& sectionName, const string& key, const string& value);
   VPinballStatus Uncompress(const string& source);
   VPinballStatus Compress(const string& source, const string& destination);
   void UpdateWebServer();
   VPinballStatus ResetIni();
   VPinballStatus Load(const string& source);
   VPinballStatus ExtractScript(const string& source);
   VPinballStatus Play();
   VPinballStatus Stop();
   void SetPlayState(int enable);
   void ToggleFPS();
   void GetTableOptions(TableOptions& tableOptions);
   void SetTableOptions(const TableOptions& tableOptions);
   void SetDefaultTableOptions();
   void ResetTableOptions();
   void SaveTableOptions();
   int GetCustomTableOptionsCount();
   void GetCustomTableOption(int index, CustomTableOption& customTableOption);
   void SetCustomTableOption(const CustomTableOption& customTableOption);
   void SetDefaultCustomTableOptions();
   void ResetCustomTableOptions();
   void SaveCustomTableOptions();
   void GetViewSetup(ViewSetup& viewSetup);
   void SetViewSetup(const ViewSetup& viewSetup);
   void SetDefaultViewSetup();
   void ResetViewSetup();
   void SaveViewSetup();
   void CaptureScreenshot(const string& filename);
   void SetWebServerUpdated();

   // All UI functions now use command-based queue for thread safety

private:
   VPinball();
   static void GameLoop(void* pUserData);
   void ProcessSetTableOptions(const TableOptions& tableOptions);
   void ProcessSetDefaultTableOptions();
   void ProcessResetTableOptions();
   void ProcessSetCustomTableOption(const CustomTableOption& customTableOption);
   void ProcessSetDefaultCustomTableOptions();
   void ProcessResetCustomTableOptions();
   void ProcessSetViewSetup(const ViewSetup& setup);
   void ProcessSetDefaultViewSetup();
   void ProcessResetViewSetup();
   void Cleanup();
   
   // Command processing methods
   void ProcessUICommand(const UICommand& command);
   void QueueUICommand(const UICommand& command);

   vector<std::shared_ptr<MsgPlugin>> m_plugins;
   std::queue<UICommand> m_uiCommandQueue;
   std::mutex m_uiCommandMutex;
   std::function<void*(Event, void*)> m_eventCallback;
   std::function<void()> m_gameLoop;
   WebServer* m_pWebServer;

   static VPinball s_instance;
};

} // namespace VPinballLib
