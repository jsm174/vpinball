// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

// Direct access to the persisted state of an object: every field the table file stores, named as in the VPZ packs, no script gate
nlohmann::ordered_json ObjectDoc(int nodeKind, const std::function<void(JSONObjectWriter&)>& save, string& error)
{
   JSONObjectWriter writer(nodeKind);
   save(writer);
   if (writer.HasError())
   {
      error = "Could not serialize the object"s;
      return nullptr;
   }
   return writer.Json();
}

json ToJson(const nlohmann::ordered_json& doc) { return json::parse(doc.dump()); }

void MergeDoc(nlohmann::ordered_json& doc, const json& patch)
{
   for (const auto& [key, value] : patch.items())
   {
      if (value.is_object() && doc.contains(key) && doc[key].is_object())
      {
         nlohmann::ordered_json sub = doc[key];
         MergeDoc(sub, value);
         doc[key] = sub;
      }
      else
         doc[key] = nlohmann::ordered_json::parse(value.dump());
   }
}

string NormalizeFieldName(const string& name)
{
   string s;
   for (const char c : name)
      if (c != '_' && c != ' ' && c != '-')
         s += static_cast<char>(tolower(static_cast<unsigned char>(c)));
   return s;
}

// Reloads a part from a document in place, the way undo restores a part: name and group are applied through the table, render and physics state is refreshed
string ReloadPart(Player& p, IEditable* part, const nlohmann::ordered_json& doc)
{
   IScriptable* const scriptable = part->GetIScriptable();
   const string nameBefore = scriptable ? scriptable->m_name : string();
   part->ClearForOverwrite();
   part->m_onLoadExpectedPartGroup.clear();
   JSONObjectReader reader(doc, static_cast<int>(part->GetItemType()), nullptr, CURRENT_FILE_FORMAT_VERSION);
   part->Load(reader);
   string warning;
   if (scriptable && scriptable->m_name != nameBefore)
   {
      const string newName = scriptable->m_name;
      scriptable->m_name = nameBefore;
      if (lowerCase(newName) == lowerCase(nameBefore) || p.m_ptable->IsNameUnique(newName))
         p.m_ptable->RenamePart(part, newName);
      else
         warning = "The name '"s + newName + "' is already used, the part keeps its name";
   }
   if (!part->m_onLoadExpectedPartGroup.empty())
   {
      for (IEditable* edit : p.m_ptable->GetParts())
         if (edit->GetItemType() == eItemPartGroup && StrCompareNoCase(edit->GetName(), part->m_onLoadExpectedPartGroup))
         {
            part->SetPartGroup(static_cast<PartGroup*>(edit));
            break;
         }
   }
   else if (part->GetItemType() == eItemPartGroup)
      part->SetPartGroup(nullptr);
   RefreshPart(p, part);
   if (reader.HasError())
      warning += (warning.empty() ? ""s : "; "s) + "some fields were rejected (see the log)";
   return warning;
}

nlohmann::ordered_json PartDoc(IEditable* part, string& error)
{
   return ObjectDoc(static_cast<int>(part->GetItemType()), [part](JSONObjectWriter& w) { part->Save(w, false); }, error);
}

// Maps the caller's field names onto the stored ones: exact names pass, names that only differ by case and underscores (HeightTop -> height_top) are mapped, the rest is an error
string ResolvePartData(IEditable* part, const json& data, json& resolved, json& mapped, nlohmann::ordered_json& doc)
{
   if (!data.is_object())
      return "data must be an object of stored fields"s;
   string error;
   doc = PartDoc(part, error);
   if (!error.empty())
      return error;
   const int nodeKind = static_cast<int>(part->GetItemType());
   resolved = json::object();
   vector<string> unknown;
   for (const auto& [key, value] : data.items())
   {
      if (JSONSerializer::GetFieldId(nodeKind, key) != 0)
      {
         resolved[key] = value;
         continue;
      }
      const string norm = NormalizeFieldName(key);
      string match;
      for (const auto& [field, v] : doc.items())
         if (NormalizeFieldName(field) == norm)
         {
            match = field;
            break;
         }
      if (match.empty())
         unknown.push_back(key);
      else
      {
         resolved[match] = value;
         mapped[key] = match;
      }
   }
   if (unknown.empty())
      return ""s;
   string names, hints, example;
   for (const string& u : unknown)
   {
      names += (names.empty() ? "'"s : ", '"s) + u + '\'';
      const string norm = NormalizeFieldName(u);
      for (const auto& [field, v] : doc.items())
         if (!norm.empty() && (NormalizeFieldName(field).find(norm) != string::npos || norm.find(NormalizeFieldName(field)) != string::npos) && field != "name")
            hints += (hints.empty() ? "'"s : ", '"s) + field + '\'';
   }
   for (const char* preferred : { "height_top", "radius", "size", "length", "force", "visible" })
      if (example.empty() && doc.contains(preferred))
         example = "data: {\""s + preferred + "\": " + doc[preferred].dump() + '}';
   for (const auto& [field, v] : doc.items())
      if (example.empty() && v.is_number() && field != "name" && field.rfind("legacy", 0) != 0 && field.rfind("ui_", 0) != 0)
         example = "data: {\""s + field + "\": " + v.dump() + '}';
   return "Unknown field "s + names + " for a " + ItemTypeName(part->GetItemType()) + (hints.empty() ? ""s : ", similar stored fields: "s + hints) + ". Pass the stored field names, e.g. " + example
      + "; vpx_part schema type:" + ItemTypeName(part->GetItemType()) + " lists them all";
}

// Merges resolved stored fields into the part definition and reloads it in place
void ApplyPartData(Player& p, IEditable* part, const json& resolved, nlohmann::ordered_json& doc, json& applied, string& warning)
{
   MergeDoc(doc, resolved);
   warning = ReloadPart(p, part, doc);
   for (const auto& [key, value] : resolved.items())
      applied.push_back(key);
}

// Meaning, enum values, units and directions of the stored fields that can not be guessed from their name and value
const char* FieldMeaning(ItemTypeEnum type, const string& field)
{
   static const std::map<std::pair<ItemTypeEnum, string>, const char*> meanings = {
      { { eItemFlasher, "render_mode" }, "0 flasher (custom blended images), 1 DMD, 2 display (CRT, LCD), 3 alphanumeric segments, 4 ancillary window render (backglass, topper, score view)" },
      { { eItemFlasher, "filter_type" }, "0 none, 1 additive, 2 overlay, 3 multiply, 4 screen" },
      { { eItemFlasher, "alignment" }, "0 world, 1 top left, 2 center" },
      { { eItemFlasher, "image_src_link" }, "display source for DMD/display render modes: empty uses the default DMD fed by the script or a plugin" },
      { { eItemFlasher, "rotation_x" }, "degrees around the x axis (tilts the flasher toward or away from the player)" },
      { { eItemFlasher, "rotation_z" }, "degrees, clockwise seen from above" },
      { { eItemFlasher, "add_blend" }, "0 alpha blending, 1 additive (amplifies the background), 2 absorbing (fake glass reflection)" },
      { { eItemLight, "shape" }, "0 circle (radius), 1 custom (drag points)" },
      { { eItemLight, "state" }, "0 off, 1 on, 2 blinking (light state, can be fractional)" },
      { { eItemLight, "color" }, "0x00BBGGRR" },
      { { eItemLight, "color2" }, "0x00BBGGRR, color at the falloff edge" },
      { { eItemLight, "image_mode" }, "true: passthrough of the playfield image (insert light), false: lit color" },
      { { eItemTrigger, "shape" }, "0 none (invisible), 1 wire A, 2 star, 3 wire B, 4 button, 5 wire C, 6 wire D, 7 inder" },
      { { eItemTrigger, "rotation" }, "degrees, clockwise seen from above" },
      { { eItemRamp, "type" }, "0 flat, 1 four wires, 2 two wires, 3 three wires left, 4 three wires right, 5 one wire" },
      { { eItemRamp, "alignment" }, "image mode: 0 world, 1 wrap" },
      { { eItemRamp, "height_bottom" }, "height at the first drag point, VPX units; the height is linear along the path up to height_top at the last point" },
      { { eItemRamp, "height_top" }, "height at the last drag point, VPX units" },
      { { eItemPlunger, "type" }, "1 modern, 2 flat, 3 custom" },
      { { eItemHitTarget, "target_type" }, "1 drop target beveled, 2 drop target simple, 3 hit target round, 4 hit target rectangle, 5 hit fat target rectangle, 6 hit fat target square, 7 drop target flat simple, 8 hit fat target slim, 9 hit target slim (1, 2 and 7 are drop targets)" },
      { { eItemHitTarget, "rot_z" }, "degrees, clockwise seen from above" },
      { { eItemGate, "type" }, "1 wire W, 2 wire rectangle, 3 plate, 4 long plate" },
      { { eItemGate, "rotation" }, "degrees, clockwise seen from above" },
      { { eItemSpinner, "rotation" }, "degrees, clockwise seen from above" },
      { { eItemTextbox, "alignment" }, "0 left, 1 center, 2 right" },
      { { eItemTextbox, "back_color" }, "0x00BBGGRR" },
      { { eItemTextbox, "font_color" }, "0x00BBGGRR" },
      { { eItemDecal, "type" }, "0 text, 1 image" },
      { { eItemDecal, "sizing_type" }, "0 auto size, 1 auto width, 2 manual size" },
      { { eItemDecal, "rotation" }, "degrees, clockwise seen from above" },
      { { eItemDecal, "color" }, "0x00BBGGRR, text color" },
      { { eItemKicker, "type" }, "0 invisible, 1 hole, 2 cup, 3 simple hole, 4 Williams, 5 Gottlieb, 6 cup 2" },
      { { eItemKicker, "orientation" }, "degrees, clockwise seen from above; the script's Kick(angle, speed) uses the same convention: 0 kicks up the table, 90 to the right" },
      { { eItemKicker, "hit_accuracy" }, "0..1: the ball is grabbed when lower than this fraction of the capture height, so higher values grab more easily" },
      { { eItemKicker, "hit_height" }, "VPX units, height of the capture volume" },
      { { eItemFlipper, "start_angle" }, "rest angle, degrees clockwise seen from above, 0 points up the table: a left flipper rests near 120, a right one near -120" },
      { { eItemFlipper, "end_angle" }, "raised angle, same convention as start_angle" },
      { { eItemFlipper, "flipper_radius_max" }, "flipper length, VPX units" },
      { { eItemBumper, "orientation" }, "degrees, clockwise seen from above" },
      { { eItemPrimitive, "prim_rot1_x" }, "script RotX, degrees" },
      { { eItemPrimitive, "prim_rot1_y" }, "script RotY, degrees" },
      { { eItemPrimitive, "prim_rot1_z" }, "script RotZ, degrees clockwise seen from above (the one to turn a part on the playfield)" },
      { { eItemPrimitive, "prim_rot2_x" }, "script ObjRotX, applied after RotX/Y/Z" },
      { { eItemPrimitive, "prim_rot2_y" }, "script ObjRotY, applied after RotX/Y/Z" },
      { { eItemPrimitive, "prim_rot2_z" }, "script ObjRotZ, applied after RotX/Y/Z" },
      { { eItemPrimitive, "prim_trans_x" }, "script TransX, offset applied before RotX/Y/Z" },
      { { eItemPrimitive, "sides" }, "built-in shape side count, 3..100 (ignored when use_mesh)" },
      { { eItemPrimitive, "size" }, "scale of the mesh per axis (VPX units per mesh unit; 1 for meshes from vpx_part mesh)" },
      { { eItemPrimitive, "color" }, "0x00BBGGRR" },
      { { eItemPrimitive, "side_color" }, "0x00BBGGRR" },
      { { eItemSurface, "is_bottom_solid" }, "the bottom face collides too: a wall above the playfield then acts as a ceiling" },
      { { eItemSurface, "height_bottom" }, "VPX units above the playfield" },
      { { eItemSurface, "height_top" }, "VPX units above the playfield" },
      { { eItemRubber, "rot_z" }, "degrees, clockwise seen from above" },
   };
   const auto it = meanings.find({ type, field });
   return it == meanings.end() ? nullptr : it->second;
}

const vector<TableField>& TableFields()
{
   static const vector<TableField> fields = {
      { "image", "string", "playfield image name", [](const PinTable* t) { return json(t->m_image); }, [](PinTable* t, const json& v) { t->m_image = v.get<string>(); } },
      { "material", "string", "playfield material name", [](const PinTable* t) { return json(t->m_playfieldMaterial); }, [](PinTable* t, const json& v) { t->m_playfieldMaterial = v.get<string>(); } },
      { "reflection_strength", "integer", "playfield reflection strength 0..255", [](const PinTable* t) { return json(t->GetPlayfieldReflectionStrength()); }, [](PinTable* t, const json& v) { t->SetPlayfieldReflectionStrength(v.get<int>()); } },
      { "ball_image", "string", "default ball image", [](const PinTable* t) { return json(t->m_ballImage); }, [](PinTable* t, const json& v) { t->m_ballImage = v.get<string>(); } },
      { "ball_spherical_mapping", "boolean", "legacy 2D ball mapping (looks bad in VR and stereo)", [](const PinTable* t) { return json(t->m_ballSphericalMapping); }, [](PinTable* t, const json& v) { t->m_ballSphericalMapping = v.get<bool>(); } },
      { "ball_decal", "string", "ball decal image", [](const PinTable* t) { return json(t->m_ballImageDecal); }, [](PinTable* t, const json& v) { t->m_ballImageDecal = v.get<string>(); } },
      { "ball_decal_logo_mode", "boolean", "decal drawn as a logo instead of scratches", [](const PinTable* t) { return json(t->m_BallDecalMode); }, [](PinTable* t, const json& v) { t->m_BallDecalMode = v.get<bool>(); } },
      { "ball_playfield_reflection", "number", "strength of the playfield reflected on the ball", [](const PinTable* t) { return json(t->m_ballPlayfieldReflectionStrength); }, [](PinTable* t, const json& v) { t->m_ballPlayfieldReflectionStrength = v.get<float>(); } },
      { "bulb_intensity_scale_on_ball", "number", "default bulb intensity scale on the ball", [](const PinTable* t) { return json(t->m_defaultBulbIntensityScaleOnBall); }, [](PinTable* t, const json& v) { t->m_defaultBulbIntensityScaleOnBall = v.get<float>(); } },
      { "backdrop_color", "color", "desktop backdrop color", [](const PinTable* t) { return json(static_cast<uint32_t>(t->m_colorbackdrop)); }, [](PinTable* t, const json& v) { t->m_colorbackdrop = v.get<uint32_t>(); } },
      { "backdrop_image_desktop", "string", "desktop backdrop image", [](const PinTable* t) { return json(t->m_BG_image[0]); }, [](PinTable* t, const json& v) { t->m_BG_image[0] = v.get<string>(); } },
      { "backdrop_image_fullscreen", "string", "fullscreen backdrop image", [](const PinTable* t) { return json(t->m_BG_image[1]); }, [](PinTable* t, const json& v) { t->m_BG_image[1] = v.get<string>(); } },
      { "backdrop_image_fss", "string", "full single screen backdrop image", [](const PinTable* t) { return json(t->m_BG_image[2]); }, [](PinTable* t, const json& v) { t->m_BG_image[2] = v.get<string>(); } },
      { "backdrop_night_day", "boolean", "apply the night/day cycle to the backdrop", [](const PinTable* t) { return json(t->m_ImageBackdropNightDay); }, [](PinTable* t, const json& v) { t->m_ImageBackdropNightDay = v.get<bool>(); } },
      { "render_em_reels", "boolean", "render EM reels", [](const PinTable* t) { return json(t->m_renderEMReels); }, [](PinTable* t, const json& v) { t->m_renderEMReels = v.get<bool>(); } },
      { "render_decals", "boolean", "render decals", [](const PinTable* t) { return json(t->m_renderDecals); }, [](PinTable* t, const json& v) { t->m_renderDecals = v.get<bool>(); } },
      { "gravity", "number", "gravity constant (0.97 is about earth)", [](const PinTable* t) { return json(t->GetGravity()); }, [](PinTable* t, const json& v) { t->SetGravity(v.get<float>()); } },
      { "elasticity", "number", "playfield elasticity", [](const PinTable* t) { return json(t->m_elasticity); }, [](PinTable* t, const json& v) { t->m_elasticity = v.get<float>(); } },
      { "elasticity_falloff", "number", "playfield elasticity falloff", [](const PinTable* t) { return json(t->m_elasticityFalloff); }, [](PinTable* t, const json& v) { t->m_elasticityFalloff = v.get<float>(); } },
      { "friction", "number", "playfield friction (>= 0)", [](const PinTable* t) { return json(t->m_friction); }, [](PinTable* t, const json& v) { t->m_friction = max(v.get<float>(), 0.f); } },
      { "scatter", "number", "playfield scatter angle, degrees", [](const PinTable* t) { return json(t->m_scatter); }, [](PinTable* t, const json& v) { t->m_scatter = v.get<float>(); } },
      { "default_scatter", "number", "default parts scatter angle, degrees", [](const PinTable* t) { return json(t->m_defaultScatter); }, [](PinTable* t, const json& v) { t->m_defaultScatter = v.get<float>(); } },
      { "override_physics", "integer", "global physics override set (0 none)", [](const PinTable* t) { return json(t->m_overridePhysics); }, [](PinTable* t, const json& v) { t->m_overridePhysics = v.get<int>(); } },
      { "override_physics_flipper", "boolean", "override also applies to flippers", [](const PinTable* t) { return json(t->m_overridePhysicsFlipper); }, [](PinTable* t, const json& v) { t->m_overridePhysicsFlipper = v.get<bool>(); } },
      { "nudge_time", "number", "nudge time", [](const PinTable* t) { return json(t->m_nudgeTime); }, [](PinTable* t, const json& v) { t->m_nudgeTime = v.get<float>(); } },
      { "physics_max_loops", "integer", "physics max loops (-1 default)", [](const PinTable* t) { return json(static_cast<int>(t->m_PhysicsMaxLoops)); }, [](PinTable* t, const json& v) { t->m_PhysicsMaxLoops = static_cast<unsigned int>(v.get<int>()); } },
      { "glass_top_height", "number", "glass height at the top of the table, VPX units", [](const PinTable* t) { return json(t->m_glassTopHeight); }, [](PinTable* t, const json& v) { t->m_glassTopHeight = v.get<float>(); } },
      { "glass_bottom_height", "number", "glass height at the bottom of the table, VPX units", [](const PinTable* t) { return json(t->m_glassBottomHeight); }, [](PinTable* t, const json& v) { t->m_glassBottomHeight = v.get<float>(); } },
      { "lockbar_height", "number", "ground to lockbar height, VPX units", [](const PinTable* t) { return json(t->m_groundToLockbarHeight); }, [](PinTable* t, const json& v) { t->m_groundToLockbarHeight = v.get<float>(); } },
      { "slope_min", "number", "playfield slope at minimum difficulty, degrees", [](const PinTable* t) { return json(t->m_angletiltMin); }, [](PinTable* t, const json& v) { t->m_angletiltMin = v.get<float>(); } },
      { "slope_max", "number", "playfield slope at maximum difficulty, degrees", [](const PinTable* t) { return json(t->m_angletiltMax); }, [](PinTable* t, const json& v) { t->m_angletiltMax = v.get<float>(); } },
      { "difficulty", "number", "game difficulty 0..1 (picks the slope between min and max)", [](const PinTable* t) { return json(t->m_difficulty); }, [](PinTable* t, const json& v) { t->m_difficulty = v.get<float>(); } },
      { "sound_volume", "integer", "table sound effect volume", [](const PinTable* t) { return json(t->GetTableSoundVolume()); }, [](PinTable* t, const json& v) { t->SetTableSoundVolume(v.get<int>()); } },
      { "music_volume", "integer", "table music volume", [](const PinTable* t) { return json(t->GetTableMusicVolume()); }, [](PinTable* t, const json& v) { t->SetTableMusicVolume(v.get<int>()); } },
      { "ambient_color", "color", "ambient light color", [](const PinTable* t) { return json(static_cast<uint32_t>(t->m_lightAmbient)); }, [](PinTable* t, const json& v) { t->m_lightAmbient = v.get<uint32_t>(); }, true },
      { "light_color", "color", "main light emission color", [](const PinTable* t) { return json(static_cast<uint32_t>(t->m_Light[0].emission)); }, [](PinTable* t, const json& v) { t->m_Light[0].emission = v.get<uint32_t>(); }, true },
      { "light_emission_scale", "number", "main light emission scale", [](const PinTable* t) { return json(t->m_lightEmissionScale); }, [](PinTable* t, const json& v) { t->m_lightEmissionScale = v.get<float>(); }, true },
      { "light_height", "number", "main light height, VPX units", [](const PinTable* t) { return json(t->m_lightHeight); }, [](PinTable* t, const json& v) { t->m_lightHeight = v.get<float>(); }, true },
      { "light_range", "number", "main light range, VPX units", [](const PinTable* t) { return json(t->m_lightRange); }, [](PinTable* t, const json& v) { t->m_lightRange = v.get<float>(); }, true },
      { "scene_lighting_scale", "number", "global emission scale (night/day), 0..1", [](const PinTable* t) { return json(t->m_globalEmissionScale); }, [](PinTable* t, const json& v) { t->m_globalEmissionScale = v.get<float>(); }, true },
      { "env_image", "string", "environment image", [](const PinTable* t) { return json(t->m_envImage); }, [](PinTable* t, const json& v) { t->m_envImage = v.get<string>(); }, true },
      { "env_emission_scale", "number", "environment emission scale", [](const PinTable* t) { return json(t->m_envEmissionScale); }, [](PinTable* t, const json& v) { t->m_envEmissionScale = v.get<float>(); } },
      { "ao_enabled", "boolean", "ambient occlusion", [](const PinTable* t) { return json(t->m_enableAO); }, [](PinTable* t, const json& v) { t->m_enableAO = v.get<bool>(); }, true },
      { "ao_scale", "number", "ambient occlusion scale", [](const PinTable* t) { return json(t->m_AOScale); }, [](PinTable* t, const json& v) { t->m_AOScale = v.get<float>(); } },
      { "ssr_enabled", "boolean", "screen space reflections", [](const PinTable* t) { return json(t->m_enableSSR); }, [](PinTable* t, const json& v) { t->m_enableSSR = v.get<bool>(); }, true },
      { "ssr_scale", "number", "screen space reflection scale", [](const PinTable* t) { return json(t->m_SSRScale); }, [](PinTable* t, const json& v) { t->m_SSRScale = v.get<float>(); } },
      { "bloom_strength", "number", "bloom strength", [](const PinTable* t) { return json(t->m_bloom_strength); }, [](PinTable* t, const json& v) { t->m_bloom_strength = v.get<float>(); } },
      { "tonemapper", "integer", "0 Reinhard, 1 AgX, 2 Filmic, 3 Neutral", [](const PinTable* t) { return json(static_cast<int>(t->GetToneMapper())); }, [](PinTable* t, const json& v) { t->SetToneMapper(static_cast<ToneMapper>(v.get<int>())); }, true },
      { "exposure", "number", "exposure", [](const PinTable* t) { return json(t->GetExposure()); }, [](PinTable* t, const json& v) { t->SetExposure(v.get<float>()); }, true },
   };
   return fields;
}

}
