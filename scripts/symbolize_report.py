#!/usr/bin/env python3
"""Read a SEND REPORT (or crash report) for an agent: a summary, and the
native crash's frames as functions and source lines.

    scripts/symbolize_report.py <report.json> [--so libhta_native.so] [--symbols DIR]

The crash record carries offsets into libhta_native.so ("lib+0x..."). The
library that shipped in that build (publish_apk.sh keeps it as
<sideload root>/symbols/libhta_native-<commit>.so) turns them into
function:file:line with llvm-addr2line from the NDK.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


def addr2line() -> str | None:
    found = shutil.which("llvm-addr2line")
    if found:
        return found
    home = os.environ.get("ANDROID_HOME", os.path.expanduser("~/android/sdk"))
    cands = sorted(glob.glob(f"{home}/ndk/*/toolchains/llvm/prebuilt/*/bin/llvm-addr2line"))
    return cands[-1] if cands else None


def crash_offsets(text: str) -> list[int]:
    """lib+0x offsets, the crashing pc first (pc - lib_base), in order."""
    offs = []
    base = re.search(r"^lib_base 0x([0-9a-f]+)", text, re.M)
    pc = re.search(r"^pc 0x([0-9a-f]+)", text, re.M)
    if base and pc and int(base.group(1), 16):
        d = int(pc.group(1), 16) - int(base.group(1), 16)
        if 0 <= d < 1 << 32:
            offs.append(d)
    for m in re.finditer(r"lib\+0x([0-9a-f]+)", text):
        v = int(m.group(1), 16)
        if v not in offs:
            offs.append(v)
    return offs


def symbolize(so: str, offsets: list[int]) -> list[str]:
    tool = addr2line()
    if not tool or not offsets:
        return [f"0x{o:x}" for o in offsets]
    # The return addresses on the stack point after the call: step back one.
    out = subprocess.run([tool, "-f", "-C", "-i", "-e", so] + [hex(o if i == 0 else o - 1) for i, o in enumerate(offsets)],
                         capture_output=True, text=True).stdout.splitlines()
    lines, i = [], 0
    while i + 1 < len(out):
        lines.append(f"{out[i]}  {out[i + 1]}")
        i += 2
    return lines


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("report")
    ap.add_argument("--so", help="the build's unstripped libhta_native.so")
    ap.add_argument("--symbols", help="a folder of libhta_native-<commit>.so (publish_apk.sh keeps them)")
    a = ap.parse_args()
    r = json.loads(Path(a.report).read_text())
    b, d, n = r.get("build", {}), r.get("device", {}), r.get("native", {})
    print(f"{r.get('reason', '?')} report, build {b.get('version')} ({'personal' if b.get('personal') else 'guest'}), {r.get('time')}")
    print(f"device: {d.get('manufacturer')} {d.get('model')} ({d.get('soc')}), Android {d.get('android')}, "
          f"thermal {d.get('thermal')}, RAM free {d.get('ram_avail_mb')}/{d.get('ram_total_mb')} MB")
    v, f = n.get("video", {}), n.get("frames", {})
    if v:
        print(f"video: {v.get('preset')} on {v.get('gpu')}, {'composed' if v.get('composed') else 'direct'}, "
              f"scale {v.get('render_scale')}, msaa {v.get('msaa')}")
    for k in ("session", "last_minute"):
        h = f.get(k)
        if h and h.get("count"):
            print(f"frames {k}: {h['fps_mean']:.1f} fps mean, p50 {h['p50_ms']} ms, p95 {h['p95_ms']} ms, "
                  f"p99 {h['p99_ms']} ms, max {h['max_ms']:.0f} ms, {h['hitches_over_50ms']} hitches")
    hs = f.get("hitches") or []
    if hs:
        print(f"hitches ({f.get('match_s', 0):.0f} s into the match): "
              + ", ".join(f"{x['ms']:.0f} ms at {x['at_s']:.1f} s" for x in hs))
    m = n.get("match", {})
    if m:
        print(f"match: {m.get('map')} mode {m.get('mode')}, {m.get('units')} units, phase {n.get('phase')}")
    crash = r.get("crash", {})
    if crash.get("java"):
        print("\njava crash:\n" + crash["java"][:4000])
    if crash.get("native"):
        text = crash["native"]
        print("\nnative crash:\n" + "\n".join(l for l in text.splitlines() if not l.startswith("  ")))
        so = a.so
        if not so and a.symbols:
            commit = str(b.get("version", "")).split("-", 1)[-1]
            cand = Path(a.symbols) / f"libhta_native-{commit}.so"
            so = str(cand) if cand.exists() else None
        offs = crash_offsets(text)
        if so:
            print(f"\nframes ({so}):")
            for i, line in enumerate(symbolize(so, offs)):
                print(f"  #{i:02d} {line}")
        else:
            print("\nframes (no symbols given; pass --so or --symbols):")
            for o in offs:
                print(f"  lib+0x{o:x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
