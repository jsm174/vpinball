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
| `vpx_table` | Open a table, create a blank one, save as, read and set table level properties, run the table audit |
| `vpx_editor` | Open and close the live editor, start and stop a playtest, undo, save |
| `vpx_part` | Parts through their stored definition: list, get, set, add, delete, points, mesh, schema, transform, order |
| `vpx_geometry` | World vertices of a part, parts at or near a point, the gap between two parts, ramp checks |
| `vpx_library` | VPZ part packs and donor tables (`.vpx` files or `.vpz` table packs): list, inspect, read a donor script, clone parts with their assets and handlers, import, export |
| `vpx_script` | Read and edit the table script, run statements and evaluate expressions in the running script |
| `vpx_play` | Play, pause, or run exactly N ms of game time |
| `vpx_ball` | List, create, move and destroy balls |
| `vpx_input` | Press and release input actions, hold them, set the plunger |
| `vpx_sim` | Game-time simulation runs: shot, plunge, flip, trace, sweep, stuckscan, autoplay, scenario |
| `vpx_job` | Calls started with `background:true`: list, get (progress, then the result; `waitMs` blocks), cancel |
| `vpx_screenshot` | Capture the playfield, a top view, a DMD frame, a play mode preview, a camera view or a turntable |
| `vpx_export` | Pack the table folder as a `.vpxz` for the mobile apps |
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
4. `vpx_editor playtest`, then `vpx_sim plunge`, `shot`, `flip` or `trace` to see what the ball does, `vpx_screenshot view:"top"` to see where it went.
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

## Testing

`tools/mcp_smoke.py <VPinballX executable or .app>` launches VPX in live edit mode on a copy of the blank table and exercises the protocol and most tools; it exits non zero on the first failed check. `tools/mcp_play.py` plays a few balls on a running table through the turn-based loop (trace until a ball approaches a flipper, flip, trace again).

## Known gaps

- No server-to-client stream: long calls report progress through `vpx_job` instead of notifications.
- The Windows build has not been exercised since the mongoose integration.
