// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"
#include "core/TableAudit.h"

namespace VPX::MCP
{

static json UsersJson(const vector<WhereUsedInfo>& users)
{
   json arr = json::array();
   for (const WhereUsedInfo& u : users)
      arr.push_back({ { "part", u.whereUsedObjectname }, { "property", u.whereUsedPropertyName } });
   return arr;
}

static bool ScriptMentions(const PinTable* table, const string& name)
{
   return !name.empty() && lowerCase(table->m_script_text).find('"' + lowerCase(name) + '"') != string::npos;
}

// The re-encode the editor's property pane and table audit offer for an asset, applied through the editor as one undo record
static ToolOutput ReencodeOutput(Player& p, vector<TableAuditItem> fixes, const std::function<json()>& describe, const string& want)
{
   if (!IsEditing(p))
      return ErrorOutput("reencode needs the editor open on the table and no playtest"s);
   json offered = json::array();
   for (const TableAuditItem& fix : fixes)
      offered.push_back(fix.fixLabel);
   if (fixes.empty())
   {
      json j = describe();
      j["reencoded"] = false;
      j["reason"] = "already stored in its compact format: nothing to re-encode";
      return TextOutput(j);
   }
   const auto chosen = want.empty() ? fixes.begin() : std::ranges::find_if(fixes, [&want](const TableAuditItem& fix) { return lowerCase(fix.fixLabel).find(lowerCase(want)) != string::npos; });
   if (chosen == fixes.end())
      return ErrorOutput("No re-encode matching '"s + want + "'; offered: " + offered.dump());
   const json before = describe();
   string message;
   const bool ok = p.m_liveUI->m_editorUI.ApplyAuditFix(*chosen, message);
   json j = describe();
   j["reencoded"] = ok;
   j[ok ? "applied" : "reason"] = message;
   j["offered"] = offered;
   if (ok)
   {
      j["before"] = before;
      McpServer::Get().NoteEdit();
   }
   return TextOutput(j);
}

static json ProbeJson(const PinTable* table, const RenderProbe* probe)
{
   const vec3 n = probe->GetReflectionPlaneNormal();
   json users = json::array();
   for (const IEditable* part : table->GetParts())
      if (part->GetItemType() == eItemPrimitive)
      {
         const Primitive* const prim = static_cast<const Primitive*>(part);
         if (prim->m_d.m_szReflectionProbe == probe->GetName())
            users.push_back({ { "part", prim->GetName() }, { "property", "reflection_probe" } });
         if (prim->m_d.m_szRefractionProbe == probe->GetName())
            users.push_back({ { "part", prim->GetName() }, { "property", "refraction_probe" } });
      }
   return json { { "name", probe->GetName() }, { "type", static_cast<int>(probe->GetType()) }, { "normal", { Round(n.x, 4), Round(n.y, 4), Round(n.z, 4) } }, { "distance", Round(probe->GetReflectionPlaneDistance(), 2) },
      { "roughness", probe->GetRoughness() }, { "reflection_mode", static_cast<int>(probe->GetReflectionMode()) }, { "no_lightmaps", probe->GetReflectionNoLightmaps() }, { "usedBy", users } };
}

void AddAssetsTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_material", "Table materials, edited directly (every stored field, named as in the VPZ packs: base_color, glossiness_color, clearcoat_color, wrap_lighting, roughness, glossy_image_lerp, thickness, edge, edge_alpha, opacity, opacity_active, type, metal...). 'list' names and types; 'get' one material's fields and usedBy (the parts and properties that refer to it); 'unused' lists the materials nothing refers to; 'set' merges data into one (applies live, parts look materials up by name); 'add' creates one (name, optional data, optional copyOf); 'delete' removes one. Undoable with vpx_editor undo while the editor is open.",
         Schema({ { "action", Prop("string", "list | get | set | add | delete | unused") }, { "name", Prop("string", "Material name") }, { "data", json { { "type", "object" }, { "description", "Field values (set, add)" } } },
                  { "copyOf", Prop("string", "add: start from this material's values") } },
            { "action" }),
         [](const json& args)
         {
            const string action = args.value("action", ""s);
            const string name = args.value("name", ""s);
            const json data = args.value("data", json::object());
            const string copyOf = args.value("copyOf", ""s);
            return WithPlayer([=](Player& p)
            {
               PinTable* table = p.m_ptable;
               auto matDoc = [](Material* mat, string& error) { return ObjectDoc(JSONSerializer::kMaterialNode, [mat](JSONObjectWriter& w) { mat->Save(w, false); }, error); };
               if (action != "list" && action != "get" && action != "unused" && IsLiveCopy(p))
                  return ErrorOutput(LiveCopyError());
               if (action == "list")
               {
                  json arr = json::array();
                  for (Material* mat : table->GetMaterialList())
                  {
                     string error;
                     const nlohmann::ordered_json doc = matDoc(mat, error);
                     arr.push_back({ { "name", mat->m_name }, { "type", doc.value("type", 0) }, { "base_color", doc.value("base_color", 0) } });
                  }
                  return TextOutput(json { { "count", arr.size() }, { "materials", arr } });
               }
               if (action == "unused")
               {
                  json arr = json::array();
                  for (Material* mat : table->GetMaterialList())
                  {
                     vector<WhereUsedInfo> users;
                     table->ShowWhereMaterialUsed(users, mat);
                     if (users.empty())
                        arr.push_back(json { { "name", mat->m_name }, { "scriptMentions", ScriptMentions(table, mat->m_name) } });
                  }
                  return TextOutput(json { { "count", arr.size() }, { "unused", arr }, { "note", "materials no part or table property refers to; scriptMentions: the script contains the name as a string, so it may assign it at run time" } });
               }
               if (action == "add")
               {
                  if (name.empty())
                     return ErrorOutput("name is required"s);
                  if (!table->IsMaterialNameUnique(name))
                     return ErrorOutput("A material named '"s + name + "' already exists");
                  Material* mat = new Material();
                  if (!copyOf.empty())
                  {
                     Material* source = table->GetMaterial(copyOf);
                     if (!source)
                     {
                        delete mat;
                        return ErrorOutput("No material named '"s + copyOf + '\'');
                     }
                     *mat = *source;
                  }
                  mat->m_name = name;
                  string error;
                  nlohmann::ordered_json doc = matDoc(mat, error);
                  if (error.empty() && !data.empty())
                  {
                     MergeDoc(doc, data);
                     doc["name"] = name;
                     JSONObjectReader reader(doc, JSONSerializer::kMaterialNode, nullptr, CURRENT_FILE_FORMAT_VERSION);
                     mat->Load(reader);
                     if (reader.HasError())
                        error = "Some fields were rejected (see the log)"s;
                  }
                  mat->m_name = name;
                  table->AddMaterial(mat);
                  table->SetupLookUpTables(true); // The name lookup map is only rebuilt at session start
                  RecordAttachment<Material>(p, table, "MCP add material: "s + name, table->m_materials, mat, nullptr, [table]() { table->SetupLookUpTables(true); });
                  string err2;
                  json result { { "name", name }, { "data", ToJson(matDoc(mat, err2)) } };
                  if (!error.empty())
                     result["warning"] = error;
                  return TextOutput(result);
               }
               Material* mat = table->GetMaterial(name);
               if (!mat || table->IsDummyMaterial(mat))
                  return ErrorOutput("No material named '"s + name + '\'');
               if (action == "get")
               {
                  string error;
                  const nlohmann::ordered_json doc = matDoc(mat, error);
                  if (!error.empty())
                     return ErrorOutput(error);
                  vector<WhereUsedInfo> users;
                  table->ShowWhereMaterialUsed(users, mat);
                  return TextOutput(json { { "name", name }, { "data", ToJson(doc) }, { "usedBy", UsersJson(users) } });
               }
               if (action == "set")
               {
                  string error;
                  nlohmann::ordered_json doc = matDoc(mat, error);
                  if (!error.empty())
                     return ErrorOutput(error);
                  const auto capture = [matDoc, mat, name]() -> std::function<void()>
                  {
                     string err;
                     const nlohmann::ordered_json snapshot = matDoc(mat, err);
                     return [mat, name, snapshot]()
                     {
                        JSONObjectReader restore(snapshot, JSONSerializer::kMaterialNode, nullptr, CURRENT_FILE_FORMAT_VERSION);
                        mat->Load(restore);
                        mat->m_name = name;
                     };
                  };
                  const std::function<void()> undo = capture();
                  MergeDoc(doc, data);
                  doc["name"] = name;
                  JSONObjectReader reader(doc, JSONSerializer::kMaterialNode, nullptr, CURRENT_FILE_FORMAT_VERSION);
                  mat->Load(reader);
                  mat->m_name = name;
                  RecordAction(p, table, "MCP material: "s + name, undo, capture);
                  json applied = json::array();
                  for (const auto& [key, value] : data.items())
                     applied.push_back(key);
                  json result { { "name", name }, { "applied", applied }, { "data", ToJson(matDoc(mat, error)) } };
                  if (reader.HasError())
                     result["warning"] = "Some fields were rejected (see the log)";
                  return TextOutput(result);
               }
               if (action == "delete")
               {
                  // Detached, not deleted: the undo record owns it until it is restored or dropped
                  RemoveFromVectorSingle(table->m_materials, mat);
                  table->SetupLookUpTables(true);
                  RecordAttachment<Material>(p, table, "MCP delete material: "s + name, table->m_materials, nullptr, mat, [table]() { table->SetupLookUpTables(true); });
                  return TextOutput(json { { "deleted", name } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

   tools.push_back(ToolDef { "vpx_image", "Table images (textures). 'list' names, sizes and source paths; 'get' one with usedBy (the parts and properties that refer to it) and scriptMentions (exportPath writes it as a PNG); 'unused' lists the images nothing refers to; 'reimport' reloads one from its original file or from path, or all:true every image; 'reencode' converts the stored file to the compact format the editor offers (WebP, or EXR for HDR), refused with the reason when that would not be smaller, undoable; 'add' imports an image file (path, optional name, replaces an image of the same name); 'set' changes alphaTest (-1 disables); 'delete' removes one. Undoable with vpx_editor undo while the editor is open.",
         Schema({ { "action", Prop("string", "list | get | add | set | delete | reimport | reencode | unused") }, { "name", Prop("string", "Image name") }, { "to", Prop("string", "reencode: text of the offered conversion to apply when several are offered (default the first)") }, { "all", Prop("boolean", "reimport: every image from its original file") }, { "path", Prop("string", "add: image file to import") },
                  { "exportPath", Prop("string", "get: write the image to this file") }, { "alphaTest", Prop("number", "set") } },
            { "action" }),
         [](const json& args)
         {
            const string action = args.value("action", ""s);
            const string name = args.value("name", ""s);
            const std::filesystem::path path = PathFromUTF8(args.value("path", ""s));
            const std::filesystem::path exportPath = PathFromUTF8(args.value("exportPath", ""s));
            return WithPlayer([=](Player& p)
            {
               PinTable* table = p.m_ptable;
               auto imageJson = [](Texture* tex)
               {
                  return json { { "name", tex->m_name }, { "width", tex->m_width.load() }, { "height", tex->m_height.load() }, { "fileSize", tex->GetFileSize() }, { "path", PathToUTF8(tex->GetFilePath()) },
                     { "alphaTest", tex->m_alphaTestValue }, { "hdr", tex->IsHDR() } };
               };
               if (action != "list" && action != "get" && action != "unused" && IsLiveCopy(p))
                  return ErrorOutput(LiveCopyError());
               if (action == "list")
               {
                  json arr = json::array();
                  for (Texture* tex : table->GetImageList())
                     arr.push_back(imageJson(tex));
                  return TextOutput(json { { "count", arr.size() }, { "images", arr } });
               }
               if (action == "unused")
               {
                  json arr = json::array();
                  for (Texture* tex : table->GetImageList())
                  {
                     vector<WhereUsedInfo> users;
                     table->ShowWhereImageUsed(users, tex);
                     if (users.empty())
                        arr.push_back(json { { "name", tex->m_name }, { "fileSize", tex->GetFileSize() }, { "scriptMentions", ScriptMentions(table, tex->m_name) } });
                  }
                  return TextOutput(json { { "count", arr.size() }, { "unused", arr }, { "note", "images no part or table property refers to; scriptMentions: the script contains the name as a string, so it may assign it at run time (FlexDMD and frame animations do)" } });
               }
               // An image of the same name is set aside instead of being replaced in place, so that undo can bring it back. The table's own
               // import deletes the image it replaces, so the replacement is done here
               const auto importAs = [&p, table](const std::filesystem::path& file, const string& as, const char* label, string& error, string& note) -> Texture*
               {
                  Texture* image = Texture::CreateFromFile(file);
                  if (!image)
                  {
                     error = "Could not import "s + PathToUTF8(file);
                     return nullptr;
                  }
                  if (!as.empty())
                     image->m_name = as;
                  const auto inList = std::ranges::find_if(table->m_vimage, [&image](const Texture* other) { return StrCompareNoCase(other->m_name, image->m_name); });
                  Texture* const existing = !as.empty() && inList != table->m_vimage.end() ? *inList : nullptr;
                  if (existing && image->HasSameContent(*existing))
                  {
                     delete image;
                     note = "the file is identical to the image the table already holds: nothing changed";
                     return existing;
                  }
                  if (!existing)
                  {
                     Texture* const tex = table->ImportImage(image);
                     if (tex != image)
                     {
                        note = "the file is identical to the image '"s + tex->m_name + "' the table already holds: that image is used, nothing was added";
                        return tex;
                     }
                  }
                  else
                  {
                     image->m_alphaTestValue = existing->m_alphaTestValue;
                     RemoveFromVectorSingle(table->m_vimage, existing);
                     p.m_renderer->m_renderDevice->m_texMan.UnloadTexture(existing);
                     table->m_vimage.push_back(image);
                  }
                  table->SetupLookUpTables(true);
                  RecordAttachment<Texture>(p, table, (existing ? "MCP "s + label + " image: " : "MCP add image: "s) + image->m_name, table->m_vimage, image, existing, [table]()
                  {
                     table->SetupLookUpTables(true);
                     if (g_pplayer && g_pplayer->m_renderer)
                        g_pplayer->m_renderer->MarkShaderDirty();
                  });
                  p.m_renderer->MarkShaderDirty();
                  McpServer::Get().NoteEdit();
                  return image;
               };
               if (action == "reimport")
               {
                  vector<Texture*> targets;
                  if (args.value("all", false))
                     targets = table->GetImageList();
                  else if (Texture* one = table->GetImage(name))
                     targets.push_back(one);
                  else
                     return ErrorOutput("No image named '"s + name + "' (or pass all:true)");
                  if (targets.size() > 1 && !path.empty())
                     return ErrorOutput("path only applies to one image"s);
                  json done = json::array(), failed = json::array();
                  if (targets.size() > 1 && IsEditing(p))
                     p.m_liveUI->m_editorUI.BeginUndoGroup();
                  for (Texture* target : targets)
                  {
                     const string targetName = target->m_name;
                     const std::filesystem::path file = path.empty() ? target->GetFilePath() : path;
                     string error, note;
                     if (file.empty() || !FileExists(file))
                        failed.push_back({ { "name", targetName }, { "reason", "file not found: "s + PathToUTF8(file) } });
                     else if (Texture* tex = importAs(file, targetName, "reimport", error, note))
                     {
                        json j = imageJson(tex);
                        if (!note.empty())
                           j["note"] = note;
                        done.push_back(j);
                     }
                     else
                        failed.push_back({ { "name", targetName }, { "reason", error } });
                  }
                  if (targets.size() > 1 && IsEditing(p))
                     p.m_liveUI->m_editorUI.EndUndoGroup(!done.empty(), "MCP reimport "s + std::to_string(done.size()) + " images");
                  return TextOutput(json { { "reimported", done }, { "failed", failed } });
               }
               if (action == "add")
               {
                  if (path.empty() || !FileExists(path))
                     return ErrorOutput("Image file not found: "s + PathToUTF8(path));
                  string importError, importNote;
                  Texture* tex = importAs(path, name, "replace", importError, importNote);
                  if (!tex)
                     return ErrorOutput(importError);
                  json j = imageJson(tex);
                  if (!importNote.empty())
                     j["note"] = importNote;
                  if (lowerCase(table->m_script_text).find("flexdmd") != string::npos)
                     j["warning"] = "FlexDMD reads 'VPX."s + tex->m_name + "' from the saved table file: save the table (vpx_editor save) before the script uses this image, or FlexDMD will not find it";
                  return TextOutput(j);
               }
               Texture* tex = table->GetImage(name);
               if (!tex)
                  return ErrorOutput("No image named '"s + name + '\'');
               if (action == "get")
               {
                  json j = imageJson(tex);
                  vector<WhereUsedInfo> users;
                  table->ShowWhereImageUsed(users, tex);
                  j["usedBy"] = UsersJson(users);
                  j["scriptMentions"] = ScriptMentions(table, tex->m_name);
                  if (!exportPath.empty())
                  {
                     std::filesystem::path out = exportPath;
                     if (lowerCase(PathToUTF8(out.extension())) != ".png" && lowerCase(PathToUTF8(out.extension())) != ".jpg")
                        out += ".png";
                     const auto bitmap = tex->GetRawBitmap(false, 0);
                     j["exported"] = bitmap && bitmap->Save(out) ? PathToUTF8(out) : "failed"s;
                  }
                  return TextOutput(j);
               }
               if (action == "reencode")
                  return ReencodeOutput(p, TableAudit::ImageFixes(*table, *tex), [table, name, imageJson]()
                  {
                     Texture* const now = table->GetImage(name);
                     return now ? imageJson(now) : json { { "name", name } };
                  }, args.value("to", ""s));
               if (action == "set")
               {
                  const auto capture = [tex]() -> std::function<void()> { return [tex, value = tex->m_alphaTestValue]() { tex->m_alphaTestValue = value; }; };
                  const std::function<void()> undo = capture();
                  if (args.contains("alphaTest"))
                     tex->m_alphaTestValue = args.value("alphaTest", -1.f) < 0.f ? static_cast<float>(-1.0 / 255.0) : args.value("alphaTest", 0.f);
                  RecordAction(p, table, "MCP image: "s + name, undo, capture);
                  return TextOutput(imageJson(tex));
               }
               if (action == "delete")
               {
                  RemoveFromVectorSingle(table->m_vimage, tex);
                  table->SetupLookUpTables(true);
                  RecordAttachment<Texture>(p, table, "MCP delete image: "s + name, table->m_vimage, nullptr, tex, [table]() { table->SetupLookUpTables(true); });
                  return TextOutput(json { { "deleted", name } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

   tools.push_back(ToolDef { "vpx_sound", "Table sounds. 'list' names, source paths, output (0 table, 1 backglass), volume, pan, fade; 'get' one with scriptMentions; 'add' imports a sound file (path); 'reimport' reloads one from its original file or from path (not undoable); 'export' writes one to path; 'reencode' converts the stored file as the editor offers (Ogg Opus, or mono for a stereo playfield sound; to picks one), refused with the reason when it would not be smaller, undoable; 'unused' lists the sounds the script never names; 'set' changes output, volume, pan or fade of one; 'delete' removes one. Undoable with vpx_editor undo while the editor is open.",
         Schema({ { "action", Prop("string", "list | get | add | set | delete | reimport | reencode | export | unused") }, { "name", Prop("string", "Sound name") }, { "to", Prop("string", "reencode: text of the offered conversion to apply when several are offered, e.g. mono (default the first)") }, { "path", Prop("string", "add: sound file to import; reimport: file to reload from (default its original file); export: file to write") },
                  { "output", Prop("integer", "set: 0 table, 1 backglass") }, { "volume", Prop("integer", "set: -100..100") }, { "pan", Prop("integer", "set: -100..100") }, { "fade", Prop("integer", "set: -100..100") } },
            { "action" }),
         [](const json& args)
         {
            const string action = args.value("action", ""s);
            const string name = args.value("name", ""s);
            const std::filesystem::path path = PathFromUTF8(args.value("path", ""s));
            return WithPlayer([=](Player& p)
            {
               PinTable* table = p.m_ptable;
               auto soundJson = [](VPX::Sound* snd)
               {
                  return json { { "name", snd->GetName() }, { "path", PathToUTF8(snd->GetImportPath()) }, { "output", static_cast<int>(snd->GetOutputTarget()) }, { "volume", snd->GetVolume() },
                     { "pan", snd->GetPan() }, { "fade", snd->GetFrontRearFade() } };
               };
               if (action != "list" && action != "get" && action != "unused" && action != "export" && IsLiveCopy(p))
                  return ErrorOutput(LiveCopyError());
               if (action == "list")
               {
                  json arr = json::array();
                  for (VPX::Sound* snd : table->m_vsound)
                     arr.push_back(soundJson(snd));
                  return TextOutput(json { { "count", arr.size() }, { "sounds", arr } });
               }
               if (action == "unused")
               {
                  json arr = json::array();
                  for (VPX::Sound* snd : table->m_vsound)
                     if (!ScriptMentions(table, snd->GetName()))
                        arr.push_back(snd->GetName());
                  return TextOutput(json { { "count", arr.size() }, { "unused", arr }, { "note", "sounds whose name does not appear as a string in the table script (sounds are only played by the script); a name built at run time (\"fx_\" & n) is not seen" } });
               }
               if (action == "add")
               {
                  if (path.empty() || !FileExists(path))
                     return ErrorOutput("Sound file not found: "s + PathToUTF8(path));
                  VPX::Sound* snd = table->ImportSound(path);
                  if (!snd)
                     return ErrorOutput("Could not import the sound"s);
                  if (!name.empty())
                     snd->SetName(name);
                  RecordAttachment<VPX::Sound>(p, table, "MCP add sound: "s + snd->GetName(), table->m_vsound, snd, nullptr, nullptr);
                  return TextOutput(soundJson(snd));
               }
               VPX::Sound* snd = table->GetSound(name);
               if (!snd)
                  return ErrorOutput("No sound named '"s + name + '\'');
               if (action == "get")
               {
                  json j = soundJson(snd);
                  j["scriptMentions"] = ScriptMentions(table, snd->GetName());
                  return TextOutput(j);
               }
               if (action == "export")
               {
                  if (path.empty())
                     return ErrorOutput("path is required: the file to write"s);
                  if (!table->ExportSound(snd, path))
                     return ErrorOutput("Could not write "s + PathToUTF8(path));
                  return TextOutput(json { { "exported", PathToUTF8(path) } });
               }
               if (action == "reencode")
               {
                  if (!p.m_audioPlayer)
                     return ErrorOutput("No audio player in this session"s);
                  return ReencodeOutput(p, TableAudit::SoundFixes(*snd, p.m_audioPlayer->GetSoundInformations(snd)), [table, name, soundJson]()
                  {
                     VPX::Sound* const now = table->GetSound(name);
                     json j = now ? soundJson(now) : json { { "name", name } };
                     if (now)
                        j["fileSize"] = now->GetFileSize();
                     return j;
                  }, args.value("to", ""s));
               }
               if (action == "reimport")
               {
                  const std::filesystem::path file = path.empty() ? snd->GetImportPath() : path;
                  if (file.empty() || !FileExists(file))
                     return ErrorOutput("Sound file not found: "s + PathToUTF8(file));
                  table->ReImportSound(snd, file);
                  table->SetNonUndoableDirty(SaveDirtyState::Dirty);
                  McpServer::Get().NoteEdit();
                  json j = soundJson(snd);
                  j["note"] = "reloaded from the file; like the editor's Reimport this can not be undone";
                  return TextOutput(j);
               }
               if (action == "set")
               {
                  const auto capture = [snd]() -> std::function<void()>
                  {
                     return [snd, output = snd->GetOutputTarget(), volume = snd->GetVolume(), pan = snd->GetPan(), fade = snd->GetFrontRearFade()]()
                     {
                        snd->SetOutputTarget(output);
                        snd->SetVolume(volume);
                        snd->SetPan(pan);
                        snd->SetFrontRearFade(fade);
                     };
                  };
                  const std::function<void()> undo = capture();
                  if (args.contains("output"))
                     snd->SetOutputTarget(static_cast<SoundOutTypes>(std::clamp(args.value("output", 0), 0, 1)));
                  if (args.contains("volume"))
                     snd->SetVolume(std::clamp(args.value("volume", 0), -100, 100));
                  if (args.contains("pan"))
                     snd->SetPan(std::clamp(args.value("pan", 0), -100, 100));
                  if (args.contains("fade"))
                     snd->SetFrontRearFade(std::clamp(args.value("fade", 0), -100, 100));
                  RecordAction(p, table, "MCP sound: "s + name, undo, capture);
                  return TextOutput(soundJson(snd));
               }
               if (action == "delete")
               {
                  RemoveFromVectorSingle(table->m_vsound, snd);
                  RecordAttachment<VPX::Sound>(p, table, "MCP delete sound: "s + name, table->m_vsound, nullptr, snd, nullptr);
                  return TextOutput(json { { "deleted", name } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

   tools.push_back(ToolDef { "vpx_collection", "Script collections of parts (what scripts and frameworks like GLF iterate over). 'list' collections with their members and flags; 'create' one (name, parts, fireEvents, stopSingleEvents, groupElements); 'add' / 'remove' parts of one; 'set' its flags; 'order' its members (parts: the names first in that order, the others keep theirs; scripts index a collection by position) or its place in the collection list (position); 'delete' one. Undoable with vpx_editor undo while the editor is open.",
         Schema({ { "action", Prop("string", "list | create | add | remove | set | order | delete") }, { "name", Prop("string", "Collection name") }, { "position", Prop("integer", "order: 0-based place of the collection in the table's collection list") },
                  { "parts", json { { "type", "array" }, { "items", json { { "type", "string" } } }, { "description", "Part names (create, add, remove)" } } },
                  { "fireEvents", Prop("boolean", "Fire the collection's events") }, { "stopSingleEvents", Prop("boolean", "Stop the members' own events") }, { "groupElements", Prop("boolean", "Group members in the editor") } },
            { "action" }),
         [](const json& args)
         {
            const string action = args.value("action", ""s);
            const string name = args.value("name", ""s);
            const vector<string> partNames = args.value("parts", vector<string>());
            return WithPlayer([=](Player& p)
            {
               PinTable* table = p.m_ptable;
               auto collJson = [](Collection* c)
               {
                  json members = json::array();
                  for (IEditable* part : c->GetParts())
                     members.push_back(part->GetName());
                  return json { { "name", c->m_name }, { "parts", members }, { "fireEvents", c->m_fireEvents }, { "stopSingleEvents", c->m_stopSingleEvents }, { "groupElements", c->m_groupElements } };
               };
               auto applyFlags = [&args](Collection* c)
               {
                  if (args.contains("fireEvents"))
                     c->m_fireEvents = args.value("fireEvents", false);
                  if (args.contains("stopSingleEvents"))
                     c->m_stopSingleEvents = args.value("stopSingleEvents", false);
                  if (args.contains("groupElements"))
                     c->m_groupElements = args.value("groupElements", false);
               };
               // A collection out of the table stays alive (RemoveCollection drops the members, the table keeps the object): undo and redo add it back with its members or take it out again
               const auto attach = [table](CComObject<Collection>* c, const vector<IEditable*>& members)
               {
                  return [table, c, members]()
                  {
                     if (!table->IsNameUnique(c->m_name))
                     {
                        PLOGW << "Undo could not restore the collection '" << c->m_name << "' as its name is now used";
                        return;
                     }
                     table->AddCollection(c);
                     table->SetCollectionContent(c, members);
                  };
               };
               const auto detach = [table](CComObject<Collection>* c) { return [table, c]() { table->RemoveCollection(c); }; };
               const auto presence = [table, attach, detach](CComObject<Collection>* c)
               {
                  return [table, attach, detach, c]() -> std::function<void()>
                  {
                     if (FindIndexOf(table->GetCollections(), c) == -1)
                        return detach(c);
                     return attach(c, vector<IEditable*>(c->GetParts().begin(), c->GetParts().end()));
                  };
               };
               if (action != "list" && IsLiveCopy(p))
                  return ErrorOutput(LiveCopyError());
               if (action == "list")
               {
                  json arr = json::array();
                  for (CComObject<Collection>* c : table->GetCollections())
                     arr.push_back(collJson(c));
                  return TextOutput(json { { "count", arr.size() }, { "collections", arr } });
               }
               if (action == "create")
               {
                  if (name.empty())
                     return ErrorOutput("name is required"s);
                  if (!table->IsNameUnique(name))
                     return ErrorOutput("The name '"s + name + "' is already used by a part or collection");
                  CComObject<Collection>* pcol;
                  CComObject<Collection>::CreateInstance(&pcol);
                  pcol->AddRef();
                  pcol->m_name = name;
                  applyFlags(pcol);
                  json missing = json::array();
                  for (const string& n : partNames)
                     if (IEditable* part = FindPart(p, n); part && part->GetIScriptable())
                        table->AddPartToCollection(pcol, part);
                     else
                        missing.push_back(n);
                  table->AddCollection(pcol);
                  pcol->Release();
                  RecordAction(p, table, "MCP add collection: "s + name, detach(pcol), presence(pcol));
                  json result = collJson(pcol);
                  result["notFound"] = missing;
                  return TextOutput(result);
               }
               CComObject<Collection>* coll = nullptr;
               for (CComObject<Collection>* c : table->GetCollections())
                  if (StrCompareNoCase(c->m_name, name))
                     coll = c;
               if (!coll)
                  return ErrorOutput("No collection named '"s + name + '\'');
               if (action == "add" || action == "remove")
               {
                  const auto capture = [table, coll]() -> std::function<void()> { return [table, coll, members = vector<IEditable*>(coll->GetParts().begin(), coll->GetParts().end())]() { table->SetCollectionContent(coll, members); }; };
                  const std::function<void()> undo = capture();
                  json missing = json::array();
                  for (const string& n : partNames)
                     if (IEditable* part = FindPart(p, n); part && part->GetIScriptable())
                     {
                        if (action == "add")
                           table->AddPartToCollection(coll, part);
                        else
                           table->RemovePartFromCollection(coll, part);
                     }
                     else
                        missing.push_back(n);
                  RecordAction(p, table, "MCP collection: "s + name, undo, capture);
                  json result = collJson(coll);
                  result["notFound"] = missing;
                  return TextOutput(result);
               }
               if (action == "order")
               {
                  const auto indexOf = [table](CComObject<Collection>* c) { return FindIndexOf(table->GetCollections(), c); };
                  // MoveCollectionUp and Down wrap around at the ends of the list: an order is set by moving each collection up to its place, which never wraps
                  const auto setOrder = [table, indexOf](const vector<CComObject<Collection>*>& wanted)
                  {
                     for (int i = 0; i < static_cast<int>(wanted.size()); i++)
                        for (int guard = 0; guard < 4096 && indexOf(wanted[i]) > i; guard++)
                           table->MoveCollectionUp(wanted[i]);
                  };
                  const auto capture = [table, coll, setOrder]() -> std::function<void()>
                  {
                     return [table, coll, setOrder, order = vector<CComObject<Collection>*>(table->GetCollections().begin(), table->GetCollections().end()),
                               members = vector<IEditable*>(coll->GetParts().begin(), coll->GetParts().end())]()
                     {
                        table->SetCollectionContent(coll, members);
                        setOrder(order);
                     };
                  };
                  const std::function<void()> undo = capture();
                  json missing = json::array();
                  if (!partNames.empty())
                  {
                     vector<IEditable*> ordered;
                     for (const string& n : partNames)
                        if (IEditable* part = FindPart(p, n); part && std::ranges::find(coll->GetParts(), part) != coll->GetParts().end() && std::ranges::find(ordered, part) == ordered.end())
                           ordered.push_back(part);
                        else
                           missing.push_back(n);
                     for (IEditable* part : vector<IEditable*>(coll->GetParts().begin(), coll->GetParts().end()))
                        if (std::ranges::find(ordered, part) == ordered.end())
                           ordered.push_back(part);
                     table->SetCollectionContent(coll, ordered);
                  }
                  if (args.contains("position"))
                  {
                     vector<CComObject<Collection>*> order(table->GetCollections().begin(), table->GetCollections().end());
                     std::erase(order, coll);
                     order.insert(order.begin() + std::clamp(args.value("position", 0), 0, static_cast<int>(order.size())), coll);
                     setOrder(order);
                  }
                  RecordAction(p, table, "MCP collection order: "s + name, undo, capture);
                  json result = collJson(coll);
                  result["position"] = indexOf(coll);
                  result["notFound"] = missing;
                  return TextOutput(result);
               }
               if (action == "set")
               {
                  const auto capture = [coll]() -> std::function<void()>
                  {
                     return [coll, fire = coll->m_fireEvents, stopSingle = coll->m_stopSingleEvents, group = coll->m_groupElements]()
                     {
                        coll->m_fireEvents = fire;
                        coll->m_stopSingleEvents = stopSingle;
                        coll->m_groupElements = group;
                     };
                  };
                  const std::function<void()> undo = capture();
                  applyFlags(coll);
                  RecordAction(p, table, "MCP collection: "s + name, undo, capture);
                  return TextOutput(collJson(coll));
               }
               if (action == "delete")
               {
                  const std::function<void()> undo = attach(coll, vector<IEditable*>(coll->GetParts().begin(), coll->GetParts().end()));
                  table->RemoveCollection(coll);
                  RecordAction(p, table, "MCP delete collection: "s + name, undo, presence(coll));
                  return TextOutput(json { { "deleted", name } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

   tools.push_back(ToolDef { "vpx_probe",
      "Render probes: the planes a Primitive's reflection_probe / refraction_probe fields name. 'list' them with their users; 'get' one; 'set' (name, data: {type 0 reflection | 1 refraction, normal [x, y, z], distance, roughness, reflection_mode 0 none .. 5 dynamic, no_lightmaps}, undoable); 'add' (name, data) and 'delete' (name, refused while a part uses it) change the list at once and are not undoable.",
      Schema({ { "action", Prop("string", "list | get | set | add | delete") }, { "name", Prop("string", "Probe name") }, { "data", json { { "type", "object" }, { "description", "set, add: {type, normal, distance, roughness, reflection_mode, no_lightmaps}" } } } },
         { "action" }),
      [](const json& args)
      {
         const string action = args.value("action", ""s);
         const string name = args.value("name", ""s);
         const json data = args.value("data", json::object());
         return WithPlayer([=](Player& p)
         {
            PinTable* table = p.m_ptable;
            if (action == "list")
            {
               json arr = json::array();
               for (const RenderProbe* probe : table->GetRenderProbeList())
                  arr.push_back(ProbeJson(table, probe));
               return TextOutput(json { { "count", arr.size() }, { "probes", arr },
                  { "note", "type 0 mirrors the scene on the plane normal . p = distance (the playfield reflection uses normal [0, 0, 1], distance 0); type 1 renders what lies behind a transparent part. reflection_mode: 0 none, 1 balls, 2 static, 3 static and balls, 4 static and dynamic, 5 dynamic" } });
            }
            if (action != "get" && IsLiveCopy(p))
               return ErrorOutput(LiveCopyError());
            const auto apply = [](RenderProbe* probe, const json& d, string& error)
            {
               for (const auto& [key, value] : d.items())
               {
                  const string k = NormalizeFieldName(key);
                  if (k == "type" && value.is_number())
                     probe->SetType(value.get<int>() == 1 ? RenderProbe::SCREEN_SPACE_TRANSPARENCY : RenderProbe::PLANE_REFLECTION);
                  else if (k == "normal" && value.is_array() && value.size() == 3 && value[0].is_number() && value[1].is_number() && value[2].is_number())
                     probe->SetReflectionPlaneNormal(vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()));
                  else if (k == "distance" && value.is_number())
                     probe->SetReflectionPlaneDistance(value.get<float>());
                  else if (k == "roughness" && value.is_number())
                     probe->SetRoughness(std::clamp(value.get<int>(), 0, 12));
                  else if (k == "reflectionmode" && value.is_number())
                     probe->SetReflectionMode(static_cast<RenderProbe::ReflectionMode>(std::clamp(value.get<int>(), 0, 5)));
                  else if (k == "nolightmaps" && value.is_boolean())
                     probe->SetReflectionNoLightmaps(value.get<bool>());
                  else
                  {
                     error = "'"s + key + "' is not a probe field with that kind of value: type, normal [x, y, z], distance, roughness, reflection_mode, no_lightmaps";
                     return false;
                  }
               }
               return true;
            };
            if (action == "add")
            {
               if (name.empty())
                  return ErrorOutput("name is required"s);
               if (table->GetRenderProbe(name) != nullptr || std::ranges::any_of(table->GetRenderProbeList(), [&name](const RenderProbe* rp) { return StrCompareNoCase(rp->GetName(), name); }))
                  return ErrorOutput("A render probe named '"s + name + "' already exists");
               RenderProbe scratch;
               string error;
               if (!apply(&scratch, data, error))
                  return ErrorOutput(error);
               RenderProbe* probe = table->NewRenderProbe();
               probe->SetName(name);
               apply(probe, data, error);
               probe->RenderSetup(p.m_renderer.get());
               table->SetupLookUpTables(true);
               table->SetNonUndoableDirty(SaveDirtyState::Dirty);
               p.m_renderer->MarkStaticsDirty();
               McpServer::Get().NoteEdit();
               json result = ProbeJson(table, probe);
               result["note"] = "added; point a Primitive at it with vpx_part set data: {reflection_probe: \""s + name + "\"} (or refraction_probe)";
               return TextOutput(result);
            }
            RenderProbe* probe = nullptr;
            for (RenderProbe* rp : table->GetRenderProbeList())
               if (StrCompareNoCase(rp->GetName(), name))
                  probe = rp;
            if (!probe)
               return ErrorOutput("No render probe named '"s + name + '\'');
            if (action == "get")
               return TextOutput(ProbeJson(table, probe));
            if (action == "set")
            {
               if (!data.is_object() || data.empty())
                  return ErrorOutput("data is required: {type, normal, distance, roughness, reflection_mode, no_lightmaps}"s);
               RenderProbe scratch;
               string error;
               if (!apply(&scratch, data, error))
                  return ErrorOutput(error);
               const auto capture = [probe]() -> std::function<void()>
               {
                  return [probe, type = probe->GetType(), normal = probe->GetReflectionPlaneNormal(), distance = probe->GetReflectionPlaneDistance(), roughness = probe->GetRoughness(), mode = probe->GetReflectionMode(),
                            noLightmaps = probe->GetReflectionNoLightmaps()]()
                  {
                     probe->SetType(type);
                     probe->SetReflectionPlaneNormal(normal);
                     probe->SetReflectionPlaneDistance(distance);
                     probe->SetRoughness(roughness);
                     probe->SetReflectionMode(mode);
                     probe->SetReflectionNoLightmaps(noLightmaps);
                     probe->MarkDirty();
                     if (g_pplayer)
                        g_pplayer->m_renderer->MarkStaticsDirty();
                  };
               };
               const std::function<void()> undo = capture();
               apply(probe, data, error);
               probe->MarkDirty();
               p.m_renderer->MarkStaticsDirty();
               RecordAction(p, table, "MCP probe: "s + probe->GetName(), undo, capture);
               McpServer::Get().NoteEdit();
               return TextOutput(ProbeJson(table, probe));
            }
            if (action == "delete")
            {
               const json info = ProbeJson(table, probe);
               if (!info["usedBy"].empty())
                  return ErrorOutput("'"s + probe->GetName() + "' is used by " + info["usedBy"].dump() + ": clear those fields first");
               if (probe->GetName() == RenderProbe::PLAYFIELD_REFLECTION_RENDERPROBE_NAME)
                  return ErrorOutput("The playfield reflection probe is built in and can not be deleted"s);
               table->RemoveRenderProbe(probe);
               table->SetupLookUpTables(true);
               table->SetNonUndoableDirty(SaveDirtyState::Dirty);
               p.m_renderer->m_renderDevice->AddEndOfFrameCmd([probe]()
               {
                  probe->RenderRelease();
                  delete probe;
               });
               McpServer::Get().NoteEdit();
               return TextOutput(json { { "deleted", name } });
            }
            return ErrorOutput("Unknown action"s);
         });
      } });

}

}
