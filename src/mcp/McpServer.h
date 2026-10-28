// license:GPLv3+

#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct mg_mgr;
struct mg_connection;
struct mg_http_message;
class EventProxyBase;

namespace VPX::MCP
{

struct ToolOutput
{
   std::string text;
   std::string imageBase64;
   std::string imageMime;
   bool isError = false;
   std::vector<std::pair<std::string, std::string>> moreImages; // base64, mime
};

// A tool step is polled on the game thread once per logic update until it yields an output
using ToolStep = std::function<std::optional<ToolOutput>()>;

struct ScriptError
{
   uint64_t seq; // Running number across the server lifetime: callers remember the last one they saw
   int session; // Play session the error happened in (a new one per table load, playtest start or stop)
   std::string type;
   int line;
   int column;
   std::string description;
   double gameTime;
   std::string lineText; // Source text of the failing line, when the engine provides it
   std::string source; // "table script", or a hint that the code came from elsewhere (ExecuteGlobal, Execute, a library .vbs)
};

// A tool call started with background:true, polled with vpx_job
struct BackgroundJobInfo
{
   std::string id;
   std::string tool;
   std::string state; // running, done, failed, cancelled
   std::string progress;
   double elapsedS = 0.0;
   std::optional<ToolOutput> output;
};

struct CapturedEvent
{
   uint32_t gameTimeMs;
   std::string part;
   std::string event;
   int ballId = -1;
};

class McpServer final
{
public:
   static McpServer& Get();

   // Start or stop the server according to the application settings
   static void ApplySettings();

   void Start(int port, const std::string& token);
   void Stop();
   bool IsRunning() const { return m_run; }
   int GetPort() const { return m_port; }
   const std::string& GetToken() const { return m_token; }
   std::string GetEndpoint() const;
   std::string GetLastError() const;

   void ProcessJobs();
   // Called by long running tool steps on the game thread: shown by vpx_job while the call runs in the background
   void ReportProgress(size_t done, size_t total, const std::string& note = std::string());
   std::vector<BackgroundJobInfo> GetBackgroundJobs() const;
   bool CancelBackgroundJob(const std::string& id);

   void OnScriptError(const std::string& type, int line, int column, const std::string& description);
   void OnLog(const std::string& line);
   std::vector<std::string> GetLogTail(size_t lines) const;
   uint64_t GetLogCount() const;
   std::vector<std::string> GetLogSince(uint64_t count) const;

   void NoteEdit() { m_editsSinceSave++; }
   void ResetEdits() { m_editsSinceSave = 0; }
   int GetEditsSinceSave() const { return m_editsSinceSave; }
   std::vector<ScriptError> GetScriptErrors() const;
   uint64_t GetScriptErrorCount() const; // Last error sequence number (monotonic)
   int GetSession() const { return m_session; }
   void SyncSession(); // Starts a new error session when the player switched to another table (load, playtest, stop)

   // Script event capture (game thread only)
   void StartEventCapture();
   std::vector<CapturedEvent> StopEventCapture();
   const std::vector<CapturedEvent>& PeekEvents() const { return m_events; }
   std::vector<CapturedEvent> TakeEvents()
   {
      std::vector<CapturedEvent> events = std::move(m_events);
      m_events.clear();
      return events;
   }

   const std::string& GetEvalVariable() const { return m_evalVar; }

   static std::string RandomHex(int nChars);

private:
   McpServer() = default;
   ~McpServer();
   void Shutdown();

   struct Job
   {
      uint64_t serial = 0;
      unsigned long connId = 0;
      std::string rpcId;
      ToolStep step;
      std::chrono::steady_clock::time_point deadline;
      bool cancelled = false;
      bool started = false;
      bool structured = false; // The client negotiated a protocol version with structuredContent
      bool background = false;
      std::string tool;
      std::string progress;
      std::chrono::steady_clock::time_point startTime;
      std::chrono::steady_clock::time_point endTime;
      std::optional<ToolOutput> result;
      std::string state;
      std::string response;
   };

   static void EventHandler(mg_connection* c, int ev, void* ev_data);
   void HandleHttp(mg_connection* c, mg_http_message* hm);
   void HandleWakeup(mg_connection* c, const void* data, size_t len);
   void ExpireJobs();
   void CompleteJob(const std::shared_ptr<Job>& job, const ToolOutput& output);
   void FailJob(const std::shared_ptr<Job>& job, const std::string& message);
   void DeliverJob(const std::shared_ptr<Job>& job);
   void FinishBackgroundJob(const std::shared_ptr<Job>& job, const ToolOutput& output, const std::string& state);

   std::string m_token;
   int m_port = 0;
   std::string m_lastError;
   std::string m_evalVar;
   std::unique_ptr<mg_mgr> m_mgr;
   std::atomic<bool> m_run { false };
   std::unique_ptr<std::thread> m_thread;

   mutable std::mutex m_jobMutex;
   uint64_t m_nextJobSerial = 1;
   std::deque<std::shared_ptr<Job>> m_pendingJobs;
   std::vector<std::shared_ptr<Job>> m_activeJobs;
   std::map<uint64_t, std::shared_ptr<Job>> m_completedJobs;
   std::atomic<int> m_nQueuedJobs { 0 };
   std::vector<std::shared_ptr<Job>> m_backgroundJobs;
   std::shared_ptr<Job> m_currentJob;

   mutable std::mutex m_logMutex;
   std::deque<std::string> m_log;
   uint64_t m_logTotal = 0;
   int m_editsSinceSave = 0;
   std::vector<ScriptError> m_scriptErrors;
   uint64_t m_errorSeq = 0;
   int m_session = 0;
   const void* m_sessionTable = nullptr;
   std::unique_ptr<class McpLogAppender> m_logAppender;

   bool m_capturingEvents = false;
   std::vector<CapturedEvent> m_events;
   std::map<const EventProxyBase*, std::string> m_eventSources;
};

}
