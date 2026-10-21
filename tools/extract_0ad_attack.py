#!/usr/bin/env python3
"""Generate RTS0's combat class data from 0 A.D.'s simulation templates.

0 A.D. decides who can hit whom, and how hard, from template data:
  * Identity/Classes + VisibleClasses: the classes an entity has.
  * Attack/<type>/RestrictedClasses: classes this attack can never target
    (rams: Field Organic; dogs: Structure Ship Siege). Tokens are OR-ed;
    "!X" tokens (e.g. !Domestic) only matter for hunting and are left out.
  * Attack/<type>/Bonuses/*: a target matching Classes (and Civ, if given)
    takes Multiplier times the damage (helpers/Attack.js GetAttackBonus).
    Classes tokens are alternatives ("Ship Building"); "+" joins classes a
    target needs all of. Each alternative becomes its own RTS0 bonus.
  * Attack/Ranged/Projectile/Spread: standard deviation, in metres at 100 m,
    of where a projectile lands (Attack.js PerformAttack).
  * Resistance/Entity/Damage: structure armour levels (units take theirs from
    the roster table).

One row per civ roster template (same order as civ_roster_data.h), one per
RTS0 UnitType for units without a roster template (extract_0ad_rosters.py's
GENERIC templates), and one per BuildingType. Each unit row describes its
primary attack (ranged if it has one, else melee), as the roster does.

Usage:
  python3 -I tools/extract_0ad_attack.py --0ad PATH/TO/0ad [--check]

Reads data/civs/rosters.json for the roster order and
binaries/data/mods/public/simulation from the 0 A.D. checkout. Writes
src/simulation/civ_attack_data.h. --check fails if the checked-in file differs.
"""

import argparse
import importlib.util
import json
import re
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_HEADER = os.path.join(REPO, "src", "simulation", "civ_attack_data.h")
ROSTERS = os.path.join(REPO, "data", "civs", "rosters.json")

_spec = importlib.util.spec_from_file_location("extract_0ad_rosters",
                                               os.path.join(REPO, "tools", "extract_0ad_rosters.py"))
ex = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ex)

# RTS0 BuildingType order (sim_types.h) to the 0 A.D. generic structure template.
BUILDINGS = [
    ("BLD_TOWN_CENTER", "template_structure_civic_civil_centre"),
    ("BLD_HOUSE", "template_structure_civic_house"),
    ("BLD_BARRACKS", "template_structure_military_barracks"),
    ("BLD_STOREHOUSE", "template_structure_economic_storehouse"),
    ("BLD_TOWER", "template_structure_defensive_tower_stone"),
    ("BLD_FARMSTEAD", "template_structure_economic_farmstead"),
    ("BLD_MARKET", "template_structure_economic_market"),
    ("BLD_FORTRESS", "template_structure_military_fortress"),
    ("BLD_GATE", "template_structure_defensive_wall_gate"),
    ("BLD_TEMPLE", "template_structure_civic_temple"),
    ("BLD_WONDER", "template_structure_wonder"),
    ("BLD_DOCK", "template_structure_military_dock"),
    ("BLD_WALL_SEGMENT", "template_structure_defensive_wall_long"),
    ("BLD_CORRAL", "template_structure_resource_corral"),
    ("BLD_FIELD", "template_structure_resource_field"),
    ("BLD_STABLE", "template_structure_military_stable"),
    ("BLD_RANGE", "template_structure_military_range"),
    ("BLD_FORGE", "template_structure_military_forge"),
    ("BLD_ARSENAL", "template_structure_military_arsenal"),
    ("BLD_SENTRY_TOWER", "template_structure_defensive_tower_sentry"),
    ("BLD_OUTPOST", "template_structure_defensive_outpost"),
    ("BLD_PALISADE", "template_structure_defensive_palisade"),
    ("BLD_WALL_TOWER", "template_structure_defensive_wall_tower"),
    ("BLD_ELEPHANT_STABLE", "template_structure_military_elephant_stable"),
    ("BLD_THEATER", "template_structure_special_theater"),
    ("BLD_EMBASSY", "template_structure_military_embassy"),
]


def civ_buildings():
    """Civ-specific BuildingTypes (ids from CIV_BUILDING_FIRST on), read from
    sim_types.h and their templates from civ_building_data.h (extract_0ad_rosters.py)."""
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "simulation")
    enum = re.findall(r"^\t(BLD_[A-Z0-9_]+) = (\d+),", open(os.path.join(here, "sim_types.h")).read(), re.M)
    names = [n for n, i in sorted(((n, int(i)) for n, i in enum), key=lambda x: x[1]) if i >= len(BUILDINGS)]
    data = open(os.path.join(here, "civ_building_data.h")).read()
    rows = data[data.index("RTSO_CIV_BUILDING_INFO_ROWS"):]
    templates = re.findall(r"/\* ((?:structures|campaigns)/[a-z0-9_/]+) \*/", rows)[:len(names)]
    if len(templates) != len(names):
        sys.exit("civ_building_data.h has %d rows for %d civ building types" % (len(templates), len(names)))
    return list(zip(names, templates))


MAX_BONUSES = 4

# RTS0 UnitType order (sim_types.h).
UNIT_TYPES = ["UNIT_VILLAGER", "UNIT_SWORDSMAN", "UNIT_ARCHER", "UNIT_SPEARMAN", "UNIT_CAVALRY",
              "UNIT_TRADER", "UNIT_CATAPULT", "UNIT_HERO", "UNIT_TRIREME", "UNIT_ELEPHANT",
              "UNIT_SIEGE_RAM", "UNIT_HEALER", "UNIT_CHARIOT", "UNIT_WAR_DOG", "UNIT_BOLT_SHOOTER",
              "UNIT_FISHING_BOAT", "UNIT_MERCHANT_SHIP", "UNIT_FEMALE_CITIZEN", "UNIT_CARNYX",
              "UNIT_WORKER_ELEPHANT", "UNIT_ARCHER_MAIDEN", "UNIT_CAMEL", "UNIT_SCYTHED_CHARIOT",
              "UNIT_MINISTER", "UNIT_CATAFALQUE", "UNIT_TYPE_TECH", "UNIT_WAGON"]


# 0 A.D. status effects (data/status_effects): RTS0 models Poisoned and Burning.
STATUS_KINDS = {"Poisoned": 1, "Burning": 2}


def status_of(atk):
    """Attack/*/ApplyStatus: (kind, damage per tick, Interval in s, Duration in s), or zeros."""
    node = atk.get("ApplyStatus")
    for name, st in sorted((node.children.items() if node else []), key=lambda kv: kv[0]):
        kind = STATUS_KINDS.get(name)
        if not kind:
            continue
        interval = st.num("Interval", 1000.0) / 1000.0
        dmg = st.get("Damage")
        per_tick = sum(float(c.text or 0) for c in (dmg.children.values() if dmg else []))
        return (kind, per_tick, interval, st.num("Duration") / 1000.0)
    return (0, 0.0, 0.0, 0.0)


def status_immunity(t):
    """Bits of STATUS_KINDS a Resistance/Entity/ApplyStatus BlockChance of 1 blocks."""
    bits = 0
    for name, kind in STATUS_KINDS.items():
        if t.num("Resistance/Entity/ApplyStatus/%s/BlockChance" % name) >= 1.0:
            bits |= kind
    return bits


def attack_of(t):
    atk, _ranged = ex.primary_attack(t)
    if atk is None:
        return {"restricted": [], "spread": 0.0, "bonuses": [], "status": (0, 0.0, 0.0, 0.0),
                "prepare": 0.0, "proj_speed": 0.0, "proj_gravity": 0.0, "fire": 0.0}
    restricted = [c for c in atk.tokens("RestrictedClasses") if not c.startswith("!")]
    bonuses = []
    b = atk.get("Bonuses")
    for _name, node in sorted((b.children.items() if b else []), key=lambda kv: kv[0]):
        mult = node.num("Multiplier", 1.0)
        civ = node.value("Civ")
        if abs(mult - 1.0) <= 1e-9:
            continue
        # MatchesClassList: space-separated tokens are alternatives, "+" joins
        # classes a target needs all of. One RTS0 bonus per alternative
        # (check_bonus_alternatives makes sure no entity matches two of them).
        alts = [tok.split("+") for tok in node.tokens("Classes")]
        for alt in alts:
            bonuses.append((alt, mult, civ, len(alts) > 1))
    return {"restricted": restricted, "spread": atk.num("Projectile/Spread", 0.0), "bonuses": bonuses,
            "status": status_of(atk), "prepare": atk.num("PrepareTime", 0.0) / 1000.0,
            "proj_speed": atk.num("Projectile/Speed", 0.0), "proj_gravity": atk.num("Projectile/Gravity", 0.0),
            "fire": atk.num("Damage/Fire", 0.0)}


def footprint_of(t):
    """Footprint radius in 0 A.D. metres (a square counts as the circle of the same area)."""
    c = t.get("Footprint/Circle")
    if c is not None and "radius" in c.attrs:
        return float(c.attrs["radius"])
    q = t.get("Footprint/Square")
    if q is not None and "width" in q.attrs:
        return (float(q.attrs["width"]) * float(q.attrs["depth"]) / 3.141592653589793) ** 0.5
    return 0.0


LOOT_KEYS = ("xp", "food", "wood", "stone", "metal")


def loot_of(t):
    """Loot/{xp,food,wood,stone,metal}: what killing the unit pays (Loot.js)."""
    return tuple(int(round(t.num("Loot/" + k, 0.0))) for k in LOOT_KEYS)


def row_of(t, structure=False):
    r = {"classes": sorted(ex.classes_of(t)), "attack": attack_of(t), "footprint": footprint_of(t),
         "immune": status_immunity(t), "capture": t.num("Attack/Capture/Capture", 0.0)}
    if structure:
        r["armour"] = tuple(t.num("Resistance/Entity/Damage/" + k) for k in ("Hack", "Pierce", "Crush"))
    return r


def collect(sim_root):
    loader = ex.TemplateLoader(sim_root)
    rosters = json.load(open(ROSTERS))
    units = []
    for u in rosters["units"]:
        name = "units/" + u["id"]
        own = loader.exists(name.split("|")[-1])
        if not own:
            name = ex.GENERIC[u["class"]]  # RTS0-only rows (the male villager)
        row = row_of(loader.load(name))
        if own:  # RTS0-only rows keep their class's loot (unit_class_loot)
            row["loot"] = loot_of(loader.load(name))
            row["garrison"] = int(loader.load(name).num("GarrisonHolder/Max", 0))
        units.append((u["id"], row))
    generic = []
    for cls in UNIT_TYPES:
        generic.append((cls, ex.GENERIC[cls] or "no template", row_of(ex.generic_template(loader, cls))))
    buildings = []
    for bld, name in BUILDINGS + civ_buildings():
        buildings.append((bld, name, row_of(loader.load(name), structure=True)))
    buildings.append(("default", "template_structure", row_of(loader.load("template_structure"), structure=True)))
    return units, generic, buildings


# Classes RTS0 code tests directly (aura filters, structure checks), kept even when
# no attack names them.
CODE_CLASSES = {"Human", "Organic", "Soldier", "Structure", "Unit"}


def class_list(units, generic, buildings):
    """Only the classes some attack's RestrictedClasses or Bonuses name (plus
    CODE_CLASSES): a class nothing tests never changes a result."""
    names = set(CODE_CLASSES)
    for row in [r for _, r in units] + [r for _, _, r in generic] + [r for _, _, r in buildings]:
        names.update(row["attack"]["restricted"])
        for b in row["attack"]["bonuses"]:
            names.update(b[0])
    names = sorted(names)
    if len(names) > 128:
        sys.exit("more than 128 classes: widen CombatClasses")
    return names


def check_bonus_alternatives(units, generic, buildings):
    """An entity matching two alternatives of one 0 A.D. bonus would take its
    multiplier twice from the split RTS0 bonuses: refuse to generate that."""
    rows = [r for _, r in units] + [r for _, _, r in generic] + [r for _, _, r in buildings]
    for row in rows:
        alts = [set(b[0]) for b in row["attack"]["bonuses"] if b[3]]
        if not alts:
            continue
        for target in rows:
            have = set(target["classes"])
            if sum(1 for a in alts if a <= have) > 1:
                sys.exit("a target matches two alternatives of one bonus: %s" % sorted(target["classes"]))


def mask(names, index):
    lo = hi = 0
    for n in names:
        if n not in index:
            continue  # a class no attack tests
        i = index[n]
        if i < 64:
            lo |= 1 << i
        else:
            hi |= 1 << (i - 64)
    return "{0x%016xull, 0x%016xull}" % (lo, hi)


def f(x):
    s = repr(float(x))
    return s + "f"


def emit(units, generic, buildings, classes):
    index = {c: i for i, c in enumerate(classes)}
    civ_index = ex.CIV_INDEX  # Civilization ids (CIV_GAIA has no row)
    out = []
    w = out.append
    w("// Generated by tools/extract_0ad_attack.py from 0 A.D.'s simulation templates; do not edit.")
    w("// 0 A.D. data is CC BY-SA 3.0 / GPL-2.0-or-later (see docs/0AD_ATTRIBUTION.md).")
    w("// Included by sim_types.h inside namespace rtso, after civ_roster_data.h.")
    w("#pragma once")
    w("")
    w("// 0 A.D. Identity classes, one bit each (combat_class_bit).")
    w("inline constexpr uint8_t COMBAT_CLASS_COUNT = %d;" % len(classes))
    w("enum CombatClass : uint8_t {")
    for i, c in enumerate(classes):
        w("\tCC_%s = %d," % (c.upper(), i))
    w("};")
    w("struct CombatClasses {")
    w("\tuint64_t lo, hi;")
    w("\tbool has(uint8_t c) const { return c < 64 ? ((lo >> c) & 1u) : ((hi >> (c - 64)) & 1u); }")
    w("\tbool any_of(const CombatClasses &o) const { return (lo & o.lo) || (hi & o.hi); }")
    w("\tbool all_of(const CombatClasses &o) const { return (lo & o.lo) == o.lo && (hi & o.hi) == o.hi; }")
    w("};")
    w("struct CombatBonus {")
    w("\tCombatClasses classes; // the target must have all of them")
    w("\tfloat multiplier;")
    w("\tuint8_t civ; // Civilization the target must be, 255 = any")
    w("};")
    w("struct CombatProfile {")
    w("\tCombatClasses classes; // what this entity is")
    w("\tCombatClasses restricted; // its attack never targets any of these")
    w("\tfloat spread; // Projectile/Spread: metres of deviation at 100 m, 0 = melee")
    w("\tuint8_t bonus_count;")
    w("\tCombatBonus bonus[%d];" % MAX_BONUSES)
    w("\tfloat hack_armor, pierce_armor, crush_armor; // structures only")
    w("\tfloat footprint; // Footprint radius in 0 A.D. metres (squares as the circle of equal area)")
    w("\tuint8_t status; // ApplyStatus its attack applies: 0 none, 1 Poisoned, 2 Burning")
    w("\tfloat status_damage, status_interval, status_time; // that status's Damage per tick, Interval and Duration (s)")
    w("\tuint8_t status_immune; // statuses it blocks (bits 1 Poisoned, 2 Burning; BlockChance 1)")
    w("\tfloat prepare; // Attack PrepareTime (s): the first hit lands this long after an attack starts")
    w("\tfloat proj_speed, proj_gravity; // Projectile Speed (m/s, horizontal) and Gravity (m/s^2), 0 = melee")
    w("\tfloat capture; // Attack/Capture/Capture: capture points a second, 0 = no Capture attack")
    w("\tfloat fire; // its attack's Damage/Fire (fire ships); no 0.28 template resists Fire")
    w("\tbool has_loot; // a roster unit's own template: loot below, else its class's unit_class_loot")
    w("\tuint16_t loot[5]; // Loot xp, food, wood, stone, metal")
    w("\tuint8_t garrison_max; // a roster unit's GarrisonHolder/Max (ships, rams, siege towers), 0 = none")
    w("};")
    w("")

    def profile(row, comment):
        a = row["attack"]
        if len(a["bonuses"]) > MAX_BONUSES:
            sys.exit("raise MAX_BONUSES for " + comment)
        bon = []
        for classes, mult, civ, _alt in a["bonuses"]:
            bon.append("{%s, %s, %d}" % (mask(classes, index), f(mult), civ_index.get(civ, 255) if civ else 255))
        while len(bon) < MAX_BONUSES:
            bon.append("{{0, 0}, 1.0f, 255}")
        arm = row.get("armour", (0, 0, 0))
        st = a["status"]
        return "\t\t{%s, %s, %s, %d, {%s}, %s, %s, %s, %s, %d, %s, %s, %s, %d, %s, %s, %s, %s, %s, %s, {%s}, %d}, // %s" % (
            mask(row["classes"], index), mask(a["restricted"], index), f(a["spread"]), len(a["bonuses"]),
            ", ".join(bon), f(arm[0]), f(arm[1]), f(arm[2]), f(round(row["footprint"], 3)),
            st[0], f(st[1]), f(st[2]), f(st[3]), row["immune"], f(round(a["prepare"], 4)),
            f(a["proj_speed"]), f(a["proj_gravity"]), f(row["capture"]), f(a["fire"]),
            "true" if "loot" in row else "false", ", ".join(str(v) for v in row.get("loot", (0, 0, 0, 0, 0))),
            row.get("garrison", 0), comment)

    w("// 0 A.D. metres to RTS0 units for ranged distances (extract_0ad_rosters.py RANGED_RANGE_SCALE).")
    w("inline constexpr float COMBAT_DISTANCE_SCALE = %s;" % f(ex.RANGED_RANGE_SCALE))
    w("")
    w("inline constexpr uint16_t COMBAT_TEMPLATE_COUNT = %d; // = CIV_UNIT_TEMPLATE_COUNT" % len(units))
    w("inline const CombatProfile *combat_template_table() {")
    w("\tstatic const CombatProfile kTable[COMBAT_TEMPLATE_COUNT] = {")
    for uid, row in units:
        w(profile(row, uid))
    w("\t};")
    w("\treturn kTable;")
    w("}")
    w("")
    w("// Units without a roster template, by UnitType (extract_0ad_rosters.py GENERIC).")
    w("inline const CombatProfile *combat_class_table() {")
    w("\tstatic const CombatProfile kTable[%d] = {" % len(generic))
    for cls, name, row in generic:
        w(profile(row, "%s: %s" % (cls, name)))
    w("\t};")
    w("\treturn kTable;")
    w("}")
    w("inline constexpr uint8_t COMBAT_CLASS_ROWS = %d; // = UNIT_TYPE_COUNT" % len(generic))
    w("")
    w("// Structures by BuildingType; types past the table use the generic structure row.")
    w("inline constexpr uint8_t COMBAT_BUILDING_ROWS = %d;" % (len(buildings) - 1))
    w("inline const CombatProfile *combat_building_table() {")
    w("\tstatic const CombatProfile kTable[COMBAT_BUILDING_ROWS + 1] = {")
    for bld, name, row in buildings:
        w(profile(row, "%s: %s" % (bld, name)))
    w("\t};")
    w("\treturn kTable;")
    w("}")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zad", required=True)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    sim_root = os.path.join(args.zad, "binaries", "data", "mods", "public", "simulation")
    units, generic, buildings = collect(sim_root)
    check_bonus_alternatives(units, generic, buildings)
    text = emit(units, generic, buildings, class_list(units, generic, buildings))
    if args.check:
        old = open(OUT_HEADER).read() if os.path.exists(OUT_HEADER) else ""
        if old != text:
            sys.exit("civ_attack_data.h is out of date")
        print("civ_attack_data.h is up to date")
        return
    with open(OUT_HEADER, "w") as fh:
        fh.write(text)
    print("wrote %s (%d templates, %d classes)" % (OUT_HEADER, len(units), len(class_list(units, generic, buildings))))


if __name__ == "__main__":
    main()
