// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

void AddAssetsTools(std::vector<ToolDef>& tools)
{
   tools.push_back(ToolDef { "vpx_material", "Table materials, edited directly (every stored field, named as in the VPZ packs: base_color, glossiness_color, clearcoat_color, wrap_lighting, roughness, glossy_image_lerp, thickness, edge, edge_alpha, opacity, opacity_active, type, metal...). 'list' names and types; 'get' one material's fields; 'set' merges data into one (applies live, parts look materials up by name); 'add' creates one (name, optional data, optional copyOf); 'delete' removes one. Undoable with vpx_editor undo while the editor is open.",
         Schema({ { "action", Prop("string", "list | get | set | add | delete") }, { "name", Prop("string", "Material name") }, { "data", json { { "type", "object" }, { "description", "Field values (set, add)" } } },
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
               if (action != "list" && action != "get" && IsLiveCopy(p))
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
                  RecordAction(p, table, [table, mat]()
                  {
                     table->RemoveMaterial(mat);
                     table->SetupLookUpTables(true);
                  });
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
                  return error.empty() ? TextOutput(json { { "name", name }, { "data", ToJson(doc) } }) : ErrorOutput(error);
               }
               if (action == "set")
               {
                  string error;
                  nlohmann::ordered_json doc = matDoc(mat, error);
                  if (!error.empty())
                     return ErrorOutput(error);
                  const nlohmann::ordered_json before = doc;
                  MergeDoc(doc, data);
                  doc["name"] = name;
                  JSONObjectReader reader(doc, JSONSerializer::kMaterialNode, nullptr, CURRENT_FILE_FORMAT_VERSION);
                  mat->Load(reader);
                  mat->m_name = name;
                  RecordAction(p, table, [mat, before, name]()
                  {
                     JSONObjectReader restore(before, JSONSerializer::kMaterialNode, nullptr, CURRENT_FILE_FORMAT_VERSION);
                     mat->Load(restore);
                     mat->m_name = name;
                  });
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
                  auto kept = std::make_shared<std::unique_ptr<Material>>(mat);
                  RecordAction(p, table, [table, kept]()
                  {
                     if (!*kept)
                        return;
                     table->m_materials.push_back(kept->release());
                     table->SetupLookUpTables(true);
                  });
                  return TextOutput(json { { "deleted", name } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

   tools.push_back(ToolDef { "vpx_image", "Table images (textures). 'list' names, sizes and source paths; 'get' one (optionally export:true writes it to exportPath as a PNG); 'add' imports an image file (path, optional name, replaces an image of the same name); 'set' changes alphaTest (-1 disables); 'delete' removes one. Undoable with vpx_editor undo while the editor is open.",
         Schema({ { "action", Prop("string", "list | get | add | set | delete") }, { "name", Prop("string", "Image name") }, { "path", Prop("string", "add: image file to import") },
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
               if (action != "list" && action != "get" && IsLiveCopy(p))
                  return ErrorOutput(LiveCopyError());
               if (action == "list")
               {
                  json arr = json::array();
                  for (Texture* tex : table->GetImageList())
                     arr.push_back(imageJson(tex));
                  return TextOutput(json { { "count", arr.size() }, { "images", arr } });
               }
               if (action == "add")
               {
                  if (path.empty() || !FileExists(path))
                     return ErrorOutput("Image file not found: "s + PathToUTF8(path));
                  // An image of the same name is set aside instead of being replaced in place, so that undo can bring it back
                  Texture* existing = name.empty() ? nullptr : table->GetImage(name);
                  if (existing)
                     RemoveFromVectorSingle(table->m_vimage, existing);
                  Texture* tex = table->ImportImage(path, name);
                  if (!tex)
                  {
                     if (existing)
                        table->m_vimage.push_back(existing);
                     return ErrorOutput("Could not import the image"s);
                  }
                  if (existing)
                     tex->m_alphaTestValue = existing->m_alphaTestValue;
                  table->SetupLookUpTables(true);
                  auto kept = std::make_shared<std::unique_ptr<Texture>>(existing);
                  RecordAction(p, table, [table, tex, kept]()
                  {
                     table->RemoveImage(tex);
                     if (*kept)
                        table->m_vimage.push_back(kept->release());
                     table->SetupLookUpTables(true);
                  });
                  json j = imageJson(tex);
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
               if (action == "set")
               {
                  const float before = tex->m_alphaTestValue;
                  if (args.contains("alphaTest"))
                     tex->m_alphaTestValue = args.value("alphaTest", -1.f) < 0.f ? static_cast<float>(-1.0 / 255.0) : args.value("alphaTest", 0.f);
                  RecordAction(p, table, [tex, before]() { tex->m_alphaTestValue = before; });
                  return TextOutput(imageJson(tex));
               }
               if (action == "delete")
               {
                  RemoveFromVectorSingle(table->m_vimage, tex);
                  table->SetupLookUpTables(true);
                  auto kept = std::make_shared<std::unique_ptr<Texture>>(tex);
                  RecordAction(p, table, [table, kept]()
                  {
                     if (!*kept)
                        return;
                     table->m_vimage.push_back(kept->release());
                     table->SetupLookUpTables(true);
                  });
                  return TextOutput(json { { "deleted", name } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

   tools.push_back(ToolDef { "vpx_sound", "Table sounds. 'list' names, source paths, output (0 table, 1 backglass), volume, pan, fade; 'add' imports a sound file (path); 'set' changes output, volume, pan or fade of one; 'delete' removes one. Undoable with vpx_editor undo while the editor is open.",
         Schema({ { "action", Prop("string", "list | add | set | delete") }, { "name", Prop("string", "Sound name") }, { "path", Prop("string", "add: sound file to import") },
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
               if (action != "list" && IsLiveCopy(p))
                  return ErrorOutput(LiveCopyError());
               if (action == "list")
               {
                  json arr = json::array();
                  for (VPX::Sound* snd : table->m_vsound)
                     arr.push_back(soundJson(snd));
                  return TextOutput(json { { "count", arr.size() }, { "sounds", arr } });
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
                  RecordAction(p, table, [table, snd]() { table->RemoveSound(snd); });
                  return TextOutput(soundJson(snd));
               }
               VPX::Sound* snd = table->GetSound(name);
               if (!snd)
                  return ErrorOutput("No sound named '"s + name + '\'');
               if (action == "set")
               {
                  const SoundOutTypes output = snd->GetOutputTarget();
                  const int volume = snd->GetVolume(), pan = snd->GetPan(), fade = snd->GetFrontRearFade();
                  if (args.contains("output"))
                     snd->SetOutputTarget(static_cast<SoundOutTypes>(std::clamp(args.value("output", 0), 0, 1)));
                  if (args.contains("volume"))
                     snd->SetVolume(std::clamp(args.value("volume", 0), -100, 100));
                  if (args.contains("pan"))
                     snd->SetPan(std::clamp(args.value("pan", 0), -100, 100));
                  if (args.contains("fade"))
                     snd->SetFrontRearFade(std::clamp(args.value("fade", 0), -100, 100));
                  RecordAction(p, table, [snd, output, volume, pan, fade]()
                  {
                     snd->SetOutputTarget(output);
                     snd->SetVolume(volume);
                     snd->SetPan(pan);
                     snd->SetFrontRearFade(fade);
                  });
                  return TextOutput(soundJson(snd));
               }
               if (action == "delete")
               {
                  RemoveFromVectorSingle(table->m_vsound, snd);
                  auto kept = std::make_shared<std::unique_ptr<VPX::Sound>>(snd);
                  RecordAction(p, table, [table, kept]()
                  {
                     if (*kept)
                        table->m_vsound.push_back(kept->release());
                  });
                  return TextOutput(json { { "deleted", name } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

   tools.push_back(ToolDef { "vpx_collection", "Script collections of parts (what scripts and frameworks like GLF iterate over). 'list' collections with their members and flags; 'create' one (name, parts, fireEvents, stopSingleEvents, groupElements); 'add' / 'remove' parts of one; 'set' its flags; 'delete' one. Undoable with vpx_editor undo while the editor is open.",
         Schema({ { "action", Prop("string", "list | create | add | remove | set | delete") }, { "name", Prop("string", "Collection name") },
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
                  RecordAction(p, table, [table, pcol]() { table->RemoveCollection(pcol); });
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
                  const vector<IEditable*> before(coll->GetParts().begin(), coll->GetParts().end());
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
                  RecordAction(p, table, [table, coll, before]() { table->SetCollectionContent(coll, before); });
                  json result = collJson(coll);
                  result["notFound"] = missing;
                  return TextOutput(result);
               }
               if (action == "set")
               {
                  const bool fire = coll->m_fireEvents, stopSingle = coll->m_stopSingleEvents, group = coll->m_groupElements;
                  applyFlags(coll);
                  RecordAction(p, table, [coll, fire, stopSingle, group]()
                  {
                     coll->m_fireEvents = fire;
                     coll->m_stopSingleEvents = stopSingle;
                     coll->m_groupElements = group;
                  });
                  return TextOutput(collJson(coll));
               }
               if (action == "delete")
               {
                  // RemoveCollection drops the members and keeps the object alive for the table's lifetime, so undo can add it back
                  const vector<IEditable*> before(coll->GetParts().begin(), coll->GetParts().end());
                  table->RemoveCollection(coll);
                  RecordAction(p, table, [table, coll, before]()
                  {
                     if (!table->IsNameUnique(coll->m_name))
                     {
                        PLOGW << "Undo could not restore the collection '" << coll->m_name << "' as its name is now used";
                        return;
                     }
                     table->AddCollection(coll);
                     table->SetCollectionContent(coll, before);
                  });
                  return TextOutput(json { { "deleted", name } });
               }
               return ErrorOutput("Unknown action"s);
            });
         } });

}

}
