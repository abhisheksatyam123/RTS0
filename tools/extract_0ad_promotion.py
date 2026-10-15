#!/usr/bin/env python3
"""Generate RTS0's unit promotion tables from 0 A.D.'s Promotion component data.

Reads, under binaries/data/mods/public/simulation:
  * templates/units/<civ>/*.xml for every unit in civ_roster_data.h, following each
    one's Promotion/Entity chain (_b -> _a -> _e, and the chains that end in another
    unit: Roman soldiers -> Marian Legionary, Athenian Elite Hoplite -> City Guard)
  * data/technologies/unit_advanced.json and unit_elite.json (the rank bonuses)
  * data/technologies/*.json that modify Promotion/RequiredXp (Hoplite Tradition,
    Marian Reforms, ...), for the ones RTS0's tech tree has, and the auto-researched
    ones every player has (soldier_ranged_experience, upgrade_rank_advanced_mercenary)

Template inheritance is resolved by extract_0ad_rosters.TemplateLoader.

Usage:
  python3 -I tools/extract_0ad_promotion.py --0ad PATH/TO/0ad [--check]

Writes src/simulation/promotion_data.h. --check regenerates into memory and fails if
the checked-in file differs.
"""

import argparse
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_rosters import TemplateLoader, tech_class_bits, tech_classes_literal, tech_enums  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_HEADER = os.path.join(REPO, "src", "simulation", "promotion_data.h")
ROSTER = os.path.join(REPO, "src", "simulation", "civ_roster_data.h")

RANKS = {"Basic": 1, "Advanced": 2, "Elite": 3}

# 0.28 promotion targets the roster has no row for yet, mapped to the roster unit (and its
# rank) that 0 A.D. promoted to before 0.28, so the chain keeps working until the roster
# gains the 0.28 unit; RequiredXp still comes from 0.28. Remove an entry once its target
# is a roster row.
INTERIM_TARGETS = {}
# Steps of roster units 0.28 no longer has (id, rank, required_xp, target or None, target rank),
# as their pre-0.28 templates had them.
INTERIM_ROWS = [
    ("rome/cavalry_auxiliary_b", 1, 150, None, 2),
    ("rome/cavalry_auxiliary_b", 2, 150, None, 3),
]


def roster_ids():
    with open(ROSTER, encoding="utf-8") as fh:
        return re.findall(r'^\t\t\{"([a-z0-9_/]+)", "(?:[^"\\]|\\.)*", "(?:[^"\\]|\\.)*", CIV_[A-Z]+, UNIT_', fh.read(), re.M)


def base_of(unit_id):
    """The unit without its rank suffix (or dock variant): athen/infantry_archer_b_dock -> athen/infantry_archer."""
    return re.sub(r"_[abe](_dock)?$", "", unit_id)


def rank_of(node):
    return RANKS.get(node.value("Identity/Rank") or "", 1)


def target_id(node):
    ent = node.value("Promotion/Entity")
    if not ent:
        return None
    ent = ent.replace("{civ}", node.value("Identity/Civ") or "")
    return ent[len("units/"):] if ent.startswith("units/") else ent


def promotion_rows(loader, ids):
    """(id, rank, required_xp, target id or None for the same unit, target rank) per step.

    A step to the same unit at the next rank keeps RTS0's template; a step to another
    roster unit swaps the template. A step to a unit RTS0 lacks ends the chain (`dropped`).
    """
    have = set(ids)
    rows, starts, dropped, interim = [], [], [], []
    for uid in ids:
        name = "units/" + uid
        if not loader.exists(name):
            continue
        node = loader.load(name)
        rank = rank_of(node)
        if rank > 1:
            starts.append((uid, rank))
        seen = set()
        while node.get("Promotion") is not None and name not in seen:
            seen.add(name)
            tgt = target_id(node)
            xp = int(float(node.value("Promotion/RequiredXp")))
            if not tgt or not loader.exists("units/" + tgt):
                break
            tnode = loader.load("units/" + tgt)
            trank = rank_of(tnode)
            if base_of(tgt) == base_of(uid):
                rows.append((uid, rank, xp, None, trank))
            elif tgt in have:
                rows.append((uid, rank, xp, tgt, trank))
                break  # the target's own rows go on from here
            elif tgt in INTERIM_TARGETS and INTERIM_TARGETS[tgt][0] in have:
                rows.append((uid, rank, xp) + INTERIM_TARGETS[tgt])
                interim.append((uid, tgt, INTERIM_TARGETS[tgt][0]))
                break
            else:
                dropped.append((uid, tgt))
                break
            name, node, rank = "units/" + tgt, tnode, trank
            if node.value("Promotion/TrickleRate"):
                raise SystemExit(f"{tgt}: Promotion/TrickleRate is not ported")
    for row in INTERIM_ROWS:
        if row[0] in have and not loader.exists("units/" + row[0]):
            rows.append(row)
    return rows, starts, dropped, interim


def rank_bonus(sim_root, tech):
    with open(os.path.join(sim_root, "data", "technologies", tech + ".json")) as fh:
        mods = {m["value"]: m for m in json.load(fh)["modifications"]}

    def mul(key):
        return mods[key]["multiply"]

    def add(key):
        return mods[key]["add"] if key in mods else 0  # 0.28 ranks add no melee resistance

    assert mul("Attack/Melee/Damage/Hack") == mul("Attack/Melee/Damage/Pierce") == mul("Attack/Melee/Damage/Crush")
    assert add("Resistance/Entity/Damage/Hack") == add("Resistance/Entity/Damage/Pierce") == \
        add("Resistance/Entity/Damage/Crush")
    return {
        "health_mul": mul("Health/Max"),
        "melee_damage_mul": mul("Attack/Melee/Damage/Hack"),
        "melee_armor_add": add("Resistance/Entity/Damage/Hack"),
        "capture_add": add("Attack/Capture/Capture"),
        "heal_add": add("Heal/Health"),
        "heal_range_add": add("Heal/Range"),
        "gather_mul": mul("ResourceGatherer/BaseSpeed"),
        "train_time_mul": mul("Cost/BuildTime"),
    }


def xp_tech_rows(sim_root):
    """Technologies that modify Promotion/RequiredXp: (TECH_*, multiply, replace, need, forbid, rank).

    Auto-researched ones (every player has them) get TECH_NONE. A rank class in
    `affects` (Basic, Advanced, Elite) becomes `rank`; 0 matches every rank.
    """
    enums = tech_enums()
    rows = []
    for path in sorted(glob.glob(os.path.join(sim_root, "data", "technologies", "**", "*.json"), recursive=True)):
        name = os.path.splitext(os.path.relpath(path, os.path.join(sim_root, "data", "technologies")))[0]
        with open(path) as fh:
            tech = json.load(fh)
        if name not in enums and not tech.get("autoResearch"):
            continue
        for m in tech.get("modifications", []):
            if m["value"] != "Promotion/RequiredXp":
                continue
            affects = m.get("affects") or tech.get("affects") or [""]
            for aff in [affects] if isinstance(affects, str) else affects:  # wagon_trains: "Wagon"
                toks = aff.split()
                rank = sum(RANKS[t] for t in toks if t in RANKS)
                need = [t for t in toks if not t.startswith("!") and t not in RANKS]
                forbid = [t[1:] for t in toks if t.startswith("!")]
                nbits = tech_class_bits(need)
                if any(not tech_class_bits([c]) for c in need):
                    continue  # a class no RTS0 unit has: matches nothing
                rows.append((enums.get(name, "TECH_NONE"), m.get("multiply", 1.0), "replace" in m, nbits,
                             tech_class_bits(forbid), rank))
    return rows


def f(x):
    s = repr(float(x))
    return s + "f"


def emit(rows, starts, dropped, interim, advanced, elite, xp_techs):
    out = []
    w = out.append
    w("// Generated by tools/extract_0ad_promotion.py from 0 A.D.'s unit templates (Promotion),")
    w("// data/technologies/unit_advanced.json and unit_elite.json, and the technologies that")
    w("// modify Promotion/RequiredXp. Do not edit by hand: change the tool and regenerate.")
    w("// 0 A.D. data is CC BY-SA 3.0, see docs/0AD_ATTRIBUTION.md.")
    w("#ifndef RTSO_PROMOTION_DATA_H")
    w("#define RTSO_PROMOTION_DATA_H")
    w("")
    w("#include <cstdint>")
    w("")
    w('#include "tech_data.h"')
    w("")
    w("namespace rtso {")
    w("")
    w("// One Promotion step: the roster unit `id` at `rank` (1 Basic, 2 Advanced, 3 Elite)")
    w("// promotes after `required_xp` to `target` at `target_rank`. A null target is the same")
    w("// unit at the next rank (0 A.D.'s _b -> _a -> _e templates); otherwise the unit becomes")
    w("// that roster unit (Promotion.js ChangeEntityTemplate).")
    w("struct PromotionStep {")
    w("\tconst char *id;")
    w("\tuint8_t rank;")
    w("\tuint16_t required_xp;")
    w("\tconst char *target;")
    w("\tuint8_t target_rank;")
    w("};")
    w("")
    w("inline constexpr PromotionStep PROMOTION_STEPS[] = {")
    for uid, rank, xp, tgt, trank in rows:
        t = f'"{tgt}"' if tgt else "nullptr"
        w(f'\t{{"{uid}", {rank}, {xp}, {t}, {trank}}},')
    w("};")
    w("")
    if interim:
        w("// 0.28 targets the roster lacks, standing in with the pre-0.28 target (INTERIM_TARGETS):")
        for uid, tgt, sub in interim:
            w(f"//   {uid} -> {tgt} (as {sub})")
        w("")
    if dropped:
        w("// Chains cut where 0 A.D. promotes to a unit RTS0's roster lacks:")
        for uid, tgt in dropped:
            w(f"//   {uid} -> {tgt}")
        w("")
    w("// Roster units whose 0 A.D. template starts above Basic (Identity/Rank).")
    w("struct PromotionStart {")
    w("\tconst char *id;")
    w("\tuint8_t rank;")
    w("};")
    w("")
    w("inline constexpr PromotionStart PROMOTION_STARTS[] = {")
    for uid, rank in starts:
        w(f'\t{{"{uid}", {rank}}},')
    w("};")
    w("")
    w("// What one rank adds: unit_advanced.json (Advanced and Elite units) and unit_elite.json")
    w("// (Elite units). An Elite unit has both. Melee: Attack/Melee damage and resistance;")
    w("// healers: Heal/Health and Heal/Range; gather is ResourceGatherer/BaseSpeed; train time is")
    w("// Cost/BuildTime of a unit trained at that rank.")
    w("struct RankBonus {")
    keys = list(advanced)
    for k in keys:
        w(f"\tfloat {k};")
    w("};")
    w("")
    for name, b in (("RANK_BONUS_ADVANCED", advanced), ("RANK_BONUS_ELITE", elite)):
        w(f"inline constexpr RankBonus {name} = {{{', '.join(f(b[k]) for k in keys)}}};")
    w("")
    w("// Technologies that change Promotion/RequiredXp for units with every `need` class and")
    w("// no `forbid` class (TechClass bits), at `rank` (0 any): multiply, or replace with 0.")
    w("// TECH_NONE rows are auto-researched: every player has them.")
    w("struct PromotionXpTech {")
    w("\tuint8_t tech;")
    w("\tfloat multiply;")
    w("\tbool replace_zero;")
    w("\tTechClasses need;")
    w("\tTechClasses forbid;")
    w("\tuint8_t rank;")
    w("};")
    w("")
    w("inline constexpr PromotionXpTech PROMOTION_XP_TECHS[] = {")
    for tech, mul, rep, need, forbid, rank in xp_techs:
        w(f"\t{{{tech}, {f(mul)}, {'true' if rep else 'false'}, {tech_classes_literal(need)}, {tech_classes_literal(forbid)}, {rank}}},")
    w("};")
    w("")
    w("} // namespace rtso")
    w("")
    w("#endif // RTSO_PROMOTION_DATA_H")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zeroad", required=True)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    sim_root = os.path.join(a.zeroad, "binaries", "data", "mods", "public", "simulation")
    loader = TemplateLoader(sim_root)
    rows, starts, dropped, interim = promotion_rows(loader, roster_ids())
    text = emit(rows, starts, dropped, interim, rank_bonus(sim_root, "unit_advanced"), rank_bonus(sim_root, "unit_elite"),
                xp_tech_rows(sim_root))
    if a.check:
        with open(OUT_HEADER) as fh:
            if fh.read() != text:
                print(f"{OUT_HEADER} is out of date; rerun tools/extract_0ad_promotion.py")
                return 1
        print("promotion_data.h is up to date")
        return 0
    with open(OUT_HEADER, "w") as fh:
        fh.write(text)
    print(f"wrote {OUT_HEADER}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
