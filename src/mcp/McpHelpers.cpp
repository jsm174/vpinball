// license:GPLv3+

#include "core/stdafx.h"
#include "mcp/McpInternal.h"

namespace VPX::MCP
{

const char* const GUIDE_TEXT = R"(Visual Pinball X MCP server. The tools act on the table session currently running inside VPX.
Coordinates are VPX units (1 inch = 47.06 units); x grows to the right, y grows toward the player, z is up (0 = playfield).
Tools: vpx_status (session, errors, log, this guide and its topics), vpx_table (open/new/save_as/get/set/audit), vpx_editor (open/close/playtest/stop/undo/save), vpx_part (list/get/set/add/delete/points/mesh/schema/transform/order),
vpx_geometry (vertices/at/near/gap/rampcheck), vpx_library (list/inspect/get_script/clone/import/export: complete assemblies with materials and script handlers from donor tables such as the VPX example tables),
vpx_script (get/set/replace/append/export, run/eval in the live script), vpx_play (play/pause/step N ms), vpx_ball (list/create/set/destroy), vpx_sim (shot/plunge/flip/trace/sweep/stuckscan/autoplay/scenario with trajectories and script events; background:true runs it as a vpx_job, polled with vpx_job list/get/cancel), vpx_input, vpx_screenshot, vpx_export (pack the table and its companion files as a .vpxz for the mobile apps),
vpx_material, vpx_image, vpx_sound, vpx_collection.
Detailed notes per topic: vpx_status guide:"<topic>" with topic parts, geometry, sim, screenshot, library, table, script, export, vr or flexdmd. Read a topic before the first use of its tool.
Building flow: vpx_editor open (or launch VPX with -LiveEdit), vpx_library clone or vpx_part add, vpx_script to wire logic, vpx_editor playtest, vpx_sim plunge/shot, vpx_editor stop, repeat, vpx_editor save.
Bare parts from vpx_part add have no materials: prefer vpx_library clone from a donor table.
Assets: vpx_image add <file> returns the image name to put in a part's image field; vpx_material list/add, vpx_sound list/add. Parts are edited through their stored fields (vpx_part schema <type> lists them).
A flat textured disc, post or plastic is vpx_part add Primitive then vpx_part mesh shape:disc (the image covers the top face); meshes from .obj/.glb files load the same way.
Save often with vpx_editor save: vpx_status shows mcpEditsSinceSave, and a VPX crash loses everything since the last save.
Game time only advances while the table is playing; vpx_play with ms runs exactly that much game time then pauses. Automation ignores window focus once a play, sim, plunge or playtest tool has been called.
Conventions:
- Units: VPX units (50 units = 1.0625 inch, a standard ball has radius 25 and diameter 50); heights are above the playfield (z up); angles in degrees.
- 2D angles (flipper start_angle/end_angle, kicker orientation and Kick angle, gate and spinner rotation, trigger and decal rotation, hit target rot_z, primitive prim_rot1_z) turn clockwise seen from above; for directions 0 points up the table (toward the backglass, -y), 90 to the right (+x).
- Colors are integers 0x00BBGGRR (red in the low byte): pure red is 255, pure blue is 16711680.
- Primitive rotations: prim_rot1_x/y/z are the script's RotX/RotY/RotZ, prim_rot2_x/y/z the script's ObjRotX/ObjRotY/ObjRotZ, prim_trans_x/y/z the TransX/TransY/TransZ offset applied before RotX/Y/Z (so it orbits around position); size multiplies the mesh.
- Walls: is_bottom_solid makes the bottom face collide too, so a wall above the playfield acts as a ceiling (keeps a ball on an upper deck).
- Ramps: height goes linearly from height_bottom at the first point to height_top at the last one, along the path (vpx_part points reports calcHeight per point); wire ramp types sit at that height with their wires above it.
- vpx_part schema type:<Type> lists each stored field with its meaning, units, values (integer coded fields) and range where known. Writes refuse values an integer coded field does not know, clamp a value outside an enforced range like VPX does, and note values outside the usual range (valueNotes in the result).
- Where a part ends up: vpx_part get reports bounds (world box after every transform, z up); vpx_geometry vertices returns a part's transformed vertices (or writes them as .obj), so positions and rotation directions never need to be worked out by hand.
If vpx_status server.toolsFingerprint differs from the suffix of the server version seen at connect, or a documented parameter is refused, the client's tool list is stale: ask the user to reconnect the MCP client.)";

const std::map<string, const char*>& GuideTopics()
{
   static const std::map<string, const char*> topics = {
      { "parts", R"(vpx_part reads and writes parts through their stored definition: every field the table file keeps, named as in the VPZ packs (center {x,y}, height_top, material, image, visible, part_group, ui_locked...), nothing is gated by the script interface.
list: optional type filter (Flipper, Wall, Ramp, Light, Kicker, Bumper, Primitive, PartGroup...); the order is the drawing order.
get: the definition of a part (fields: [...] for a subset), its bounds (world axis aligned box {min, max, size} in table coordinates, z up, after every transform, from the mesh, outline or collision shape, see from) and for primitives their mesh (vertices, triangles, meshFile or builtin + sides).
set: data: {field: value} is merged into the definition (objects merge field by field, so size: {x: 60} keeps y and z) and the part is reloaded in place with meshes, physics and render state rebuilt. Script-style names are mapped when only case and underscores differ (HeightTop -> height_top, reported in mapped); unknown names are refused with suggestions before anything changes. Undoable.
add: type, x, y, optional name, data and points; type PartGroup creates a group; centerline (points) + thickness generates the band outline of a Wall, guide or rim; circle (radius) + segments a round outline centered at x,y for a Light insert, Trigger, Wall post or Flasher. Reports overlaps: the solid parts whose collision shape the new part intersects. Bare added parts have no materials: prefer vpx_library clone for complete assemblies.
delete: names; a non-empty group needs withChildren:true; the playfield mesh needs force:true; balls are destroyed with vpx_ball; notDeleted lists the refused ones with the reason.
points: reads or replaces (points: [{x, y, z?, smooth?, slingshot?}]) the drag points of a Wall, Ramp, Rubber, Light, Trigger or Flasher; ramps go from the first point (bottom) to the last (top); calcHeight is reported per point.
mesh: gives a Primitive a real mesh. shape disc | cylinder | box with radius, height, width, depth, segments in VPX units (z up, bottom at the part's position z, top face mapped to the whole image, size reset to 1,1,1): the built-in shape's cap UVs only cover a quarter of the image, so use this for discs, posts, plastics and targets. Or path to a .obj (units vpx, the default, z up; or meters: Blender's meters, +Y up) or a .glb (meters, +Y up).
schema: type: every stored field of a part type with its JSON type, its default (or an example from this table) and its meaning, enum values and units where known. Call it before setting fields of a type for the first time.
transform: dx, dy move; rotate (degrees), scale (or scaleX/scaleY), flipX, flipY act about the part's center: drag point parts fully, others through their rotation and size fields (reported in done/skipped).
order: moves the named parts to the end of the drawing order, in the given order: the last one is drawn last, over the others (decals, flashers and transparent parts depend on it).
add, delete, mesh and points need the editor open; set, add, delete, points, mesh, transform and order are undoable (vpx_editor undo). Edits during a playtest only reach the playtest copy (a warning says so). Images, materials and sounds are referenced by name: vpx_image add imports a file and returns its name. Asset, collection, script and table property edits made while the editor is open are undoable too.)" },
      { "geometry", R"(vpx_geometry queries the collision shapes (what the ball actually collides with) and the visible meshes of the running table.
vertices: name: the world mesh vertices of a part. A primitive after its full transform (size, prim_trans, prim_rot1 Z then Y then X, prim_rot2 Z then Y then X, position); ramps (incl. wire ramps), rubbers, walls, flippers, bumpers, gates, spinners, targets, kickers and triggers as VPX builds their visible mesh; all with the cabinet placement for parts in a cabinet or room space group. Up to max (default 500) evenly sampled vertices, or path writes the whole transformed mesh as a .obj. Lights, flashers, decals and text boxes have no mesh export: vpx_part get reports their bounds.
at: x, y (z default 25, a resting ball's center): the parts whose collision shape a ball centered there touches.
near: x, y, radius (default 150): the parts within radius with their distance, the closest point and gapForBall (distance minus a standard ball radius of 25: a ball centered at the point touches parts with gapForBall <= 0). Volumes (trigger and kicker capture zones) are flagged.
gap: names: [a, b]: the closest distance between two parts' collision shapes (sampled, about 1 unit precision), with the points it is measured between; a standard ball needs 50 to pass, less than that traps or blocks it.
rampcheck: names (ramp or group names, default every ramp), joinDistance (default 20). Wire ramps collide as flat walls at the collision width (wire_distance_x + 20), the drawn wires sit at wire_distance_x. joins: an end of one ramp close to an end of another (gap, lateral offset, height step, direction change, a narrower ramp whose edge sticks into the wider one's path, visible wire ends that do not line up). surfaceEnds: ramp ends that meet no other ramp, with the surface found by probing down just past the end (playfield, wall top, primitive) and the height step there. wiresInBallPath: a visible wire of one ramp within a ball's reach while it rides another (clearance < 0: drawn through the ball). wireCrossings: wires of two ramps touching or crossing. warnings sums them up. Only ballPath ramps (collidable, playfield space, not a single wire guide rail) count as paths.)" },
      { "sim", R"(vpx_sim runs the physics in game time (deterministic, independent of the frame rate) and reports trajectories (samples every sampleMs), maxHeight, the end state, the script events fired (Hit, Unhit, Slingshot, Spin, Dropped, Raised, EOS, BOS), what the ball rode and touched, script errors and WARN/ERROR log lines. The table is paused afterwards. ms is game time to simulate (default 3000, max 60000).
shot: create a ball at x, y (z default 0) with vx, vy, or aimed at targetX, targetY with speed (default 30), run ms, destroy it unless keepBall:true (then its ballId can be traced further); reports whether it drained, settled or ended in a kicker (outcome, state, lastKicker, touching). Balls created by shot, flip or vpx_ball bypass the table's own ball release, so a table script that counts its balls (trough, BallsOnPlayfield...) does not know them: to test game rules, plunge or move the game's own ball (vpx_ball set).
plunge: real game flow: optionally press Start (startGame), pull the plunger (pull 0..1, default 1) for holdMs (default 400), release, trace every ball for ms. pullAtRelease reports how far each plunger had actually been pulled (its speed_pull sets how fast it pulls back).
flip: a flipper shot. Take the first ball, ballId, or create one at x, y (vx, vy); press the flipper (side left/right, default from the ball position) when triggered: flipWhenAlong (0 = flipper base, 1 = tip, the ball rolling down onto the flipper), flipAtMs after the start, or when the ball crosses flipWhenX / flipWhenY; preHold:true holds the flipper up first (cradle) and drops it for dropMs (default 150) before flipping; the flipper stays pressed holdMs (default 300). Reports atFlip (ball position, along, flipper angle), exit (speed and angle once the ball left the flipper: 0 = straight up, negative = left) and hitAfterFlip. Sweep flipWhenAlong to map release point to shot.
trace: run up to ms and trace every ball, stopping early on an until condition: {y: 1600} (a ball reaches y >= value, i.e. comes down to the flippers), {yBelow: 500} (reaches the top), {x}, {xBelow}, {speedBelow: 0.5, settleMs: 300} (ball settled, cradled or stuck), {event: "Bumper1" or "Drain Hit"}, {ballLost: true}, {approachingFlipper: true} (a ball is coming down onto a flipper; approach gives the flipper, side, ball and its position along the flipper); ballId restricts the checks to one ball; minMs (default 50) before any check. stoppedBy says what stopped it (timeout otherwise). The turn-based play loop: trace until approachingFlipper, flip that side with flipWhenAlong, trace again.
sweep: run a shot, flip or plunge once per value of a parameter: over (a name such as flipWhenAlong, flipAtMs, speed, pull, x) and values: [...]; or over: ["x", "y"] with values: [[...], [...]] for every combination, or with points: [[x1, y1], [x2, y2], ...] for those pairs only; base holds the run's arguments including its action. Each run is summarized: outcome, maxHeight, topY, exit, atFlip, hits, rode, crossed, goal, end. Up to 400 runs. The shot map a table author wants.
stuckscan: drop resting balls on a grid (area {left, top, right, bottom}, step default 60, z: 25 for the playfield or deck height + 25) in batches (maxBalls, default 20, fewer if the table script can not handle many balls), run ms (default 3000) per batch in real time, and report every spot where balls came to rest outside kickers and the plunger lane with the parts holding them: the traps. Balls the script released meanwhile are removed.
autoplay: a soak test bot for ms of game time (default 60000, up to 9 minutes, in real time): flips when a ball comes down onto a flipper, plunges, presses Start, nudges balls resting for 3 s; reports balls lost, average ball time, stuck episodes, script errors, switches that never fired and event counts per part. seed picks the random release points.
Every run tracks the ball each frame: rode lists the surfaces it rolled on in order ({on: part or playfield, t, x, y, z} at each change, so a ramp shows up even though ramps fire no Hit and the last entry says where it came back down), contacts every part it touched. goal: "RightInlane" reports made or missed (an event of that part, or the ball touched it; ramps need the ball to ride them; sweeps add goalRate). crossAt: {y: 1000} or {x: 500} reports where the ball crossed that line (interpolated per frame, with speed and angle; sweeps get a crossed column). isolate: true (shot, flip, and sweeps of them) destroys every other ball first (except ballId) and the leftovers after, for repeatable runs; balls of the game removed this way are reported, the table script may still count them.
scenario: a regression suite for a table, run again after each change. path: a .json file (relative to the table folder) or scenario: the same object inline; only: [run names] for a subset; save: write the results to a .json file. Format: {"name": "Main shots", "defaults": {args merged into every run, e.g. "ms": 4000, "isolate": true}, "runs": [{"name": "left orbit", "args": {"action": "shot", "x": 120, "y": 1700, "targetX": 150, "targetY": 300, "goal": "LeftOrbit"}, "expect": {"goal": true}}, {"name": "spinner", "args": {"action": "sweep", "over": "vx", "values": [-2, -1, 0, 1, 2], "base": {"action": "shot", "x": 450, "y": 1500, "vy": -30}, "goal": "Spinner1"}, "expect": {"goalRate": 80}}]}. The run arguments may also sit directly in the run instead of args. expect: goal (true/false), outcome (prefix of the outcome text, or a list: "drained", "captured", "captured by Kicker1", "on the table"), fired ["Part" or "Part.Event"], notFired, rode [parts], maxHeight {min, max}, scriptErrors (most allowed, default 0, always checked); a sweep checks goalRate (least percent) and scriptErrors. The result lists each run with passed, the failed checks and a summary.
background: true on any vpx_sim call returns a job id at once; vpx_job get id:<job> (waitMs to block) returns the progress, then the call's own result. Use it for sweeps, scenarios, stuckscans and autoplay that may outlast the client's request timeout.)" },
      { "screenshot", R"(vpx_screenshot captures the playfield window (the play view, or the editor view when the editor is open) downscaled to maxSize pixels (default 1280), as jpg (default) or png.
uiPage (e.g. homepage, settings/mcp, table/options) opens that in-game UI page for the capture and closes it afterwards.
view:'top' (editor open or playtesting) renders the whole playfield from above, orthographic, without the editor panels, cropped to the table: textured (shade:'wireframe' for outlines only), with the collision shapes outlined in red (colliders, default true), part names (labels, default true) and the ball paths of the last vpx_sim run with their hit points (trajectory, default true). The previous editor view is restored afterwards.
target:'dmd' returns the current frame of the table's display (FlexDMD, PinMAME, script DMD...) at its native resolution scaled up, independent of where it is drawn; link picks another display source than the default DMD/CRT.
mode previews another play mode: the part groups visible in that mode (desktop, fss, cabinet, vr, mr) and, for desktop/fss/cabinet, that mode's view setup (camera, backdrop). The static prepass is disabled for the capture so the override shows at once.
camera {eye:[x,y,z], target:[x,y,z]} (table units, z up, editor open or playtesting) renders a perspective view from eye toward target, e.g. a standing player in front of the cabinet: eye [table center x, table bottom + 900, 1100]. mode 'vr' or 'mr' without camera uses that default player viewpoint.
turntable {center:[x,y,z] (default the table center), radius, height, count (default 4, up to 12), startAngle (degrees, 0 = from the player side)} returns count camera views evenly spaced around the center, one image each (maxSize default 640), e.g. to show a model from all sides. Mode and camera are restored afterwards.)" },
      { "library", R"(vpx_library works with VPZ part packs and donor tables.
list: the .vpz part packs of the configured library folders (plus an optional folder path) and the donor tables: the ones bundled with VPX (exampleTable.vpz is a complete game to clone working assemblies from; blankTable.vpz the new table template; strippedTable.vpz a minimal table; lightSeqTable.vpz a light sequencer demo) and the tables in those folders (.vpx files, .vpz table packs: a pack with a table.json, as a folder or a zip).
inspect: a .vpz part pack (parts, asset counts) or a donor table (parts by type with positions, asset counts, script size; type filter).
get_script: reads a donor table's script like vpx_script get (sub, pattern, context, subs, startLine, endLine) without loading it as the session table.
clone: named parts of a donor table into the edited table with their materials, images and sounds, plus their script event handlers (Sub <Part>_<Event>) and the helper Subs and Functions those call, unless withScript:false; optionally centered at x, y. Handlers are only copied when the parts kept their donor names (conflicting names are renamed: adapt them with get_script and vpx_script append). Colliding material and image names are renamed on import.
import: a .vpz part pack (optionally centered at x, y). export: named parts of the edited table to a .vpz part pack (path).
group: put the imported or cloned parts into this group (created if missing) instead of the pack's own root groups, so importing twice does not pile up numbered groups.
import, clone and export need the editor open; an import or clone undoes as one record, copied script handlers included (vpx_editor undo).
Pack layout (a folder or a zip): manifest.json, table.json, parts/<name>.json with the stored fields (the same names as vpx_part get), images/<file> plus <name>.json (md5, alpha test...), sounds/, fonts/, meshes/<name>.glb (meters, +Y up, converted to VPX units on load; the primitive's size field then multiplies the mesh).)" },
      { "table", R"(vpx_editor drives the live editor: open it on the current table, close it back to play, playtest the edited table (a play session on a copy: part, table and script edits made during the playtest only reach that copy, the tools warn about it), stop the playtest, undo the last edit, save the table to its file. playtest and stop return once the switch is done, with the new status.
vpx_table open (path) or new (a blank table) replace the running session (same play mode) and return once the new session is up, with its status; save_as (path) saves under a new name: a .vpx path writes a table file, a .vpz path a zip pack, an existing folder a folder pack.
vpx_table get lists the table level properties with their meaning (playfield image and material, ball image, backdrop images and color, physics: gravity, slope, friction, elasticity, scatter, nudge; lighting, environment, AO, SSR, bloom, tonemapper, exposure; glass heights; volumes); fields: [...] for a subset. set applies data: {field: value} live, undoable while the editor is open; during a playtest the edited table gets the change too, so it survives the playtest. Image and material names are checked.
vpx_table audit runs the VPX table audit (script problems such as duplicate declarations or deprecated APIs, invalid glass, legacy ball mapping, data fixed on load, part inconsistencies) and returns errors, warnings and notes; tables with VR content (part groups limited to some play modes or placed in cabinet/room space) also get a vr section: groups with the modes they show in and their space, cabinet part bounds against the table extents, and warnings (VR-only groups never shown under forced VR without a headset, VR-visible parts outside the playfield, a cabinet shorter than the table).
Saving: vpx_editor save writes the table to its file through the editor (authored visibility, undo clean point); mcpEditsSinceSave in vpx_status counts the edits since. A <table>.vbs sidecar next to the table file replaces the embedded script on load: vpx_status and vpx_script report it.)" },
      { "script", R"(vpx_script works on the table script (the edited table's during a playtest).
get: the whole script, a line range (startLine, endLine), one Sub (sub), grep matches (pattern, context lines around each, default 2) or the Sub/Function index (subs:true).
set replaces the whole script (text, or path to a .vbs file); export writes it to a file (path); replace swaps find with replacement (all:true for every occurrence); append adds text at the end. Edits apply to the next play session (vpx_editor playtest), they are undoable while the editor is open (the whole script is snapshotted per edit), and they are refused during a playtest.
run executes VBScript statements in the running script context and returns the script errors and WARN/ERROR log lines it caused; eval evaluates an expression and returns its value (e.g. Flipper1.X, Score, BallsOnPlayfield). Both need a running script (not in editor mode).
Script errors carry a sequence number (seq), the session they happened in, the failing line's text and whether the code was the table script or loaded at run time (ExecuteGlobal, a library .vbs): vpx_status recentScriptErrors and lastScriptErrorSeq, sim results scriptErrors (deduplicated by line and message).)" },
      { "export", R"(vpx_export packages the current table as a .vpxz archive for the mobile apps (iOS, Android): a zip holding one folder with the table and its companion files, the format their import and web server accept.
The table's folder is packed (the table, its .vbs sidecar, .ini settings, backglass, DMD/FlexDMD folders, pinmame, music...) except cache/ and any other table (.vpx file or .vpz pack): the mobile import expects exactly one table per archive, so other versions kept in the folder are left out and listed (otherTablesLeftOut). The table ini is included even when it lives elsewhere.
System files (.DS_Store, ._*, any dot file, Thumbs.db, desktop.ini) are skipped. exclude: ['art/', '*.psd'] leaves out files or folders by relative path or name (* and ? wildcards, a trailing / for folders). Authoring files (.psd, .blend, .xcf, .kra, .ai, .fbx, .zip...) are packed but listed under possibleSourceFiles.
The table file on disk is packed: save:true saves the table first, otherwise unsaved edits are reported and left out. dryRun:true lists what would be packed. path defaults to ~/Downloads/<table folder>.vpxz. The archive is written in the background, VPX keeps running. Copy it to the device (AirDrop, Files, the app's web server upload, adb push) and open it with VPinball: it imports as one table.)" },
      { "vr", R"(Play modes and VR: part groups choose the play modes they show in (desktop, fss, cabinet, vr, mr) and a space (playfield, or cabinet/cabinet feet/room, placed through the table's default cabinet transform).
VPX uses the VR/MR visibility only when a headset is actually connected. RenderingModeOverride 2 (forced VR, e.g. on a phone) without a headset makes the script see RenderingMode 2, but parts are filtered and the camera set by the current view mode (desktop, FSS or cabinet): VR-only groups stay hidden and cabinet space parts are drawn on the normal view.
vpx_screenshot mode:'vr' (or desktop/fss/cabinet) previews a mode, camera {eye, target} picks any viewpoint, turntable shows a model from all sides; vpx_table audit lists the VR groups, cabinet bounds and likely problems; vpx_part get reports each part's bounds to check what lies outside the playfield.)" },
      { "flexdmd", R"(FlexDMD in VPX: the plugin needs Run = True to produce frames (Show is already True by default, False hides it). Images named "VPX.<image>" are read from the saved table file, so images added since the last save are missing: save first, or use image files on disk relative to ProjectFolder (vpx_image add warns when the script mentions FlexDMD).
To show it on the playfield, use a Flasher with render_mode 1 (DMD) and an empty image_src_link (the default DMD). vpx_screenshot target:"dmd" returns the current frame at native resolution.
A plugin call that fails (e.g. a FlexDMD object reporting an error) raises a script error, reported like any other (vpx_status recentScriptErrors, the errors of vpx_script run); other plugin messages (missing assets...) are in vpx_status recentLogErrors and in the warnings of sim results.)" },
   };
   return topics;
}

static const char* const ITEM_TYPE_NAMES[] = { "Wall", "Flipper", "Timer", "Plunger", "Textbox", "Bumper", "Trigger", "Light", "Kicker", "Decal", "Gate", "Spinner", "Ramp", "Table",
   "LightCenter", "DragPoint", "Collection", "DispReel", "LightSeq", "Primitive", "Flasher", "Rubber", "HitTarget", "Ball", "PartGroup" };

const char* const ACTION_NAMES[] = { "LeftFlipper", "RightFlipper", "StagedLeftFlipper", "StagedRightFlipper", "LeftMagnaSave", "RightMagnaSave", "LaunchBall", "LeftNudge",
   "CenterNudge", "RightNudge", "Tilt", "AddCredit", "AddCredit2", "StartGame", "Lockbar", "Pause", "PerfOverlay", "OpenInGameUI", "ExitGame", "InGameUI", "VolumeDown", "VolumeUp",
   "VRRecenter", "VRUp", "VRDown" };

string ItemTypeName(ItemTypeEnum type)
{
   const size_t i = static_cast<size_t>(type);
   return i < std::size(ITEM_TYPE_NAMES) ? ITEM_TYPE_NAMES[i] : "Unknown"s;
}

ItemTypeEnum ItemTypeFromName(const string& name)
{
   for (size_t i = 0; i < std::size(ITEM_TYPE_NAMES); i++)
      if (StrCompareNoCase(name, ITEM_TYPE_NAMES[i]))
         return static_cast<ItemTypeEnum>(i);
   return eItemInvalid;
}

string EventName(int dispid)
{
   switch (dispid)
   {
   case DISPID_HitEvents_Hit: return "Hit"s;
   case DISPID_HitEvents_Unhit: return "Unhit"s;
   case DISPID_SurfaceEvents_Slingshot: return "Slingshot"s;
   case DISPID_FlipperEvents_Collide: return "Collide"s;
   case DISPID_SpinnerEvents_Spin: return "Spin"s;
   case DISPID_TargetEvents_Dropped: return "Dropped"s;
   case DISPID_TargetEvents_Raised: return "Raised"s;
   case DISPID_LimitEvents_EOS: return "EOS"s;
   case DISPID_LimitEvents_BOS: return "BOS"s;
   default: return ""s;
   }
}

ToolOutput TextOutput(const json& j) { return ToolOutput { j.dump(), ""s, ""s, false }; }

ToolOutput ErrorOutput(const string& msg) { return ToolOutput { msg, ""s, ""s, true }; }

ToolStep Immediate(std::function<ToolOutput()> fn)
{
   return [fn]() -> std::optional<ToolOutput> { return fn(); };
}

ToolStep WithPlayer(std::function<ToolOutput(Player&)> fn)
{
   return Immediate([fn]() { return g_pplayer ? fn(*g_pplayer) : ErrorOutput("No table session is running"s); });
}

static string VariantToString(VARIANT& v, json& out)
{
   switch (V_VT(&v))
   {
   case VT_EMPTY:
   case VT_NULL: out = nullptr; return ""s;
   case VT_BOOL: out = V_BOOL(&v) != VARIANT_FALSE; return ""s;
   case VT_I2: out = V_I2(&v); return ""s;
   case VT_I4: out = V_I4(&v); return ""s;
   case VT_UI4: out = V_UI4(&v); return ""s;
   case VT_R4: out = V_R4(&v); return ""s;
   case VT_R8: out = V_R8(&v); return ""s;
   case VT_BSTR: out = MakeString(V_BSTR(&v)); return ""s;
   case VT_DISPATCH: out = "[object]"s; return ""s;
   default: break;
   }
   VARIANT conv;
   VariantInit(&conv);
   if (SUCCEEDED(VariantChangeType(&conv, &v, 0, VT_BSTR)))
   {
      out = MakeString(V_BSTR(&conv));
      VariantClear(&conv);
      return ""s;
   }
   return "Unsupported variant type "s + std::to_string(V_VT(&v));
}

HRESULT GetDispProperty(IDispatch* disp, const string& name, json& out, string& error)
{
   wstring wname = MakeWString(name);
   LPOLESTR names[] = { const_cast<LPOLESTR>(wname.c_str()) };
   DISPID dispid;
   HRESULT hr = disp->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &dispid);
   if (FAILED(hr))
   {
      error = "Unknown property '"s + name + '\'';
      return hr;
   }
   DISPPARAMS params = { nullptr, nullptr, 0, 0 };
   VARIANT result;
   VariantInit(&result);
   EXCEPINFO excep {};
   hr = disp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &params, &result, &excep, nullptr);
   if (FAILED(hr))
   {
      error = "Failed to read '"s + name + "' (hr=" + std::to_string(hr) + ')';
      return hr;
   }
   error = VariantToString(result, out);
   VariantClear(&result);
   return error.empty() ? S_OK : E_FAIL;
}

IEditable* FindPart(Player& player, const string& name)
{
   for (IEditable* part : player.m_ptable->GetParts())
      if (StrCompareNoCase(part->GetName(), name))
         return part;
   return nullptr;
}

Ball* FindBall(Player& player, int id)
{
   for (Ball* ball : player.m_vball)
   {
      int ballId;
      if (SUCCEEDED(ball->get_ID(&ballId)) && ballId == id)
         return ball;
   }
   return nullptr;
}

json BallJson(Ball* ball)
{
   int id = -1;
   ball->get_ID(&id);
   const vec3& p = ball->GetPosition();
   const vec3& v = ball->GetVelocity();
   return json { { "id", id }, { "x", Round(p.x) }, { "y", Round(p.y) }, { "z", Round(p.z) }, { "vx", Round(v.x, 2) }, { "vy", Round(v.y, 2) }, { "vz", Round(v.z, 2) }, { "radius", Round(ball->GetRadius()) } };
}

json EventsJson(const std::vector<CapturedEvent>& events, uint32_t startMs)
{
   json arr = json::array();
   for (const CapturedEvent& e : events)
   {
      json j { { "t", e.gameTimeMs - startMs }, { "part", e.part }, { "event", e.event } };
      if (e.ballId >= 0)
         j["ball"] = e.ballId;
      arr.push_back(j);
   }
   return arr;
}

// Keeps the events of one ball (and the ones without a ball, like timers) in a sim result, counting the others
void FocusEvents(json& result, int ballId)
{
   json mine = json::array();
   int others = 0;
   for (const json& e : result["events"])
   {
      if (!e.contains("ball") || e["ball"].get<int>() == ballId)
         mine.push_back(e);
      else
         others++;
   }
   result["events"] = mine;
   result["otherBallEvents"] = others;
}

// Summarizes a run for one ball: stayed on the table, drained, or captured by a kicker; a ball destroyed by the script right after a kicker hit counts as captured by it
void AddOutcome(json& result, Player& p, Ball* ball)
{
   string lastKicker;
   for (const json& e : result["events"])
      if (e["event"] == "Hit")
         if (IEditable* part = FindPart(p, e["part"].get<string>()); part && part->GetItemType() == eItemKicker)
            lastKicker = part->GetName();
   if (ball)
   {
      const vec3& pos = ball->GetPosition();
      const bool drained = pos.y > p.m_ptable->m_bottom;
      result["drained"] = drained;
      const vec3& vel = ball->GetVelocity();
      const float speed = sqrtf(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
      const bool inKicker = ball->m_hitBall.m_d.m_lockedInKicker;
      const string state = drained ? "drained"s : inKicker ? "in kicker"s : speed < 0.5f ? "settled"s : "moving"s;
      result["state"] = state;
      result["outcome"] = drained ? "drained"s : inKicker ? ("captured by "s + lastKicker) : ("on the table, "s + state);
      if (!drained)
      {
         json touching = json::array();
         for (const PartDistance& d : PartsNear(p, Vertex3Ds(pos.x, pos.y, pos.z), ball->GetRadius() + 2.f))
            if (!d.volume)
               touching.push_back(d.part->GetName());
         result["touching"] = touching;
      }
   }
   else
   {
      const bool drainLike = !lastKicker.empty() && (lowerCase(lastKicker).find("drain") != string::npos || lowerCase(lastKicker).find("outhole") != string::npos);
      result["ballDestroyedByScript"] = true;
      result["drained"] = drainLike;
      result["outcome"] = lastKicker.empty() ? "destroyed by the script"s : (drainLike ? "drained ("s + lastKicker + ')' : "captured by "s + lastKicker + " and destroyed by the script");
   }
   if (!lastKicker.empty())
      result["lastKicker"] = lastKicker;
}

int ActionIndex(const string& name)
{
   for (size_t i = 0; i < std::size(ACTION_NAMES); i++)
      if (StrCompareNoCase(name, ACTION_NAMES[i]))
         return static_cast<int>(i);
   return -1;
}

DragPointCurve* GetCurve(IEditable* part)
{
   switch (part->GetItemType())
   {
   case eItemSurface: return &static_cast<Surface*>(part)->m_curve;
   case eItemRamp: return &static_cast<Ramp*>(part)->m_curve;
   case eItemRubber: return &static_cast<Rubber*>(part)->m_curve;
   case eItemFlasher: return &static_cast<Flasher*>(part)->m_curve;
   case eItemLight: return &static_cast<Light*>(part)->m_curve;
   case eItemTrigger: return &static_cast<Trigger*>(part)->m_curve;
   default: return nullptr;
   }
}

json PointsJson(const DragPointCurve* curve)
{
   json arr = json::array();
   for (const auto& dp : curve->GetPoints())
   {
      const Vertex3Ds& v = dp->GetVertex();
      arr.push_back({ { "x", Round(v.x, 2) }, { "y", Round(v.y, 2) }, { "z", Round(v.z, 2) }, { "smooth", dp->IsSmooth() }, { "slingshot", dp->m_slingshot }, { "calcHeight", Round(dp->GetCalcHeight()) } });
   }
   return arr;
}

void SetCurvePoints(DragPointCurve* curve, const json& points)
{
   curve->ClearPoints();
   for (const json& pt : points)
      curve->PushPoint(std::make_unique<DragPoint>(curve, pt.value("x", 0.f), pt.value("y", 0.f), pt.value("z", 0.f), pt.value("smooth", false)));
   for (size_t i = 0; i < points.size(); i++)
      curve->GetPoints()[i]->m_slingshot = points[i].value("slingshot", false);
}

bool IsEditing(Player& p) { return p.m_liveUI && p.m_liveUI->IsEditorUIOpened() && !p.m_liveUI->m_editorUI.IsInspectMode(); }

bool IsLiveCopy(Player& p) { return p.m_ptable->m_liveBaseTable != nullptr; }

string LiveCopyError() { return "A playtest is running: stop it with vpx_editor stop first, a change made now would only reach the playtest copy and be lost when it stops"s; }

ToolOutput WithLiveCopyNote(Player& p, json result)
{
   if (IsLiveCopy(p))
      result["warning"] = "Applied to the playtest copy only: it is lost when the playtest stops. Stop the playtest first to edit the table itself";
   return TextOutput(result);
}

string RequireEditor(Player& p)
{
   if (!p.m_liveUI || !p.m_liveUI->IsEditorUIOpened())
      return "The editor is not open: call vpx_editor open first"s;
   if (p.m_liveUI->m_editorUI.IsInspectMode())
      return "A playtest is running: stop it with vpx_editor stop before editing"s;
   return ""s;
}

// Record the part state for undo before an MCP edit (only meaningful while the editor is open on the base table)
void RecordEdit(Player& p, IEditable* part, bool withMesh)
{
   if (IsEditing(p))
      p.m_liveUI->m_editorUI.RecordPartEdit(part, withMesh);
}

void RecordAction(Player& p, PinTable* table, std::function<void()> undo)
{
   if (IsEditing(p) && p.m_ptable == table)
      p.m_liveUI->m_editorUI.RecordUndoAction(std::move(undo));
   else
      table->SetNonUndoableDirty(SaveDirtyState::Dirty);
}

void RefreshPart(Player& p, IEditable* part)
{
   p.m_renderer->ReinitRenderable(part->GetIRenderable());
   if (part->GetIHitable() && p.m_physics)
      p.m_physics->Update(part);
}

void TranslatePart(Player& p, IEditable* part, float dx, float dy)
{
   part->Translate(Vertex2D(dx, dy));
   RefreshPart(p, part);
}

void PlaceParts(Player& p, const vector<IEditable*>& parts, float x, float y)
{
   if (parts.empty())
      return;
   Vertex2D centroid(0.f, 0.f);
   int n = 0;
   for (IEditable* part : parts)
      if (part->GetItemType() != eItemPartGroup)
      {
         const Vertex2D c = part->GetCenter();
         centroid.x += c.x;
         centroid.y += c.y;
         n++;
      }
   if (n == 0)
      return;
   const float dx = x - centroid.x / n, dy = y - centroid.y / n;
   for (IEditable* part : parts)
      if (part->GetItemType() != eItemPartGroup)
         TranslatePart(p, part, dx, dy);
}

json PartsJson(const vector<IEditable*>& parts)
{
   json arr = json::array();
   for (IEditable* part : parts)
   {
      json j { { "name", part->GetName() }, { "type", ItemTypeName(part->GetItemType()) } };
      if (part->GetPartGroup())
         j["group"] = part->GetPartGroup()->GetName();
      if (part->GetItemType() != eItemPartGroup)
      {
         const Vertex2D c = part->GetCenter();
         j["x"] = Round(c.x);
         j["y"] = Round(c.y);
      }
      arr.push_back(j);
   }
   return arr;
}

json PackInfoJson(const std::filesystem::path& path)
{
   json j { { "path", PathToUTF8(path) } };
   const auto pack = JSONSerializer::CreateReader(path);
   if (!pack)
   {
      j["valid"] = false;
      return j;
   }
   j["valid"] = true;
   json parts = json::array();
   int materials = 0, images = 0, sounds = 0, collections = 0;
   for (const std::filesystem::path& file : pack->ListFiles())
   {
      const string folder = PathToUTF8(file.parent_path());
      if (file.extension() != ".json")
         continue;
      if (folder == "parts")
         parts.push_back(PathToUTF8(file.stem()));
      else if (folder == "materials")
         materials++;
      else if (folder == "images")
         images++;
      else if (folder == "sounds")
         sounds++;
      else if (folder == "collections")
         collections++;
   }
   j["parts"] = parts;
   j["materials"] = materials;
   j["images"] = images;
   j["sounds"] = sounds;
   j["collections"] = collections;
   return j;
}

bool IsTablePack(const std::filesystem::path& path)
{
   const auto pack = JSONSerializer::CreateReader(path);
   return pack && std::ranges::any_of(pack->ListFiles(), [](const std::filesystem::path& file) { return file == "table.json"; });
}

vector<std::filesystem::path> LibraryFolders()
{
   vector<std::filesystem::path> folders;
   const string list = g_settingsService.GetAppSettings().GetEditor_PartLibraryFolders();
   size_t start = 0;
   while (start <= list.size())
   {
      const size_t end = list.find(';', start);
      const string entry = list.substr(start, end == string::npos ? string::npos : end - start);
      if (!entry.empty())
         folders.push_back(PathFromUTF8(entry));
      if (end == string::npos)
         break;
      start = end + 1;
   }
   return folders;
}

CComObject<PinTable>* LoadDonorTable(const std::filesystem::path& path, string& error)
{
   if (path.empty() || !std::filesystem::exists(path))
   {
      error = "Table not found: "s + PathToUTF8(path);
      return nullptr;
   }
   CComObject<PinTable>* table;
   CComObject<PinTable>::CreateInstance(&table);
   table->AddRef();
   VPXFileFeedback feedback;
   const HRESULT hr = table->LoadGameFromFilename(path, feedback);
   if (FAILED(hr) && hr != APPX_E_BLOCK_HASH_INVALID && hr != APPX_E_CORRUPT_CONTENT)
   {
      table->Release();
      error = "Failed to load "s + PathToUTF8(path);
      return nullptr;
   }
   return table;
}

// Saves the edited table like the editor does: with the editor open on it, through the editor (it saves the authored part visibility, not its
// display overrides, and sets the undo clean point); otherwise directly
bool SaveTableFile(Player& p, PinTable* table)
{
   if (p.m_liveUI && p.m_liveUI->IsEditorUIOpened() && p.m_ptable == table && !p.m_liveUI->m_editorUI.IsInspectMode())
      return p.m_liveUI->m_editorUI.SaveTableBlocking();
   VPXFileFeedback feedback;
   if (FAILED(table->Save(feedback)))
      return false;
   table->SetDirty(SaveDirtyState::Clean);
   return true;
}

json StatusJson(Player& player)
{
   json j;
   j["tablePath"] = PathToUTF8(player.m_ptable->m_filename);
   j["tableName"] = player.m_ptable->m_title;
   j["tableWidth"] = player.m_ptable->m_right - player.m_ptable->m_left;
   j["tableHeight"] = player.m_ptable->m_bottom - player.m_ptable->m_top;
   switch (player.m_playMode)
   {
   case Player::PlayMode::Play: j["playMode"] = "Play"; break;
   case Player::PlayMode::LiveEdit: j["playMode"] = "LiveEdit"; break;
   case Player::PlayMode::FullEdit: j["playMode"] = "FullEdit"; break;
   case Player::PlayMode::CaptureAttract: j["playMode"] = "CaptureAttract"; break;
   default: j["playMode"] = "Unknown"; break;
   }
   j["playing"] = player.IsPlaying();
   j["windowFocused"] = player.m_playfieldWnd->IsFocused();
   j["gameTimeMs"] = player.m_time_msec;
   j["fps"] = Round(player.m_fps, 1);
   j["balls"] = player.m_vball.size();
   j["editorOpened"] = player.m_liveUI && player.m_liveUI->IsEditorUIOpened();
   j["isLiveCopy"] = player.m_ptable->m_liveBaseTable != nullptr;
   j["scriptRunning"] = player.m_scriptInterpreter != nullptr;
   j["canUndo"] = IsEditing(player) && player.m_liveUI->m_editorUI.HasUndo();
   j["mcpPort"] = McpServer::Get().GetPort();
   j["mcpEditsSinceSave"] = McpServer::Get().GetEditsSinceSave();
   if (player.m_audioPlayer)
      j["musicPlaying"] = player.m_audioPlayer->IsMusicPlaying();
   AddSidecarWarning(player.m_ptable->m_liveBaseTable ? player.m_ptable->m_liveBaseTable : player.m_ptable, j);
   static const auto startup = std::chrono::steady_clock::now();
   j["uptimeMinutes"] = std::chrono::duration_cast<std::chrono::minutes>(std::chrono::steady_clock::now() - startup).count();
#ifdef __APPLE__
   // The crash report folder is scanned at most once a minute, vpx_status is called often
   static std::filesystem::path newest;
   static std::filesystem::file_time_type newestTime;
   static std::chrono::steady_clock::time_point scanned;
   if (const char* home = getenv("HOME"); home && (scanned == std::chrono::steady_clock::time_point() || std::chrono::steady_clock::now() - scanned > std::chrono::minutes(1)))
   {
      scanned = std::chrono::steady_clock::now();
      newest.clear();
      std::error_code ec;
      for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(home) / "Library/Logs/DiagnosticReports", ec))
      {
         const string fn = entry.path().filename().string();
         if (fn.rfind("VPinballX", 0) != 0 || entry.path().extension() != ".ips")
            continue;
         const auto t = entry.last_write_time(ec);
         if (newest.empty() || t > newestTime)
         {
            newest = entry.path();
            newestTime = t;
         }
      }
   }
   if (!newest.empty())
   {
      const auto age = std::chrono::duration_cast<std::chrono::minutes>(std::filesystem::file_time_type::clock::now() - newestTime).count();
      if (age <= 24 * 60)
         j["lastCrashReport"] = json { { "path", PathToUTF8(newest) }, { "ageMinutes", age } };
   }
#endif
   return j;
}

CComObject<PinTable>* LoadTableForSwitch(const std::filesystem::path& path, string& error)
{
   if (!path.empty() && !std::filesystem::exists(path))
   {
      error = "File not found: "s + PathToUTF8(path);
      return nullptr;
   }
   CComObject<PinTable>* table;
   CComObject<PinTable>::CreateInstance(&table);
   table->AddRef();
   VPXFileFeedback feedback;
   if (path.empty())
   {
      table->m_glassTopHeight = table->m_glassBottomHeight = 210;
      for (int i = 0; i < 16; i++)
         table->m_rgcolorcustom[i] = RGB(0, 0, 0);
      table->LoadGameFromFilename(g_app->m_fileLocator.GetAppPath(FileLocator::AppSubFolder::Assets, "blankTable.vpz"), feedback);
      table->m_title = "Table1"s;
      table->GetSettings().SetIniPath(std::filesystem::path());
      table->m_filename.clear();
   }
   else
   {
      const HRESULT hr = table->LoadGameFromFilename(path, feedback);
      if (FAILED(hr) && hr != APPX_E_BLOCK_HASH_INVALID && hr != APPX_E_CORRUPT_CONTENT)
      {
         table->Release();
         error = "Failed to load "s + PathToUTF8(path);
         return nullptr;
      }
   }
   return table;
}

SimTrace g_lastSimTrace;

json Schema(json properties, json required)
{
   return json { { "type", "object" }, { "properties", std::move(properties) }, { "required", std::move(required) } };
}

json Prop(const char* type, const char* description) { return json { { "type", type }, { "description", description } }; }

json PointsProp(const char* description) { return json { { "type", "array" }, { "items", json { { "type", "object" } } }, { "description", description } }; }

}
