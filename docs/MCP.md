# MCP server

Visual Pinball X can expose the running table session to AI assistants through the [Model Context Protocol](https://modelcontextprotocol.io) (MCP). The server is embedded in VPX: the tools act on the table that is loaded, its running script, its physics and its live editor. This is a proof of concept.

## Enabling it

Settings live in the `[MCP]` section of `VPinballX.ini`, or in the in-game UI under *MCP Server Settings* (`settings/mcp`), which also shows the client setup lines.

| Setting | Default | Meaning |
|---|---|---|
| `Enable` | 1 | Start the server with VPX |
| `Port` | 2114 | Loopback TCP port; the next 9 ports are tried when it is taken |
| `Token` | empty | Bearer token clients must send; empty accepts any local client |

The server only listens on `127.0.0.1`. No token is set by default; *Generate New Token* in the settings page sets one. Browser pages are only accepted from a loopback origin.

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
| `vpx_library` | VPZ packs and donor tables: list, inspect, read a donor script, clone parts with their assets and handlers, import, export |
| `vpx_script` | Read and edit the table script, run statements and evaluate expressions in the running script |
| `vpx_play` | Play, pause, or run exactly N ms of game time |
| `vpx_ball` | List, create, move and destroy balls |
| `vpx_input` | Press and release input actions, hold them, set the plunger |
| `vpx_sim` | Game-time simulation runs: shot, plunge, flip, trace, sweep, stuckscan, autoplay |
| `vpx_screenshot` | Capture the playfield, a top view, a DMD frame, a play mode preview, a camera view or a turntable |
| `vpx_export` | Pack the table folder as a `.vpxz` for the mobile apps |
| `vpx_material`, `vpx_image`, `vpx_sound`, `vpx_collection` | Table assets and script collections |

Every tool is action based: `action` picks what it does and the other arguments depend on it. The tool descriptions stay short; the server instructions sent at connect hold the conventions (units, angles, colors), and `vpx_status guide:"<topic>"` returns the detailed notes of a topic: `parts`, `geometry`, `sim`, `screenshot`, `library`, `table`, `script`, `export`, `vr`, `flexdmd`.

## How it works

Tool calls are queued and run on the game thread, once per logic update, so the physics and the script never run concurrently with an edit. A tool that needs game time (a simulation run, a playtest switch, a screenshot) is polled every frame until it has a result; the HTTP reply is sent when it does. Simulation runs are deterministic: they advance game time by exactly the requested amount, independent of the frame rate, and the window focus requirement is lifted once automation has driven the player.

Part edits go through the stored definition of the part (the fields the table file keeps, named as in the VPZ packs), reloaded in place the way undo restores a part, so every property is reachable whether or not the script exposes it. Edits made with the editor open are undoable with `vpx_editor undo`: parts, materials, images, sounds, collections, the script and table properties. Edits made during a playtest only reach the playtest copy.

## A building session

1. Launch VPX with no arguments (blank table) or `-LiveEdit <table.vpx>`, or open the editor on the current table with `vpx_editor open`.
2. Clone complete assemblies from a donor table with `vpx_library clone` (the bundled `exampleTable.vpx` is a full game), or add bare parts with `vpx_part add` and give them materials and images.
3. Wire the logic with `vpx_script append` or `replace`.
4. `vpx_editor playtest`, then `vpx_sim plunge`, `shot`, `flip` or `trace` to see what the ball does, `vpx_screenshot view:"top"` to see where it went.
5. `vpx_editor stop`, fix, repeat. Save often with `vpx_editor save`: `vpx_status` reports `mcpEditsSinceSave`.

## Testing

`tools/mcp_smoke.py <VPinballX executable or .app>` launches VPX in live edit mode on a copy of the blank table and exercises the protocol and most tools; it exits non zero on the first failed check. `tools/mcp_play.py` plays a few balls on a running table through the turn-based loop (trace until a ball approaches a flipper, flip, trace again).

## Known gaps

- No server-to-client stream (notifications, progress).
- The Windows build has not been exercised since the mongoose integration.
