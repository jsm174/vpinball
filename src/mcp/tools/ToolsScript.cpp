// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "ui/live/editor/script/ScriptApi.h"
#include "core/ScriptInterpreter.h"

namespace VPX::MCP
{

void AddScriptTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_script", "The table script. Actions: get (whole, startLine/endLine, sub, pattern + context, or subs:true for the Sub index) | set (text, or path to a .vbs) | export (path) | replace (find, replacement, all) | append (text) | run (text: VBScript statements in the running script, returns its errors and warnings) | eval (text: an expression, returns its value, e.g. Flipper1.X) | check (compile the script without playing: syntax errors with line, column and source line, editor open and no playtest) | api (name: a part, part type or interface: its script properties, methods and events with the handler Sub to write) | outline (kind: the script's Subs, Functions, Classes with their members and Consts, with line and signature, as the editor's script outliner) | symbol (name: where a Sub, Function, Class, Const or variable is declared, in the table script or in core.vbs and the included libraries, with its signature and comment; for a class or an object variable, its members). Edits apply to the next play session and are undoable while the editor is open. Details: vpx_status guide:\"script\".",
         Schema({ { "action", Prop("string", "get | set | replace | append | check | api | outline | symbol | run | eval | export") }, { "name", Prop("string", "api: a part or collection name, a part type or a script interface; symbol: a Sub, Function, Class, Const or variable") }, { "kind", Prop("string", "outline: only sub, function, class, const, dim or property declarations") }, { "path", Prop("string", "set: read the script from this file instead of text; export: write the script to this file") }, { "sub", Prop("string", "get: Sub or Function name") }, { "pattern", Prop("string", "get: text to search") },
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
               if (action == "api")
               {
                  static VPX::EditorUI::ScriptApi api;
                  api.Refresh(table, table->m_script_text);
                  const string name = args.value("name", ""s);
                  if (name.empty())
                     return ErrorOutput("api needs name: a part or collection name (Bumper1), a part type (Bumper) or a script interface (IBumper, ITableGlobal)"s);
                  const VPX::EditorUI::ScriptApiInterface* iface = api.InterfaceOfName(name);
                  if (!iface)
                     iface = VPX::EditorUI::ScriptApi::FindInterface(name);
                  if (!iface)
                     iface = VPX::EditorUI::ScriptApi::FindInterface("I"s + name);
                  if (!iface)
                     return ErrorOutput("No script interface for '"s + name + "': give a part or collection name of this table, a part type or an interface name");
                  const bool isPart = api.InterfaceOfName(name) != nullptr;
                  json methods = json::array(), properties = json::array(), constants = json::array(), events = json::array();
                  for (int i = 0; i < iface->memberCount; i++)
                  {
                     const VPX::EditorUI::ScriptApiEntry& m = iface->members[i];
                     if (m.flags & VPX::EditorUI::kScriptApiMethod)
                        methods.push_back(m.signature[0] ? m.signature : m.name);
                     else if (m.flags & VPX::EditorUI::kScriptApiConst)
                        constants.push_back(m.signature[0] ? m.signature : m.name);
                     else
                        properties.push_back(json { { "name", m.name }, { "signature", m.signature },
                           { "access", (m.flags & VPX::EditorUI::kScriptApiPropRead) && (m.flags & VPX::EditorUI::kScriptApiPropWrite) ? "read/write" : (m.flags & VPX::EditorUI::kScriptApiPropWrite) ? "write" : "read" } });
                  }
                  for (int i = 0; i < iface->eventCount; i++)
                  {
                     const VPX::EditorUI::ScriptApiEntry& e = iface->events[i];
                     const string handler = (isPart ? name : "<Name>"s) + '_' + e.name;
                     events.push_back(json { { "event", e.name }, { "handler", "Sub "s + handler + '(' + e.signature + ')' } });
                  }
                  json result { { "name", name }, { "interface", iface->name }, { "properties", properties }, { "methods", methods }, { "events", events } };
                  if (!constants.empty())
                     result["constants"] = constants;
                  return TextOutput(result);
               }
               if (action == "outline" || action == "symbol")
               {
                  using VPX::EditorUI::Symbol;
                  static VPX::EditorUI::ScriptApi api;
                  api.Refresh(table, table->m_script_text);
                  VPX::EditorUI::ScriptModel model;
                  model.Parse(table->m_script_text);
                  const auto brief = [](const Symbol& symbol)
                  {
                     json j { { "name", symbol.name }, { "kind", VPX::EditorUI::SymbolKindName(symbol.kind) }, { "line", symbol.line + 1 }, { "signature", symbol.signature } };
                     if (!symbol.comment.empty())
                        j["comment"] = symbol.comment;
                     return j;
                  };
                  if (action == "outline")
                  {
                     const string kind = lowerCase(args.value("kind", ""s));
                     json symbols = json::array();
                     size_t total = 0;
                     for (const Symbol* symbol : model.TopLevel())
                     {
                        if (!kind.empty() && lowerCase(VPX::EditorUI::SymbolKindName(symbol->kind)).find(kind) == string::npos)
                           continue;
                        if (kind.empty() && symbol->kind == VPX::EditorUI::SymbolKind::Dim)
                           continue;
                        if (++total > 500)
                           continue;
                        json j = brief(*symbol);
                        j.erase("comment");
                        if (symbol->kind == VPX::EditorUI::SymbolKind::Class)
                        {
                           json members = json::array();
                           for (const int child : symbol->children)
                              members.push_back(model.Symbols()[static_cast<size_t>(child)].signature);
                           j["members"] = members;
                        }
                        symbols.push_back(j);
                     }
                     json result { { "count", total }, { "symbols", symbols }, { "note", "the declarations at the top level of the table script, as the editor's script outliner lists them (kind: sub, function, class, const, dim, property; without kind the Dim variables are left out). symbol name:<name> gives one declaration with its comment, also for the builtin libraries (core.vbs...)" } };
                     if (total > 500)
                        result["truncated"] = "the first 500 are listed: filter with kind";
                     return TextOutput(result);
                  }
                  const string name = args.value("name", ""s);
                  if (name.empty())
                     return ErrorOutput("symbol needs name: a Sub, Function, Class, Const or variable of the table script or of the builtin libraries (core.vbs and the files the script includes)"s);
                  json result { { "name", name } };
                  bool found = false;
                  if (const Symbol* symbol = model.Find(name))
                  {
                     result["definition"] = brief(*symbol);
                     result["source"] = "table script";
                     found = true;
                  }
                  else if (const Symbol* core = api.CoreModel().Find(name))
                  {
                     result["definition"] = brief(*core);
                     result["source"] = "builtin library (core.vbs or a file the script includes): line is the line in that file";
                     found = true;
                  }
                  if (const string bound = model.ResolveClassName(name); !bound.empty() && !StrCompareNoCase(bound, name))
                     result["boundTo"] = bound;
                  json members = json::array();
                  for (const Symbol* member : api.MembersOf(name, model))
                     if (members.size() < 300)
                        members.push_back(json { { "name", member->name }, { "kind", VPX::EditorUI::SymbolKindName(member->kind) }, { "signature", member->signature } });
                  if (!members.empty())
                  {
                     result["members"] = members;
                     found = true;
                  }
                  if (const string iface = api.InterfaceOf(name); !iface.empty())
                  {
                     result["interface"] = iface;
                     found = true;
                  }
                  if (!found)
                     return ErrorOutput("'"s + name + "' is not declared in the table script nor in the builtin libraries, and is not a part of the table (vpx_script outline lists the script's declarations)");
                  return TextOutput(result);
               }
               if (action == "check")
               {
                  if (p.m_ptable->m_liveBaseTable || !p.IsEditorMode())
                     return ErrorOutput("check parses the script in a second script engine: like the editor's Check syntax it is only done while the editor is open and no game runs (vpx_editor stop, or vpx_editor open)"s);
                  json errors = json::array();
                  const vector<string> lines = ScriptLines(table->m_script_text);
                  CComObject<ScriptInterpreter>* interpreter;
                  CComObject<ScriptInterpreter>::CreateInstance(&interpreter);
                  interpreter->AddRef();
                  interpreter->Start(table);
                  interpreter->SetScriptErrorHandler([&errors, &lines, &p](ScriptInterpreter::ErrorType type, int line, int column, const string& description, const vector<string>& stackDump)
                  {
                     if (type == ScriptInterpreter::ErrorType::DebugConsole)
                        return;
                     json e { { "line", line }, { "column", column }, { "message", description } };
                     if (line >= 1 && static_cast<size_t>(line) <= lines.size())
                        e["source"] = lines[line - 1];
                     if (errors.size() < 50)
                        errors.push_back(e);
                     if (p.m_liveUI)
                        p.m_liveUI->m_editorUI.OnScriptError(type, line, column, description, stackDump);
                  });
                  interpreter->Parse(table->m_script_text);
                  const bool hasError = interpreter->HasError();
                  interpreter->Stop(table);
                  interpreter->Release();
                  return TextOutput(json { { "ok", !hasError && errors.empty() }, { "errors", errors }, { "lines", lines.size() },
                     { "note", "the editor's Check syntax (F7): the script is parsed, nothing runs; the errors are also marked in the editor's script pane. Errors that only happen when the code runs (unknown names, missing parts) need a playtest" } });
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
                  RecordScriptEdit(p, table, "MCP script set"s, table->m_script_text);
                  table->m_script_text = text;
                  table->SetDirtyScript(SaveDirtyState::Dirty);
                  McpServer::Get().NoteEdit();
                  json result { { "lines", ScriptLines(table->m_script_text).size() } };
                  AddSidecarWarning(table, result);
                  return TextOutput(result);
               }
               if (action == "append")
               {
                  RecordScriptEdit(p, table, "MCP script append"s, table->m_script_text);
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
                  RecordScriptEdit(p, table, "MCP script replace"s, before);
                  table->SetDirtyScript(SaveDirtyState::Dirty);
                  McpServer::Get().NoteEdit();
                  return TextOutput(json { { "replaced", count } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

}

}
