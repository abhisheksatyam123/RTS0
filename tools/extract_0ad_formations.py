#!/usr/bin/env python3
"""Generate RTS0's formation tables from 0 A.D.'s simulation templates.

Reads, from a checkout of https://github.com/0ad/0ad:
  * templates/special/formations/*.xml (over template_formation.xml): every
    Formation.js parameter (shape, separation multipliers, rows/columns,
    sorting, sloppiness, centre gap, required members, speed multiplier)
  * templates/template_player.xml and special/players/<civ>.xml: the
    formations each civ may use (Player.js GetFormations)
  * every civ roster unit's template chain (parents and mixins, CParamNode
    token merging): UnitAI/Formations (UnitAI.js CanUseFormation) and the
    Identity classes Formation.js sorts members by

Writes src/simulation/formation_data.h. The roster row order is read from
src/simulation/civ_roster_data.h so the per-row table lines up with it.

Usage:
  python3 -I tools/extract_0ad_formations.py --0ad PATH/TO/0ad [--check]
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import extract_0ad_rosters as roster  # noqa: E402  (same CParamNode loader)
import extract_0ad_attack as atk  # noqa: E402  (RTS0 UnitType order)

REPO = os.path.dirname(HERE)
OUT_HEADER = os.path.join(REPO, "src", "simulation", "formation_data.h")
ROSTER_HEADER = os.path.join(REPO, "src", "simulation", "civ_roster_data.h")

# RTS0 UnitFormation ids (sim_types.h). Ids 0-12 predate this port and stay put
# for saves and the lockstep formation command; 13-16 are new.
FORMATIONS = [
    ("FORMATION_NONE", "special/formations/null"),
    ("FORMATION_LINE", "special/formations/line_closed"),
    ("FORMATION_BOX", "special/formations/box"),
    ("FORMATION_COLUMN", "special/formations/column_closed"),
    ("FORMATION_PHALANX", "special/formations/phalanx"),
    ("FORMATION_WEDGE", "special/formations/wedge"),
    ("FORMATION_SKIRMISH", "special/formations/skirmish"),
    ("FORMATION_LINE_OPEN", "special/formations/line_open"),
    ("FORMATION_COLUMN_OPEN", "special/formations/column_open"),
    ("FORMATION_FLANK", "special/formations/flank"),
    ("FORMATION_BATTLE_LINE", "special/formations/battle_line"),
    ("FORMATION_SHIPS_LINE", "special/formations/ships/line_staggered"),
    ("FORMATION_SHIPS_COLUMN", "special/formations/ships/column"),
    ("FORMATION_SHIPS_FLANK", "special/formations/ships/flank"),
    ("FORMATION_SYNTAGMA", "special/formations/syntagma"),
    ("FORMATION_TESTUDO", "special/formations/testudo"),
    ("FORMATION_ANTI_CAVALRY", "special/formations/anti_cavalry"),
    ("FORMATION_SHIELD_WALL", "special/formations/shield_wall"),
]
FORMATION_INDEX = {path: i for i, (_, path) in enumerate(FORMATIONS)}

# Roster rows whose template 0.28 split up, read from the 0.28 template that took their
# place so they keep forming up (0.28 split rome/cavalry_auxiliary_b into a javelin and a
# spear auxiliary; the row is ranged, like extract_0ad_promotion.py's INTERIM_TARGETS).
STAND_INS = {
    "rome/cavalry_auxiliary_b": "rome/cavalry_javelineer_auxiliary_b",
}

# Classes Formation.js sorts by (template SortingClasses, plus the columnar
# order "Cavalry Infantry"). Bit i of a unit's class mask = CLASSES[i].
CLASSES = ["Hero", "Champion", "Cavalry", "Melee", "Ranged", "Infantry",
           "Elite", "Advanced", "Basic", "Medium", "Light", "Heavy"]

SHAPES = {"square": "FSHAPE_SQUARE", "triangle": "FSHAPE_TRIANGLE", "special": "FSHAPE_SPECIAL"}
ORDERS = {None: "FSORT_DEFAULT", "": "FSORT_DEFAULT", "fillFromTheSides": "FSORT_FROM_THE_SIDES",
          "fillToTheCenter": "FSORT_TO_THE_CENTER"}


def cstr(s):
    return roster.cstr(s or "")


def num(x):
    s = repr(float(x))
    return s + "f"


def formation_mask(paths):
    m = 0
    for p in paths:
        if p in FORMATION_INDEX:
            m |= 1 << FORMATION_INDEX[p]
    return m


def class_mask(t):
    cls = set(roster.classes_of(t))
    rank = t.value("Identity/Rank")
    if rank:
        cls.add(rank)
    m = 0
    for i, c in enumerate(CLASSES):
        if c in cls:
            m |= 1 << i
    return m


def roster_ids():
    ids = []
    with open(ROSTER_HEADER, encoding="utf-8") as f:
        for line in f:
            mm = re.match(r'\t\t\{"([a-z0-9_/]+)", "(?:[^"\\]|\\.)*", "(?:[^"\\]|\\.)*", CIV_[A-Z]+, UNIT_', line)
            if mm:
                ids.append(mm.group(1))
    return ids


def collect(sim_root):
    loader = roster.TemplateLoader(sim_root)
    forms = []
    for enum, path in FORMATIONS:
        t = loader.load(path)
        f = lambda k, d=None: t.value("Formation/" + k, d)
        forms.append({
            "enum": enum, "path": path,
            "name": t.value("Identity/GenericName", ""),
            "tooltip": t.value("Identity/Tooltip", ""),
            "disabled": f("DisabledTooltip", ""),
            "icon": t.value("Identity/Icon", ""),
            "required": int(float(f("RequiredMemberCount", "2"))),
            "speed": float(f("SpeedMultiplier", "1")),
            "shape": SHAPES[f("FormationShape", "square")],
            "turn": float(f("MaxTurningAngle", "0")),
            "sorting": f("SortingClasses", "").split(),
            "order": ORDERS[f("SortingOrder")],
            "shift": f("ShiftRows", "false") == "true",
            "wmul": float(f("UnitSeparationWidthMultiplier", "1")),
            "dmul": float(f("UnitSeparationDepthMultiplier", "1")),
            "ratio": float(f("WidthDepthRatio", "1")),
            "slop": float(f("Sloppiness", "0")),
            "mincols": int(float(f("MinColumns", "0") or 0)),
            "maxcols": int(float(f("MaxColumns", "0") or 0)),
            "maxrows": int(float(f("MaxRows", "0") or 0)),
            "gap": float(f("CenterGap", "0") or 0),
            "attack": t.value("FormationAttack/CanAttackAsFormation", "false") == "true",
        })

    base = loader.load("template_player").tokens("Player/Formations")
    civs = []
    for civ in roster.CIVS:
        p = loader.load("special/players/" + civ)
        civs.append(formation_mask(p.tokens("Player/Formations")) | formation_mask(base))

    rows = []
    for rid in roster_ids():
        name = "units/" + rid
        if not loader.exists(name) and rid in STAND_INS and loader.exists("units/" + STAND_INS[rid]):
            name = "units/" + STAND_INS[rid]
        if not loader.exists(name):  # RTS0-only rows (rts0_villager)
            rows.append((rid, 0, 0, "RTS0-only"))
            continue
        t = loader.load(name)
        rows.append((rid, formation_mask(t.tokens("UnitAI/Formations")), class_mask(t), ""))

    generic = []
    for cls in atk.UNIT_TYPES:  # kGeneric is indexed by UnitType
        tmpl = roster.GENERIC[cls] or "no template"
        if cls in roster.RTS0_ONLY:
            generic.append((cls, 0, 0, tmpl))
            continue
        t = roster.generic_template(loader, cls)
        generic.append((cls, formation_mask(t.tokens("UnitAI/Formations")), class_mask(t), tmpl))
    return forms, civs, rows, generic


def emit(forms, civs, rows, generic):
    out = []
    w = out.append
    w("// GENERATED by tools/extract_0ad_formations.py from 0 A.D. simulation templates.")
    w("// Do not edit by hand; rerun the tool. 0 A.D. data is CC BY-SA 3.0 (Wildfire Games).")
    w("#ifndef RTSO_FORMATION_DATA_H")
    w("#define RTSO_FORMATION_DATA_H")
    w("")
    w("// Included from formation.h inside namespace rtso.")
    w("")
    w("inline const FormationTemplate *formation_template_table() {")
    w("\t// special/formations/*.xml over template_formation.xml (Formation.js Schema).")
    w("\t// template, name, tooltip, disabled tooltip, icon, required members, speed multiplier, shape,")
    w("\t// max turning angle, sorting classes (FCLASS bits, most important first), sorting order, shift rows,")
    w("\t// width/depth separation multipliers, width/depth ratio, sloppiness (m), min/max columns, max rows,")
    w("\t// centre gap (m), can attack as formation")
    w("\tstatic const FormationTemplate kTemplates[FORMATION_COUNT] = {")
    for fm in forms:
        sort = ", ".join("FCLASS_" + c.upper() for c in fm["sorting"] if c in CLASSES) or "0"
        sort_n = len([c for c in fm["sorting"] if c in CLASSES])
        w("\t\t{%s, %s, %s, %s, %s, %d, %s, %s, %s, {%s}, %d, %s, %s, %s, %s, %s, %s, %d, %d, %d, %s, %s}, // %s" % (
            cstr(fm["path"]), cstr(fm["name"]), cstr(fm["tooltip"]), cstr(fm["disabled"]), cstr(fm["icon"]),
            fm["required"], num(fm["speed"]), fm["shape"], num(fm["turn"]), sort, sort_n, fm["order"],
            "true" if fm["shift"] else "false", num(fm["wmul"]), num(fm["dmul"]), num(fm["ratio"]),
            num(fm["slop"]), fm["mincols"], fm["maxcols"], fm["maxrows"], num(fm["gap"]),
            "true" if fm["attack"] else "false", fm["enum"]))
    w("\t};")
    w("\treturn kTemplates;")
    w("}")
    w("")
    w("// Player.js GetFormations: template_player.xml plus special/players/<civ>.xml, bit = UnitFormation.")
    w("inline uint32_t civ_formation_mask(uint8_t civ) {")
    w("\tstatic const uint32_t kMasks[%d] = {" % roster.CIV_COUNT)
    by_civ = dict(zip(roster.CIVS, civs))
    for civ in roster.CIV_SLOTS:
        w("\t\t0x%05xu, // %s" % ((by_civ[civ], roster.CIV_ENUM_OF[civ]) if civ else (0, "CIV_GAIA")))
    w("\t};")
    w("\treturn civ < %d ? kMasks[civ] : 0u;" % roster.CIV_COUNT)
    w("}")
    w("")
    w("// Per civ roster row (civ_unit_template_table order): UnitAI/Formations as UnitFormation bits,")
    w("// and the Identity classes (plus Rank) Formation.js sorts by, as FCLASS bits.")
    w("inline const FormationUnitInfo *formation_unit_table() {")
    w("\tstatic const FormationUnitInfo kRows[CIV_UNIT_TEMPLATE_COUNT] = {")
    for rid, fm, cm, note in rows:
        w("\t\t{0x%05xu, 0x%03xu}, // %s%s" % (fm, cm, rid, " (" + note + ")" if note else ""))
    w("\t};")
    w("\treturn kRows;")
    w("}")
    w("")
    w("// Units with no roster template: the 0 A.D. generic template of their RTS0 class.")
    w("inline FormationUnitInfo formation_generic_info(uint8_t unit_type) {")
    w("\tstatic const FormationUnitInfo kGeneric[UNIT_TYPE_COUNT] = {")
    for cls, fm, cm, tmpl in generic:
        w("\t\t{0x%05xu, 0x%03xu}, // %s: %s" % (fm, cm, cls, tmpl))
    w("\t};")
    w("\treturn unit_type < UNIT_TYPE_COUNT ? kGeneric[unit_type] : FormationUnitInfo{0u, 0u};")
    w("}")
    w("")
    w("#endif // RTSO_FORMATION_DATA_H")
    w("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--0ad", dest="zad", required=True, help="0 A.D. source checkout")
    ap.add_argument("--check", action="store_true", help="fail if the output is stale")
    args = ap.parse_args()
    sim_root = os.path.join(args.zad, "binaries", "data", "mods", "public", "simulation")
    forms, civs, rows, generic = collect(sim_root)
    text = emit(forms, civs, rows, generic)
    if args.check:
        if not os.path.isfile(OUT_HEADER) or open(OUT_HEADER, encoding="utf-8").read() != text:
            print("stale: " + os.path.relpath(OUT_HEADER, REPO), file=sys.stderr)
            return 1
        return 0
    with open(OUT_HEADER, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("%d formations, %d civs, %d roster rows (%d can form up)" % (
        len(forms), len(civs), len(rows), sum(1 for r in rows if r[1] & ~1)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
