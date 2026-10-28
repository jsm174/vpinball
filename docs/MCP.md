# MCP server

Visual Pinball X can expose the running table session to AI assistants through the [Model Context Protocol](https://modelcontextprotocol.io) (MCP). The server is embedded in VPX: the tools act on the table that is loaded, its running script, its physics and its live editor. This is a proof of concept.

## Enabling it

Settings live in the `[MCP]` section of `VPinballX.ini`, or in the in-game UI under *MCP Server Settings* (`settings/mcp`), which also shows the client setup lines.

| Setting | Default | Meaning |
|---|---|---|
| `Enable` | 1 | Start the server with VPX (off in the mobile apps) |
| `Port` | 2114 | Loopback TCP port; the next 9 ports are tried when it is taken |
| `Token` | empty | Bearer token clients must send; empty accepts any local client |

The server is on by default without a token for now, while the testers are the only users, and only listens on `127.0.0.1`. *Generate New Token* in the settings page sets one; enabling the server from that page also generates one when none is set. Browser pages are only accepted from a loopback origin (`Origin: null`, sent by sandboxed iframes and `file://` pages, is refused), the `Host` header must be a loopback name (DNS rebinding), and requests must be `application/json`, which a cross-origin page cannot send without a CORS preflight the server never grants.

The endpoint is Streamable HTTP, JSON-RPC over POST at `http://127.0.0.1:<port>/mcp`, without a server-to-client stream. `GET /health` reports the version, the tool count and whether a session is active.

## Connecting a client

Claude Code:

```
claude mcp add --transport http vpx http://127.0.0.1:2114/mcp --header "Authorization: Bearer <token>"
```

Codex CLI (`~/.codex/config.toml`):

```
[mcp_servers.vpx]
url = "http://127.0.0.1:2114/mcp"
http_headers = { Authorization = "Bearer <token>" }
```

The server version reported at connect carries a hash of the tool definitions. After a VPX rebuild, `vpx_status` reports the current hash under `server.toolsFingerprint`: when it differs, the client's tool list is stale and the client must be reconnected.

## Tools

| Tool | What it does |
|---|---|
| `vpx_status` | Session state, recent script and log errors, edits since the last save, the usage guide and its topics (`guide:"sim"`) |
| `vpx_table` | Open a table, create a blank one, save as, read and set table level properties, run the table audit, keep and restore checkpoints of the edited table |
| `vpx_editor` | Open and close the live editor, start (after writing a backup copy) and stop a playtest, undo, save; switch the editor layout preset, show an area, select parts and frame the views on them |
| `vpx_part` | Parts through their stored definition: list, get, set, add, delete, points, mesh, schema, transform (one part, or several as a group about a pivot), order |
| `vpx_geometry` | World vertices of a part, parts at or near a point, the gap between two parts, ramp checks (joins, surface ends, path clearance, underside gaps, wires) |
| `vpx_library` | VPZ part packs and donor tables (`.vpx` files or `.vpz` table packs): list, inspect, read a donor script, clone parts with their assets and handlers, import, export |
| `vpx_script` | Read and edit the table script, run statements and evaluate expressions in the running script |
| `vpx_play` | Play, pause, or run exactly N ms of game time |
| `vpx_ball` | List, create, move and destroy balls |
| `vpx_input` | Press and release input actions, hold them, set the plunger |
| `vpx_sim` | Game-time simulation runs: shot, plunge, flip, trace, sweep, stuckscan, autoplay, scenario; every run reports where the ball lost speed (impacts), `record` adds a run to the table's scenario file |
| `vpx_job` | Calls started with `background:true`: list, get (progress, then the result; `waitMs` blocks), cancel |
| `vpx_screenshot` | Capture the playfield, a top view or the blueprint schematic (whole table or a region), a DMD frame, a play mode preview, a camera view or a turntable; `path` also writes the image to a file |
| `vpx_export` | Pack the table folder as a `.vpxz` for the mobile apps |
| `vpx_ramp` | Lay out a collision ramp from waypoints (`plan`, `apply`), draw the plastic or metal sheet over it (`sheet`), grade a collision ramp against what works in real tables (`fit`: width, walls, effective grade, bends, climb speed, rampcheck findings) and ride test it in a playtest (`test`: descents at 3 and 15, climb bisection, verdict and diagnosis per run) |
| `vpx_wireform` | Build the visible wireform (habitrail) around a collision ramp's route: rails in a cross-section, rings, rungs, cradles and stands along it, hairpin or elbow at the ends |
| `vpx_material`, `vpx_image`, `vpx_sound`, `vpx_collection` | Table assets and script collections |

Tools carry annotations (`readOnlyHint`, `destructiveHint`, a title) so clients can let read-only calls (`vpx_status`, `vpx_geometry`, `vpx_screenshot`) through without asking. Clients of protocol `2025-06-18` or later also get each JSON result as `structuredContent`.

Every tool is action based: `action` picks what it does and the other arguments depend on it. The tool descriptions stay short; the server instructions sent at connect hold the conventions (units, angles, colors), and `vpx_status guide:"<topic>"` returns the detailed notes of a topic: `parts`, `geometry`, `sim`, `screenshot`, `library`, `table`, `script`, `export`, `vr`, `flexdmd`.

## How it works

Tool calls are queued and run on the game thread, once per logic update, so the physics and the script never run concurrently with an edit. A tool that needs game time (a simulation run, a playtest switch, a screenshot) is polled every frame until it has a result; the HTTP reply is sent when it does. A call with `background: true` (advertised on `vpx_sim`, accepted by any tool) gets a job id back at once and keeps running on the game thread; `vpx_job get` reports its progress and finally returns the call's own result, so a long sweep or soak test never hits the client's request timeout. Simulation runs are deterministic: they advance game time by exactly the requested amount, independent of the frame rate, and the window focus requirement is lifted once automation has driven the player.

Part edits go through the stored definition of the part (the fields the table file keeps, named as in the VPZ packs), reloaded in place the way undo restores a part, so every property is reachable whether or not the script exposes it. `vpx_part schema` gives each field its meaning, units, the values of integer coded fields and its range; writes refuse unknown enum values, clamp values outside an enforced range the way the editor and script setters do (a stored definition skips those setters), and note values outside the usual range. Edits made with the editor open are undoable with `vpx_editor undo`: parts, materials, images, sounds, collections, the script and table properties. Edits made during a playtest only reach the playtest copy.

## A building session

1. Launch VPX with no arguments (blank table) or `-LiveEdit <table>` (a `.vpx` file or a `.vpz` pack), or open the editor on the current table with `vpx_editor open`.
2. Clone complete assemblies from a donor table with `vpx_library clone` (the bundled `exampleTable.vpz` is a full game), or add bare parts with `vpx_part add` and give them materials and images.
3. Wire the logic with `vpx_script append` or `replace`.
4. `vpx_editor playtest`, then `vpx_sim plunge`, `shot`, `flip` or `trace` to see what the ball does, `vpx_screenshot view:"top"` to see where it went (`view:"blueprint"` for the schematic of every stored part, `region` to zoom on an area).
5. `vpx_editor stop`, fix, repeat. Save often with `vpx_editor save`: `vpx_status` reports `mcpEditsSinceSave`.

## Scenario files

A scenario file keeps the shots a table must make, with what each is expected to give, next to the table, and `vpx_sim action:"scenario" path:"shots.json"` runs them all again after a change:

```json
{
  "name": "Main shots",
  "defaults": { "ms": 4000, "isolate": true },
  "runs": [
    { "name": "left orbit", "args": { "action": "shot", "x": 120, "y": 1700, "targetX": 150, "targetY": 300, "goal": "LeftOrbit" }, "expect": { "goal": true } },
    { "name": "spinner", "args": { "action": "sweep", "over": "vx", "values": [-2, -1, 0, 1, 2], "base": { "action": "shot", "x": 450, "y": 1500, "vy": -30 }, "goal": "Spinner1" }, "expect": { "goalRate": 80 } }
  ]
}
```

Each run is any `vpx_sim` call (sweeps included) with `defaults` merged in. `expect` checks `goal`, `outcome` (a prefix such as `drained` or `captured by Kicker1`), `fired` and `notFired` (`Part` or `Part.Event`), `rode`, `maxHeight {min, max}` and `scriptErrors` (at most, default 0, always checked); a sweep checks `goalRate` (least percent). `only` runs a subset, `save` writes the results to a file, and `background: true` runs the whole scenario as a job.

`record: true` (or a file name) on a shot, plunge, flip or sweep appends that run to the scenario file once it ran, with what it gave as its expectation (goal, outcome, the furthest ramp rode, a sweep's goal rate), so the shots tuned during a session become the regression suite without writing the file by hand; `name` sets the run name and replaces an existing run of that name.

## Ramps

`vpx_ramp` carries the rules from a survey of every ramp in 172 real tables (`~/test/ramp_survey`, 2026-11-03: 2,315 ball-path Ramp parts, 1,598 raised ones ride tested). `plan` lays a route out from waypoints with heights (corners become arcs of 150, the width follows the finish: 60 for wire, 70 under a sheet, walls 62; `exit: "spread"` runs it on by the ~58 a spread wireform exit needs before the ball drops through) and reports the bends, the effective grades and the climb speed the entry needs; `apply` creates that invisible collision Ramp and grades it. `sheet` draws the plastic, metal or clear sheet ramp over a collision ramp (floor 74 / 69 / 76 wide with thickness, walls 50 with a fillet or a metal hem flange, per side or per span, the entry flap, a material created when missing). `fit` grades a collision ramp: width 55–60 for wire rails or ~70 under a sheet and never under 50, walls of 20 or more, an effective downhill grade of 4° or more with the table tilt included and no level stretch, bend radius 150 or more, the climb speed its low end needs (16.4 + 0.077 × effective height), and the rampcheck findings; each rule reports the share of real descents that got through at that value. `test` runs the survey's ride test in a playtest: a ball rolled down from the high end at 3 and 15, and up from the low end with a bisection for the slowest speed that makes it; every run gets a verdict (through, stalled, rolled_back, fell_off, left_side, missed) and a failed run a diagnosis of where it went wrong and the likely cause. `vpx_status guide:"ramp"` has the numbers.

## Wireforms

`vpx_wireform` builds the bent wire rail the ball appears to ride, shaped after the 360 wireform meshes measured on their collision ramps in the `~/tables` survey (2026-11-03): a route (the collision ramp's centerline at the ball's floor height, or `points`), a style (`rail4`, the default: floor pair 38 apart, guards 56 apart and 38 up, open-top U hoops 65 wide every 95, vertical legs under every second hoop; `rail3`, `rail2`; `williams`, Theatre of Magic's 33 / 67 / 28.5 with stirrups and diagonal struts; `gottlieb`, Tee'd Off's 50 x 54 box in closed rings), supports (`hoops`/`stirrups`, `rings`, `rungs`, `cradles`), legs (`legs`, vertical with a short foot; `struts`, the long diagonal brace; `stands`, short brackets with a U-hook foot) and end fittings (`spread`, the floor pair opening from 38 to 68 over 100 of wire so the ball falls through, closing 180 past the end, as on No Fear and Maverick; `hairpin`, a rounded tip 60 past the end; `tab`, the floor wires hooking down over the ramp that hands the ball over, the default entry on the rail styles; `drop`, the bundle bending down with rings; `elbow`; `tunnels` put closed rings along a span). Each takes a spacing or a list of `{at, ...}` positions. The mesh is written into a non collidable Primitive at (0, 0, 0) in table coordinates; the physics stays on the invisible ramp. With nothing given, the rail styles get hoops, legs and the exit (spread when the low end is 40 or more above the playfield, else hairpin) automatically. `vpx_status guide:"wireform"` has the rules and the numbers.

VPX can crash while a playtest runs or stops, and a crash loses every edit since the last save. `vpx_editor playtest` therefore writes `<table>.before-playtest.vpx` next to the table first (`backup: false` skips it). `vpx_table checkpoint name:"..."` writes a copy of the edited table to `<folder>/<table>.checkpoints/<name>.vpx` without touching the table's own file, `checkpoints` lists them and `restore name:"..."` reloads one as the current table (keeping the table's file name, marked unsaved), for experiments that may have to be undone wholesale. Both copies are plain `.vpx` files that `vpx_table open` loads.

## Testing

`tools/mcp_smoke.py <VPinballX executable or .app>` launches VPX in live edit mode on a copy of the blank table and exercises the protocol and most tools; it exits non zero on the first failed check. `tools/mcp_play.py` plays a few balls on a running table through the turn-based loop (trace until a ball approaches a flipper, flip, trace again).

## Known gaps

- `vpx_wireform`'s proportions come from the survey medians; the exit loops rest on the 18 closed loops the survey found. Per-span layouts (wires starting or ending along the route), mounting tabs and the entry hand-over from a sheet ramp are not generated.
- No server-to-client stream: long calls report progress through `vpx_job` instead of notifications.
- The Windows build has not been exercised since the mongoose integration.
