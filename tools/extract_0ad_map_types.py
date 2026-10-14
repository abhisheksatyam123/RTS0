#!/usr/bin/env python3
"""Map types and scenario settings for Game Setup, from 0 A.D. 0.28.

0 A.D.'s Map Type dropdown (gui/common/settings.js loadMapTypes) lists
maps/skirmishes, maps/random and maps/scenarios separately, and a scenario
locks the settings its ScriptSettings carry (gui/gamesetup: VictoryConditions,
LockTeams, RevealMap, ExploreMap, PlayerData civs, teams and resources ...).
This writes data/map_types.json: for every ported map in data/maps/ its
type ("skirmish" or "scenario"), its Keywords (for the Map Filter), and for a
scenario the settings Game Setup shows locked. Random maps (RTS0's RMS list
in scenes/menu/gamesetup.gd) get their keywords from maps/random/<script>.json.

  python3 tools/extract_0ad_map_types.py <0ad public dir> [--check]

<0ad public dir> holds maps/ (unzipped from 0.28's public.zip).
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "data", "map_types.json")
# The maps 0 A.D. keeps in maps/tutorials (Learn to Play), not in Game Setup.
TUTORIALS = {"introductory_tutorial", "starting_economy_walkthrough"}
SETTING_KEYS = ["VictoryConditions", "LockTeams", "LastManStanding", "RevealMap", "ExploreMap", "AllyView",
                "DisableTreasures", "DisableSpies", "PopulationCap", "WorldPopulationCap",
                "StartingResources", "Ceasefire", "Nomad"]


def script_settings(path):
    text = open(path, encoding="utf-8").read()
    m = re.search(r"<ScriptSettings><!\[CDATA\[(.*?)\]\]></ScriptSettings>", text, re.S)
    return json.loads(m.group(1)) if m else {}


def player_count(name):
    m = re.search(r"\((\d+)", name)
    return int(m.group(1)) if m else None


def build(public):
    scen = {os.path.basename(p)[:-4]: p for p in glob.glob(os.path.join(public, "maps/scenarios/*.xml"))}
    skirm = {}
    for p in glob.glob(os.path.join(public, "maps/skirmishes/*.xml")):
        base = os.path.basename(p)[:-4]
        m = re.match(r"(.*?)_(\d+)p(?:v\d+p)?$", base)
        stem, n = (m.group(1), int(m.group(2))) if m else (base, None)
        skirm.setdefault(stem, []).append((n, p))
    maps = {}
    for f in sorted(glob.glob(os.path.join(ROOT, "data/maps/*.json"))):
        mid = os.path.basename(f)[:-5]
        if mid in TUTORIALS:
            continue
        rts0 = json.load(open(f, encoding="utf-8"))
        n = player_count(rts0.get("name", "")) or len(rts0.get("players", []))
        entry = None
        # A "(N players)" name is the skirmish of that size; scenarios have bare names.
        if mid in skirm and (mid not in scen or player_count(rts0.get("name", ""))):
            cands = sorted(skirm[mid], key=lambda c: (c[0] != n, c[0] or 0))
            s = script_settings(cands[0][1])
            entry = {"type": "skirmish", "keywords": s.get("Keywords", [])}
        elif mid in scen or mid.endswith("_scenario") and mid[:-9] in scen:
            # <name>_scenario: the scenario version of a skirmish map that keeps the plain id.
            s = script_settings(scen.get(mid) or scen[mid[:-9]])
            entry = {"type": "scenario", "keywords": s.get("Keywords", [])}
            settings = {k: s[k] for k in SETTING_KEYS if k in s}
            players = []
            for pd in s.get("PlayerData", []):
                pd = pd or {}
                row = {"civ": pd.get("Civ", ""), "team": pd.get("Team", -1)}
                if "Resources" in pd:
                    row["resources"] = pd["Resources"]
                if "PopulationLimit" in pd:
                    row["pop"] = pd["PopulationLimit"]
                players.append(row)
            settings["players"] = players
            entry["settings"] = settings
        else:
            print("no 0.28 map for %s" % mid, file=sys.stderr)
            continue
        maps[mid] = entry
    random = {}
    for p in sorted(glob.glob(os.path.join(public, "maps/random/*.json"))):
        s = json.load(open(p, encoding="utf-8")).get("settings", {})
        random[os.path.basename(p)[:-5]] = s.get("Keywords", [])
    return {"maps": maps, "random_keywords": random}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    text = json.dumps(build(args[0]), indent="\t", sort_keys=True, ensure_ascii=False) + "\n"
    if "--check" in sys.argv:
        old = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if old != text:
            print("%s is out of date" % OUT, file=sys.stderr)
            return 1
        return 0
    open(OUT, "w", encoding="utf-8").write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
