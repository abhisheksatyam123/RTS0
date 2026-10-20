#!/usr/bin/env python3
"""Map types and scenario settings for Game Setup, from 0 A.D. 0.28.

0 A.D.'s Map Type dropdown (gui/common/settings.js loadMapTypes) lists
maps/skirmishes, maps/random and maps/scenarios separately, and a scenario
locks the settings its ScriptSettings carry (gui/gamesetup: VictoryConditions,
LockTeams, RevealMap, ExploreMap, PlayerData civs, teams and resources ...).
This writes data/map_types.json: for every ported map in data/maps/ its
type ("skirmish" or "scenario"), its Keywords (for the Map Filter), and for a
scenario the settings Game Setup shows locked. Random maps (RTS0's RMS list
in scenes/menu/gamesetup.gd) get their keywords and their Player Placement
patterns (PlayerPlacements, in the map's order) from maps/random/<script>.json,
their Description (gui/gamesetup MapCache.getTranslatableMapDescription), and their biomes from its SupportedBiomes (gui/gamesetup's Biome dropdown
lists only those, and is hidden on maps without any): each entry is a 0.28
rmbiome id ("generic/aegean"), with a folder ("generic/") expanded to every
biome in it.

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
HEADER = os.path.join(ROOT, "src", "simulation", "map_biomes_data.h")
# 0.28 rmbiome id -> RTS0 BiomeType (sim_types.h). Biomes RTS0 lacks (the map folders such as
# alpine/ and fields_of_meroe/) have no entry yet.
RTS0_BIOMES = {"generic/aegean": "BIOME_AEGEAN", "generic/alpine": "BIOME_ALPINE", "generic/arctic": "BIOME_POLAR",
               "generic/sahara": "BIOME_OASIS", "generic/temperate": "BIOME_TEMPERATE", "generic/steppe": "BIOME_STEPPE",
               "generic/savanna": "BIOME_SAVANNA", "generic/india": "BIOME_INDIA",
               "generic/autumn": "BIOME_AUTUMN", "generic/nubia": "BIOME_NUBIA"}
# 0.28 script name -> RTS0 MapTypeInfo id where they differ.
RTS0_MAP_IDS = {"archipelago": "archipelago_0ad", "neareastern_badlands": "nearestern_badlands"}
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
        key = mid = os.path.basename(f)[:-5]
        if mid in TUTORIALS:
            continue
        rts0 = json.load(open(f, encoding="utf-8"))
        n = player_count(rts0.get("name", "")) or len(rts0.get("players", []))
        entry = None
        sized = re.match(r"(.*?)_(\d+)p(?:v\d+p)?$", mid)
        if mid not in skirm and sized and sized.group(1) in skirm:
            # <stem>_<N>p: one size of a skirmish 0.28 ships in several (Tarim Basin).
            mid, n = sized.group(1), int(sized.group(2))
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
        maps[key] = entry
    random, placements, biomes, descriptions = {}, {}, {}, {}
    for p in sorted(glob.glob(os.path.join(public, "maps/random/*.json"))):
        doc = json.load(open(p, encoding="utf-8"))
        if not isinstance(doc, dict):  # a heightmap image's <name>.png.json, not a map script
            continue
        s = doc.get("settings", {})
        name = os.path.basename(p)[:-5]
        random[name] = s.get("Keywords", [])
        if s.get("Description"):
            descriptions[name] = s["Description"]  # translated in the "map description" context
        if s.get("PlayerPlacements"):
            placements[name] = s["PlayerPlacements"]
        supported = s.get("SupportedBiomes", [])
        if supported:
            biomes[name] = expand_biomes(public, supported)
    return {"maps": maps, "random_keywords": random, "player_placements": placements, "random_biomes": biomes,
            "random_descriptions": descriptions}


def expand_biomes(public, supported):
    """rmbiome ids for a SupportedBiomes value (Biomes.js getSupportedBiomes: a string or a
    list; an entry ending in "/" is every biome in that maps/random/rmbiome folder)."""
    out = []
    for entry in [supported] if isinstance(supported, str) else supported:
        if entry.endswith("/") or entry == "generic":
            folder = entry.rstrip("/")
            names = sorted(os.path.basename(f)[:-5] for f in
                           glob.glob(os.path.join(public, "maps/random/rmbiome", folder, "*.json")))
            out += ["%s/%s" % (folder, n) for n in names]
        else:
            out.append(entry)
    return out


def header(biomes):
    """map_biomes_data.h: the RTS0 biomes each random map's SupportedBiomes allows."""
    src = open(os.path.join(ROOT, "src/simulation/sim_types.h"), encoding="utf-8").read()
    enum_of = dict((mid, enum) for enum, mid in re.findall(r'\{(MAP_[A-Z0-9_]+), "([a-z0-9_]+)", "[^"]*\(RMS\)"', src))
    rows = []
    for script, ids in sorted(biomes.items()):
        enum = enum_of.get(RTS0_MAP_IDS.get(script, script))
        bits = sorted({RTS0_BIOMES[b] for b in ids if b in RTS0_BIOMES})
        if enum and bits:
            rows.append("\t\tcase %s: return %s;" % (enum, " | ".join("1u << %s" % b for b in bits)))
    return ("// Generated by tools/extract_0ad_map_types.py from 0 A.D. 0.28's maps/random/*.json\n"
            "// SupportedBiomes. Do not edit by hand.\n#pragma once\n#include \"sim_types.h\"\n\n"
            "namespace rtso {\n\n"
            "// The BiomeType bits a random map's Game Setup Biome may pick (gui/gamesetup lists only the\n"
            "// script's SupportedBiomes); 0 for a map with none, which keeps its own biome.\n"
            "inline uint32_t map_supported_biomes(int type) {\n\tswitch (type) {\n" + "\n".join(rows) +
            "\n\t\tdefault: return 0;\n\t}\n}\n\n} // namespace rtso\n")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    data = build(args[0])
    outputs = {OUT: json.dumps(data, indent="\t", sort_keys=True, ensure_ascii=False) + "\n",
               HEADER: header(data["random_biomes"])}
    if "--check" in sys.argv:
        stale = [p for p, text in outputs.items()
                 if (open(p, encoding="utf-8").read() if os.path.exists(p) else "") != text]
        for p in stale:
            print("%s is out of date" % p, file=sys.stderr)
        return 1 if stale else 0
    for p, text in outputs.items():
        open(p, "w", encoding="utf-8").write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
