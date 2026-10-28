// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

// The whole script is small enough to snapshot per edit
static void RecordScriptEdit(Player& p, PinTable* table)
{
   const string before = table->m_script_text;
   RecordAction(p, table, [table, before]()
   {
      table->m_script_text = before;
      table->SetDirtyScript(SaveDirtyState::Dirty);
   });
}

void AddScriptTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_script", "The table script. Actions: get (whole, startLine/endLine, sub, pattern + context, or subs:true for the Sub index) | set (text, or path to a .vbs) | export (path) | replace (find, replacement, all) | append (text) | run (text: VBScript statements in the running script, returns its errors and warnings) | eval (text: an expression, returns its value, e.g. Flipper1.X). Edits apply to the next play session and are undoable while the editor is open. Details: vpx_status guide:\"script\".",
         Schema({ { "action", Prop("string", "get | set | replace | append | run | eval | export") }, { "path", Prop("string", "set: read the script from this file instead of text; export: write the script to this file") }, { "sub", Prop("string", "get: Sub or Function name") }, { "pattern", Prop("string", "get: text to search") },
                  { "context", Prop("integer", "get: lines around each match, default 2") }, { "subs", Prop("boolean", "get: list the Sub/Function names") }, { "startLine", Prop("integer", "get: 1-based") }, { "endLine", Prop("integer", "") },
                  { "text", Prop("string", "set, append: script text; run: statements; eval: expression") }, { "find", Prop("string", "replace") }, { "replacement", Prop("string", "replace") }, { "all", Prop("boolean", "replace all occurrences") } },
            { "action" }),
         [](const json& args)
         {
            const string action = args.value("action", ""s);
            return WithPlayer([args, action](Player& p)
            {
               PinTable* table = p.m_ptable->m_liveBaseTable ? p.m_ptable->m_liveBaseTable : p.m_ptable;
               if (action == "get")
                  return ScriptGet(table->m_script_text, args);
               if (action == "run" || action == "eval")
               {
                  if (!p.m_scriptInterpreter)
                     return ErrorOutput("No script is running (editor mode or table failed to load)"s);
                  const string text = args.value("text", ""s);
                  const uint64_t before = McpServer::Get().GetScriptErrorCount();
                  if (action == "run")
                  {
                     const uint64_t logCursor = McpServer::Get().GetLogCount();
                     p.m_scriptInterpreter->Evaluate(p.m_pluginAPI.ApplyScriptCOMObjectOverrides(text), false);
                     const json errs = ScriptErrorsJson(before);
                     json result { { "ok", errs.empty() }, { "errors", errs } };
                     json warnings = json::array();
                     for (const string& line : McpServer::Get().GetLogSince(logCursor))
                        if ((line.rfind("WARN ", 0) == 0 || line.rfind("ERROR ", 0) == 0) && line.find("Runtime error on line") == string::npos && warnings.size() < 20)
                           warnings.push_back(line);
                     if (!warnings.empty())
                        result["warnings"] = warnings;
                     return ToolOutput { result.dump(), ""s, ""s, !errs.empty() };
                  }
                  // The eval variable is declared once per script session (Option Explicit): keyed on the server session, not on the
                  // interpreter address, which a new session can reuse
                  static int declaredForSession = -1;
                  McpServer::Get().SyncSession();
                  const string& var = McpServer::Get().GetEvalVariable();
                  if (declaredForSession != McpServer::Get().GetSession())
                  {
                     p.m_scriptInterpreter->Evaluate("Dim "s + var, false);
                     declaredForSession = McpServer::Get().GetSession();
                  }
                  p.m_scriptInterpreter->Evaluate(p.m_pluginAPI.ApplyScriptCOMObjectOverrides(var + " = ("s + text + ')'), false);
                  if (const json errs = ScriptErrorsJson(before); !errs.empty())
                     return ErrorOutput(errs.back().dump());
                  IDispatch* disp = nullptr;
                  p.m_scriptInterpreter->GetScriptDispatch(&disp);
                  if (!disp)
                     return ErrorOutput("Script dispatch is not available"s);
                  json value;
                  string error;
                  GetDispProperty(disp, var, value, error);
                  disp->Release();
                  if (!error.empty())
                     return ErrorOutput(error);
                  return TextOutput(json { { "expression", text }, { "value", value } });
               }
               if (action == "export")
               {
                  const std::filesystem::path out = PathFromUTF8(args.value("path", ""s));
                  if (out.empty())
                     return ErrorOutput("path is required"s);
                  std::ofstream file(out, std::ios::binary);
                  file << table->m_script_text;
                  if (!file)
                     return ErrorOutput("Could not write "s + PathToUTF8(out));
                  return TextOutput(json { { "exported", PathToUTF8(out) }, { "lines", ScriptLines(table->m_script_text).size() } });
               }
               if (p.m_ptable->m_liveBaseTable)
                  return ErrorOutput("A playtest is running: stop it before editing the script"s);
               if (action == "set")
               {
                  string text = args.value("text", ""s);
                  if (args.contains("path"))
                  {
                     const std::filesystem::path in = PathFromUTF8(args.value("path", ""s));
                     std::ifstream file(in, std::ios::binary);
                     if (!file)
                        return ErrorOutput("Could not read "s + PathToUTF8(in));
                     text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                  }
                  RecordScriptEdit(p, table);
                  table->m_script_text = text;
                  table->SetDirtyScript(SaveDirtyState::Dirty);
                  McpServer::Get().NoteEdit();
                  json result { { "lines", ScriptLines(table->m_script_text).size() } };
                  AddSidecarWarning(table, result);
                  return TextOutput(result);
               }
               if (action == "append")
               {
                  RecordScriptEdit(p, table);
                  if (!table->m_script_text.empty() && table->m_script_text.back() != '\n')
                     table->m_script_text += '\n';
                  table->m_script_text += args.value("text", ""s);
                  table->SetDirtyScript(SaveDirtyState::Dirty);
                  McpServer::Get().NoteEdit();
                  return TextOutput(json { { "lines", ScriptLines(table->m_script_text).size() } });
               }
               if (action == "replace")
               {
                  const string find = args.value("find", ""s), replacement = args.value("replacement", ""s);
                  if (find.empty())
                     return ErrorOutput("find is required"s);
                  const string before = table->m_script_text;
                  int count = 0;
                  size_t pos = table->m_script_text.find(find);
                  while (pos != string::npos)
                  {
                     table->m_script_text.replace(pos, find.size(), replacement);
                     count++;
                     if (!args.value("all", false))
                        break;
                     pos = table->m_script_text.find(find, pos + replacement.size());
                  }
                  if (count == 0)
                     return ErrorOutput("Text not found in the script"s);
                  RecordAction(p, table, [table, before]()
                  {
                     table->m_script_text = before;
                     table->SetDirtyScript(SaveDirtyState::Dirty);
                  });
                  table->SetDirtyScript(SaveDirtyState::Dirty);
                  McpServer::Get().NoteEdit();
                  return TextOutput(json { { "replaced", count } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

}

}
