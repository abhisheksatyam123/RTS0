#!/usr/bin/env python3
"""Writes data/reference/templates.json: the text 0 A.D.'s reference pages
(gui/reference/civinfo, structree and viewer) show for each entity and that
RTS0's simulation tables don't carry: Identity/Tooltip, Identity/History,
VisibleClasses and Builder/Entities for every roster unit and structure,
technology descriptions, and each civilization's History.

Numbers (health, costs, attacks, armour) are not copied here; the reference
pages read them from the game's own tables through RtsoReference, so they
always show what the simulation plays.

0 A.D. text is CC BY-SA 3.0, Wildfire Games (see docs/0AD_ATTRIBUTION.md).

Usage:
  python3 -I tools/extract_0ad_reference.py --0ad PATH/TO/0ad [--previous-0ad PATH] [--check]

--previous-0ad: roster rows and technologies RTS0 still has that the --0ad release
dropped (a retired roster row kept for saves, Flanking Maneuvers) keep that tree's text.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_rosters import (BUILDING_BITS, CIVS, UNIQUE_BUILDINGS,  # noqa: E402
                                 TemplateLoader)

import re  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "data", "reference", "templates.json")
ROSTERS = os.path.join(REPO, "data", "civs", "rosters.json")
TECH_DATA = os.path.join(REPO, "src", "simulation", "tech_data.h")

# 0 A.D. structure file stems for RTS0's generic BuildingTypes, first match wins
# (structures/<civ>/<stem>.xml, then the generic template_structure_* parent).
GENERIC_STEMS = {
    "BLD_TOWN_CENTER": (["civil_centre"], "template_structure_civic_civil_centre"),
    "BLD_HOUSE": (["house"], "template_structure_civic_house"),
    "BLD_BARRACKS": (["barracks"], "template_structure_military_barracks"),
    "BLD_STOREHOUSE": (["storehouse"], "template_structure_economic_storehouse"),
    "BLD_TOWER": (["defense_tower"], "template_structure_defensive_tower_stone"),
    "BLD_FARMSTEAD": (["farmstead"], "template_structure_economic_farmstead"),
    "BLD_MARKET": (["market"], "template_structure_economic_market"),
    "BLD_FORTRESS": (["fortress"], "template_structure_military_fortress"),
    "BLD_GATE": (["wall_gate"], "template_structure_defensive_wall_gate"),
    "BLD_TEMPLE": (["temple"], "template_structure_civic_temple"),
    "BLD_WONDER": (["wonder"], "template_structure_wonder"),
    "BLD_DOCK": (["dock"], "template_structure_military_dock"),
    "BLD_WALL_SEGMENT": (["wall_long"], "template_structure_defensive_wall_long"),
    "BLD_CORRAL": (["corral"], "template_structure_economic_corral"),
    "BLD_FIELD": (["field"], "template_structure_resource_field"),
    "BLD_STABLE": (["stable"], "template_structure_military_stable"),
    "BLD_RANGE": (["range"], "template_structure_military_range"),
    "BLD_FORGE": (["forge"], "template_structure_military_forge"),
    "BLD_ARSENAL": (["arsenal", "workshop"], "template_structure_military_arsenal"),
    "BLD_SENTRY_TOWER": (["sentry_tower"], "template_structure_defensive_tower_sentry"),
    "BLD_OUTPOST": (["outpost"], "template_structure_defensive_outpost"),
    "BLD_PALISADE": (["palisades_long"], "template_structure_defensive_palisade"),
    "BLD_WALL_TOWER": (["wall_tower"], "template_structure_defensive_wall_tower"),
    "BLD_ELEPHANT_STABLE": (["elephant_stable"], "template_structure_military_elephant_stable"),
    "BLD_THEATER": (["theater"], "template_structure_special_theater"),
    "BLD_EMBASSY": (["embassy_celtic"], "template_structure_military_embassy"),  # Iberian, Italic: own types
}
# A wall set builds its segments, towers and gates (0 A.D. WallSet).
WALLSETS = {
    "wallset_stone": ("BLD_WALL_SEGMENT", "BLD_WALL_TOWER", "BLD_GATE"),
    "wallset_palisade": ("BLD_PALISADE",),
}
STEMS = dict(GENERIC_STEMS)
for _enum, _stems, _model, _portrait in UNIQUE_BUILDINGS:
    STEMS[_enum] = (_stems, None)


def text_of(t, path):
    v = t.value(path, "")
    return " ".join(v.split()) if v else ""


def identity(t):
    return {
        "generic_name": text_of(t, "Identity/GenericName"),
        "specific_name": text_of(t, "Identity/SpecificName"),
        "tooltip": t.value("Identity/Tooltip", "").strip(),
        "history": text_of(t, "Identity/History"),
        "classes": t.tokens("Identity/VisibleClasses"),
    }


def structure_template(loader, civ, enum):
    stems, generic = STEMS[enum]
    for st in stems:
        for name in ("structures/%s/%s" % (civ, st), "structures/" + st):
            if loader.exists(name):
                return name
    return generic if generic and loader.exists(generic) else None


def build_list(loader, t, civ):
    """Builder/Entities mapped to RTS0 BuildingType ids (0 A.D. structures this unit builds)."""
    out = []
    for e in t.tokens("Builder/Entities"):
        e = e.replace("{civ}", civ).replace("{native}", civ)
        if not loader.exists(e):
            continue  # template_unit lists every civ's structures; only existing ones count
        stem = e.rsplit("/", 1)[-1]
        for i, enum in enumerate(BUILDING_BITS):
            if (stem in STEMS.get(enum, ([], None))[0] or enum in WALLSETS.get(stem, ())) and i not in out:
                out.append(i)
    return sorted(out)


def sim_root_of(zad):
    return os.path.join(zad, "binaries", "data", "mods", "public", "simulation")


def tech_text(path):
    with open(path, encoding="utf-8") as f:
        j = json.load(f)
    if "description" not in j and "tooltip" not in j:
        return None
    return {
        "tooltip": j.get("tooltip", ""),
        "description": j.get("description", ""),
        "requirements_tooltip": j.get("requirementsTooltip", ""),
    }


def collect(zad, prev=None):
    sim_root = sim_root_of(zad)
    loader = TemplateLoader(sim_root)
    prev_loader = TemplateLoader(sim_root_of(prev)) if prev else None
    with open(ROSTERS, encoding="utf-8") as f:
        rosters = json.load(f)
    units = {}
    for r in rosters["units"]:
        name = "units/" + r["id"]
        ld = loader
        if not ld.exists(name):
            ld = prev_loader  # a row the release dropped, kept for saves
            if ld is None or not ld.exists(name):
                continue  # RTS0-only rows (rts0_*) have no 0 A.D. template
        t = ld.load(name)
        d = identity(t)
        d["builds"] = build_list(ld, t, r["civ"])
        units[r["id"]] = d
    structures = {}
    for civ in CIVS:
        rows = {}
        for i, enum in enumerate(BUILDING_BITS):
            name = structure_template(loader, civ, enum)
            if name is None:
                continue
            d = identity(loader.load(name))
            d["template"] = name
            rows[str(i)] = d
        structures[civ] = rows
    techs = {}
    tdir = os.path.join(sim_root, "data", "technologies")
    for root, _dirs, files in os.walk(tdir):
        for fn in sorted(files):
            if not fn.endswith(".json"):
                continue
            rel = os.path.relpath(os.path.join(root, fn), tdir)[:-5].replace(os.sep, "/")
            d = tech_text(os.path.join(root, fn))
            if d is not None:
                techs[rel] = d
    if prev:
        # Technologies tech_data.h still plays that the release dropped.
        with open(TECH_DATA, encoding="utf-8") as f:
            ids = re.findall(r'"([a-z0-9_/]+)"\},\s*(?://.*)?$', f.read(), re.M)
        pdir = os.path.join(sim_root_of(prev), "data", "technologies")
        for tid in ids:
            p = os.path.join(pdir, tid + ".json")
            if tid not in techs and os.path.exists(p):
                d = tech_text(p)
                if d is not None:
                    techs[tid] = d
    civs = {}
    for civ in CIVS:
        p = loader.load("special/players/" + civ)
        civs[civ] = {"name": text_of(p, "Identity/GenericName"), "history": text_of(p, "Identity/History")}
    return {
        "generator": "tools/extract_0ad_reference.py",
        "license": "0 A.D. data, CC BY-SA 3.0, Wildfire Games",
        "civs": civs,
        "units": units,
        "structures": structures,
        "techs": dict(sorted(techs.items())),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zad", required=True, help="0 A.D. source checkout")
    ap.add_argument("--previous-0ad", dest="prev", help="older 0 A.D. tree for rows and techs the release dropped")
    ap.add_argument("--check", action="store_true", help="fail if the checked-in file differs")
    args = ap.parse_args()
    text = json.dumps(collect(args.zad, args.prev), indent=1, ensure_ascii=False) + "\n"
    if args.check:
        with open(OUT, encoding="utf-8") as f:
            if f.read() != text:
                sys.exit("data/reference/templates.json is stale; rerun tools/extract_0ad_reference.py")
        print("templates.json up to date")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    d = json.loads(text)
    print("wrote %s: %d units, %d civs of structures, %d techs" % (
        OUT, len(d["units"]), len(d["structures"]), len(d["techs"])))


if __name__ == "__main__":
    main()
