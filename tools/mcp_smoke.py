#!/usr/bin/env python3
"""Smoke test for the embedded MCP server.

Launches VPinballX in live edit mode on a copy of the blank table, adds a bare bumper, clones complete
bumpers from the example table with their materials and script handlers, wires script handlers, playtests,
fires a shot at them, plunges a ball, checks the ball collides, triggers Hit events and runs the script
handler, stops the playtest and undoes the edits. Exits non zero on the first failed check.

Usage: tools/mcp_smoke.py <VPinballX executable or .app> [--port 2114] [--token xxx] [--keep]
The MCP server must be enabled in VPinballX.ini ([MCP] Enable = 1).
"""

import argparse
import json
import zipfile
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Client:
    def __init__(self, port, token):
        self.url = f"http://127.0.0.1:{port}/mcp"
        self.health = f"http://127.0.0.1:{port}/health"
        self.token = token
        self.next_id = 0
        self.session = None

    def rpc(self, method, params=None, timeout=120):
        self.next_id += 1
        body = json.dumps({"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params or {}}).encode()
        headers = {"Content-Type": "application/json", "Accept": "application/json"}
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        if self.session:
            headers["Mcp-Session-Id"] = self.session
        req = urllib.request.Request(self.url, data=body, headers=headers, method="POST")
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            sid = resp.headers.get("Mcp-Session-Id")
            if sid:
                self.session = sid
            return json.loads(resp.read().decode())

    def call(self, name, args=None, timeout=120):
        r = self.rpc("tools/call", {"name": name, "arguments": args or {}}, timeout)
        if "error" in r:
            raise RuntimeError(f"{name}: {r['error']['message']}")
        res = r["result"]
        text = next((c["text"] for c in res["content"] if c["type"] == "text"), "")
        if res.get("isError"):
            raise RuntimeError(f"{name}: {text}")
        try:
            return json.loads(text)
        except ValueError:
            return text

    def raw(self, body, method):
        headers = {"Content-Type": "application/json", "Accept": "application/json"}
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        req = urllib.request.Request(self.url, data=body if method == "POST" else None, headers=headers, method=method)
        self.last_body = {}
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:
                data = resp.read().decode()
                try:
                    self.last_body = json.loads(data) if data else {}
                except ValueError:
                    pass
                return resp.status
        except urllib.error.HTTPError as e:
            return e.code

    def wait_ready(self, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            try:
                with urllib.request.urlopen(self.health, timeout=1) as resp:
                    if json.loads(resp.read().decode()).get("sessionActive"):
                        return True
            except (urllib.error.URLError, OSError, ValueError):
                pass
            time.sleep(0.5)
        return False


def resolve_executable(path):
    if path.endswith(".app") and os.path.isdir(path):
        name = os.path.splitext(os.path.basename(path))[0]
        return os.path.join(path, "Contents", "MacOS", name)
    return path


def find_blank_table(executable):
    candidates = [
        os.path.join(ROOT, "src", "assets", "blankTable.vpx"),
        os.path.join(os.path.dirname(executable), "..", "Resources", "assets", "blankTable.vpx"),
        os.path.join(os.path.dirname(executable), "assets", "blankTable.vpx"),
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    raise FileNotFoundError("blankTable.vpx not found")


def find_example_table(executable):
    candidates = [
        os.path.join(ROOT, "src", "assets", "exampleTable.vpx"),
        os.path.join(os.path.dirname(executable), "..", "Resources", "assets", "exampleTable.vpx"),
        os.path.join(os.path.dirname(executable), "assets", "exampleTable.vpx"),
        os.path.expanduser("~/vpx/vpx-editor/public/templates/exampleTable.vpx"),
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    raise FileNotFoundError("exampleTable.vpx not found")


def check(cond, message):
    status = "ok  " if cond else "FAIL"
    print(f"[{status}] {message}")
    if not cond:
        raise AssertionError(message)


def wait_status(client, predicate, seconds, what):
    deadline = time.time() + seconds
    last = None
    while time.time() < deadline:
        try:
            last = client.call("vpx_status")
            if predicate(last):
                return last
        except (RuntimeError, urllib.error.URLError, OSError):
            pass
        time.sleep(0.5)
    raise AssertionError(f"timed out waiting for {what}: {last}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--port", type=int, default=2114)
    parser.add_argument("--token", default="")
    parser.add_argument("--keep", action="store_true", help="leave VPX running at the end")
    args = parser.parse_args()

    executable = resolve_executable(args.executable)
    workdir = tempfile.mkdtemp(prefix="vpx-mcp-smoke-")
    table = os.path.join(workdir, "Smoke.vpx")
    shutil.copy(find_blank_table(executable), table)
    log = open(os.path.join(workdir, "vpx.log"), "w")
    proc = subprocess.Popen([executable, "-LiveEdit", table], stdout=log, stderr=subprocess.STDOUT)
    client = Client(args.port, args.token)
    failed = False
    try:
        check(client.wait_ready(60), "VPX started and the MCP server answers")
        init = client.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "mcp_smoke", "version": "1"}})
        check(init["result"]["serverInfo"]["name"] == "vpinball", "initialize")
        tools = [t["name"] for t in client.rpc("tools/list")["result"]["tools"]]
        check("vpx_sim" in tools and "vpx_part" in tools, f"{len(tools)} tools listed")
        fingerprint = client.call("vpx_status")["server"]["toolsFingerprint"]
        check(init["result"]["serverInfo"]["version"].endswith("+" + fingerprint), "status reports the tools fingerprint of serverInfo")
        topic = client.call("vpx_status", {"guide": "sim"})
        check("flipWhenAlong" in topic["guide"] and "parts" in topic["topics"], f"guide topics served: {topic['topics']}")

        # Protocol edge cases
        r = client.rpc("tools/call", {"name": "vpx_nope", "arguments": {}})
        check("error" in r and "Unknown tool" in r["error"]["message"], "unknown tool is a JSON-RPC error")
        r = client.rpc("no/such/method")
        check("error" in r and r["error"]["code"] == -32601, "unknown method is a JSON-RPC error")
        check(client.raw(b"{not json", "POST") == 200 and client.last_body.get("error", {}).get("code") == -32700, "invalid JSON answers a parse error")
        check(client.raw(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}).encode(), "POST") == 202, "notifications are accepted with 202")
        check(client.raw(b"", "GET") == 405, "GET on the endpoint is 405 (no SSE stream)")

        status = wait_status(client, lambda s: s["editorOpened"] and s["playMode"] == "FullEdit", 30, "editor session")
        before = client.call("vpx_part", {"action": "list", "type": "Bumper"})["count"]

        # Bare parts: created, read back, undone
        r = client.call("vpx_part", {"action": "add", "type": "Bumper", "x": 475, "y": 900, "name": "SmokeBare", "data": {"force": 20, "radius": 45}})
        check(r["name"] == "SmokeBare" and "force" in r["applied"], "bare bumper added")
        got = client.call("vpx_part", {"action": "get", "name": "SmokeBare", "fields": ["force", "radius", "center", "nope"]})
        check(got["data"]["force"] == 20 and got["data"]["radius"] == 45 and got["data"]["center"]["x"] == 475 and got["unknown"] == ["nope"], "bare bumper fields read back, unknown field reported")
        try:
            client.call("vpx_part", {"action": "set", "name": "SmokeBare", "data": {"Radiuss": 50}})
            check(False, "unknown field name is rejected")
        except RuntimeError as e:
            check("Unknown field 'Radiuss'" in str(e) and "'radius'" in str(e), "unknown field name is rejected with a suggestion")
        r = client.call("vpx_part", {"action": "set", "name": "SmokeBare", "data": {"Radius": 50}})
        check(r["applied"] == ["radius"] and r["mapped"]["Radius"] == "radius", "script-style field name mapped onto the stored one")
        client.call("vpx_editor", {"action": "undo"})
        try:
            client.call("vpx_part", {"action": "add", "type": "Bumper", "x": 475, "y": 900, "name": "SmokeBad", "data": {"Forcee": 20}})
            check(False, "add with an unknown field creates nothing")
        except RuntimeError:
            check(client.call("vpx_part", {"action": "list", "type": "Bumper"})["count"] == before + 1, "add with an unknown field creates nothing")
        client.call("vpx_editor", {"action": "undo"})
        check(client.call("vpx_part", {"action": "list", "type": "Bumper"})["count"] == before, "undo removed the bare bumper")

        # Geometry rebuilding property changes: each one regenerates meshes, physics or render buffers and used to crash
        regen = [
            ("Primitive", {"size": {"x": 100, "y": 100, "z": 60}}, [("sides", 3), ("sides", 12), ("sides", 100), ("sides", 2)], ("sides", 3)),
            ("Trigger", {}, [("shape", v) for v in (1, 2, 3, 4, 5, 0, 2)], ("shape", 2)),
            ("Wall", {}, [("height_top", 120), ("height_bottom", 20), ("height_top", 50)], ("height_top", 50)),
            ("Ramp", {}, [("height_top", 200), ("width_top", 150), ("type", 1), ("type", 0)], ("height_top", 200)),
            ("Bumper", {}, [("radius", 20), ("radius", 60), ("height_scale", 120)], ("radius", 60)),
            ("Flipper", {}, [("flipper_radius_max", 80), ("flipper_radius_max", 150), ("base_radius", 30)], ("flipper_radius_max", 150)),
            ("Kicker", {}, [("radius", 40), ("type", 1), ("type", 2)], ("radius", 40)),
            ("Rubber", {}, [("thickness", 4), ("thickness", 20), ("height_top", 40)], ("thickness", 20)),
            ("Gate", {}, [("length", 60), ("type", 1), ("type", 3), ("show_bracket", False)], ("length", 60)),
            ("Spinner", {}, [("length", 60), ("height", 80)], ("length", 60)),
            ("HitTarget", {}, [("target_type", 2), ("target_type", 6), ("target_type", 1), ("size", {"x": 60})], ("target_type", 1)),
            ("Light", {}, [("radius", 80), ("radius", 200)], ("radius", 200)),
        ]
        regen_names = []
        for i, (ptype, init, changes, (verify_prop, verify_value)) in enumerate(regen):
            name = f"Regen{ptype}"
            r = client.call("vpx_part", {"action": "add", "type": ptype, "x": 200 + (i % 4) * 150, "y": 500 + (i // 4) * 200, "name": name, "data": init})
            check(r["name"] == name, f"added {name}")
            regen_names.append(name)
            for prop, value in changes:
                r = client.call("vpx_part", {"action": "set", "name": name, "data": {prop: value}})
                if prop not in r["applied"] or "warning" in r:
                    raise AssertionError(f"{name}.{prop} = {value} not applied: {r}")
                client.call("vpx_screenshot", {"maxSize": 64})
            got = client.call("vpx_part", {"action": "get", "name": name, "fields": [verify_prop]})["data"][verify_prop]
            check(got == verify_value, f"{name}: {len(changes)} field changes rendered, {verify_prop} reads back {got}")
        got = client.call("vpx_part", {"action": "get", "name": "RegenHitTarget", "fields": ["size"]})["data"]["size"]
        check(got["x"] == 60 and got["z"] != 0, f"partial object merge kept the other components (size {got})")
        r = client.call("vpx_part", {"action": "delete", "names": regen_names})
        check(r["deleted"] == len(regen_names), f"deleted the {len(regen_names)} regenerated parts")
        client.call("vpx_editor", {"action": "undo"})
        got = client.call("vpx_part", {"action": "get", "name": "RegenPrimitive", "fields": ["sides"]})["data"]["sides"]
        check(got == 3, f"undo restored the deleted parts with their edited values (sides {got})")
        client.call("vpx_screenshot", {"maxSize": 64})
        client.call("vpx_part", {"action": "delete", "names": regen_names})
        check(client.call("vpx_part", {"action": "list", "type": "Bumper"})["count"] == before, "regenerated parts removed again")

        # Direct access to the stored definition of a part (fields the script cannot reach), undo snapshots, materials
        first_image = client.call("vpx_image", {"action": "list"})["images"][0]["name"]
        raw = client.call("vpx_part", {"action": "get", "name": "Peg1"})["data"]
        check(raw.get("use_mesh") is True and len(raw) > 30, f"definition of a mesh primitive has {len(raw)} fields")
        client.call("vpx_part", {"action": "set", "name": "Peg1", "data": {"depth_bias": 5, "ui_edge_factor": 0.5}})
        raw = client.call("vpx_part", {"action": "get", "name": "Peg1"})["data"]
        check(raw["depth_bias"] == 5 and raw["ui_edge_factor"] == 0.5 and raw["use_mesh"] is True, "stored fields set and reloaded in place, mesh kept")
        client.call("vpx_screenshot", {"maxSize": 64})
        wall_top = client.call("vpx_part", {"action": "get", "name": "Wall25", "fields": ["height_top"]})["data"]["height_top"]
        client.call("vpx_part", {"action": "set", "name": "Wall25", "data": {"height_top": wall_top + 30, "name": "Wall25Renamed"}})
        got = client.call("vpx_part", {"action": "get", "name": "Wall25Renamed", "fields": ["height_top"]})["data"]["height_top"]
        check(got == wall_top + 30, "set changed a wall height and renamed it through the table")
        client.call("vpx_editor", {"action": "undo"})
        got = client.call("vpx_part", {"action": "get", "name": "Wall25", "fields": ["height_top"]})["data"]["height_top"]
        check(got == wall_top, "undo restored the wall's height and name from the snapshot")
        client.call("vpx_part", {"action": "set", "name": "Wall25", "data": {"height_bottom": 10}})
        client.call("vpx_editor", {"action": "undo"})
        check(client.call("vpx_part", {"action": "get", "name": "Wall25", "fields": ["height_bottom"]})["data"]["height_bottom"] == 0, "undo restored a field edit")
        r = client.call("vpx_part", {"action": "set", "name": "Wall25", "data": {"HeightBottom": 10}})
        check(r["applied"] == ["height_bottom"] and r["mapped"] == {"HeightBottom": "height_bottom"}, "script-style names map onto the stored fields")
        got = client.call("vpx_part", {"action": "get", "name": "Wall25", "fields": ["HeightBottom"]})["data"]
        check(got["height_bottom"] == 10, "get maps script-style field names too")
        client.call("vpx_editor", {"action": "undo"})
        try:
            client.call("vpx_part", {"action": "set", "name": "Wall25", "data": {"HeightTopp": 10}})
            check(False, "unknown field error suggests fields and an example")
        except RuntimeError as e:
            check("'HeightTopp'" in str(e) and "height_top" in str(e) and "data: {" in str(e), "unknown field error suggests fields and an example")

        # Table audit and drawing order
        audit = client.call("vpx_table", {"action": "audit"})
        check(isinstance(audit["errors"], list) and isinstance(audit["warnings"], list), f"table audit ran ({len(audit['errors'])} errors, {len(audit['warnings'])} warnings)")
        client.call("vpx_script", {"action": "append", "text": "Sub SmokeDup()\nEnd Sub\nSub SmokeDup()\nEnd Sub\n"})
        audit = client.call("vpx_table", {"action": "audit"})
        check(any("smokedup" in e.lower() for e in audit["errors"]), f"audit reports a duplicate Sub in the script: {audit}")
        client.call("vpx_editor", {"action": "undo"})
        check(not any("smokedup" in e.lower() for e in client.call("vpx_table", {"action": "audit"})["errors"]), "undo removed the appended script text")
        decal = client.call("vpx_part", {"action": "add", "type": "Decal", "x": 300, "y": 1200, "name": "SmokeDecal", "data": {"type": 0, "text": "VPX", "width": 200, "height": 80}})
        check(decal["name"] == "SmokeDecal", "text decal added")
        client.call("vpx_screenshot", {"maxSize": 64})
        names_before = [p["name"] for p in client.call("vpx_part", {"action": "list"})["parts"]]
        order = client.call("vpx_part", {"action": "order", "names": ["SmokeDecal", "Wall25"]})["order"]
        names = [p["name"] for p in client.call("vpx_part", {"action": "list"})["parts"]]
        check(order == ["SmokeDecal", "Wall25"] and names[-2:] == ["SmokeDecal", "Wall25"], f"drawing order set: {names[-2:]}")
        client.call("vpx_editor", {"action": "undo"})
        check([p["name"] for p in client.call("vpx_part", {"action": "list"})["parts"]] == names_before, "undo restored the drawing order")
        client.call("vpx_part", {"action": "delete", "names": ["SmokeDecal"]})

        # Table level properties, field meanings, geometry queries and helpers
        tbl = client.call("vpx_table", {"action": "get"})
        check("image" in tbl["data"] and "gravity" in tbl["data"] and "meaning" not in tbl, f"table properties read ({len(tbl['data'])} fields)")
        old_slope = tbl["data"]["slope_min"]
        r = client.call("vpx_table", {"action": "set", "data": {"slope_min": old_slope + 1, "Gravity": tbl["data"]["gravity"]}})
        check(client.call("vpx_table", {"action": "get", "fields": ["slope_min"]})["data"]["slope_min"] == old_slope + 1 and "gravity" in r["applied"], "table property set, script-style name mapped")
        client.call("vpx_editor", {"action": "undo"})
        check(client.call("vpx_table", {"action": "get", "fields": ["slope_min"]})["data"]["slope_min"] == old_slope, "undo restored the table property")
        try:
            client.call("vpx_table", {"action": "set", "data": {"image": "NoSuchImage"}})
            check(False, "unknown playfield image refused")
        except RuntimeError:
            check(True, "unknown playfield image refused")
        schema = client.call("vpx_part", {"action": "schema", "type": "Kicker"})
        check("hole" in schema["fields"]["type"].get("meaning", "") and "clockwise" in schema["fields"]["orientation"].get("meaning", ""), "schema explains enum values and directions")
        fl = client.call("vpx_part", {"action": "get", "name": "LeftFlipper", "fields": ["center"]})["data"]["center"]
        at = client.call("vpx_geometry", {"action": "at", "x": fl["x"], "y": fl["y"]})
        check(any(p["name"] == "LeftFlipper" for p in at["parts"]), "hit test at a point finds the flipper")
        gap = client.call("vpx_geometry", {"action": "gap", "names": ["LeftFlipper", "RightFlipper"]})
        check(20 < gap["gap"] < 200, f"gap between the flippers measured: {gap['gap']:.1f} (ball fits: {gap['ballFits']})")
        guide = client.call("vpx_part", {"action": "add", "type": "Wall", "x": 0, "y": 0, "name": "SmokeGuide", "centerline": [{"x": 300, "y": 900}, {"x": 400, "y": 1000}, {"x": 400, "y": 1150}], "thickness": 8})
        check(len(guide["points"]) == 6, "wall built from a centerline and a thickness")
        guide2 = client.call("vpx_part", {"action": "add", "type": "Wall", "x": 0, "y": 0, "name": "SmokeGuide2", "centerline": [{"x": 380, "y": 1000}, {"x": 420, "y": 1000}], "thickness": 8})
        check("SmokeGuide" in guide2.get("overlaps", []), "overlapping part reported on add")
        ins = client.call("vpx_part", {"action": "add", "type": "Light", "x": 600, "y": 900, "name": "SmokeInsert", "circle": 22, "segments": 12, "data": {"shape": 1}})
        check(len(ins["points"]) == 12, "round insert outline generated")
        client.call("vpx_part", {"action": "delete", "names": ["SmokeGuide", "SmokeGuide2", "SmokeInsert"]})
        script_path = os.path.join(workdir, "smoke_script.vbs")
        client.call("vpx_script", {"action": "export", "path": script_path})
        with open(script_path, "a") as f:
            f.write("\nSub SmokeFromFile()\nEnd Sub\n")
        client.call("vpx_script", {"action": "set", "path": script_path})
        check(client.call("vpx_script", {"action": "get", "sub": "SmokeFromFile"})["text"].startswith("Sub SmokeFromFile"), "script exported to a file and set back from it")
        top_img = client.call("vpx_screenshot", {"view": "top", "maxSize": 300})
        check(isinstance(top_img, str) and "screenshot" in top_img.lower(), f"top view captured: {top_img}")
        check(client.call("vpx_status", {})["editorOpened"], "editor view restored after the top capture")

        # Generated meshes on primitives: the image covers the top face, undo restores the built-in shape
        disc = client.call("vpx_part", {"action": "add", "type": "Primitive", "x": 450, "y": 1000, "name": "SmokeDisc", "data": {"image": first_image}})
        check(disc["name"] == "SmokeDisc", "primitive for a disc added")
        m = client.call("vpx_part", {"action": "mesh", "name": "SmokeDisc", "shape": "disc", "radius": 60, "height": 6, "segments": 24})
        check(m["vertices"] == 1 + 24 + 1 + 24 + 50 and m["triangles"] == 24 * 4 and m["bounds"]["max"]["z"] == 6, f"disc mesh generated ({m['vertices']} vertices, {m['triangles']} triangles)")
        got = client.call("vpx_part", {"action": "get", "name": "SmokeDisc", "fields": ["use_mesh", "size"]})
        check(got["data"]["use_mesh"] is True and got["data"]["size"]["x"] == 1 and got["mesh"]["meshFile"] == "generated:disc", "primitive reports its mesh")
        client.call("vpx_screenshot", {"maxSize": 64})
        m = client.call("vpx_part", {"action": "mesh", "name": "SmokeDisc", "shape": "box", "width": 100, "depth": 60, "height": 20})
        check(m["triangles"] == 12 and m["bounds"]["min"]["x"] == -50, "box mesh generated")
        client.call("vpx_editor", {"action": "undo"})
        got = client.call("vpx_part", {"action": "get", "name": "SmokeDisc"})
        check(got["mesh"]["meshFile"] == "generated:disc" and got["mesh"]["triangles"] == 96 and got["data"]["size"]["x"] == 1, "undo restored the previous mesh")
        client.call("vpx_editor", {"action": "undo"})
        got = client.call("vpx_part", {"action": "get", "name": "SmokeDisc"})
        check(got["mesh"].get("builtin") is True, "undo restored the built-in shape")
        client.call("vpx_screenshot", {"maxSize": 64})
        try:
            client.call("vpx_part", {"action": "mesh", "name": "SmokeDisc", "shape": "sphere"})
            check(False, "unknown shape is refused")
        except RuntimeError:
            check(True, "unknown shape is refused")
        client.call("vpx_part", {"action": "delete", "names": ["SmokeDisc"]})

        schema = client.call("vpx_part", {"action": "schema", "type": "Spinner"})
        check("angle_max" in schema["fields"] and schema["fields"]["angle_max"]["type"] == "number", f"schema of a type with no instance lists {len(schema['fields'])} fields with defaults")
        schema = client.call("vpx_part", {"action": "schema", "type": "Flipper"})
        check("example" in schema["fields"]["start_angle"], "schema of an instantiated type carries example values")

        pts_before = client.call("vpx_part", {"action": "points", "name": "Wall25"})["points"]
        t = client.call("vpx_part", {"action": "transform", "name": "Wall25", "dx": 50, "rotate": 15, "scale": 1.5})
        check(t["done"] == ["rotate", "scale", "translate"], "wall transformed: rotate, scale, translate")
        client.call("vpx_editor", {"action": "undo"})
        pts_after = client.call("vpx_part", {"action": "points", "name": "Wall25"})["points"]
        check(abs(pts_after[0]["x"] - pts_before[0]["x"]) < 0.01, "undo restored the wall's points")
        def flipper_geom():
            d = client.call("vpx_part", {"action": "get", "name": "LeftFlipper", "fields": ["center", "start_angle", "end_angle", "flipper_radius_max"]})["data"]
            return d["center"]["x"], d["start_angle"], d["end_angle"], d["flipper_radius_max"]
        fl = flipper_geom()
        t = client.call("vpx_part", {"action": "transform", "name": "LeftFlipper", "dx": 10, "rotate": 5, "scale": 1.2})
        got = flipper_geom()
        check(abs(got[0] - fl[0] - 10) < 0.01 and abs(got[1] - fl[1] - 5) < 0.01 and abs(got[2] - fl[2] - 5) < 0.01 and abs(got[3] - fl[3] * 1.2) < 0.01, f"flipper transformed through its fields: {t['done']}")
        client.call("vpx_editor", {"action": "undo"})
        got = flipper_geom()
        check(abs(got[0] - fl[0]) < 0.01 and abs(got[3] - fl[3]) < 0.01, "undo restored the flipper")

        images = client.call("vpx_image", {"action": "list"})
        check(images["count"] > 0 and all("width" in i for i in images["images"]), f"{images['count']} images listed")
        first = images["images"][0]["name"]
        export_path = os.path.join(workdir, "export.png")
        got = client.call("vpx_image", {"action": "get", "name": first, "exportPath": export_path})
        check(got.get("exported") == export_path and os.path.getsize(export_path) > 100, f"image '{first}' exported to PNG")
        added = client.call("vpx_image", {"action": "add", "path": export_path, "name": "SmokeImage"})
        check(added["name"] == "SmokeImage" and added["width"] == images["images"][0]["width"], "image imported back under a new name")
        client.call("vpx_part", {"action": "set", "name": "Peg1", "data": {"image": "SmokeImage"}})
        client.call("vpx_screenshot", {"maxSize": 64})
        client.call("vpx_part", {"action": "set", "name": "Peg1", "data": {"image": ""}})
        client.call("vpx_image", {"action": "delete", "name": "SmokeImage"})
        check(client.call("vpx_image", {"action": "list"})["count"] == images["count"], "image deleted")
        client.call("vpx_editor", {"action": "undo"})
        check(client.call("vpx_image", {"action": "get", "name": "SmokeImage"})["width"] == added["width"], "undo restored the deleted image")
        for _ in range(3):
            client.call("vpx_editor", {"action": "undo"})
        check(client.call("vpx_image", {"action": "list"})["count"] == images["count"], "undo removed the imported image")

        sounds = client.call("vpx_sound", {"action": "list"})
        if sounds["count"] > 0:
            snd = sounds["sounds"][0]
            got = client.call("vpx_sound", {"action": "set", "name": snd["name"], "volume": -20})
            check(got["volume"] == -20, f"sound '{snd['name']}' volume set")
            client.call("vpx_editor", {"action": "undo"})
            check(next(s["volume"] for s in client.call("vpx_sound", {"action": "list"})["sounds"] if s["name"] == snd["name"]) == snd["volume"], "undo restored the sound volume")

        coll = client.call("vpx_collection", {"action": "create", "name": "SmokeColl", "parts": ["LeftFlipper", "RightFlipper", "Nope"], "fireEvents": True})
        check(coll["parts"] == ["LeftFlipper", "RightFlipper"] and coll["notFound"] == ["Nope"] and coll["fireEvents"], "collection created with members and flags")
        coll = client.call("vpx_collection", {"action": "remove", "name": "SmokeColl", "parts": ["RightFlipper"]})
        check(coll["parts"] == ["LeftFlipper"], "member removed from the collection")
        client.call("vpx_collection", {"action": "delete", "name": "SmokeColl"})
        check(all(c["name"] != "SmokeColl" for c in client.call("vpx_collection", {"action": "list"})["collections"]), "collection deleted")
        client.call("vpx_editor", {"action": "undo"})
        coll = next(c for c in client.call("vpx_collection", {"action": "list"})["collections"] if c["name"] == "SmokeColl")
        check(coll["parts"] == ["LeftFlipper"] and coll["fireEvents"], "undo restored the deleted collection with its members")
        client.call("vpx_editor", {"action": "undo"})
        coll = next(c for c in client.call("vpx_collection", {"action": "list"})["collections"] if c["name"] == "SmokeColl")
        check(coll["parts"] == ["LeftFlipper", "RightFlipper"], "undo restored the removed member")
        client.call("vpx_editor", {"action": "undo"})
        check(all(c["name"] != "SmokeColl" for c in client.call("vpx_collection", {"action": "list"})["collections"]), "undo removed the created collection")

        n_materials = client.call("vpx_material", {"action": "list"})["count"]
        white = client.call("vpx_material", {"action": "get", "name": "Plastic White"})["data"]
        check(white["base_color"] != 0 and "roughness" in white, f"material fields read ({len(white)} fields)")
        added = client.call("vpx_material", {"action": "add", "name": "SmokeRed", "copyOf": "Plastic White", "data": {"base_color": 255, "roughness": 0.9}})["data"]
        check(added["base_color"] == 255 and abs(added["roughness"] - 0.9) < 0.01, "material added from a copy with overrides")
        client.call("vpx_material", {"action": "set", "name": "SmokeRed", "data": {"roughness": 0.2}})
        got = client.call("vpx_material", {"action": "get", "name": "SmokeRed"})["data"]
        check(got["base_color"] == 255 and abs(got["roughness"] - 0.2) < 0.01, "material set changed one field and kept the others")
        peg_mat = client.call("vpx_part", {"action": "get", "name": "Peg1", "fields": ["material"]})["data"]["material"]
        client.call("vpx_part", {"action": "set", "name": "Peg1", "data": {"material": "SmokeRed"}})
        client.call("vpx_screenshot", {"maxSize": 64})
        client.call("vpx_part", {"action": "set", "name": "Peg1", "data": {"material": peg_mat}})
        client.call("vpx_material", {"action": "delete", "name": "SmokeRed"})
        check(client.call("vpx_material", {"action": "list"})["count"] == n_materials, "material deleted")
        try:
            client.call("vpx_material", {"action": "get", "name": "SmokeRed"})
            check(False, "deleted material is not found")
        except RuntimeError:
            check(True, "deleted material is not found")
        client.call("vpx_editor", {"action": "undo"})
        got = client.call("vpx_material", {"action": "get", "name": "SmokeRed"})["data"]
        check(abs(got["roughness"] - 0.2) < 0.01 and client.call("vpx_material", {"action": "list"})["count"] == n_materials + 1, "undo restored the deleted material")
        for _ in range(3):
            client.call("vpx_editor", {"action": "undo"})
        got = client.call("vpx_material", {"action": "get", "name": "SmokeRed"})["data"]
        check(abs(got["roughness"] - 0.9) < 0.01, "undo restored the material field")
        client.call("vpx_editor", {"action": "undo"})
        check(client.call("vpx_material", {"action": "list"})["count"] == n_materials, "undo removed the added material")

        # Donor table inspection and script reading without loading it
        donor = find_example_table(executable)
        info = client.call("vpx_library", {"action": "inspect", "path": donor, "type": "Bumper"})
        donor_names = [p["name"] for p in info["parts"]]
        check(len(donor_names) >= 4, f"donor table {os.path.basename(donor)} has bumpers {donor_names}")
        sub = client.call("vpx_library", {"action": "get_script", "path": donor, "sub": f"{donor_names[0]}_Hit"})
        check(sub["text"].lower().startswith(f"sub {donor_names[0]}_hit".lower()), "donor script Sub read without loading the table")

        # Imports can land in a named group; groups are deleted with their parts only on request
        groups_before = client.call("vpx_part", {"action": "list", "type": "PartGroup"})["count"]
        g = client.call("vpx_part", {"action": "add", "type": "PartGroup", "name": "SmokeGroup"})
        check(g["type"] == "PartGroup" and g["name"] == "SmokeGroup", "part group added")
        r = client.call("vpx_library", {"action": "clone", "path": donor, "parts": donor_names[:2], "withScript": False, "group": "SmokeGroup"})
        in_group = [p for p in r["imported"] if p["type"] == "Bumper" and p.get("group") == "SmokeGroup"]
        check(len(in_group) == 2 and r["group"] == "SmokeGroup" and all(p["type"] != "PartGroup" or p["name"] == "SmokeGroup" for p in r["imported"]), "clone placed the parts in the requested group without a new root group")
        check(client.call("vpx_part", {"action": "list", "type": "PartGroup"})["count"] == groups_before + 1, "no numbered group piled up")
        r = client.call("vpx_part", {"action": "delete", "names": ["SmokeGroup"]})
        check(r["deleted"] == 0 and "withChildren" in r["notDeleted"][0]["reason"], "deleting a non-empty group is refused with the reason")
        r = client.call("vpx_part", {"action": "delete", "names": ["SmokeGroup"], "withChildren": True})
        check(r["deleted"] >= 3, f"group deleted with its {r['deleted'] - 1} parts")
        check(client.call("vpx_part", {"action": "list", "type": "PartGroup"})["count"] == groups_before and client.call("vpx_part", {"action": "list", "type": "Bumper"})["count"] == before, "table back to its part count")

        # Complete bumpers cloned with materials, wired to our own script handlers
        names = donor_names[:3]
        r = client.call("vpx_library", {"action": "clone", "path": donor, "parts": names, "withScript": False})
        cloned = [p["name"] for p in r["imported"] if p["type"] == "Bumper"]
        check(cloned == names, f"cloned {cloned} with their materials")
        got = client.call("vpx_part", {"action": "get", "name": names[0], "fields": ["material", "center"]})["data"]
        check(got["material"] != "", "cloned bumper has a cap material")
        cap = client.call("vpx_material", {"action": "get", "name": got["material"]})["data"]
        check("base_color" in cap and cap["base_color"] != 0, f"cloned material '{got['material']}' came with its values (base_color {cap['base_color']})")
        target = (got["center"]["x"], got["center"]["y"])
        script = "Dim SmokeHits : SmokeHits = 0\n" + "".join(f"Sub {n}_Hit() : SmokeHits = SmokeHits + 1 : End Sub\n" for n in names)
        client.call("vpx_script", {"action": "append", "text": script})
        check(client.call("vpx_script", {"action": "get", "sub": f"{names[0]}_Hit"})["text"].startswith(f"Sub {names[0]}_Hit"), "hit handlers appended to the script")

        # A clone that also carries the donor's event handlers
        r = client.call("vpx_library", {"action": "clone", "path": donor, "parts": [donor_names[3]]})
        copied = [c.lower() for c in r.get("copiedSubs", [])]
        check(f"{donor_names[3]}_hit".lower() in copied, f"clone copied the {donor_names[3]}_Hit handler from the donor script")
        check(len(copied) > 1, f"clone also copied the helpers the handler calls: {copied[1:6]}")

        status = client.call("vpx_editor", {"action": "playtest"})
        check(status["isLiveCopy"] and "switched" in status, "playtest returned once the play session runs")
        status = wait_status(client, lambda s: s["isLiveCopy"] and s["playing"], 30, "playtest session")
        check(len(status["recentScriptErrors"]) == 0, "script compiled without errors")

        shot = client.call("vpx_sim", {"action": "shot", "x": target[0], "y": 1500, "targetX": target[0], "targetY": target[1], "speed": 40, "ms": 2500, "sampleMs": 100}, timeout=600)
        hits = [e for e in shot["events"] if e["event"] == "Hit" and e["part"] in names]
        zs = [s["z"] for s in shot["samples"]]
        check(min(zs) > 0, f"ball stayed above the playfield (min z {min(zs):.1f})")
        check(len(hits) > 0, f"bumpers were hit: {[h['part'] for h in hits]}")
        check(client.call("vpx_script", {"action": "eval", "text": "SmokeHits"})["value"] >= 1, "script hit handler ran")
        check(all("ball" in e for e in shot["events"] if e["event"] in ("Hit", "Unhit")) and "otherBallEvents" in shot, "shot events carry the ball id and other balls' events are counted apart")
        status = client.call("vpx_status", {})
        check(status["mcpEditsSinceSave"] > 0 and "uptimeMinutes" in status, f"status counts {status['mcpEditsSinceSave']} MCP edits since the last save")
        try:
            client.call("vpx_image", {"action": "add", "path": export_path, "name": "SmokeLost"})
            check(False, "asset edits are refused during a playtest")
        except RuntimeError as e:
            check("playtest" in str(e), "asset edits are refused during a playtest")

        plunge = client.call("vpx_sim", {"action": "plunge", "pull": 1, "holdMs": 400, "ms": 4000, "sampleMs": 100}, timeout=600)
        travelled = [b for b in plunge["balls"] if b["samples"] and min(s["y"] for s in b["samples"]) < 1000]
        check(len(travelled) > 0, f"full plunge sent a ball up the table ({len(plunge['events'])} events)")
        check(all(v > 0.9 for v in plunge.get("pullAtRelease", {}).values()) and plunge.get("pullAtRelease"), f"plunge reports the pull at release: {plunge.get('pullAtRelease')}")

        sweep = client.call("vpx_sim", {"action": "sweep", "over": "flipWhenAlong", "values": [0.2, 0.6], "base": {"action": "flip", "x": 130, "y": 1450, "vy": 4, "ms": 3000, "sampleMs": 100}}, timeout=1800)
        check(len(sweep["runs"]) == 2 and all("outcome" in r and "topY" in r for r in sweep["runs"]), f"sweep over the release point: {[(r['value'], r['outcome'], round(r['topY'])) for r in sweep['runs']]}")

        flip = client.call("vpx_sim", {"action": "flip", "x": 130, "y": 1450, "vy": 4, "flipWhenAlong": 0.6, "ms": 5000, "sampleMs": 100}, timeout=600)
        top = min(s["y"] for s in flip["samples"])
        check(flip["triggered"] and 0.5 < flip["atFlip"]["along"] < 0.8, f"flip triggered at {flip['atFlip']['along']:.2f} along the {flip['flipper']['name']}")
        check("exit" in flip and flip["exit"]["speed"] > 5, f"exit vector measured: {flip['exit']['angle']:.0f} deg at {flip['exit']['speed']:.1f}, top y {top:.0f}, outcome: {flip['outcome']}")

        approach = client.call("vpx_sim", {"action": "trace", "x": 0, "ms": 6000, "sampleMs": 100, "until": {"approachingFlipper": True, "event": "Drain", "ballLost": True}}, timeout=600)
        client.call("vpx_ball", {"action": "create", "x": 743, "y": 1450, "vy": 4})
        approach = client.call("vpx_sim", {"action": "trace", "ms": 6000, "sampleMs": 100, "until": {"approachingFlipper": True, "event": "Drain", "ballLost": True}}, timeout=600)
        check(approach["stoppedBy"] == "approachingFlipper" and approach["approach"]["side"] == "right", f"trace stopped when a ball approached the {approach.get('approach', {}).get('flipper')} at along {approach.get('approach', {}).get('along', 0):.2f}")
        client.call("vpx_ball", {"action": "destroy", "id": approach["approach"]["ballId"]})

        before_seq = client.call("vpx_status", {})["lastScriptErrorSeq"]
        r = None
        try:
            client.call("vpx_script", {"action": "run", "text": "smokeZero = 1 / 0"})
        except RuntimeError as e:
            r = json.loads(str(e).split(": ", 1)[1])
        check(r and r["errors"] and r["errors"][0]["seq"] > before_seq, "script error reported with a new sequence number")
        st = client.call("vpx_status", {})
        check(st["scriptErrors"] >= 1 and st["recentScriptErrors"][-1]["description"] == r["errors"][0]["description"] and "recentLogErrors" in st, "status lists the error of this session")
        kept = client.call("vpx_sim", {"action": "shot", "x": 300, "y": 1300, "vx": 0, "vy": 0, "ms": 1500, "keepBall": True}, timeout=600)
        check(kept.get("ballId") is not None and kept.get("state") in ("settled", "moving") and "touching" in kept, f"shot kept its ball and reports its state ({kept.get('state')}, touching {kept.get('touching')})")
        client.call("vpx_ball", {"action": "destroy", "id": kept["ballId"]})
        sweep2 = client.call("vpx_sim", {"action": "sweep", "over": ["flipWhenAlong", "vy"], "values": [[0.3, 0.6], [4]], "base": {"action": "flip", "x": 130, "y": 1450, "ms": 1500, "sampleMs": 100}}, timeout=1800)
        check(len(sweep2["runs"]) == 2 and isinstance(sweep2["runs"][0]["value"], dict), "sweep over two parameters ran every combination")
        sweep3 = client.call("vpx_sim", {"action": "sweep", "over": '["flipWhenAlong", "vy"]', "values": "[[0.3], [4]]", "base": {"action": "flip", "x": 130, "y": 1450, "ms": 1000, "sampleMs": 100}}, timeout=1800)
        check(len(sweep3["runs"]) == 1 and "error" not in sweep3["runs"][0], "sweep accepts over and values sent as JSON strings")
        sweep4 = client.call("vpx_sim", {"action": "sweep", "over": ["x", "y"], "points": [[130, 1450], [140, 1440]], "base": {"action": "flip", "vy": 4, "ms": 1000, "sampleMs": 100}}, timeout=1800)
        check(len(sweep4["runs"]) == 2 and all("error" not in r for r in sweep4["runs"]) and sweep4["runs"][1]["value"] == {"x": 140, "y": 1440}, "sweep over explicit x, y points")
        errs = client.call("vpx_sim", {"action": "shot", "x": 300, "y": 1300, "ms": 300}, timeout=600)["scriptErrors"]
        check(isinstance(errs, dict) and "count" in errs and "distinct" in errs, "sim results summarize script errors")
        tracked = client.call("vpx_sim", {"action": "shot", "x": target[0], "y": 1500, "targetX": target[0], "targetY": target[1], "speed": 40, "ms": 1500, "goal": "Bumper1", "crossAt": {"y": 1200}}, timeout=600)
        check(tracked["rode"] and tracked["rode"][0]["on"] == "playfield" and "Bumper1" in tracked["contacts"] and tracked["goal"]["made"] and tracked["crossings"], f"shot reports what it rode ({[s['on'] for s in tracked['rode']]}), its goal and the y=1200 crossing")
        client.call("vpx_sim", {"action": "shot", "x": 300, "y": 1300, "ms": 200, "keepBall": True}, timeout=600)
        iso = client.call("vpx_sim", {"action": "sweep", "over": "speed", "values": [30, 40], "goal": "Bumper1", "isolate": True, "base": {"action": "shot", "x": target[0], "y": 1500, "targetX": target[0], "targetY": target[1], "ms": 1500}}, timeout=1800)
        check(iso["goalRate"]["runs"] == 2 and all("goal" in r and "rode" in r and "isolated" in r for r in iso["runs"]) and iso["runs"][0]["isolated"]["removedBefore"], f"isolated sweep cleared the kept ball and reports goal rate {iso['goalRate']['percent']}%")
        check(len(client.call("vpx_ball", {"action": "list"})["balls"]) == 0, "isolated runs left no balls behind")
        top_play = client.call("vpx_screenshot", {"view": "top", "maxSize": 300})
        check(isinstance(top_play, str) and "screenshot" in top_play.lower(), "top view captured during a playtest with the last trajectory")
        check(client.call("vpx_status", {})["isLiveCopy"] and not client.call("vpx_status", {})["editorOpened"], "playtest left as it was after the top capture")
        tt = client.rpc("tools/call", {"name": "vpx_screenshot", "arguments": {"turntable": {"count": 3}, "maxSize": 200}}, timeout=120)["result"]
        check(not tt["isError"] and sum(1 for c in tt["content"] if c["type"] == "image") == 3 and len(json.loads(tt["content"][0]["text"])["turntable"]) == 3, "turntable returned 3 views")
        client.call("vpx_script", {"action": "run", "text": "DMDWidth = 16 : DMDHeight = 4 : Dim smokePx(63) : For smokeI = 0 To 63 : smokePx(smokeI) = (smokeI Mod 2) * 100 : Next : DMDPixels = smokePx"})
        client.call("vpx_play", {"ms": 300})
        dmd = client.call("vpx_screenshot", {"target": "dmd"})
        check(isinstance(dmd, str) and "16x4" in dmd, f"script DMD frame captured: {dmd}")
        balls_before = len(client.call("vpx_ball", {"action": "list"})["balls"])
        scan = client.call("vpx_sim", {"action": "stuckscan", "area": {"left": 330, "top": 850, "right": 620, "bottom": 1000}, "step": 70, "ms": 2000, "maxBalls": max(1, 5 - balls_before)}, timeout=900)
        check(scan["tested"] > 0 and "stuckSpots" in scan and len(client.call("vpx_ball", {"action": "list"})["balls"]) <= balls_before, f"stuck scan ran ({scan['tested']} balls, {len(scan['stuckSpots'])} resting spots) and cleaned up")
        auto = client.call("vpx_sim", {"action": "autoplay", "ms": 8000}, timeout=900)
        check(auto["playedMs"] >= 7900 and "switchesNeverFired" in auto and "eventCounts" in auto, f"autoplay soak ran: {auto['plunges']} plunges, {auto['flips']} flips, {auto['ballsLost']} balls lost")

        status = client.call("vpx_editor", {"action": "stop"})
        check(not status["isLiveCopy"] and status["editorOpened"] and "switched" in status, "stop returned once the editor was restored")
        check(client.call("vpx_part", {"action": "list", "type": "Bumper"})["count"] == before + 4, "bumpers survived the playtest")

        # Three records: the first clone, the appended hit handlers, the second clone with its copied donor handlers
        client.call("vpx_editor", {"action": "undo"})
        check(client.call("vpx_part", {"action": "list", "type": "Bumper"})["count"] == before + 3, "undo removed the second clone")
        try:
            client.call("vpx_script", {"action": "get", "sub": f"{donor_names[3]}_Hit"})
            check(False, "the copied donor handlers went with the clone")
        except RuntimeError:
            check(True, "the copied donor handlers went with the clone")
        client.call("vpx_editor", {"action": "undo"})
        client.call("vpx_editor", {"action": "undo"})
        check(client.call("vpx_part", {"action": "list", "type": "Bumper"})["count"] == before, "undo removed the cloned bumpers")
        try:
            client.call("vpx_script", {"action": "get", "sub": f"{names[0]}_Hit"})
            check(False, "undo removed the appended hit handlers")
        except RuntimeError:
            check(True, "undo removed the appended hit handlers")

        # Reopening the table replaces the session and returns once it runs
        reopened = client.call("vpx_table", {"action": "open", "path": table}, timeout=180)
        check("switched" in reopened and reopened["editorOpened"] and reopened["playMode"] == "FullEdit", "table reopened, the new editor session is up")

        # Mobile package: one folder, exactly one table (the iOS import refuses archives with several), no cache
        shutil.copyfile(os.path.join(workdir, "Smoke.vpx"), os.path.join(workdir, "Smoke old.vpx"))
        os.makedirs(os.path.join(workdir, "cache"), exist_ok=True)
        with open(os.path.join(workdir, "cache", "junk.bin"), "w") as f:
            f.write("x")
        with open(os.path.join(workdir, ".DS_Store"), "w") as f:
            f.write("x")
        os.makedirs(os.path.join(workdir, "art"), exist_ok=True)
        with open(os.path.join(workdir, "art", "layer.psd"), "w") as f:
            f.write("x")
        out = os.path.join(workdir, "pkg", "smoke.vpxz")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        exp = client.call("vpx_export", {"path": out}, timeout=600)
        with zipfile.ZipFile(out) as z:
            names = z.namelist()
        tables = [n for n in names if n.lower().endswith(".vpx")]
        check(not any(n.endswith(".DS_Store") for n in names) and "art/layer.psd" in exp.get("possibleSourceFiles", []), "system files skipped, authoring files flagged")
        dry = client.call("vpx_export", {"dryRun": True, "exclude": ["art/"]})
        check(not any("/art/" in n for n in dry["contents"]) and dry.get("excluded") == ["art/"], "exclude leaves a folder out")
        check(len(tables) == 1 and tables[0].endswith("/Smoke.vpx") and not any("/cache/" in n for n in names) and "Smoke old.vpx" in exp.get("otherTablesLeftOut", []),
              f"vpxz package holds one table in its folder, no cache, other versions left out ({len(names)} files)")
        os.remove(os.path.join(workdir, "Smoke old.vpx"))

        shot_img = client.call("vpx_screenshot", {"maxSize": 400})
        check(isinstance(shot_img, str) and "screenshot" in shot_img.lower(), "screenshot captured")
        prim = next(p["name"] for p in client.call("vpx_part", {"action": "list", "type": "Primitive"})["parts"] if p["name"] != "playfield_mesh")
        bounds = client.call("vpx_part", {"action": "get", "name": prim, "fields": ["position"]}).get("bounds")
        verts = client.call("vpx_geometry", {"action": "vertices", "name": prim, "max": 20})
        check(bounds and all(bounds["min"][i] <= v[i] + 0.11 and v[i] <= bounds["max"][i] + 0.11 for v in verts["vertices"] for i in range(3)), f"{prim} vertices lie inside its reported bounds")
        for kind in ("Ramp", "Bumper", "Wall", "Flipper"):
            parts = client.call("vpx_part", {"action": "list", "type": kind})["parts"]
            if not parts:
                continue
            obj_path = os.path.join(workdir, f"smoke-{kind}.obj")
            m = client.call("vpx_geometry", {"action": "vertices", "name": parts[0]["name"], "path": obj_path})
            with open(obj_path) as f:
                lines = f.read().splitlines()
            check(m["vertexCount"] > 0 and m["triangles"] > 0 and sum(1 for l in lines if l.startswith("f ")) == m["triangles"], f"{kind} {parts[0]['name']} mesh exported ({m['vertexCount']} vertices, {m['triangles']} triangles)")
            os.remove(obj_path)
        rc = client.call("vpx_geometry", {"action": "rampcheck"})
        check("joins" in rc and "wiresInBallPath" in rc and "warnings" in rc and len(rc["ramps"]) > 0, f"rampcheck ran on {len(rc['ramps'])} ramps ({len(rc['warnings'])} warnings)")
        lib = client.call("vpx_library", {"action": "list"})
        check(any(d["path"].endswith("exampleTable.vpx") and d.get("bundled") for d in lib["donors"]), "library lists the bundled example table as a donor")
        for mode in ("desktop", "vr"):
            img = client.call("vpx_screenshot", {"mode": mode, "maxSize": 300})
            check(isinstance(img, str) and "mode preview" in img, f"{mode} mode preview captured")
        check(client.call("vpx_status")["editorOpened"], "editor view restored after the previews")
        print("ALL CHECKS PASSED")
    except (AssertionError, RuntimeError, urllib.error.URLError, OSError) as e:
        failed = True
        print(f"FAILED: {e}")
        print(f"log: {log.name}")
    finally:
        if not args.keep:
            proc.terminate()
            try:
                proc.wait(10)
            except subprocess.TimeoutExpired:
                proc.kill()
            log.close()
            if not failed:
                shutil.rmtree(workdir, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
