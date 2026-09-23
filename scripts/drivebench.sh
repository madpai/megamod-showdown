#!/bin/sh
# How well bots drive, averaged over several matches: eight 5-minute bot
# games with live vehicles, one per seed, run side by side. One match is
# chaos -- a tiny change sends every bot somewhere else -- so judge a
# change to driving on this, not on a single htamatch run.
#
#   HTA_MAP=.../bloodgulch.map scripts/drivebench.sh [team|ctf|ffa] [seeds]
#
# Prints each match's kills and driving line, then the averages. "blocked"
# is the share of time at a wheel the vehicle physics spent refusing a move.
set -eu
cd "$(dirname "$0")/.."
: "${HTA_MAP:?set HTA_MAP to bloodgulch.map}"
mode=${1:-team}
seeds=${2:-8}
out=$(mktemp)
trap 'rm -f "$out"' EXIT
s=1
while [ "$s" -le "$seeds" ]; do
    ( ./build-host/htamatch "$HTA_MAP" --bots 8 --mode "$mode" --seconds 300 \
          --shots 0 --vehicles --seed "$s" 2>/dev/null |
      grep -E "^(simulated|driving)" | tr '\n' ' '; echo ) >> "$out" &
    s=$((s + 1))
done
wait
cat "$out"
awk '{ gsub(",", "")
       for (i = 1; i <= NF; i++) {
           if ($i == "kills") k += $(i-1)
           if ($i == "entries") e += $(i-1)
           if ($i == "at") w += $(i-2)
           if ($i == "blocked") b += $(i-2)
       }
       n++ }
     END { if (n && w > 0)
             printf "%s, %d matches: %.1f kills, %.1f entries, %.0f s at a wheel, %.0f s blocked (%.1f%%)\n",
                    "'"$mode"'", n, k/n, e/n, w/n, b/n, 100*b/w }' "$out"
