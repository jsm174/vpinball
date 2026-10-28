// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

#include <mongoose/mongoose.h>

namespace VPX::MCP
{

class McpLogAppender final : public plog::IAppender
{
public:
   explicit McpLogAppender(McpServer& server) : m_server(server) { }
   void write(const plog::Record& record) PLOG_OVERRIDE
   {
      std::string level;
      switch (record.getSeverity())
      {
      case plog::fatal: level = "FATAL"; break;
      case plog::error: level = "ERROR"; break;
      case plog::warning: level = "WARN"; break;
      case plog::info: level = "INFO"; break;
      case plog::debug: level = "DEBUG"; break;
      default: level = "VERBOSE"; break;
      }
      m_server.OnLog(level + ' ' + record.getMessage());
   }

private:
   McpServer& m_server;
};

static constexpr const char* HEADER_JSON = "Content-Type: application/json\r\n";
static constexpr size_t MAX_LOG_LINES = 2000;
static constexpr size_t MAX_SCRIPT_ERRORS = 200;
static constexpr size_t MAX_EVENTS = 5000;
static constexpr int PORT_ATTEMPTS = 10;
static constexpr size_t MAX_FINISHED_BACKGROUND_JOBS = 32;

///////////////////////////////////////////////////////////////////////////////
// Server

McpServer& McpServer::Get()
{
   static McpServer server;
   return server;
}

// Static destruction: the logger may already be gone, so no logging here
McpServer::~McpServer() { Shutdown(); }

void McpServer::Shutdown()
{
   if (!m_run)
      return;
   m_run = false;
   if (m_thread && m_thread->joinable())
      m_thread->join();
   m_thread = nullptr;
   m_mgr = nullptr;
   m_port = 0;
   std::lock_guard lock(m_jobMutex);
   m_pendingJobs.clear();
   m_activeJobs.clear();
   m_completedJobs.clear();
   m_backgroundJobs.clear();
   m_currentJob = nullptr;
   m_nQueuedJobs = 0;
}

void McpServer::ApplySettings()
{
   const Settings& settings = g_settingsService.GetAppSettings();
   McpServer& server = Get();
   if (!settings.GetMCP_Enable())
   {
      server.Stop();
      return;
   }
   if (server.IsRunning() && server.m_port >= settings.GetMCP_Port() && server.m_port < settings.GetMCP_Port() + PORT_ATTEMPTS && server.m_token == settings.GetMCP_Token())
      return;
   server.Stop();
   server.Start(settings.GetMCP_Port(), settings.GetMCP_Token());
}

std::string McpServer::GetEndpoint() const { return m_run ? "http://127.0.0.1:"s + std::to_string(m_port) + "/mcp" : ""s; }

std::string McpServer::GetLastError() const { return m_lastError; }

string McpServer::RandomHex(int nChars)
{
   static const char* digits = "0123456789abcdef";
   std::random_device rd;
   string s;
   for (int i = 0; i < nChars; i++)
      s += digits[rd() % 16];
   return s;
}

void McpServer::Start(int port, const std::string& token)
{
   if (m_run)
      return;
   m_token = token;
   m_lastError.clear();
   m_evalVar = "mcp_eval_"s + RandomHex(6);
   mg_log_set(MG_LL_ERROR);
   mg_log_set_fn(
      [](char c, void*)
      {
         thread_local string line;
         if (c != '\n')
            line += c;
         else if (!line.empty())
         {
            PLOGE << "MCP server: " << line;
            line.clear();
         }
      },
      nullptr);
   m_mgr = std::make_unique<mg_mgr>();
   mg_mgr_init(m_mgr.get());
   mg_wakeup_init(m_mgr.get());
   int boundPort = 0;
   for (int attempt = 0; attempt < PORT_ATTEMPTS; attempt++)
   {
      const int candidate = port + attempt;
      const string url = "http://127.0.0.1:"s + std::to_string(candidate);
      if (mg_http_listen(m_mgr.get(), url.c_str(), &McpServer::EventHandler, this) != nullptr)
      {
         boundPort = candidate;
         break;
      }
      PLOGW << "MCP server could not listen on port " << candidate << " (already in use?), trying the next one";
   }
   if (boundPort == 0)
   {
      m_lastError = "No free port between "s + std::to_string(port) + " and " + std::to_string(port + PORT_ATTEMPTS - 1);
      PLOGE << "MCP server not started: " << m_lastError;
      mg_mgr_free(m_mgr.get());
      m_mgr = nullptr;
      return;
   }
   m_port = boundPort;
   m_run = true;
   if (!m_logAppender)
   {
      m_logAppender = std::make_unique<McpLogAppender>(*this);
      plog::Logger<PLOG_DEFAULT_INSTANCE_ID>::getInstance()->addAppender(m_logAppender.get());
      plog::Logger<PLOG_NO_DBG_OUT_INSTANCE_ID>::getInstance()->addAppender(m_logAppender.get());
   }
   m_thread = std::make_unique<std::thread>([this]()
   {
      while (m_run)
      {
         mg_mgr_poll(m_mgr.get(), 50);
         ExpireJobs();
      }
      mg_mgr_free(m_mgr.get());
   });
   PLOGI << "MCP server listening on " << GetEndpoint() << " (" << Tools().size() << " tools, " << (m_token.empty() ? "no token" : "bearer token required") << ')';
}

void McpServer::Stop()
{
   if (!m_run)
      return;
   Shutdown();
   PLOGI << "MCP server stopped";
}

void McpServer::ProcessJobs()
{
   if (m_nQueuedJobs == 0)
      return;
   if (g_pplayer && g_pplayer->m_liveUI && g_pplayer->m_liveUI->m_editorUI.IsFileOpRunning())
      return;
   std::vector<std::shared_ptr<Job>> jobs;
   {
      std::lock_guard lock(m_jobMutex);
      while (!m_pendingJobs.empty())
      {
         m_activeJobs.push_back(m_pendingJobs.front());
         m_pendingJobs.pop_front();
      }
      jobs = m_activeJobs;
   }
   for (const std::shared_ptr<Job>& job : jobs)
   {
      bool cancelled;
      {
         std::lock_guard lock(m_jobMutex);
         cancelled = job->cancelled;
         if (cancelled)
         {
            std::erase(m_activeJobs, job);
            m_nQueuedJobs--;
         }
      }
      if (cancelled)
      {
         // A step stopped midway (timeout, vpx_job cancel) may have left a timed run playing and the event capture on
         if (job->started && g_pplayer)
         {
            if (m_capturingEvents)
               StopEventCapture();
            g_pplayer->SetPlayState(false);
         }
         continue;
      }
      std::optional<ToolOutput> out;
      m_currentJob = job;
      job->started = true;
      try
      {
         out = job->step();
      }
      catch (const std::exception& e)
      {
         out = ErrorOutput("Tool failed: "s + e.what());
      }
      m_currentJob = nullptr;
      if (out)
         CompleteJob(job, *out);
   }
}

void McpServer::ReportProgress(size_t done, size_t total, const std::string& note)
{
   if (!m_currentJob)
      return;
   std::lock_guard lock(m_jobMutex);
   m_currentJob->progress = std::to_string(done) + '/' + std::to_string(total) + (note.empty() ? ""s : ' ' + note);
}

std::vector<BackgroundJobInfo> McpServer::GetBackgroundJobs() const
{
   const auto now = std::chrono::steady_clock::now();
   std::vector<BackgroundJobInfo> infos;
   std::lock_guard lock(m_jobMutex);
   for (const std::shared_ptr<Job>& job : m_backgroundJobs)
   {
      const bool running = job->state.empty();
      infos.push_back({ "job-"s + std::to_string(job->serial), job->tool, running ? "running"s : job->state, job->progress,
         std::chrono::duration<double>((running ? now : job->endTime) - job->startTime).count(), job->result });
   }
   return infos;
}

bool McpServer::CancelBackgroundJob(const std::string& id)
{
   std::shared_ptr<Job> job;
   {
      std::lock_guard lock(m_jobMutex);
      const auto it = std::ranges::find_if(m_backgroundJobs, [&id](const std::shared_ptr<Job>& j) { return "job-"s + std::to_string(j->serial) == id; });
      if (it == m_backgroundJobs.end() || !(*it)->state.empty())
         return false;
      job = *it;
      job->cancelled = true;
   }
   FinishBackgroundJob(job, ErrorOutput("Cancelled"s), "cancelled"s);
   return true;
}

void McpServer::FinishBackgroundJob(const std::shared_ptr<Job>& job, const ToolOutput& output, const std::string& state)
{
   std::lock_guard lock(m_jobMutex);
   if (!job->state.empty())
      return;
   job->result = output;
   job->state = state;
   job->endTime = std::chrono::steady_clock::now();
   size_t finished = std::ranges::count_if(m_backgroundJobs, [](const std::shared_ptr<Job>& j) { return !j->state.empty(); });
   for (auto it = m_backgroundJobs.begin(); it != m_backgroundJobs.end() && finished > MAX_FINISHED_BACKGROUND_JOBS;)
      if (!(*it)->state.empty())
      {
         it = m_backgroundJobs.erase(it);
         finished--;
      }
      else
         ++it;
}

static json RpcError(const json& id, int code, const string& message)
{
   return json { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", message } } } };
}

static json RpcResult(const json& id, json result)
{
   return json { { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move(result) } };
}

// The JSON object of a text result is also sent as structuredContent to clients of protocol 2025-06-18 and later, the text stays for older clients
static json ToolResult(const ToolOutput& out, bool structured)
{
   json content = json::array();
   content.push_back({ { "type", "text" }, { "text", out.text } });
   if (!out.imageBase64.empty())
      content.push_back({ { "type", "image" }, { "data", out.imageBase64 }, { "mimeType", out.imageMime } });
   for (const auto& [data, mime] : out.moreImages)
      content.push_back({ { "type", "image" }, { "data", data }, { "mimeType", mime } });
   json result { { "content", content }, { "isError", out.isError } };
   if (structured && !out.isError && out.text.starts_with('{'))
      if (json doc = json::parse(out.text, nullptr, false); !doc.is_discarded() && doc.is_object())
         result["structuredContent"] = std::move(doc);
   return result;
}

void McpServer::CompleteJob(const std::shared_ptr<Job>& job, const ToolOutput& out)
{
   {
      std::lock_guard lock(m_jobMutex);
      std::erase(m_activeJobs, job);
      m_nQueuedJobs--;
      if (job->cancelled)
         return;
   }
   if (job->background)
   {
      FinishBackgroundJob(job, out, out.isError ? "failed"s : "done"s);
      return;
   }
   job->response = RpcResult(json::parse(job->rpcId), ToolResult(out, job->structured)).dump();
   DeliverJob(job);
}

void McpServer::FailJob(const std::shared_ptr<Job>& job, const std::string& message)
{
   if (job->background)
   {
      FinishBackgroundJob(job, ErrorOutput(message), "failed"s);
      return;
   }
   job->response = RpcError(json::parse(job->rpcId), -32000, message).dump();
   DeliverJob(job);
}

void McpServer::DeliverJob(const std::shared_ptr<Job>& job)
{
   {
      std::lock_guard lock(m_jobMutex);
      m_completedJobs[job->serial] = job;
   }
   if (m_mgr)
      mg_wakeup(m_mgr.get(), job->connId, &job->serial, sizeof(job->serial));
}

// Runs on the server thread: fails jobs that were not processed in time (no game loop running, or a tool that never completes)
void McpServer::ExpireJobs()
{
   const auto now = std::chrono::steady_clock::now();
   std::vector<std::shared_ptr<Job>> expired;
   std::vector<std::shared_ptr<Job>> unprocessed;
   {
      std::lock_guard lock(m_jobMutex);
      for (auto it = m_pendingJobs.begin(); it != m_pendingJobs.end();)
      {
         if ((*it)->deadline < now)
         {
            unprocessed.push_back(*it);
            it = m_pendingJobs.erase(it);
            m_nQueuedJobs--;
         }
         else
            ++it;
      }
      for (const std::shared_ptr<Job>& job : m_activeJobs)
         if (job->deadline < now && !job->cancelled)
         {
            job->cancelled = true;
            expired.push_back(job);
         }
      for (auto it = m_completedJobs.begin(); it != m_completedJobs.end();)
         it = (it->second->deadline + std::chrono::seconds(60) < now) ? m_completedJobs.erase(it) : std::next(it);
   }
   for (const std::shared_ptr<Job>& job : unprocessed)
      FailJob(job, "No table session is processing requests (is a table loaded and the game loop running?)"s);
   for (const std::shared_ptr<Job>& job : expired)
      FailJob(job, "Tool timed out"s);
}

void McpServer::OnScriptError(const std::string& type, int line, int column, const std::string& description)
{
   SyncSession();
   string lineText, source;
   const vector<string> lines = g_pplayer ? ScriptLines(g_pplayer->m_ptable->m_script_text) : vector<string>();
   if (g_pplayer && type != "Compile" && line > 0 && static_cast<size_t>(line) > lines.size())
      source = "not the table script: the line is past its end, so the code was loaded at run time (ExecuteGlobal, Execute, a library .vbs such as controller.vbs)"s;
   if (g_pplayer && g_pplayer->m_scriptInterpreter)
   {
      lineText = g_pplayer->m_scriptInterpreter->GetLastErrorLineText();
      while (!lineText.empty() && (lineText.back() == '\r' || lineText.back() == '\n'))
         lineText.pop_back();
      if (!lineText.empty())
      {
         auto trimmed = [](const string& s)
         {
            const size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
            return a == string::npos ? string() : s.substr(a, b - a + 1);
         };
         const bool matches = line >= 1 && static_cast<size_t>(line) <= lines.size() && trimmed(lines[line - 1]) == trimmed(lineText);
         source = matches ? "table script"s : "not the table script: code loaded at run time (ExecuteGlobal, Execute, a library .vbs such as controller.vbs), the line number is relative to that code"s;
      }
   }
   std::lock_guard lock(m_logMutex);
   if (m_scriptErrors.size() >= MAX_SCRIPT_ERRORS)
      m_scriptErrors.erase(m_scriptErrors.begin());
   m_scriptErrors.push_back({ ++m_errorSeq, m_session, type, line, column, description, g_pplayer ? g_pplayer->m_time_sec : 0.0, lineText, source });
}

void McpServer::OnLog(const std::string& line)
{
   std::lock_guard lock(m_logMutex);
   if (m_log.size() >= MAX_LOG_LINES)
      m_log.pop_front();
   m_log.push_back(line);
   m_logTotal++;
}

uint64_t McpServer::GetLogCount() const
{
   std::lock_guard lock(m_logMutex);
   return m_logTotal;
}

std::vector<std::string> McpServer::GetLogSince(uint64_t count) const
{
   std::lock_guard lock(m_logMutex);
   const uint64_t available = std::min<uint64_t>(m_logTotal - std::min(count, m_logTotal), m_log.size());
   return std::vector<std::string>(m_log.end() - available, m_log.end());
}

std::vector<std::string> McpServer::GetLogTail(size_t lines) const
{
   std::lock_guard lock(m_logMutex);
   const size_t n = std::min(lines, m_log.size());
   return std::vector<std::string>(m_log.end() - n, m_log.end());
}

std::vector<ScriptError> McpServer::GetScriptErrors() const
{
   std::lock_guard lock(m_logMutex);
   return m_scriptErrors;
}

uint64_t McpServer::GetScriptErrorCount() const
{
   std::lock_guard lock(m_logMutex);
   return m_errorSeq;
}

void McpServer::SyncSession()
{
   const void* const table = g_pplayer ? static_cast<const void*>(g_pplayer->m_ptable) : nullptr;
   std::lock_guard lock(m_logMutex);
   if (table != m_sessionTable)
   {
      m_sessionTable = table;
      m_session++;
   }
}

void McpServer::StartEventCapture()
{
   m_events.clear();
   m_eventSources.clear();
   if (!g_pplayer)
      return;
   for (IEditable* part : g_pplayer->m_ptable->GetParts())
      if (EventProxyBase* proxy = part->GetEventProxyBase())
         m_eventSources[proxy] = part->GetName();
   m_capturingEvents = true;
   EventProxyBase::s_eventListener = [](EventProxyBase* source, int dispid)
   {
      McpServer& server = Get();
      if (!server.m_capturingEvents || server.m_events.size() >= MAX_EVENTS)
         return;
      const string name = EventName(dispid);
      if (name.empty())
         return;
      const auto it = server.m_eventSources.find(source);
      if (it == server.m_eventSources.end())
         return;
      int ballId = -1;
      if (g_pplayer && g_pplayer->m_pactiveball)
         g_pplayer->m_pactiveball->get_ID(&ballId);
      server.m_events.push_back({ g_pplayer ? g_pplayer->m_time_msec : 0u, it->second, name, ballId });
   };
}

std::vector<CapturedEvent> McpServer::StopEventCapture()
{
   m_capturingEvents = false;
   EventProxyBase::s_eventListener = nullptr;
   m_eventSources.clear();
   return std::move(m_events);
}


///////////////////////////////////////////////////////////////////////////////
// HTTP / JSON-RPC

void McpServer::EventHandler(mg_connection* c, int ev, void* ev_data)
{
   McpServer* server = static_cast<McpServer*>(c->fn_data);
   if (ev == MG_EV_HTTP_MSG)
      server->HandleHttp(c, static_cast<mg_http_message*>(ev_data));
   else if (ev == MG_EV_WAKEUP)
   {
      const mg_str* data = static_cast<mg_str*>(ev_data);
      server->HandleWakeup(c, data->buf, data->len);
   }
}

void McpServer::HandleWakeup(mg_connection* c, const void* data, size_t len)
{
   if (len != sizeof(uint64_t))
      return;
   uint64_t serial;
   memcpy(&serial, data, sizeof(serial));
   std::shared_ptr<Job> job;
   {
      std::lock_guard lock(m_jobMutex);
      if (const auto it = m_completedJobs.find(serial); it != m_completedJobs.end())
      {
         job = it->second;
         m_completedJobs.erase(it);
      }
   }
   if (job)
      mg_http_reply(c, 200, HEADER_JSON, "%s", job->response.c_str());
}

static string HeaderValue(mg_http_message* hm, const char* name)
{
   mg_str* h = mg_http_get_header(hm, name);
   return h ? string(h->buf, h->len) : string();
}

static void ReplyJson(mg_connection* c, const json& body) { mg_http_reply(c, 200, HEADER_JSON, "%s", body.dump().c_str()); }

static bool IsLoopbackHost(string host)
{
   if (host.starts_with('['))
      host = host.substr(0, host.find(']') + 1);
   else if (const size_t colon = host.find(':'); colon != string::npos)
      host = host.substr(0, colon);
   return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
}

// A browser page may only talk to the server from this computer: the origin host must be exactly a loopback name (a prefix match would let
// localhost.example.com through). "null" is refused: sandboxed iframes of any web site and file:// pages send it. Clients that are not
// browsers send no origin.
static bool IsLocalOrigin(const string& origin)
{
   if (origin.empty())
      return true;
   const size_t scheme = origin.find("://");
   if (scheme == string::npos)
      return false;
   string host = origin.substr(scheme + 3);
   if (const size_t slash = host.find('/'); slash != string::npos)
      host = host.substr(0, slash);
   return IsLoopbackHost(host);
}

void McpServer::HandleHttp(mg_connection* c, mg_http_message* hm)
{
   const string uri(hm->uri.buf, hm->uri.len);
   const string method(hm->method.buf, hm->method.len);
   if (uri != "/mcp" && uri != "/health")
   {
      mg_http_reply(c, 404, HEADER_JSON, "{\"error\":\"not found\"}");
      return;
   }
   if (!IsLocalOrigin(HeaderValue(hm, "Origin")))
   {
      mg_http_reply(c, 403, HEADER_JSON, "{\"error\":\"forbidden origin\"}");
      return;
   }
   // DNS rebinding: a page of a host name resolving to 127.0.0.1 sends that name as Host
   if (!IsLoopbackHost(HeaderValue(hm, "Host")))
   {
      mg_http_reply(c, 403, HEADER_JSON, "{\"error\":\"forbidden host\"}");
      return;
   }
   if (!m_token.empty())
   {
      const string auth = HeaderValue(hm, "Authorization");
      if (auth != "Bearer "s + m_token)
      {
         PLOGW << "MCP: rejected a request with " << (auth.empty() ? "no" : "a wrong") << " access token";
         mg_http_reply(c, 401, "Content-Type: application/json\r\nWWW-Authenticate: Bearer\r\n",
            "{\"error\":\"unauthorized\",\"hint\":\"Send 'Authorization: Bearer <token>' using the token shown in the VPX MCP settings page\"}");
         return;
      }
   }
   if (uri == "/health")
   {
      ReplyJson(c, json { { "ok", true }, { "name", "vpinball" }, { "version", VP_VERSION_STRING_DIGITS }, { "tools", Tools().size() }, { "sessionActive", g_pplayer != nullptr } });
      return;
   }
   if (method == "GET")
   {
      mg_http_reply(c, 405, "Allow: POST, DELETE\r\n", "");
      return;
   }
   if (method == "DELETE")
   {
      mg_http_reply(c, 204, "", "");
      return;
   }
   if (method != "POST")
   {
      mg_http_reply(c, 405, "Allow: POST, DELETE\r\n", "");
      return;
   }

   // Browsers send a cross origin POST without asking first only for form and text content types, application/json needs a preflight
   if (!lowerCase(HeaderValue(hm, "Content-Type")).starts_with("application/json"))
   {
      mg_http_reply(c, 415, HEADER_JSON, "{\"error\":\"Content-Type must be application/json\"}");
      return;
   }

   json req;
   try
   {
      req = json::parse(string(hm->body.buf, hm->body.len));
   }
   catch (const std::exception&)
   {
      ReplyJson(c, RpcError(nullptr, -32700, "Parse error"s));
      return;
   }
   if (!req.is_object())
   {
      ReplyJson(c, RpcError(nullptr, -32600, "Batch requests are not supported"s));
      return;
   }
   const string rpcMethod = req.value("method", ""s);
   const json params = req.value("params", json::object());
   if (!req.contains("id"))
   {
      mg_http_reply(c, 202, "", "");
      return;
   }
   const json id = req["id"];

   if (rpcMethod == "initialize")
   {
      static const std::vector<string> known = { "2025-06-18"s, "2025-03-26"s, "2024-11-05"s };
      string version = params.value("protocolVersion", "2025-03-26"s);
      if (std::ranges::find(known, version) == known.end())
         version = "2025-03-26"s;
      const string client = params.contains("clientInfo") ? params["clientInfo"].value("name", "?"s) : "?"s;
      PLOGI << "MCP client connected: " << client;
      // Stateless server: no Mcp-Session-Id is issued, every request stands on its own
      ReplyJson(c,
         RpcResult(id,
            json { { "protocolVersion", version }, { "capabilities", { { "tools", { { "listChanged", false } } } } },
               { "serverInfo", { { "name", "vpinball" }, { "version", VP_VERSION_STRING_DIGITS "+"s + ToolsFingerprint() } } }, { "instructions", GUIDE_TEXT } }));
      return;
   }
   if (rpcMethod == "ping")
   {
      ReplyJson(c, RpcResult(id, json::object()));
      return;
   }
   if (rpcMethod == "tools/list")
   {
      json tools = json::array();
      for (const ToolDef& t : Tools())
      {
         json tool { { "name", t.name }, { "description", t.description }, { "inputSchema", t.inputSchema } };
         if (json annotations = ToolAnnotations(t.name); !annotations.empty())
         {
            tool["title"] = annotations.value("title", ""s);
            tool["annotations"] = std::move(annotations);
         }
         tools.push_back(std::move(tool));
      }
      ReplyJson(c, RpcResult(id, json { { "tools", tools } }));
      return;
   }
   if (rpcMethod == "tools/call")
   {
      const string name = params.value("name", ""s);
      json args = params.value("arguments", json::object());
      const bool background = args.is_object() && args.value("background", false);
      if (background)
         args.erase("background");
      const auto it = std::ranges::find_if(Tools(), [&name](const ToolDef& t) { return t.name == name; });
      if (it == Tools().end())
      {
         ReplyJson(c, RpcError(id, -32602, "Unknown tool: "s + name));
         return;
      }
      auto job = std::make_shared<Job>();
      try
      {
         job->step = it->build(args);
      }
      catch (const std::exception& e)
      {
         ReplyJson(c, RpcError(id, -32602, "Invalid arguments: "s + e.what()));
         return;
      }
      job->connId = c->id;
      job->rpcId = id.dump();
      job->tool = name;
      job->background = background;
      job->structured = HeaderValue(hm, "MCP-Protocol-Version") >= "2025-06-18";
      job->startTime = std::chrono::steady_clock::now();
      job->deadline = job->startTime + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(it->timeoutS));
      {
         std::lock_guard lock(m_jobMutex);
         job->serial = m_nextJobSerial++;
         m_pendingJobs.push_back(job);
         m_nQueuedJobs++;
         if (background)
            m_backgroundJobs.push_back(job);
      }
      if (background)
      {
         const string jobId = "job-"s + std::to_string(job->serial);
         ReplyJson(c, RpcResult(id, ToolResult(TextOutput(json { { "job", jobId }, { "tool", name }, { "state", "running" },
            { "next", "vpx_job get id:\""s + jobId + "\" (waitMs to block until it is done), vpx_job cancel to stop it" } }), job->structured)));
      }
      return;
   }
   ReplyJson(c, RpcError(id, -32601, "Method not found: "s + rpcMethod));
}

}
