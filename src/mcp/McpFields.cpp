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
string ResolvePartData(IEditable* part, const json& data, json& resolved, json& mapped, nlohmann::ordered_json& doc, json& valueNotes)
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
      return CheckFieldValues(part->GetItemType(), resolved, valueNotes);
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
      { { eItemFlasher, "render_style" }, "depends on render_mode: DMD 0 legacy VPX, 1 neon plasma, 2 red LED, 3 green LED, 4 yellow LED, 5 generic plasma, 6 generic LED; display 0 pixelated, 1 smoothed, 2 CRT; alphanumeric segments family * 8 + style (families 0 generic, 1 Gottlieb, 2 Williams, 3 Bally, 4 Atari; styles 0 neon plasma, 1 blue VFD, 2 green VFD, 3 red LED, 4 green LED, 5 yellow LED, 6 generic plasma, 7 generic LED); ancillary window render 1 backglass, 2 score view, 3 topper" },
      { { eItemFlasher, "image_src_link" }, "display source for DMD/display render modes: empty uses the default DMD fed by the script or a plugin" },
      { { eItemFlasher, "rotation_x" }, "degrees around the x axis (tilts the flasher toward or away from the player)" },
      { { eItemFlasher, "rotation_z" }, "degrees, clockwise seen from above" },
      { { eItemLight, "shape" }, "not stored: any value only gives a light without drag points a circle of drag points from its radius" },
      { { eItemLight, "state" }, "0 off, 1 on, 2 blinking; fractional values between 0 and 1 are dimmed states" },
      { { eItemLight, "state_legacy" }, "legacy integer state, overridden by state when both are stored: edit state" },
      { { eItemLight, "color" }, "0x00BBGGRR" },
      { { eItemLight, "color2" }, "0x00BBGGRR, color at the falloff edge" },
      { { eItemLight, "image_mode" }, "true: passthrough of the playfield image (insert light), false: lit color" },
      { { eItemTrigger, "rotation" }, "degrees, clockwise seen from above" },
      { { eItemRamp, "height_bottom" }, "height at the first drag point, VPX units; the height is linear along the path up to height_top at the last point" },
      { { eItemRamp, "height_top" }, "height at the last drag point, VPX units" },
      { { eItemHitTarget, "target_type" }, "1, 2 and 7 are drop targets, the others hit targets" },
      { { eItemHitTarget, "rot_z" }, "degrees, clockwise seen from above" },
      { { eItemGate, "rotation" }, "degrees, clockwise seen from above" },
      { { eItemGate, "angle_min" }, "RADIANS (unlike the spinner): swing limit, the live editor shows the raw value" },
      { { eItemGate, "angle_max" }, "RADIANS (unlike the spinner), default pi/2: swing limit, the live editor shows the raw value" },
      { { eItemSpinner, "rotation" }, "degrees, clockwise seen from above" },
      { { eItemSpinner, "angle_min" }, "degrees; equal to angle_max means the spinner turns freely" },
      { { eItemSpinner, "angle_max" }, "degrees; equal to angle_min means the spinner turns freely" },
      { { eItemTextbox, "back_color" }, "0x00BBGGRR" },
      { { eItemTextbox, "font_color" }, "0x00BBGGRR" },
      { { eItemDecal, "rotation" }, "degrees, clockwise seen from above" },
      { { eItemDecal, "color" }, "0x00BBGGRR, text color" },
      { { eItemKicker, "orientation" }, "degrees, clockwise seen from above; the script's Kick(angle, speed) uses the same convention: 0 kicks up the table, 90 to the right" },
      { { eItemKicker, "hit_accuracy" }, "0..1: the ball is grabbed when lower than this fraction of (the kicker base height + the ball radius), so higher values grab more easily; ignored in legacy_mode" },
      { { eItemKicker, "hit_height" }, "VPX units, height of the capture volume" },
      { { eItemFlipper, "start_angle" }, "rest angle, degrees clockwise seen from above, 0 points up the table: a left flipper rests near 120, a right one near -120" },
      { { eItemFlipper, "end_angle" }, "raised angle, same convention as start_angle" },
      { { eItemFlipper, "flipper_radius_max" }, "flipper length, VPX units" },
      { { eItemFlipper, "rubber_height_legacy" }, "legacy integer, ignored when rubber_height is stored: edit rubber_height" },
      { { eItemFlipper, "rubber_thickness_legacy" }, "legacy integer, ignored when rubber_thickness is stored: edit rubber_thickness" },
      { { eItemFlipper, "rubber_width_legacy" }, "legacy integer, ignored when rubber_width is stored: edit rubber_width" },
      { { eItemBumper, "orientation" }, "degrees, clockwise seen from above" },
      { { eItemPrimitive, "prim_rot1_x" }, "script RotX, degrees" },
      { { eItemPrimitive, "prim_rot1_y" }, "script RotY, degrees" },
      { { eItemPrimitive, "prim_rot1_z" }, "script RotZ, degrees clockwise seen from above (the one to turn a part on the playfield)" },
      { { eItemPrimitive, "prim_rot2_x" }, "script ObjRotX, applied after RotX/Y/Z" },
      { { eItemPrimitive, "prim_rot2_y" }, "script ObjRotY, applied after RotX/Y/Z" },
      { { eItemPrimitive, "prim_rot2_z" }, "script ObjRotZ, applied after RotX/Y/Z" },
      { { eItemPrimitive, "prim_trans_x" }, "script TransX, offset applied before RotX/Y/Z" },
      { { eItemPrimitive, "sides" }, "built-in shape side count (ignored when use_mesh)" },
      { { eItemPrimitive, "size" }, "scale of the mesh per axis (VPX units per mesh unit; 1 for meshes from vpx_part mesh)" },
      { { eItemPrimitive, "color" }, "0x00BBGGRR" },
      { { eItemPrimitive, "side_color" }, "0x00BBGGRR" },
      { { eItemSurface, "is_bottom_solid" }, "the bottom face collides too: a wall above the playfield then acts as a ceiling" },
      { { eItemSurface, "height_bottom" }, "VPX units above the playfield" },
      { { eItemSurface, "height_top" }, "VPX units above the playfield" },
      { { eItemSurface, "slingshot_force" }, "10 times the script's SlingshotStrength" },
      { { eItemRubber, "rot_z" }, "degrees, clockwise seen from above" },
   };
   const auto it = meanings.find({ type, field });
   return it == meanings.end() ? nullptr : it->second;
}

// Ranges of the numeric fields. enforced: VPX clamps the value (when loading it, using it, or in the editor and script setters), and
// MCP writes clamp it the same way; otherwise the range is the usual authoring range and a value outside it only gets a note.
// An item type of eItemInvalid applies to every part type.
const FieldRange* FieldRangeOf(ItemTypeEnum type, const string& field)
{
   static const std::map<std::pair<ItemTypeEnum, string>, FieldRange> ranges = {
      { { eItemInvalid, "timer_interval" }, { -2.f, FLT_MAX, true, "ms; -1 fires every frame, -2 every game sync, positive values are at least 1" } },
      { { eItemFlipper, "elasticity" }, { 0.f, 1.f, false, "coefficient of restitution" } },
      { { eItemFlipper, "elasticity_falloff" }, { 0.f, FLT_MAX, false, "elasticity loss per impact speed, 0 disables it" } },
      { { eItemFlipper, "friction" }, { 0.f, FLT_MAX, true, "can not be negative, may exceed 1" } },
      { { eItemFlipper, "mass" }, { 0.f, FLT_MAX, false, "flipper mass" } },
      { { eItemFlipper, "strength" }, { 0.f, FLT_MAX, false, "coil torque" } },
      { { eItemFlipper, "return" }, { 0.f, 1.f, false, "release torque as a fraction of strength" } },
      { { eItemFlipper, "ramp_up" }, { 0.f, FLT_MAX, false, "coil ramp up, 0 is instant full torque" } },
      { { eItemFlipper, "torque_damping" }, { 0.f, 1.f, false, "torque multiplier of the hold coil at the end of stroke" } },
      { { eItemFlipper, "torque_damping_angle" }, { 0.f, FLT_MAX, false, "degrees from the end angle where torque_damping fades in" } },
      { { eItemFlipper, "base_radius" }, { 0.01f, FLT_MAX, true, "VPX units" } },
      { { eItemFlipper, "end_radius" }, { 0.01f, FLT_MAX, true, "VPX units" } },
      { { eItemFlipper, "flipper_radius_max" }, { 0.01f, FLT_MAX, true, "flipper length at the lowest difficulty, VPX units" } },
      { { eItemFlipper, "flipper_radius_min" }, { 0.f, FLT_MAX, false, "length at the highest difficulty, VPX units; only used when above 0 and below flipper_radius_max" } },
      { { eItemFlipper, "rubber_height" }, { 0.f, 1000.f, true, "VPX units" } },
      { { eItemBumper, "threshold" }, { 0.f, FLT_MAX, false, "lowest impact speed that fires Hit" } },
      { { eItemSurface, "elasticity" }, { 0.f, 1.f, false, "coefficient of restitution, used when overwrite_physics" } },
      { { eItemSurface, "elasticity_falloff" }, { 0.f, FLT_MAX, false, "0 disables it, used when overwrite_physics" } },
      { { eItemSurface, "friction" }, { 0.f, FLT_MAX, true, "can not be negative, may exceed 1; used when overwrite_physics" } },
      { { eItemSurface, "threshold" }, { 0.f, FLT_MAX, false, "lowest impact speed that fires Hit" } },
      { { eItemSurface, "slingshot_threshold" }, { 0.f, FLT_MAX, false, "lowest impact speed that fires the slingshot" } },
      { { eItemSurface, "disable_lighting_top" }, { 0.f, 1.f, false, "0 lit .. 1 unlit (emissive)" } },
      { { eItemSurface, "disable_lighting_below" }, { 0.f, 1.f, false, "0 lets light from below through .. 1 blocks it" } },
      { { eItemRamp, "elasticity" }, { 0.f, 1.f, false, "coefficient of restitution, used when overwrite_physics" } },
      { { eItemRamp, "friction" }, { 0.f, FLT_MAX, true, "can not be negative, may exceed 1; used when overwrite_physics" } },
      { { eItemRamp, "threshold" }, { 0.f, FLT_MAX, false, "lowest impact speed that fires Hit" } },
      { { eItemRamp, "left_wall_height" }, { 0.f, FLT_MAX, true, "colliding wall, VPX units" } },
      { { eItemRamp, "right_wall_height" }, { 0.f, FLT_MAX, true, "colliding wall, VPX units" } },
      { { eItemRamp, "left_wall_height_visible" }, { 0.f, FLT_MAX, true, "VPX units" } },
      { { eItemRamp, "right_wall_height_visible" }, { 0.f, FLT_MAX, true, "VPX units" } },
      { { eItemRubber, "elasticity" }, { 0.f, 1.f, false, "coefficient of restitution, used when overwrite_physics" } },
      { { eItemRubber, "elasticity_falloff" }, { 0.f, FLT_MAX, false, "0 disables it, used when overwrite_physics" } },
      { { eItemRubber, "friction" }, { 0.f, FLT_MAX, true, "can not be negative, may exceed 1; used when overwrite_physics" } },
      { { eItemKicker, "hit_accuracy" }, { 0.f, 1.f, true, "grab height factor" } },
      { { eItemGate, "elasticity" }, { 0.f, 1.f, false, "coefficient of restitution" } },
      { { eItemGate, "friction" }, { 0.f, FLT_MAX, true, "can not be negative" } },
      { { eItemGate, "damping" }, { 0.f, 1.f, true, "swing speed kept per physics step, 1 is no damping" } },
      { { eItemGate, "gravity_factor" }, { 0.f, 100.f, true, "" } },
      { { eItemSpinner, "elasticity" }, { 0.f, 1.f, false, "coefficient of restitution" } },
      { { eItemSpinner, "damping" }, { 0.f, 1.f, true, "spin speed kept per physics step, 1 is no damping" } },
      { { eItemPlunger, "stroke" }, { 16.5f, FLT_MAX, true, "VPX units" } },
      { { eItemPlunger, "park_position" }, { 0.f, 1.f, false, "rest position as a fraction of the stroke" } },
      { { eItemPlunger, "anim_frames" }, { 1.f, FLT_MAX, true, "cells of the flat plunger image" } },
      { { eItemHitTarget, "elasticity" }, { 0.f, 1.f, false, "coefficient of restitution, used when overwrite_physics" } },
      { { eItemHitTarget, "elasticity_falloff" }, { 0.f, FLT_MAX, false, "0 disables it, used when overwrite_physics" } },
      { { eItemHitTarget, "friction" }, { 0.f, FLT_MAX, true, "can not be negative, may exceed 1; used when overwrite_physics" } },
      { { eItemHitTarget, "threshold" }, { 0.f, FLT_MAX, false, "lowest impact speed that fires Hit" } },
      { { eItemHitTarget, "drop_speed" }, { 0.f, FLT_MAX, false, "drop and raise animation speed, VPX units per ms" } },
      { { eItemHitTarget, "raise_delay" }, { 0.f, FLT_MAX, false, "ms before a drop target raises" } },
      { { eItemHitTarget, "disable_lighting_top" }, { 0.f, 1.f, false, "0 lit .. 1 unlit (emissive)" } },
      { { eItemHitTarget, "disable_lighting_below" }, { 0.f, 1.f, false, "0 lets light from below through .. 1 blocks it" } },
      { { eItemLight, "intensity" }, { 0.f, FLT_MAX, true, "" } },
      { { eItemLight, "radius" }, { 0.f, FLT_MAX, true, "falloff range, VPX units" } },
      { { eItemLight, "fade_speed_up" }, { 0.f, FLT_MAX, false, "intensity per ms: intensity / fade up time in ms" } },
      { { eItemLight, "fade_speed_down" }, { 0.f, FLT_MAX, false, "intensity per ms: intensity / fade down time in ms" } },
      { { eItemLight, "modulate_vs_add" }, { 0.f, 1.f, true, "bulb light modulate" } },
      { { eItemLight, "transmission_scale" }, { 0.f, 1.f, false, "fraction of the bulb light transmitted" } },
      { { eItemFlasher, "alpha" }, { 0.f, 100.f, false, "opacity percent" } },
      { { eItemFlasher, "modulate_vs_add" }, { 0.f, 1.f, true, "" } },
      { { eItemFlasher, "filter_amount" }, { 0.f, 100.f, false, "image A/B mix percent" } },
      { { eItemFlasher, "render_style" }, { 0.f, 39.f, true, "see the meaning: the valid values depend on render_mode" } },
      { { eItemFlasher, "glass_pad_left" }, { 0.f, 1.f, false, "fraction of the flasher width" } },
      { { eItemFlasher, "glass_pad_right" }, { 0.f, 1.f, false, "fraction of the flasher width" } },
      { { eItemFlasher, "glass_pad_top" }, { 0.f, 1.f, false, "fraction of the flasher height" } },
      { { eItemFlasher, "glass_pad_bottom" }, { 0.f, 1.f, false, "fraction of the flasher height" } },
      { { eItemPrimitive, "sides" }, { 3.f, 100.f, true, "" } },
      { { eItemPrimitive, "alpha" }, { 0.f, 100.f, false, "opacity percent, 100 is opaque" } },
      { { eItemPrimitive, "elasticity" }, { 0.f, 1.f, false, "coefficient of restitution, used when overwrite_physics" } },
      { { eItemPrimitive, "elasticity_falloff" }, { 0.f, FLT_MAX, false, "0 disables it, used when overwrite_physics" } },
      { { eItemPrimitive, "friction" }, { 0.f, FLT_MAX, true, "can not be negative, may exceed 1; used when overwrite_physics" } },
      { { eItemPrimitive, "threshold" }, { 0.f, FLT_MAX, false, "lowest impact speed that fires Hit" } },
      { { eItemPrimitive, "collision_reduction_factor" }, { 0.f, 1.f, true, "0 collides with the full mesh .. 1 most reduced" } },
      { { eItemPrimitive, "ui_edge_factor" }, { 0.f, 1.f, false, "editor wireframe edge filter" } },
      { { eItemPrimitive, "disable_lighting_top" }, { 0.f, 1.f, false, "0 lit .. 1 unlit (emissive)" } },
      { { eItemPrimitive, "disable_lighting_below" }, { 0.f, 1.f, false, "0 lets light from below through .. 1 blocks it" } },
      { { eItemLightSeq, "update_interval" }, { 1.f, FLT_MAX, true, "ms" } },
      { { eItemDispReel, "reel_count" }, { 1.f, 32.f, true, "" } },
      { { eItemDispReel, "digit_range" }, { 0.f, 511.f, true, "highest digit" } },
      { { eItemDispReel, "motor_steps" }, { 1.f, FLT_MAX, true, "" } },
      { { eItemDispReel, "update_interval" }, { 5.f, FLT_MAX, true, "ms" } },
      { { eItemDispReel, "images_per_grid_row" }, { 1.f, FLT_MAX, true, "" } },
      { { eItemDispReel, "width" }, { 0.f, FLT_MAX, true, "reel width" } },
      { { eItemDispReel, "height" }, { 0.f, FLT_MAX, true, "reel height" } },
      { { eItemDispReel, "reel_spacing" }, { 0.f, FLT_MAX, true, "" } },
   };
   if (const auto it = ranges.find({ type, field }); it != ranges.end())
      return &it->second;
   const auto it = ranges.find({ eItemInvalid, field });
   return it == ranges.end() ? nullptr : &it->second;
}

// Integer coded fields. strict: VPX only knows these values (most are not checked when loading, so MCP refuses the others)
const FieldEnum* FieldEnumOf(ItemTypeEnum type, const string& field)
{
   static const std::map<std::pair<ItemTypeEnum, string>, FieldEnum> enums = {
      { { eItemFlipper, "override_physics" }, { { { 0, "disabled" }, { 1, "set 1" }, { 2, "set 2" }, { 3, "set 3" }, { 4, "set 4" }, { 5, "set 5" }, { 6, "set 6" }, { 7, "set 7" }, { 8, "set 8" } } } },
      { { eItemLight, "state" }, { { { 0, "off" }, { 1, "on" }, { 2, "blinking" } }, false } },
      { { eItemLight, "fader" }, { { { 0, "none (LED)" }, { 1, "linear" }, { 2, "incandescent" } } } },
      { { eItemLight, "shadows" }, { { { 0, "none" }, { 1, "raytraced ball shadows" } } } },
      { { eItemFlasher, "render_mode" }, { { { 0, "flasher (custom blended images)" }, { 1, "DMD" }, { 2, "display (CRT, LCD)" }, { 3, "alphanumeric segments" }, { 4, "ancillary window render (backglass, topper, score view)" } } } },
      { { eItemFlasher, "filter_type" }, { { { 0, "none" }, { 1, "additive" }, { 2, "overlay" }, { 3, "multiply" }, { 4, "screen" } } } },
      { { eItemFlasher, "alignment" }, { { { 0, "world" }, { 1, "wrap" } } } },
      { { eItemFlasher, "add_blend" }, { { { 0, "alpha blending" }, { 1, "additive (amplifies the background)" }, { 2, "absorbing (fake glass reflection)" } } } },
      { { eItemTrigger, "shape" }, { { { 0, "none (invisible)" }, { 1, "wire A" }, { 2, "star" }, { 3, "wire B" }, { 4, "button" }, { 5, "wire C" }, { 6, "wire D" }, { 7, "inder" } } } },
      { { eItemRamp, "type" }, { { { 0, "flat" }, { 1, "four wires" }, { 2, "two wires" }, { 3, "three wires left" }, { 4, "three wires right" }, { 5, "one wire" } } } },
      { { eItemRamp, "alignment" }, { { { 0, "world" }, { 1, "wrap" } } } },
      { { eItemPlunger, "type" }, { { { 1, "modern" }, { 2, "flat" }, { 3, "custom" } } } },
      { { eItemHitTarget, "target_type" }, { { { 1, "drop target beveled" }, { 2, "drop target simple" }, { 3, "hit target round" }, { 4, "hit target rectangle" }, { 5, "hit fat target rectangle" }, { 6, "hit fat target square" }, { 7, "drop target flat simple" }, { 8, "hit fat target slim" }, { 9, "hit target slim" } } } },
      { { eItemGate, "type" }, { { { 1, "wire W" }, { 2, "wire rectangle" }, { 3, "plate" }, { 4, "long plate" } } } },
      { { eItemTextbox, "alignment" }, { { { 0, "left" }, { 1, "center" }, { 2, "right" } } } },
      { { eItemDecal, "type" }, { { { 0, "text" }, { 1, "image" } } } },
      { { eItemDecal, "sizing_type" }, { { { 0, "auto size" }, { 1, "auto width" }, { 2, "manual size" } } } },
      { { eItemKicker, "type" }, { { { 0, "invisible" }, { 1, "hole" }, { 2, "cup" }, { 3, "simple hole" }, { 4, "Williams" }, { 5, "Gottlieb" }, { 6, "cup 2" } } } },
   };
   const auto it = enums.find({ type, field });
   return it == enums.end() ? nullptr : &it->second;
}

string CheckFieldValues(ItemTypeEnum type, json& resolved, json& notes)
{
   const auto number = [](float v) { return v == FLT_MAX ? "no limit"s : std::format("{}", Round(v, 4)); };
   for (auto& [key, value] : resolved.items())
   {
      if (const FieldEnum* e = FieldEnumOf(type, key); e && e->strict)
      {
         string values;
         for (const auto& [v, label] : e->values)
            values += (values.empty() ? ""s : ", "s) + std::to_string(v) + ' ' + label;
         const bool known = value.is_number() && value.get<double>() == std::floor(value.get<double>())
            && std::ranges::any_of(e->values, [&value](const auto& entry) { return entry.first == value.get<int>(); });
         if (!known)
            return "Field '"s + key + "' of a " + ItemTypeName(type) + " takes " + values + ", got " + value.dump();
      }
      const FieldRange* r = FieldRangeOf(type, key);
      if (!r || !value.is_number())
         continue;
      const float v = value.get<float>();
      if (v >= r->min && v <= r->max)
         continue;
      const string range = number(r->min) + ".." + number(r->max) + (r->note[0] ? " ("s + r->note + ')' : ""s);
      if (r->enforced)
      {
         value = std::clamp(v, r->min, r->max);
         notes[key] = "clamped to "s + value.dump() + ": VPX keeps it within " + range;
      }
      else
         notes[key] = "kept, but outside the usual range "s + range;
   }
   return ""s;
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
