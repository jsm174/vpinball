// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

#include <miniz/miniz.h>

namespace VPX::MCP
{

static json VRAuditJson(PinTable* table)
{
   constexpr unsigned int flatModes = PartGroupData::PlayerModeVisibilityMask::PMVM_DESKTOP | PartGroupData::PlayerModeVisibilityMask::PMVM_FSS | PartGroupData::PlayerModeVisibilityMask::PMVM_CABINET;
   constexpr unsigned int xrModes = PartGroupData::PlayerModeVisibilityMask::PMVM_VIRTUAL_REALITY | PartGroupData::PlayerModeVisibilityMask::PMVM_MIXED_REALITY;
   const auto modes = [](unsigned int mask)
   {
      json list = json::array();
      for (const auto& [bit, label] : { std::pair { PartGroupData::PlayerModeVisibilityMask::PMVM_DESKTOP, "desktop" }, { PartGroupData::PlayerModeVisibilityMask::PMVM_FSS, "fss" },
              { PartGroupData::PlayerModeVisibilityMask::PMVM_CABINET, "cabinet" }, { PartGroupData::PlayerModeVisibilityMask::PMVM_VIRTUAL_REALITY, "vr" }, { PartGroupData::PlayerModeVisibilityMask::PMVM_MIXED_REALITY, "mr" } })
         if (mask & bit)
            list.push_back(label);
      return list;
   };
   const auto spaceName = [](PartGroupData::SpaceReference space)
   {
      switch (space)
      {
      case PartGroupData::SpaceReference::SR_CABINET: return "cabinet";
      case PartGroupData::SpaceReference::SR_CABINET_FEET: return "cabinet feet";
      case PartGroupData::SpaceReference::SR_ROOM: return "room";
      default: return "playfield";
      }
   };
   json groups = json::array(), warnings = json::array(), cabinetParts = json::array(), outside = json::array();
   bool hasXRContent = false;
   float cabinetLength = 0.f;
   string xrOnly;
   for (IEditable* part : table->GetParts())
   {
      if (part->GetItemType() != eItemPartGroup)
         continue;
      const PartGroup* group = static_cast<PartGroup*>(part);
      const unsigned int mask = group->GetPlayerModeVisibilityMask();
      const PartGroupData::SpaceReference space = group->GetReferenceSpace();
      if (mask == PartGroupData::PlayerModeVisibilityMask::PMVM_ALL && space == PartGroupData::SpaceReference::SR_PLAYFIELD)
         continue;
      hasXRContent |= (mask & xrModes) && !(mask & flatModes);
      hasXRContent |= space != PartGroupData::SpaceReference::SR_PLAYFIELD;
      groups.push_back(json { { "name", group->GetName() }, { "visibleIn", modes(mask) }, { "space", spaceName(space) } });
      if ((mask & xrModes) && !(mask & flatModes))
         xrOnly += (xrOnly.empty() ? ""s : ", "s) + group->GetName();
   }
   if (!xrOnly.empty())
      warnings.push_back("Groups shown only in VR/MR (" + xrOnly + "): VPX uses that visibility only when a headset is connected. With RenderingModeOverride 2 (forced VR) and no headset, "
         "the script sees RenderingMode 2 but parts are filtered with the desktop, FSS or cabinet visibility of the current view mode, so these groups stay hidden. Preview with vpx_screenshot mode:'vr'");
   for (IEditable* part : table->GetParts())
   {
      const PartGroup* group = part->GetPartGroup();
      if (part->GetItemType() == eItemPartGroup || part->GetItemType() == eItemBall || group == nullptr || table->IsPlayerSessionObject(part))
         continue;
      const unsigned int mask = group->GetPlayerModeVisibilityMask();
      const json bounds = PartBoundsJson(part);
      if (bounds.is_null())
         continue;
      const float x0 = bounds["min"][0], y0 = bounds["min"][1], x1 = bounds["max"][0], y1 = bounds["max"][1];
      if (group->GetReferenceSpace() != PartGroupData::SpaceReference::SR_PLAYFIELD)
      {
         cabinetParts.push_back(json { { "name", part->GetName() }, { "bounds", bounds } });
         cabinetLength = max(cabinetLength, y1 - y0);
      }
      else if ((mask & PartGroupData::PlayerModeVisibilityMask::PMVM_VIRTUAL_REALITY) && hasXRContent
         && (x1 < table->m_left || x0 > table->m_right || y1 < table->m_top || y0 > table->m_bottom))
         outside.push_back(part->GetName());
   }
   if (!outside.empty())
   {
      string names;
      for (size_t i = 0; i < outside.size() && i < 12; i++)
         names += (i ? ", "s : ""s) + outside[i].get<string>();
      warnings.push_back(std::to_string(outside.size()) + " part(s) visible in VR lie entirely outside the playfield (" + names + (outside.size() > 12 ? ", ..." : "")
         + "): desktop backdrop decorations or spare parts there float around the cabinet in VR; put them in a group hidden in VR (vpx_part get reports each one's bounds)");
   }
   if (groups.empty() && cabinetParts.empty())
      return json();
   const float tableLength = table->m_bottom - table->m_top;
   if (!cabinetParts.empty() && cabinetLength < tableLength * 0.95f)
      warnings.push_back("The longest cabinet/room space part spans "s + std::to_string(static_cast<int>(cabinetLength)) + " units along y, shorter than the playfield (" + std::to_string(static_cast<int>(tableLength))
         + "): the cabinet does not cover the table; compare the bounds in cabinetParts with the table extents");
   json result { { "groups", groups }, { "table", json { { "left", table->m_left }, { "top", table->m_top }, { "right", table->m_right }, { "bottom", table->m_bottom }, { "glassTop", table->m_glassTopHeight }, { "glassBottom", table->m_glassBottomHeight } } } };
   if (!cabinetParts.empty())
   {
      if (cabinetParts.size() > 40)
         cabinetParts.erase(cabinetParts.begin() + 40, cabinetParts.end());
      result["cabinetParts"] = cabinetParts;
   }
   if (!warnings.empty())
      result["warnings"] = warnings;
   return result;
}

void AddTableTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_editor", "The live editor: 'open' it on the current table, 'close' it back to play, 'playtest' the edited table (a play session on a copy), 'stop' the playtest (both return once the switch is done, with the new status), 'undo' the last edit, 'save' the table to its file.",
         Schema({ { "action", Prop("string", "open | close | playtest | stop | undo | save") } }, { "action" }),
         [](const json& args) -> ToolStep
         {
            const string action = args.value("action", ""s);
            if (action == "playtest" || action == "stop")
            {
               struct Switch { bool requested = false; int framesAfter = 0; std::chrono::steady_clock::time_point start; };
               auto sw = std::make_shared<Switch>();
               return [action, sw]() -> std::optional<ToolOutput>
               {
                  if (!g_pplayer)
                     return ErrorOutput("No table session is running"s);
                  Player& p = *g_pplayer;
                  if (!sw->requested)
                  {
                     if (!p.m_liveUI)
                        return ErrorOutput("No UI available"s);
                     if (action == "playtest")
                     {
                        if (!p.m_liveUI->IsEditorUIOpened())
                           return ErrorOutput("The editor is not open"s);
                        if (p.m_liveUI->m_editorUI.IsInspectMode())
                           return ErrorOutput("A playtest is already running"s);
                        p.m_ignoreWndFocus = true;
                        p.m_liveUI->m_editorUI.PlayTest();
                     }
                     else
                     {
                        if (p.m_ptable->m_liveBaseTable == nullptr || !p.HasStackedTableSession())
                           return ErrorOutput("No playtest session to stop"s);
                        p.SetCloseState(Player::CS_STOP_PLAY);
                     }
                     sw->requested = true;
                     sw->start = std::chrono::steady_clock::now();
                     return std::nullopt;
                  }
                  const bool done = action == "playtest" ? IsLiveCopy(p) && !p.IsEditorMode() : !IsLiveCopy(p) && IsEditing(p);
                  if (done && ++sw->framesAfter > 1)
                  {
                     json result = StatusJson(p);
                     result["switched"] = action == "playtest" ? "playtest running on a copy of the edited table: part, table and script edits now reach only that copy; vpx_editor stop returns to the editor"s
                                                               : "editor restored on the edited table: edits now reach the table that is saved"s;
                     return TextOutput(result);
                  }
                  if (std::chrono::steady_clock::now() - sw->start > std::chrono::seconds(20))
                     return ErrorOutput("The "s + (action == "playtest" ? "playtest did not start" : "editor was not restored") + " within 20 s; check vpx_status"s);
                  return std::nullopt;
               };
            }
            return WithPlayer([action](Player& p)
            {
               if (!p.m_liveUI)
                  return ErrorOutput("No UI available"s);
               if (action == "open")
               {
                  p.m_liveUI->OpenEditorUI();
                  return TextOutput(StatusJson(p));
               }
               if (action == "close")
               {
                  p.m_liveUI->m_editorUI.Close();
                  return TextOutput(StatusJson(p));
               }
               if (action == "undo")
               {
                  if (const string err = RequireEditor(p); !err.empty())
                     return ErrorOutput(err);
                  if (!p.m_liveUI->m_editorUI.HasUndo())
                     return ErrorOutput("Nothing to undo"s);
                  p.m_liveUI->m_editorUI.Undo();
                  return TextOutput(json { { "undone", true }, { "canUndo", p.m_liveUI->m_editorUI.HasUndo() } });
               }
               if (action == "save")
               {
                  PinTable* table = p.m_ptable->m_liveBaseTable ? p.m_ptable->m_liveBaseTable : p.m_ptable;
                  if (table->m_filename.empty())
                     return ErrorOutput("The table has no file name yet: use vpx_table save_as"s);
                  const bool saved = SaveTableFile(p, table);
                  if (saved)
                     McpServer::Get().ResetEdits();
                  return saved ? TextOutput(json { { "saved", PathToUTF8(table->m_filename) } }) : ErrorOutput("Save failed"s);
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

   tools.push_back(ToolDef { "vpx_table",
      "The table as a whole. Actions: open (path) | new (a blank table): both replace the running session and return once the new one is up | save_as (path) | get (fields subset: table level properties with their meaning) | set (data: {field: value}, applied live, undoable while the editor is open) | audit (the VPX table audit, plus a vr section for tables with VR content). Details: vpx_status guide:\"table\".",
         Schema({ { "action", Prop("string", "open | new | save_as | audit | get | set") }, { "data", json { { "type", "object" }, { "description", "set: {field: value}" } } }, { "fields", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "get: subset" } } }, { "path", Prop("string", "Absolute .vpx path (open, save_as)") } }, { "action" }),
         [](const json& args) -> ToolStep
         {
            const string action = args.value("action", ""s);
            const std::filesystem::path path = PathFromUTF8(args.value("path", ""s));
            if (action == "open" || action == "new")
            {
               // The player replaces its session on the next frame: poll until the new table runs (jobs are not processed while it loads)
               struct Switch { const PinTable* target = nullptr; int framesAfter = 0; std::chrono::steady_clock::time_point start; };
               auto sw = std::make_shared<Switch>();
               return [action, path, sw]() -> std::optional<ToolOutput>
               {
                  if (!g_pplayer)
                     return ErrorOutput("No table session is running"s);
                  Player& p = *g_pplayer;
                  if (!sw->target)
                  {
                     if (action == "open" && path.empty())
                        return ErrorOutput("path is required"s);
                     string error;
                     CComObject<PinTable>* table = LoadTableForSwitch(action == "open" ? path : std::filesystem::path(), error);
                     if (!table)
                        return ErrorOutput(error);
                     sw->target = table;
                     sw->start = std::chrono::steady_clock::now();
                     p.SetTable(table, Player::TableTransition::Replace);
                     McpServer::Get().ResetEdits();
                     return std::nullopt;
                  }
                  if ((p.m_ptable == sw->target || p.m_ptable->m_liveBaseTable == sw->target) && ++sw->framesAfter > 1)
                  {
                     json result = StatusJson(p);
                     result["switched"] = action == "open" ? "the table was opened and its session started"s : "a new blank table was created, save it with vpx_table save_as"s;
                     return TextOutput(result);
                  }
                  if (std::chrono::steady_clock::now() - sw->start > std::chrono::seconds(90))
                     return ErrorOutput("The new table session did not start within 90 s; check vpx_status"s);
                  return std::nullopt;
               };
            }
            return WithPlayer([action, path, args](Player& p)
            {
               if (action == "get")
               {
                  const vector<string> wanted = args.value("fields", vector<string>());
                  json values = json::object(), meaning = json::object(), unknown = json::array();
                  for (const string& w : wanted)
                     if (std::ranges::none_of(TableFields(), [&w](const TableField& f) { return w == f.name; }))
                        unknown.push_back(w);
                  for (const TableField& f : TableFields())
                     if (wanted.empty() || std::ranges::find(wanted, string(f.name)) != wanted.end())
                     {
                        values[f.name] = f.get(p.m_ptable);
                        meaning[f.name] = string(f.kind) + ": " + f.description;
                     }
                  json result { { "table", p.m_ptable->m_title }, { "data", values } };
                  if (wanted.empty())
                     result["fields"] = meaning;
                  if (!unknown.empty())
                     result["unknown"] = unknown;
                  return TextOutput(result);
               }
               if (action == "set")
               {
                  const json data = args.value("data", json::object());
                  if (!data.is_object() || data.empty())
                     return ErrorOutput("data is required: {field: value}, e.g. data: {\"image\": \"MyPlayfield\"}; vpx_table get lists the fields"s);
                  vector<std::pair<const TableField*, json>> updates;
                  for (const auto& [key, value] : data.items())
                  {
                     const auto it = std::ranges::find_if(TableFields(), [&key](const TableField& f) { return NormalizeFieldName(key) == NormalizeFieldName(f.name); });
                     if (it == TableFields().end())
                        return ErrorOutput("Unknown table field '"s + key + "': vpx_table get lists them");
                     const string kind = it->kind;
                     const bool ok = kind == "string" ? value.is_string() : kind == "boolean" ? value.is_boolean() : value.is_number();
                     if (!ok)
                        return ErrorOutput("'"s + it->name + "' expects a " + kind);
                     if (kind == "string" && !value.get<string>().empty())
                     {
                        const string v = value.get<string>();
                        const bool isMaterial = string(it->name) == "material";
                        if (isMaterial ? p.m_ptable->IsDummyMaterial(p.m_ptable->GetMaterial(v)) : p.m_ptable->GetImage(v) == nullptr)
                           return ErrorOutput("'"s + it->name + "': no " + (isMaterial ? "material" : "image") + " named '" + v + '\'');
                     }
                     updates.emplace_back(&*it, value);
                  }
                  bool shaderDirty = false;
                  json applied = json::array();
                  vector<std::pair<const TableField*, json>> previous;
                  for (const auto& [field, value] : updates)
                  {
                     previous.emplace_back(field, field->get(p.m_ptable));
                     field->set(p.m_ptable, value);
                     if (p.m_ptable->m_liveBaseTable)
                        field->set(p.m_ptable->m_liveBaseTable, value);
                     shaderDirty |= field->shaderDirty;
                     applied.push_back(field->name);
                  }
                  if (shaderDirty)
                     p.m_renderer->MarkShaderDirty();
                  PinTable* const table = p.m_ptable;
                  RecordAction(p, table, [table, previous, shaderDirty]()
                  {
                     for (const auto& [field, value] : previous)
                        field->set(table, value);
                     if (shaderDirty && g_pplayer && g_pplayer->m_ptable == table)
                        g_pplayer->m_renderer->MarkShaderDirty();
                  });
                  if (p.m_ptable->m_liveBaseTable)
                     p.m_ptable->m_liveBaseTable->SetNonUndoableDirty(SaveDirtyState::Dirty);
                  McpServer::Get().NoteEdit();
                  json result { { "applied", applied } };
                  if (IsLiveCopy(p))
                     result["note"] = "Applied to the running playtest and to the edited table, so it stays after the playtest stops; save with vpx_editor save";
                  return TextOutput(result);
               }
               if (action == "audit")
               {
                  const PinTable* table = p.m_ptable->m_liveBaseTable ? p.m_ptable->m_liveBaseTable : p.m_ptable;
                  json errors = json::array(), warnings = json::array(), other = json::array();
                  std::istringstream lines(table->AuditTable(false));
                  for (string line; std::getline(lines, line);)
                  {
                     if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                     if (line.rfind(". ", 0) == 0)
                        line = line.substr(2);
                     if (line.empty())
                        continue;
                     if (line.rfind("Error: ", 0) == 0)
                        errors.push_back(line.substr(7));
                     else if (line.rfind("Warning: ", 0) == 0)
                        warnings.push_back(line.substr(9));
                     else
                        other.push_back(line);
                  }
                  json result { { "errors", errors }, { "warnings", warnings } };
                  if (!other.empty())
                     result["notes"] = other;
                  if (json vr = VRAuditJson(p.m_ptable); !vr.is_null())
                     result["vr"] = vr;
                  return TextOutput(result);
               }
               if (action == "save_as")
               {
                  if (path.empty())
                     return ErrorOutput("path is required"s);
                  PinTable* table = p.m_ptable->m_liveBaseTable ? p.m_ptable->m_liveBaseTable : p.m_ptable;
                  table->m_filename = path;
                  table->m_title = PathToUTF8(path.stem());
                  const bool saved = SaveTableFile(p, table);
                  if (saved)
                     McpServer::Get().ResetEdits();
                  return saved ? TextOutput(json { { "saved", PathToUTF8(table->m_filename) } }) : ErrorOutput("Save failed"s);
               }
               return ErrorOutput("Unknown action"s);
            });
         }, 120.0 });

   tools.push_back(ToolDef { "vpx_export",
      "Package the table folder as a .vpxz archive for the mobile apps: one folder holding exactly one table with its companion files (sidecar script, ini, backglass, DMD, pinmame, music...), cache/ and other .vpx files left out. Options: path (default ~/Downloads/<table folder>.vpxz) | save (save the table first, else unsaved edits are reported and left out) | dryRun (list only) | exclude (relative paths, names or wildcards, a trailing / for folders). Written in the background. Details: vpx_status guide:\"export\".",
         Schema({ { "path", Prop("string", "Output .vpxz file, default ~/Downloads/<table>.vpxz") }, { "save", Prop("boolean", "Save the table before packing, default false") },
                  { "dryRun", Prop("boolean", "List the files without writing the archive") },
                  { "exclude", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "Relative paths, names or wildcard patterns to leave out ('art/', '*.psd', 'notes.txt')" } } } }),
         [](const json& args)
         {
            struct Export
            {
               bool started = false;
               std::atomic<bool> done { false };
               string error;
               std::filesystem::path out;
               vector<std::pair<std::filesystem::path, string>> files; // source, archive name
               json summary;
               std::thread worker;
               ~Export()
               {
                  if (worker.joinable())
                     worker.join();
               }
            };
            auto ex = std::make_shared<Export>();
            return ToolStep([ex, args]() -> std::optional<ToolOutput>
            {
               if (!ex->started)
               {
                  ex->started = true;
                  if (!g_pplayer)
                     return ErrorOutput("No table session is running"s);
                  Player& p = *g_pplayer;
                  PinTable* table = p.m_ptable->m_liveBaseTable ? p.m_ptable->m_liveBaseTable : p.m_ptable;
                  if (table->m_filename.empty())
                     return ErrorOutput("The table has never been saved: use vpx_table save_as first"s);
                  json warnings = json::array();
                  if (args.value("save", false))
                  {
                     if (!SaveTableFile(p, table))
                        return ErrorOutput("Saving the table failed"s);
                     McpServer::Get().ResetEdits();
                  }
                  else if (table->GetDirtyState() != SaveDirtyState::Clean)
                     warnings.push_back("The table has unsaved changes: the archive holds the version on disk (pass save:true to include them)");
                  const std::filesystem::path tableFile = table->m_filename;
                  const std::filesystem::path folder = tableFile.parent_path();
                  const string stem = PathToUTF8(tableFile.stem());
                  std::error_code ec;
                  const string root = PathToUTF8(folder.filename());
                  json otherTables = json::array(), excluded = json::array(), sourceFiles = json::array();
                  int systemFiles = 0;
                  const vector<string> excludes = args.value("exclude", vector<string>());
                  const auto glob = [](const string& pattern, const string& text)
                  {
                     size_t p = 0, t = 0, star = string::npos, mark = 0;
                     while (t < text.size())
                     {
                        if (p < pattern.size() && (pattern[p] == '?' || tolower(pattern[p]) == tolower(text[t])))
                           p++, t++;
                        else if (p < pattern.size() && pattern[p] == '*')
                           star = p++, mark = t;
                        else if (star != string::npos)
                           p = star + 1, t = ++mark;
                        else
                           return false;
                     }
                     while (p < pattern.size() && pattern[p] == '*')
                        p++;
                     return p == pattern.size();
                  };
                  const auto isExcluded = [&excludes, &glob](const string& relPath, const string& fileName, bool isDir)
                  {
                     for (string pattern : excludes)
                     {
                        std::ranges::replace(pattern, '\\', '/');
                        const bool dirPattern = !pattern.empty() && pattern.back() == '/';
                        if (dirPattern)
                           pattern.pop_back();
                        if (dirPattern && !isDir)
                           continue;
                        if (glob(pattern, relPath) || (pattern.find('/') == string::npos && glob(pattern, fileName)))
                           return true;
                     }
                     return false;
                  };
                  auto add = [&ex, &root](const std::filesystem::path& src, const std::filesystem::path& rel)
                  {
                     string name = root + '/' + PathToUTF8(rel.generic_u8string());
                     std::ranges::replace(name, '\\', '/');
                     if (std::ranges::none_of(ex->files, [&name](const auto& f) { return f.second == name; }))
                        ex->files.emplace_back(src, name);
                  };
                  for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                  {
                     if (ec)
                        break;
                     const std::filesystem::path rel = std::filesystem::relative(it->path(), folder, ec);
                     const string relName = PathToUTF8(rel.generic_u8string()), fileName = PathToUTF8(it->path().filename());
                     if (!fileName.empty() && (fileName[0] == '.' || StrCompareNoCase(fileName, "Thumbs.db"s) || StrCompareNoCase(fileName, "desktop.ini"s)))
                     {
                        if (it->is_directory(ec))
                           it.disable_recursion_pending();
                        else
                           systemFiles++;
                        continue;
                     }
                     if (isExcluded(relName, fileName, it->is_directory(ec)))
                     {
                        excluded.push_back(relName + (it->is_directory(ec) ? "/" : ""));
                        if (it->is_directory(ec))
                           it.disable_recursion_pending();
                        continue;
                     }
                     if (it->is_directory(ec))
                     {
                        if (it.depth() == 0 && StrCompareNoCase(PathToUTF8(rel), "cache"s))
                           it.disable_recursion_pending();
                        continue;
                     }
                     if (!it->is_regular_file(ec) || StrCompareNoCase(PathToUTF8(it->path().extension()), ".vpxz"s) || StrCompareNoCase(PathToUTF8(it->path().extension()), ".tmp"s))
                        continue;
                     if (StrCompareNoCase(PathToUTF8(it->path().extension()), ".vpx"s) && it->path() != tableFile)
                     {
                        otherTables.push_back(PathToUTF8(rel.generic_u8string()));
                        continue;
                     }
                     static const char* const authoringExts[] = { ".psd", ".psb", ".xcf", ".kra", ".ora", ".ai", ".afphoto", ".afdesign", ".procreate", ".blend", ".blend1", ".fbx", ".max", ".ma", ".mb", ".c4d", ".zip", ".7z", ".rar", ".vpxz", ".bak" };
                     const string ext = PathToUTF8(it->path().extension());
                     if (std::ranges::any_of(authoringExts, [&ext](const char* e) { return StrCompareNoCase(ext, string(e)); }))
                        sourceFiles.push_back(relName);
                     add(it->path(), rel);
                  }
                  if (!otherTables.empty())
                     ex->summary["otherTablesLeftOut"] = otherTables;
                  if (!excluded.empty())
                     ex->summary["excluded"] = excluded;
                  if (systemFiles > 0)
                     ex->summary["systemFilesSkipped"] = systemFiles;
                  if (!sourceFiles.empty())
                  {
                     ex->summary["possibleSourceFiles"] = sourceFiles;
                     warnings.push_back("Some packed files look like authoring sources (see possibleSourceFiles): pass exclude to leave them out");
                  }
                  // The table ini may live outside the table folder (settings folder)
                  if (const std::filesystem::path ini = table->GetSettingsFileName(); !ini.empty() && FileExists(ini))
                     add(ini, std::filesystem::path(stem + ".ini"));
                  if (std::ranges::none_of(ex->files, [&tableFile](const auto& f) { return f.first == tableFile; }))
                     add(tableFile, tableFile.filename());
                  const string outArg = args.value("path", ""s);
                  if (!outArg.empty())
                     ex->out = PathFromUTF8(outArg);
                  else
                  {
                     const char* home = getenv("HOME");
                     std::filesystem::path downloads = home ? std::filesystem::path(home) / "Downloads" : std::filesystem::temp_directory_path();
                     if (!std::filesystem::is_directory(downloads, ec))
                        downloads = std::filesystem::temp_directory_path();
                     ex->out = downloads / (root + ".vpxz");
                  }
                  if (ex->out.extension() != ".vpxz")
                     ex->out += ".vpxz";
                  uintmax_t total = 0;
                  json list = json::array();
                  for (const auto& [src, name] : ex->files)
                  {
                     total += std::filesystem::file_size(src, ec);
                     if (list.size() < 200)
                        list.push_back(name);
                  }
                  ex->summary["files"] = ex->files.size();
                  ex->summary["sourceBytes"] = total;
                  ex->summary["contents"] = list;
                  if (!warnings.empty())
                     ex->summary["warnings"] = warnings;
                  if (args.value("dryRun", false))
                  {
                     ex->summary["path"] = PathToUTF8(ex->out);
                     ex->summary["dryRun"] = true;
                     return TextOutput(ex->summary);
                  }
                  ex->worker = std::thread([ex]()
                  {
                     const std::filesystem::path tmp = std::filesystem::path(ex->out).concat(".tmp");
                     mz_zip_archive zip {};
                     if (!mz_zip_writer_init_file_v2(&zip, PathToUTF8(tmp).c_str(), 0, MZ_ZIP_FLAG_WRITE_ZIP64))
                        ex->error = "Could not create "s + PathToUTF8(tmp);
                     else
                     {
                        static const std::set<string> stored { ".zip", ".7z", ".rar", ".mp4", ".mkv", ".avi", ".webm", ".m4v", ".png", ".jpg", ".jpeg", ".webp", ".gif", ".apng", ".ogg", ".mp3", ".m4a", ".flac", ".crz", ".cromc", ".exr", ".hdr" };
                        for (const auto& [src, name] : ex->files)
                        {
                           const bool alreadyCompressed = stored.contains(lowerCase(PathToUTF8(src.extension())));
                           if (!mz_zip_writer_add_file(&zip, name.c_str(), PathToUTF8(src).c_str(), nullptr, 0, alreadyCompressed ? MZ_NO_COMPRESSION : MZ_BEST_SPEED))
                           {
                              ex->error = "Could not add "s + PathToUTF8(src) + " to the archive";
                              break;
                           }
                        }
                        if (ex->error.empty() && !mz_zip_writer_finalize_archive(&zip))
                           ex->error = "Could not finalize the archive"s;
                        mz_zip_writer_end(&zip);
                     }
                     std::error_code ec;
                     if (ex->error.empty())
                     {
                        std::filesystem::rename(tmp, ex->out, ec);
                        if (ec)
                           ex->error = "Could not write "s + PathToUTF8(ex->out) + ": " + ec.message();
                     }
                     if (!ex->error.empty())
                        std::filesystem::remove(tmp, ec);
                     ex->done = true;
                  });
                  return std::nullopt;
               }
               if (!ex->done)
                  return std::nullopt;
               ex->worker.join();
               if (!ex->error.empty())
                  return ErrorOutput(ex->error);
               std::error_code ec;
               ex->summary["path"] = PathToUTF8(ex->out);
               ex->summary["archiveBytes"] = std::filesystem::file_size(ex->out, ec);
               ex->summary["note"] = "Copy it to the device (AirDrop, Files, the app's web server upload, or adb push) and open it with VPinball: it imports as one table";
               return TextOutput(ex->summary);
            });
         }, 600.0 });

}

}
