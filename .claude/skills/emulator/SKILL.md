---
name: emulator
description: Run the real Android APK in the desktop's Android emulator and drive it like the owner would -- menus, matches, hosting a LAN game the desktop joins -- instead of asking the owner for a phone check. Use for every functional phone check; the owner's phone is for feel and performance only.
---

# Test the APK in the Android emulator

The owner asked (2026-09-26) that agents run the phone checks themselves.
The emulator runs the same APK -- Java HUD, menus, JNI, Vulkan through the
desktop's GPU (RTX 3060 Ti), AAudio -- on AVD `megamod` (Android 14,
x86_64, 2400x1080 landscape, 6 GB). The emulator APK carries x86_64 beside
arm64 (`publish_apk.sh --emulator-apk`); the owner's build stays arm64.

## Start
```sh
scripts/emu/start.sh            # boots the AVD headless, builds + installs the emulator APK
scripts/emu/start.sh --no-build # reinstall the last one
```

## Drive: scripts/emu/emu.py
Commands run in order; screenshots land in `scratch/emulator/NAME.png`
(and `NAME_s.png`, half size: Read that one).
```sh
E=scripts/emu/emu.py
$E stop start wait:8 tap:1870,310 wait:1.5 tap:1870,310 wait:2 shot:menu
$E clear tap:1670,794 "until:\[game\] (Slayer|CTF|Team),60" wait:2 tap:696,978 wait:5 shot:game
$E "log:killed|\[world\]|\[net\]"
```
- **Taps are 150 ms presses** (`input swipe x y x y 150`): the game's menu
  reads touches through one mailbox and misses an instant `input tap`.
- **Title menu: the first tap selects, the second opens.** SINGLEPLAYER
  (1870,310), MULTIPLAYER (1870,400).
- Singleplayer setup: MAP (686,326) and GAME (686,443) cycle on each tap
  (maps in list order: Blood Gulch, cs_compound, cs_office, cs_office_lit,
  ctf_2fort, de_aztec, de_dust2, gm_construct; games Slayer, Team Slayer,
  CTF); START GAME (1670,794). Create game (multiplayer): MAP (686,518),
  START GAME (1670,820).
- Character screen: SPAWN (696,978). Team screen: RED (696,244), BLUE (696,336).
- `until:REGEX,SECONDS` waits for a log line: use it, not fixed sleeps, for loads.
- Logs: `$E "log:REGEX"` greps the game's logcat (`[game]`, `[world]`,
  `[net]`, `[perf]`...). SEND REPORT from the emulator posts to the sideload
  server like the phone.

## Hosting in the emulator, the desktop joining
The emulator's UDP port forward answers from a random source port, which
the game's client drops (it only accepts the server's address). Put the
relay in between and join the relay:
```sh
python3 scripts/emu/udprelay.py 32271 32270 &      # (start.sh already forwarded 32270)
build-host/megamod-join 127.0.0.1 32271 --world de_dust2 --auto 60
```
On the emulator's log: `[net] player 2 joined game unit N`, then
`[net] id 1 remote 2 ... invalid 0`.

## What it will not tell you
Adreno/Mali GPU behaviour and frame times, thermal throttling, touch
feel, real Wi-Fi. Frame numbers from the emulator are the desktop GPU's.
Ask the owner for those, not for functional checks.
