#!/usr/bin/env python3
"""Generate RTS0's per-player entity limits from 0 A.D.'s EntityLimits data.

0 A.D. caps how many entities of a category each player may have (EntityLimits.js):
the player template lists the limits, the classes whose entities raise them and
the requirements that lift them; units and structures name their category in
TrainingRestrictions/Category and BuildRestrictions/Category, and a unit's
TrainingRestrictions/MatchLimit caps how often that template may ever be trained.

Reads, under binaries/data/mods/public/simulation/templates:
  * template_player.xml and special/players/<civ>.xml (EntityLimits)
  * units/<civ>/*.xml for every roster row (data/civs/rosters.json), the generic unit
    templates (extract_0ad_rosters.GENERIC) and the structure templates of every
    RTS0 BuildingType (extract_0ad_attack.BUILDINGS and civ_buildings())

Usage:
  python3 -I tools/extract_0ad_entity_limits.py --0ad PATH/TO/0ad [--check]

Writes src/simulation/entity_limits_data.h. --check fails if the checked-in file differs.
"""

import argparse
import importlib.util
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_HEADER = os.path.join(REPO, "src", "simulation", "entity_limits_data.h")
ROSTERS = os.path.join(REPO, "data", "civs", "rosters.json")


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(REPO, "tools", name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ex = _load("extract_0ad_rosters")
atk = _load("extract_0ad_attack")

# LimitRemovers/RequiredTechs that are phases: RTS0 keeps the phase on the player
# (a Game Setup starting phase reaches it without the tech).
PHASE_TECHS = {"phase_town": 2, "phase_city": 3}
# LimitRemovers/RequiredClasses RTS0 can count: roster flag to test.
REMOVER_CLASSES = {"Hero": "ROSTER_HERO"}


def entity_limits(node):
    """(limits, changers, removers) of a player template's EntityLimits."""
    el = node.get("EntityLimits")
    limits, changers, removers = {}, {}, {}
    if el is None:
        return limits, changers, removers
    for cat, n in (el.get("Limits").children.items() if el.get("Limits") is not None else []):
        limits[cat] = int(n.text.strip())
    for cat, n in (el.get("LimitChangers").children.items() if el.get("LimitChangers") is not None else []):
        changers[cat] = {cls: int(c.text.strip()) for cls, c in n.children.items()}
    for cat, n in (el.get("LimitRemovers").children.items() if el.get("LimitRemovers") is not None else []):
        removers[cat] = {k: (c.text or "").split() for k, c in n.children.items()}
    return limits, changers, removers


def category(node, structure):
    return node.value("BuildRestrictions/Category" if structure else "TrainingRestrictions/Category")


def collect(sim_root):
    loader = ex.TemplateLoader(sim_root)
    rosters = json.load(open(ROSTERS))
    civs = ex.CIVS

    players = [entity_limits(loader.load("special/players/" + c)) for c in civs]
    cats = sorted({cat for lim, _, _ in players for cat in lim})

    units = []  # (id, civ index, category, match limit, classes)
    for u in rosters["units"]:
        name = "units/" + u["id"]
        if not loader.exists(name):
            name = ex.GENERIC[u["class"]]  # RTS0-only rows (the male villager)
        t = loader.load(name)
        # A map-only row without a civ (extract_0ad_rosters.MAP_ROWS: Samnites, pirate
        # ships) is in no civ's per-class changer rows (-1).
        units.append((u["id"], civs.index(u["civ"]) if u["civ"] in civs else -1, category(t, False),
                      int(t.value("TrainingRestrictions/MatchLimit") or 0), ex.classes_of(t)))
    generic = [(cls, category(ex.generic_template(loader, cls), False)) for cls in atk.UNIT_TYPES]
    buildings = []  # (enum, category, classes)
    for bld, name in atk.BUILDINGS + atk.civ_buildings():
        t = loader.load(name)
        buildings.append((bld, category(t, True), ex.classes_of(t)))

    techs = ex.tech_enums()
    rows, changer_rows, remover_rows = [], [], []
    for ci, (lim, chg, rem) in enumerate(players):
        rows.append([lim.get(cat, -1) for cat in cats])
        for cat, by_class in sorted(chg.items()):
            if cat not in cats:
                continue
            for cls, value in sorted(by_class.items()):
                # The entities of this civ with the class (EntityLimits.js counts
                # every owned entity whose Identity has it, foundations excluded).
                for bi, (bld, _, classes) in enumerate(buildings):
                    if cls in classes:
                        changer_rows.append((ci, cat, bld, None, value))
                for ri, (uid, uciv, _, _, classes) in enumerate(units):
                    if uciv == ci and cls in classes:
                        changer_rows.append((ci, cat, None, ri, value))
        for cat, req in sorted(rem.items()):
            phase, tech = 0, "TECH_NONE"
            for name in req.get("RequiredTechs", []):
                if name in PHASE_TECHS:
                    phase = max(phase, PHASE_TECHS[name])
                elif name in techs:
                    tech = techs[name]
                else:
                    sys.exit("LimitRemovers %s: technology %s is not in tech_data.h" % (cat, name))
            flags = []
            for cls in req.get("RequiredClasses", []):
                if cls not in REMOVER_CLASSES:
                    sys.exit("LimitRemovers %s: class %s is not ported" % (cat, cls))
                flags.append(REMOVER_CLASSES[cls])
            remover_rows.append((ci, cat, phase, tech, " | ".join(flags) or "0"))
    return cats, rows, changer_rows, remover_rows, units, generic, buildings


def enum_of(cat):
    out = ""
    for i, ch in enumerate(cat):
        if ch.isupper() and i and not cat[i - 1].isupper():
            out += "_"
        out += ch.upper()
    return "LIMIT_" + out


def emit(cats, rows, changers, removers, units, generic, buildings):
    def ce(cat):
        return enum_of(cat) if cat in cats else "LIMIT_NONE"

    out = []
    w = out.append
    w("// Generated by tools/extract_0ad_entity_limits.py from 0 A.D.'s template_player.xml,")
    w("// special/players/*.xml (EntityLimits) and the TrainingRestrictions/BuildRestrictions of")
    w("// RTS0's unit and structure templates. Do not edit by hand: change the tool and regenerate.")
    w("// 0 A.D. data is CC BY-SA 3.0, see docs/0AD_ATTRIBUTION.md.")
    w("#ifndef RTSO_ENTITY_LIMITS_DATA_H")
    w("#define RTSO_ENTITY_LIMITS_DATA_H")
    w("")
    w("#include <cstdint>")
    w("")
    w('#include "sim_types.h"')
    w("")
    w("namespace rtso {")
    w("")
    w("// Categories some player template limits. Every other category (Tower, Dock, ...) is")
    w("// unlimited, so its entities count as LIMIT_NONE.")
    w("enum LimitCategory : uint8_t {")
    w("\tLIMIT_NONE = 0,")
    for i, cat in enumerate(cats):
        w(f"\t{enum_of(cat)} = {i + 1},")
    w(f"\tLIMIT_CATEGORY_COUNT = {len(cats) + 1},")
    w("};")
    w("")
    w("inline constexpr const char *LIMIT_CATEGORY_NAMES[LIMIT_CATEGORY_COUNT] = {")
    w('\t"", ' + ", ".join(f'"{c}"' for c in cats) + ",")
    w("};")
    w("")
    w("// EntityLimits/Limits per civ (template_player.xml plus special/players/<civ>.xml); -1 = none.")
    w("inline constexpr int16_t CIV_ENTITY_LIMITS[CIV_COUNT][LIMIT_CATEGORY_COUNT] = {")
    by_civ = dict(zip(ex.CIVS, rows))
    for civ in ex.CIV_SLOTS:  # CIV_GAIA's row has no limits
        row = by_civ[civ] if civ else [-1] * len(rows[0])
        w(f"\t{{-1, {', '.join(str(v) for v in row)}}}, // {ex.CIV_ENUM_OF[civ] if civ else 'CIV_GAIA'}")
    w("};")
    w("")
    w("// EntityLimits/LimitChangers: each finished entity of `civ` that is building `bld` (or")
    w("// roster row `tmpl`) adds `value` to the category's limit.")
    w("struct LimitChanger {")
    w("\tuint8_t civ;")
    w("\tuint8_t category;")
    w("\tuint8_t bld;   // BLD_TYPE_COUNT when the changer is a unit")
    w("\tuint16_t tmpl; // CIV_UNIT_TEMPLATE_NONE when it is a building")
    w("\tint16_t value;")
    w("};")
    w("")
    w("inline constexpr LimitChanger LIMIT_CHANGERS[] = {")
    for ci, cat, bld, ri, value in changers:
        b = bld or "BLD_TYPE_COUNT"
        t = str(ri) if ri is not None else "CIV_UNIT_TEMPLATE_NONE"
        note = f" // {units[ri][0]}" if ri is not None else ""
        w(f"\t{{{ex.CIV_ENUM[ci]}, {enum_of(cat)}, {b}, {t}, {value}}},{note}")
    w("};")
    w("")
    w("// EntityLimits/LimitRemovers: the category's limit is lifted while the player is in")
    w("// `phase` (RequiredTechs phase_town/phase_city), has `tech`, and owns an entity of every")
    w("// RequiredClasses class (roster `flags`).")
    w("struct LimitRemover {")
    w("\tuint8_t civ;")
    w("\tuint8_t category;")
    w("\tuint8_t phase;")
    w("\tuint8_t tech;")
    w("\tuint32_t flags;")
    w("};")
    w("")
    w("inline constexpr LimitRemover LIMIT_REMOVERS[] = {")
    for ci, cat, phase, tech, flags in removers:
        w(f"\t{{{ex.CIV_ENUM[ci]}, {enum_of(cat)}, {phase}, {tech}, {flags}}},")
    w("};")
    w("")
    w("// TrainingRestrictions per roster row (civ_roster_data.h order): category and")
    w("// MatchLimit (how often the template may be trained in a match; 0 = no limit).")
    w("struct UnitLimit {")
    w("\tuint8_t category;")
    w("\tuint8_t match_limit;")
    w("};")
    w("")
    w("inline constexpr UnitLimit UNIT_LIMITS[] = {")
    for uid, _, cat, match, _ in units:
        w(f"\t{{{ce(cat)}, {match}}}, // {uid}")
    w("};")
    w("static_assert(sizeof(UNIT_LIMITS) / sizeof(UNIT_LIMITS[0]) == CIV_UNIT_TEMPLATE_COUNT,")
    w('\t\t"one UnitLimit per roster row: rerun tools/extract_0ad_entity_limits.py");')
    w("")
    w("// Units without a roster template, by UnitType (extract_0ad_rosters.py GENERIC).")
    w("inline constexpr uint8_t UNIT_TYPE_LIMIT_CATEGORY[UNIT_TYPE_COUNT] = {")
    for cls, cat in generic:
        w(f"\t{ce(cat)}, // {cls}")
    w("};")
    w("")
    w("// BuildRestrictions/Category per BuildingType.")
    w("inline constexpr uint8_t BUILDING_LIMIT_CATEGORY[BLD_TYPE_COUNT] = {")
    for bld, cat, _ in buildings:
        w(f"\t{ce(cat)}, // {bld}")
    w("};")
    w("")
    w("} // namespace rtso")
    w("")
    w("#endif // RTSO_ENTITY_LIMITS_DATA_H")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zeroad", required=True)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    sim_root = os.path.join(a.zeroad, "binaries", "data", "mods", "public", "simulation")
    text = emit(*collect(sim_root))
    if a.check:
        old = open(OUT_HEADER).read() if os.path.exists(OUT_HEADER) else ""
        if old != text:
            print(f"{OUT_HEADER} is out of date; rerun tools/extract_0ad_entity_limits.py")
            return 1
        print("entity_limits_data.h is up to date")
        return 0
    with open(OUT_HEADER, "w") as fh:
        fh.write(text)
    print(f"wrote {OUT_HEADER}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
