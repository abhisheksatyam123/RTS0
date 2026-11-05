#!/bin/bash
# Re-test the three stuck-unit repros on a built soak_match. Run from the repo root:
#   tools/stall_repros.sh path/to/soak_match
# Each line prints whether the watched unit still stalls: [stuck] / [hits-nothing] / [capture-stall]
# lines for that unit id in the soak log. A fixed case prints none for its id.
set -u
SOAK=${1:?usage: stall_repros.sh path/to/soak_match}
OUT=${OUT:-$(mktemp -d)}
run() { # name unit_id watch_from soak args...
	local name=$1 uid=$2 from=$3; shift 3
	timeout 1500 "$SOAK" "$@" --twin --watch "$uid" --watch-from "$from" > "$OUT/$name.log" 2>&1
	local hits; hits=$(grep -cE "^\[(stuck|hits-nothing|capture-stall)\].* id=$uid " "$OUT/$name.log")
	echo "$name unit=$uid stalls=$hits (log: $OUT/$name.log)"
}
# Siege Oxybeles 7002: packed, no path, 129 units from its target (map 5, seed 100).
run siege7002 7002 1850 --map 5 --players 2 --civs 0,1 --seed 100 --size 256 --minutes 40
# Cavalry javelineer 2210: attack-move jitter by trees (crocodilopolis, seed 3).
run croc2210 2210 650 --json crocodilopolis --civs 3,4,5,6 --seed 3 --size 256 --minutes 25
# Infantry spearman 3988: in range of a structure with no damage (Formations' G96 bisect, map 15 seed 100).
run spear3988 3988 1650 --map 15 --players 2 --civs 0,1 --seed 100 --size 256 --minutes 40
