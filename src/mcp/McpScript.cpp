// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

namespace
{
   struct ScriptRange { bool found = false; size_t startLine = 0; size_t endLine = 0; };
}

vector<string> ScriptLines(const string& script)
{
   vector<string> lines;
   size_t start = 0;
   while (start <= script.size())
   {
      const size_t end = script.find('\n', start);
      if (end == string::npos)
      {
         lines.push_back(script.substr(start));
         break;
      }
      lines.push_back(script.substr(start, end - start));
      start = end + 1;
   }
   if (!lines.empty() && lines.back().empty())
      lines.pop_back();
   for (string& line : lines)
      if (!line.empty() && line.back() == '\r')
         line.pop_back();
   return lines;
}

static ScriptRange FindSub(const vector<string>& lines, const string& name)
{
   ScriptRange range;
   const string lname = lowerCase(name);
   for (size_t i = 0; i < lines.size(); i++)
   {
      string l = lowerCase(lines[i]);
      l.erase(0, l.find_first_not_of(" \t"));
      const bool isSub = l.starts_with("sub ") || l.starts_with("function ") || l.starts_with("private sub ") || l.starts_with("public sub ") || l.starts_with("private function ") || l.starts_with("public function ");
      if (!isSub)
         continue;
      const size_t nameStart = l.find(lname);
      if (nameStart == string::npos)
         continue;
      const size_t nameEnd = nameStart + lname.size();
      if (nameEnd < l.size() && (isalnum(l[nameEnd]) || l[nameEnd] == '_'))
         continue;
      if (nameStart > 0 && (isalnum(l[nameStart - 1]) || l[nameStart - 1] == '_'))
         continue;
      range.found = true;
      range.startLine = i + 1;
      for (size_t j = i + 1; j < lines.size(); j++)
      {
         string e = lowerCase(lines[j]);
         e.erase(0, e.find_first_not_of(" \t"));
         if (e.starts_with("end sub") || e.starts_with("end function"))
         {
            range.endLine = j + 1;
            return range;
         }
      }
      range.endLine = lines.size();
      return range;
   }
   return range;
}

// A <table>.vbs next to the table file (or <folder>/<folder>.vbs) replaces the embedded script when the table is loaded
static std::filesystem::path SidecarScript(const PinTable* table)
{
   if (table == nullptr || table->m_filename.empty())
      return {};
   std::filesystem::path tableFile = table->m_filename;
   if (std::filesystem::path p = g_app->m_fileLocator.SearchScript(table, tableFile.replace_extension(".vbs")); !p.empty())
      return p;
   const std::filesystem::path folder = table->m_filename.parent_path();
   auto fn = folder.filename();
   fn += ".vbs";
   return find_case_insensitive_file_path(folder / fn);
}

void AddSidecarWarning(const PinTable* table, json& result)
{
   if (const std::filesystem::path sidecar = SidecarScript(table); !sidecar.empty())
      result["sidecarScript"] = json { { "path", PathToUTF8(sidecar) },
         { "warning", "This file replaces the embedded script every time the table is loaded: script edits are saved in the table but the next load runs the .vbs file instead. Update it with vpx_script export path:<it>, or remove it" } };
}

json ScriptErrorJson(const ScriptError& e)
{
   json j { { "seq", e.seq }, { "type", e.type }, { "line", e.line }, { "column", e.column }, { "description", e.description }, { "gameTime", e.gameTime } };
   if (!e.lineText.empty())
      j["lineText"] = e.lineText;
   if (!e.source.empty())
      j["source"] = e.source;
   return j;
}

// Script errors after a sequence number, deduplicated by line and message (a script failing every frame would flood the result)
json ScriptErrorsSummary(uint64_t after, size_t limit)
{
   json distinct = json::array();
   size_t total = 0;
   std::map<std::pair<int, string>, size_t> seen;
   for (const ScriptError& e : McpServer::Get().GetScriptErrors())
      if (e.seq > after)
      {
         total++;
         if (const auto it = seen.find({ e.line, e.description }); it != seen.end())
         {
            if (it->second < distinct.size())
               distinct[it->second]["count"] = distinct[it->second].value("count", 1) + 1;
         }
         else
         {
            seen[{ e.line, e.description }] = distinct.size() < limit ? distinct.size() : SIZE_MAX;
            if (distinct.size() < limit)
               distinct.push_back(ScriptErrorJson(e));
         }
      }
   const uint64_t last = McpServer::Get().GetScriptErrorCount();
   return json { { "count", last > after ? last - after : total }, { "distinct", distinct } };
}

// Errors with a sequence number above the given one
json ScriptErrorsJson(uint64_t after)
{
   json errs = json::array();
   for (const ScriptError& e : McpServer::Get().GetScriptErrors())
      if (e.seq > after)
         errs.push_back(ScriptErrorJson(e));
   return errs;
}

// Lines [start, end] of a script (1-based, inclusive) as text
static string ScriptText(const vector<string>& lines, size_t start, size_t end)
{
   string text;
   for (size_t i = start; i <= end && i <= lines.size(); i++)
      text += lines[i - 1] + '\n';
   return text;
}

// Name of the Sub or Function declared on a line, empty if none
static string DeclaredSubName(const string& line)
{
   string l = lowerCase(line);
   l.erase(0, l.find_first_not_of(" \t"));
   for (const char* prefix : { "private sub ", "public sub ", "private function ", "public function ", "sub ", "function " })
      if (l.starts_with(prefix))
      {
         string name = l.substr(strlen(prefix));
         name.erase(0, name.find_first_not_of(" \t"));
         const size_t end = name.find_first_of(" \t(");
         return name.substr(0, end);
      }
   return ""s;
}

ToolOutput ScriptGet(const string& script, const json& args)
{
   const vector<string> lines = ScriptLines(script);
   if (args.contains("sub"))
   {
      const ScriptRange range = FindSub(lines, args.value("sub", ""s));
      if (!range.found)
         return ErrorOutput("No Sub or Function named '"s + args.value("sub", ""s) + '\'');
      return TextOutput(json { { "startLine", range.startLine }, { "endLine", range.endLine }, { "totalLines", lines.size() }, { "text", ScriptText(lines, range.startLine, range.endLine) } });
   }
   if (args.contains("pattern"))
   {
      const string pattern = lowerCase(args.value("pattern", ""s));
      const int context = std::clamp(args.value("context", 2), 0, 20);
      json matches = json::array();
      for (size_t i = 0; i < lines.size() && matches.size() < 100; i++)
         if (lowerCase(lines[i]).find(pattern) != string::npos)
         {
            const size_t from = i + 1 > static_cast<size_t>(context) ? i + 1 - context : 1;
            matches.push_back({ { "line", i + 1 }, { "text", ScriptText(lines, from, std::min(lines.size(), i + 1 + context)) } });
         }
      return TextOutput(json { { "totalLines", lines.size() }, { "matches", matches } });
   }
   if (args.contains("subs") && args.value("subs", false))
   {
      json subs = json::array();
      for (size_t i = 0; i < lines.size(); i++)
         if (const string name = DeclaredSubName(lines[i]); !name.empty())
            subs.push_back({ { "name", name }, { "line", i + 1 } });
      return TextOutput(json { { "totalLines", lines.size() }, { "subs", subs } });
   }
   const size_t start = std::clamp<size_t>(args.value("startLine", 1), 1, std::max<size_t>(1, lines.size()));
   const size_t end = std::clamp<size_t>(args.value("endLine", static_cast<int>(lines.size())), start, lines.size());
   return TextOutput(json { { "startLine", start }, { "endLine", end }, { "totalLines", lines.size() }, { "text", ScriptText(lines, start, end) } });
}

// Identifiers used in a piece of script (lower case, without string literals and comments)
static std::set<string> ScriptIdentifiers(const string& text)
{
   std::set<string> ids;
   string current;
   bool inString = false, inComment = false;
   for (const char c : text)
   {
      if (inComment)
      {
         if (c == '\n')
            inComment = false;
         continue;
      }
      if (c == '"')
         inString = !inString;
      if (inString)
         continue;
      if (c == '\'')
      {
         inComment = true;
         continue;
      }
      if (isalnum(static_cast<unsigned char>(c)) || c == '_')
         current += static_cast<char>(tolower(c));
      else
      {
         if (!current.empty() && !isdigit(static_cast<unsigned char>(current[0])))
            ids.insert(current);
         current.clear();
      }
   }
   if (!current.empty())
      ids.insert(current);
   return ids;
}

// Event handlers (Sub <Part>_<Event>) of the given parts found in a script, plus the donor Subs and Functions they call (transitively, a few levels),
// as text, skipping the ones already declared in 'existing'
string CollectPartHandlers(const string& script, const vector<string>& partNames, const string& existing, json& copied)
{
   const vector<string> lines = ScriptLines(script);
   const vector<string> existingLines = ScriptLines(existing);
   std::map<string, ScriptRange> donorSubs;
   for (size_t i = 0; i < lines.size(); i++)
      if (const string name = DeclaredSubName(lines[i]); !name.empty() && !donorSubs.contains(name))
         donorSubs[name] = FindSub(lines, name);
   vector<string> wanted;
   for (const auto& [name, range] : donorSubs)
      for (const string& part : partNames)
         if (name.starts_with(lowerCase(part) + '_'))
            wanted.push_back(name);
   string text;
   std::set<string> done;
   for (int level = 0; level < 4 && !wanted.empty(); level++)
   {
      vector<string> next;
      for (const string& name : wanted)
      {
         if (done.contains(name) || !donorSubs.contains(name) || !donorSubs[name].found)
            continue;
         done.insert(name);
         if (FindSub(existingLines, name).found)
            continue;
         const string body = ScriptText(lines, donorSubs[name].startLine, donorSubs[name].endLine);
         text += body;
         copied.push_back(name);
         for (const string& id : ScriptIdentifiers(body))
            if (id != name && donorSubs.contains(id) && !done.contains(id))
               next.push_back(id);
      }
      wanted = next;
   }
   return text;
}

}
