#!/usr/bin/env python3
"""Plays a few balls on the running table through the MCP server, the way an assistant would.

The loop is turn based: the game only advances inside tool calls. Each turn traces until a ball comes
down to the flipper zone, picks the flipper on the ball's side, flips when the ball crosses the release
point, then traces again. It is a deliberately simple policy meant to exercise the loop, not to play well.

Usage: tools/mcp_play.py [--port 2114] [--token xxx] [--balls 3] [--turns 40]
VPX must be running a table in a play session with the MCP server enabled.
"""

import argparse
import json
import sys
import urllib.request


class Client:
    def __init__(self, port, token):
        self.url = f"http://127.0.0.1:{port}/mcp"
        self.token = token
        self.next_id = 0
        self.session = None

    def rpc(self, method, params=None, timeout=600):
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

    def call(self, name, args=None):
        r = self.rpc("tools/call", {"name": name, "arguments": args or {}})
        if "error" in r:
            raise RuntimeError(f"{name}: {r['error']['message']}")
        res = r["result"]
        text = next((c["text"] for c in res["content"] if c["type"] == "text"), "")
        if res.get("isError"):
            raise RuntimeError(f"{name}: {text}")
        return json.loads(text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=2114)
    parser.add_argument("--token", default="")
    parser.add_argument("--balls", type=int, default=3)
    parser.add_argument("--turns", type=int, default=40)
    args = parser.parse_args()
    client = Client(args.port, args.token)
    client.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "mcp_play", "version": "1"}})

    status = client.call("vpx_status")
    width, height = status["tableWidth"], status["tableHeight"]
    flipper_zone_y = height * 0.77
    release_y = height * 0.83
    print(f"table {status['tableName']} {width:.0f}x{height:.0f}, flipper zone from y {flipper_zone_y:.0f}, release at y {release_y:.0f}")

    def in_lane(b):
        return "end" in b and b["end"]["x"] > width * 0.88 and b["end"]["y"] > height * 0.8

    def settle():
        return client.call("vpx_sim", {"action": "trace", "ms": 6000, "sampleMs": 200, "until": {"speedBelow": 0.2, "settleMs": 400, "event": "Drain", "ballLost": True}})

    # Skill plunge: find the weakest pull that gets the ball into play (not back in the lane, not straight to the drain)
    pull = 1.0
    client.call("vpx_sim", {"action": "plunge", "startGame": True, "pull": 0.2, "holdMs": 300, "ms": 500, "sampleMs": 250})
    for candidate in [0.6, 0.7, 0.8, 0.85, 0.9, 1.0]:
        settle()
        plunge = client.call("vpx_sim", {"action": "plunge", "pull": candidate, "holdMs": 400, "ms": 4000, "sampleMs": 200})
        balls = plunge["balls"]
        hits = [e["part"] for e in plunge["events"] if e["event"] == "Hit" and not e["part"].lower().startswith(("trigger", "sw", "startball", "stopball"))]
        drained = any(e["part"].lower().startswith("drain") and e["event"] == "Hit" for e in plunge["events"])
        back_in_lane = any(in_lane(b) for b in balls)
        verdict = "drained" if drained else "fell back into the lane" if back_in_lane else "in play"
        print(f"calibration pull {candidate}: {verdict}, hit {hits[:4]}")
        if verdict == "in play":
            pull = candidate
            break
    print(f"skill plunge: pull {pull}")

    for ball_no in range(1, args.balls + 1):
        settled = settle()
        if not any(in_lane(b) for b in settled["balls"] if not b.get("destroyed")):
            print(f"ball {ball_no}: already in play")
        else:
            plunge = client.call("vpx_sim", {"action": "plunge", "pull": pull, "holdMs": 400, "ms": 2500, "sampleMs": 200})
            print(f"ball {ball_no}: plunged with pull {pull}, {len(plunge['events'])} events, balls at {[(round(b['end']['x']), round(b['end']['y'])) for b in plunge['balls'] if 'end' in b]}")
        turns = 0
        while turns < args.turns:
            turns += 1
            trace = client.call("vpx_sim", {"action": "trace", "ms": 8000, "sampleMs": 200,
                                            "until": {"approachingFlipper": True, "speedBelow": 0.3, "settleMs": 400, "event": "Drain", "ballLost": True}})
            alive = [b for b in trace["balls"] if not b.get("destroyed")]
            drained = any(e["part"].lower().startswith("drain") and e["event"] == "Hit" for e in trace["events"])
            print(f"  turn {turns}: {trace['stoppedBy']}, {len(trace['events'])} events, balls {[(b['id'], round(b['end']['x']), round(b['end']['y'])) for b in alive if 'end' in b]}")
            if drained or trace["stoppedBy"] == "ballLost" or not alive:
                print(f"  ball {ball_no} lost after {turns} turns")
                break
            if trace["stoppedBy"] == "approachingFlipper":
                a = trace["approach"]
                flip = client.call("vpx_sim", {"action": "flip", "ballId": a["ballId"], "side": a["side"], "flipWhenAlong": 0.55, "flipAtMs": 1500, "holdMs": 300, "ms": 3000, "sampleMs": 200})
                at = flip.get("atFlip", {})
                ex = flip.get("exit", {})
                print(f"  flip {a['side']} ({a['flipper']}) at along {at.get('along', 0) or 0:.2f} t {flip.get('flipAtMs')}: exit {ex.get('angle', 0):.0f} deg at {ex.get('speed', 0):.1f}, {flip['outcome']}, hit {flip.get('hitAfterFlip', [])[:4]}")
                if flip.get("drained"):
                    print(f"  ball {ball_no} drained after {turns} turns")
                    break
                continue
            if trace["stoppedBy"] == "timeout":
                continue
            ball = max(alive, key=lambda b: b["end"]["y"])
            if in_lane(ball):
                plunge = client.call("vpx_sim", {"action": "plunge", "pull": pull, "holdMs": 400, "ms": 2500, "sampleMs": 200})
                print(f"  ball waiting in the plunger lane, plunged with pull {pull}: {len(plunge['events'])} events")
                continue
            if trace["stoppedBy"] == "settled":
                nudge = "RightNudge" if ball["end"]["x"] < width / 2 else "LeftNudge"
                client.call("vpx_input", {"action": nudge, "holdMs": 80})
                print(f"  ball settled at ({ball['end']['x']:.0f},{ball['end']['y']:.0f}), nudged ({nudge})")
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
