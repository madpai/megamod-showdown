#!/usr/bin/env python3
"""emu.py: drive the Android emulator running MEGAMOD.
   emu.py cmd [cmd ...]   cmds: tap:X,Y  wait:S  shot:NAME  log:REGEX  until:REGEX,SECONDS
                                 clear (logcat)  start  stop  back
   Coordinates are in the 2400x1080 landscape screen."""
import os, subprocess, sys, time, re
from PIL import Image
ADB = os.path.expanduser(os.environ.get("ADB", "~/android/sdk/platform-tools/adb"))
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scratch", "emulator")
os.makedirs(D, exist_ok=True)
def adb(*a, **k): return subprocess.run([ADB, *a], capture_output=True, **k)
for c in sys.argv[1:]:
    op, _, arg = c.partition(":")
    if op == "tap":
        x, y = arg.split(","); adb("shell", "input", "swipe", x, y, x, y, "150")
    elif op == "wait": time.sleep(float(arg))
    elif op == "shot":
        png = adb("exec-out", "screencap", "-p").stdout
        open(f"{D}/{arg}.png", "wb").write(png)
        im = Image.open(f"{D}/{arg}.png"); im.thumbnail((1200, 1200)); im.save(f"{D}/{arg}_s.png")
        print(f"{D}/{arg}_s.png")
    elif op == "log":
        out = adb("logcat", "-d", text=True).stdout
        for l in out.splitlines():
            if "halo-trial" in l and re.search(arg, l): print(l[19:200])
    elif op == "clear": adb("logcat", "-c")
    elif op == "start":
        adb("shell", "monkey", "-p", "net.megamod.showdown", "-c", "android.intent.category.LAUNCHER", "1")
    elif op == "stop": adb("shell", "am", "force-stop", "net.megamod.showdown")
    elif op == "back": adb("shell", "input", "keyevent", "KEYCODE_BACK")
    elif op == "until":   # until:REGEX,SECONDS -- wait for a log line
        rx, _, s = arg.rpartition(","); t0 = time.time()
        while time.time() - t0 < float(s):
            if any("halo-trial" in l and re.search(rx, l) for l in adb("logcat", "-d", text=True).stdout.splitlines()):
                print(f"until {rx}: {time.time()-t0:.0f}s"); break
            time.sleep(1)
        else: print(f"until {rx}: TIMEOUT")
