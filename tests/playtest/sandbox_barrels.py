#!/usr/bin/env python3
"""Playtest: the sandbox, driven like an agent drives it (docs/DESKTOP_AGENT.md).

Starts megamod-sandbox headless with the control channel, walks it through a
scene -- step, aim at the barrels, throw a grenade, wait -- and checks what
the world says happened, through the same state / report / event log an
agent reads. Exit 0 on pass; prints what failed otherwise.

    tests/playtest/sandbox_barrels.py build-host/megamod-sandbox [--render]
"""
import json, math, os, re, socket, subprocess, sys, tempfile, time

def main():
    exe = sys.argv[1]
    render = "--render" in sys.argv
    tmp = tempfile.mkdtemp(prefix="hta_playtest_")
    events, report = os.path.join(tmp, "ev.jsonl"), os.path.join(tmp, "report.json")
    args = [exe, "--headless" if render else "--no-render", "--size", "320x180", "--preset", "low",
            "--control", "0", "--events", events, "--report", report]
    p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    port = None
    for _ in range(100):
        line = p.stdout.readline()
        m = re.search(r"control on 127\.0\.0\.1:(\d+)", line or "")
        if m: port = int(m.group(1)); break
    if not port:
        p.kill(); print("FAIL: no control port"); return 1
    s = socket.create_connection(("127.0.0.1", port), timeout=60)
    f = s.makefile("rw")
    def ask(o):
        f.write(json.dumps(o) + "\n"); f.flush()
        r = json.loads(f.readline())
        if not r.get("ok"): raise SystemExit("FAIL: %s -> %s" % (o, r))
        return r
    fails = []
    def check(cond, what):
        print(("  ok   " if cond else "  FAIL ") + what)
        if not cond: fails.append(what)

    st = ask({"cmd": "state"})
    check(st["paused"] and st["frame"] == 0, "starts paused at frame 0")
    r = ask({"cmd": "step", "frames": 30})
    check(r["frame"] == 30, "step 30 replies after 30 frames")
    st = ask({"cmd": "state"})
    check(st["on_ground"], "the player stands on the floor")
    p0 = st["pos"]
    dx, dy, dz = 6.2 - p0[0], -8.0 - p0[1], 0.4 - (p0[2] + 0.6)
    ask({"cmd": "input", "yaw": math.atan2(dy, dx), "pitch": math.atan2(dz, math.hypot(dx, dy)) + 0.2,
         "grenade": True, "frames": 1})
    ask({"cmd": "step", "frames": 240})
    st = ask({"cmd": "state"})
    check(st["props_broken"] >= 1, "a grenade at the barrels breaks props (%d)" % st["props_broken"])
    ask({"cmd": "input", "forward": 1, "frames": 60})
    st2 = ask({"cmd": "state"})
    moved = math.dist(st["pos"][:2], st2["pos"][:2])
    check(moved > 0.5, "input forward 1 for 60 frames walks (%.2f world units, ~3 m each)" % moved)
    rep = ask({"cmd": "report"})
    check(rep["totals"]["grenades"] == 1 and rep["totals"]["explosions"] >= 1, "report counts the throw and blasts")
    ask({"cmd": "quit"})
    p.wait(timeout=30)
    check(p.returncode == 0, "exits cleanly")

    evs = [json.loads(l) for l in open(events)]
    kinds = [e["ev"] for e in evs]
    check(kinds[0] == "run_start" and kinds[-1] == "run_end", "event log opens and closes the run")
    check("explosion" in kinds and "prop_broken" in kinds, "event log has the explosion and the break")
    check(all(evs[i]["frame"] <= evs[i + 1]["frame"] for i in range(len(evs) - 1)), "events in frame order")
    rj = json.load(open(report))
    check(rj["kind"] == "megamod-sandbox-report" and rj["frames_run"] == 331, "report file written (%d frames)" % rj["frames_run"])
    print("playtest sandbox_barrels: %s" % ("PASS" if not fails else "FAIL (%d)" % len(fails)))
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())
