// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

void AddLibraryTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_library",
      "Part library and donor tables. Actions: list (the .vpz packs of the library folders plus an optional folder path, and the donor .vpx tables: the bundled example tables and the .vpx files in those folders) | inspect (path: parts by type with positions, asset counts, script size; type filter) | get_script (path, plus the vpx_script get options sub, pattern, context, subs, startLine, endLine) | clone (path, parts, group, x, y, withScript: copy parts with their materials, images, sounds and script handlers from a donor .vpx) | import (path .vpz, group, x, y) | export (path, parts: write a .vpz pack). import, clone and export need the editor open; imports are undoable. Bare parts from vpx_part add have no materials: prefer clone. Details: vpx_status guide:\"library\".",
         Schema({ { "action", Prop("string", "list | inspect | get_script | import | clone | export") }, { "path", Prop("string", "Pack (.vpz), donor table (.vpx) or folder") }, { "group", Prop("string", "import, clone: target group name") },
                  { "parts", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "Part names (clone, export)" } } }, { "withScript", Prop("boolean", "clone: copy the parts' event handlers, default true") },
                  { "x", Prop("number", "Center the imported parts here (import, clone)") }, { "y", Prop("number", "") }, { "type", Prop("string", "inspect: part type filter") },
                  { "sub", Prop("string", "get_script") }, { "pattern", Prop("string", "get_script") }, { "context", Prop("integer", "get_script") }, { "subs", Prop("boolean", "get_script") }, { "startLine", Prop("integer", "get_script") }, { "endLine", Prop("integer", "get_script") } },
            { "action" }),
         [](const json& args)
         {
            const string action = args.value("action", ""s);
            const std::filesystem::path path = PathFromUTF8(args.value("path", ""s));
            const vector<string> partNames = args.value("parts", vector<string>());
            const bool hasPos = args.contains("x") && args.contains("y");
            const float x = args.value("x", 0.f), y = args.value("y", 0.f);
            const string typeFilter = args.value("type", ""s);
            const bool withScript = args.value("withScript", true);
            const string groupName = args.value("group", ""s);
            return WithPlayer([=](Player& p)
            {
               if (action == "list")
               {
                  json packs = json::array(), donors = json::array(), folderNames = json::array();
                  vector<std::filesystem::path> folders = LibraryFolders();
                  if (!path.empty())
                     folders.push_back(path);
                  for (const std::filesystem::path& folder : folders)
                     folderNames.push_back(PathToUTF8(folder));
                  static const std::pair<const char*, const char*> bundled[] = {
                     { "exampleTable.vpx", "complete example game: flippers, slingshots, bumpers, targets, kickers, wire and plastic ramps, lights, script with handlers, VR cabinet and room" },
                     { "blankTable.vpx", "the new table template: playfield, flippers, plunger, drain, walls, VR cabinet" },
                     { "strippedTable.vpx", "a minimal table with only the playfield" },
                     { "lightSeqTable.vpx", "light sequencer demo" } };
                  for (const auto& [file, content] : bundled)
                     if (const std::filesystem::path donor = g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Assets, file); FileExists(donor))
                        donors.push_back({ { "path", PathToUTF8(donor) }, { "bundled", true }, { "content", content } });
                  // One walk of the library folders collects both the packs and the donor tables
                  std::error_code ec;
                  for (const std::filesystem::path& folder : folders)
                     for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                     {
                        if (it.depth() > 5)
                           it.disable_recursion_pending();
                        if (!it->is_regular_file(ec))
                           continue;
                        const string ext = lowerCase(PathToUTF8(it->path().extension()));
                        if (ext == ".vpz")
                           packs.push_back(PackInfoJson(it->path()));
                        else if (ext == ".vpx" && donors.size() < 200)
                           donors.push_back({ { "path", PathToUTF8(it->path()) } });
                     }
                  return TextOutput(json { { "folders", folderNames }, { "packs", packs }, { "donors", donors },
                     { "note", "donors are .vpx tables to clone complete parts from (with materials, images, sounds and script handlers): vpx_library inspect path:<donor> lists their parts, clone copies them" } });
               }
               if (action == "inspect" || action == "get_script")
               {
                  if (action == "inspect" && lowerCase(PathToUTF8(path.extension())) != ".vpx")
                     return TextOutput(PackInfoJson(path));
                  string error;
                  CComObject<PinTable>* donor = LoadDonorTable(path, error);
                  if (!donor)
                     return ErrorOutput(error);
                  ToolOutput out;
                  if (action == "get_script")
                     out = ScriptGet(donor->m_script_text, args);
                  else
                  {
                     vector<IEditable*> parts;
                     for (IEditable* part : donor->GetParts())
                        if (typeFilter.empty() || StrCompareNoCase(typeFilter, ItemTypeName(part->GetItemType())))
                           parts.push_back(part);
                     out = TextOutput(json { { "path", PathToUTF8(path) }, { "name", donor->m_title }, { "width", donor->m_right - donor->m_left }, { "height", donor->m_bottom - donor->m_top },
                        { "materials", donor->m_materials.size() }, { "images", donor->m_vimage.size() }, { "sounds", donor->m_vsound.size() }, { "scriptLines", ScriptLines(donor->m_script_text).size() },
                        { "parts", PartsJson(parts) } });
                  }
                  donor->Release();
                  return out;
               }
               if (const string err = RequireEditor(p); !err.empty())
                  return ErrorOutput(err);
               if (path.empty())
                  return ErrorOutput("path is required"s);
               if (action == "export")
               {
                  vector<IEditable*> parts;
                  for (const string& n : partNames)
                     if (IEditable* part = FindPart(p, n))
                        parts.push_back(part);
                  if (parts.empty())
                     return ErrorOutput("No matching parts"s);
                  VPXFileFeedback feedback;
                  std::filesystem::path file = path;
                  if (lowerCase(PathToUTF8(file.extension())) != ".vpz")
                     file += ".vpz";
                  if (FAILED(p.m_ptable->SavePartsToJSONPack(file, parts, feedback)))
                     return ErrorOutput("Export failed"s);
                  return TextOutput(json { { "exported", PathToUTF8(file) }, { "parts", PartsJson(parts) } });
               }
               // The import, the group moves and the copied script handlers undo as one
               struct UndoGroup
               {
                  VPX::EditorUI::EditorUI& editor;
                  bool keep = false;
                  ~UndoGroup() { editor.EndUndoGroup(keep); }
               } undoGroup { p.m_liveUI->m_editorUI };
               p.m_liveUI->m_editorUI.BeginUndoGroup();
               std::filesystem::path pack = path;
               std::filesystem::path tempPack;
               string handlers;
               json copiedSubs = json::array();
               if (action == "clone")
               {
                  string error;
                  CComObject<PinTable>* donor = LoadDonorTable(path, error);
                  if (!donor)
                     return ErrorOutput(error);
                  vector<IEditable*> parts;
                  vector<string> found;
                  json missing = json::array();
                  for (const string& n : partNames)
                  {
                     IEditable* part = nullptr;
                     for (IEditable* candidate : donor->GetParts())
                        if (StrCompareNoCase(candidate->GetName(), n))
                           part = candidate;
                     if (part)
                     {
                        parts.push_back(part);
                        found.push_back(part->GetName());
                     }
                     else
                        missing.push_back(n);
                  }
                  if (parts.empty())
                  {
                     donor->Release();
                     return ErrorOutput("None of the parts exist in the donor table: "s + missing.dump());
                  }
                  tempPack = std::filesystem::temp_directory_path() / ("vpx-mcp-clone-"s + std::to_string(usec()) + ".vpz");
                  VPXFileFeedback feedback;
                  const HRESULT hr = donor->SavePartsToJSONPack(tempPack, parts, feedback);
                  if (withScript)
                     handlers = CollectPartHandlers(donor->m_script_text, found, p.m_ptable->m_script_text, copiedSubs);
                  donor->Release();
                  if (FAILED(hr))
                     return ErrorOutput("Could not extract the parts from the donor table"s);
                  pack = tempPack;
               }
               else if (action != "import")
                  return ErrorOutput("Unknown action"s);
               string error;
               const vector<IEditable*> imported = p.m_liveUI->m_editorUI.ImportPack(pack, error);
               if (!tempPack.empty())
               {
                  std::error_code ec;
                  std::filesystem::remove(tempPack, ec);
               }
               if (!error.empty())
                  return ErrorOutput(error);
               if (hasPos)
                  PlaceParts(p, imported, x, y);
               vector<IEditable*> kept = imported;
               json result;
               if (!groupName.empty())
               {
                  IEditable* target = FindPart(p, groupName);
                  if (target && target->GetItemType() != eItemPartGroup)
                     result["groupNote"] = "'"s + groupName + "' is not a group: the parts stayed in the pack's groups";
                  else
                  {
                     if (!target)
                     {
                        target = p.m_liveUI->m_editorUI.CreatePart(eItemPartGroup, Vertex2D(0.f, 0.f));
                        if (target)
                           p.m_ptable->RenamePart(target, groupName);
                     }
                     if (!target)
                        result["groupNote"] = "Could not create the group '"s + groupName + '\'';
                     else
                     {
                        PartGroup* group = static_cast<PartGroup*>(target);
                        vector<IEditable*> emptied;
                        for (IEditable* part : imported)
                        {
                           if (part == target)
                              continue;
                           if (part->GetItemType() == eItemPartGroup && part->GetPartGroup() == nullptr)
                           {
                              for (IEditable* child : imported)
                                 if (child->GetPartGroup() == part)
                                    child->SetPartGroup(group);
                              emptied.push_back(part);
                           }
                           else if (part->GetPartGroup() == nullptr)
                              part->SetPartGroup(group);
                        }
                        p.m_liveUI->m_editorUI.DeleteParts(emptied);
                        std::erase_if(kept, [&emptied](IEditable* part) { return std::ranges::find(emptied, part) != emptied.end(); });
                        result["group"] = target->GetName();
                     }
                  }
               }
               result["imported"] = PartsJson(kept);
               McpServer::Get().NoteEdit();
               if (!handlers.empty())
               {
                  // The handlers were collected for the donor names: they only apply when the parts kept their names
                  bool renamed = false;
                  for (IEditable* part : imported)
                     if (part->GetItemType() != eItemPartGroup && std::ranges::none_of(partNames, [part](const string& n) { return StrCompareNoCase(n, part->GetName()); }))
                        renamed = true;
                  if (renamed)
                     result["scriptNote"] = "Some parts were renamed to avoid conflicts: their event handlers were not copied, use vpx_library get_script and vpx_script append to adapt them";
                  else
                  {
                     PinTable* const table = p.m_ptable;
                     const string before = table->m_script_text;
                     RecordAction(p, table, [table, before]()
                     {
                        table->m_script_text = before;
                        table->SetDirtyScript(SaveDirtyState::Dirty);
                     });
                     if (!table->m_script_text.empty() && table->m_script_text.back() != '\n')
                        table->m_script_text += '\n';
                     table->m_script_text += handlers;
                     table->SetDirtyScript(SaveDirtyState::Dirty);
                     result["copiedSubs"] = copiedSubs;
                  }
               }
               undoGroup.keep = true;
               return TextOutput(result);
            });
         }, 120.0 });

}

}
