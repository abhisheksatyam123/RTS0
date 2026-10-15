#!/usr/bin/env python3
"""Generate src/simulation/aura_modifier_data.h from 0 A.D.'s auras.

Reads simulation/data/auras/ from 0 A.D.'s public mod (0.28):
- units/catafalques/*.json: each "global" or "player" aura becomes a list of player
  modifiers that Simulation::compute_tech_mods folds in for every player the aura
  reaches while a relic carrying it is owned (Capture the Relic). Their "range" auras
  stay with the AuraManager (civ_roster_data.h).
- units/heroes/*.json: the Cost/ and Researcher/ modifications of each "global" or
  "player" hero aura (Pericles' Periclean Building, Themistocles, Agis, Indibil...), the
  same kind of player modifiers while the hero lives. Their other modifications (speed,
  armour, vision...) are AuraManager effects in civ_roster_data.h.
- every "range" aura with a Promotion/RequiredXp modification (Han Xin's Northern
  Army, the centurion): its multiplier, registered as an AURA_REQUIRED_XP source.
- every "range" aura on units with an Attack/Capture/Capture modification (the heroes'
  capture strength auras, the carnyx): its multiply and add, registered as
  AURA_CAPTURE_MUL / AURA_CAPTURE_ADD sources.
- every "range" aura with Looter/Resource modifications (Brennus, Nastasen, Viriato):
  its multiply and add per resource, an AURA_LOOTER source; and every one replacing
  its victims' Loot with 0 (Pericles), an AURA_NO_LOOT source.
- every "range" aura with ResourceGatherer/Rates multipliers (the Spartan catafalque's
  Iron Pelanors): the multiplier per GatherKind, an AURA_GATHER_RATE source.
- every "range" or "global" unit aura's AuraManager effects of each kind
  (extract_0ad_rosters.rts0_aura_effects): civ_roster_data.h keeps only one kind per
  unit aura, so the others (Brasidas' ranged damage beside his armour, Boudicca's
  speed beside her attack...) are registered from here.
- every "range" aura with a Builder/Rate modification (Xerxes, Ptolemy I, the Han
  minister): its multiplier and whether it stacks, registered as an AURA_BUILD source.
- every "garrison" aura on BuildingAI/GarrisonArrowMultiplier or MaxArrowCount (Caros'
  Valiant Defender): what it multiplies them by on the structure or siege tower holding it.

Usage:
    python3 tools/extract_0ad_aura_modifiers.py --public PATH [--out src/simulation/aura_modifier_data.h]

PATH is either 0 A.D.'s public.zip or a directory it was unzipped into.
The 0 A.D. data is CC BY-SA 3.0, (C) Wildfire Games; see docs/0AD_ATTRIBUTION.md.
"""

from __future__ import annotations

import argparse
import json
import xml.etree.ElementTree as ET
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_techs import CLASSES, CLASS_ALIASES, Source, stat_of  # noqa: E402

AURA_DIR = "simulation/data/auras/units/catafalques"

# Modification paths the tech table does not map, and the TechStat RTS0 uses for them.
EXTRA_STATS = {
    "ResourceGatherer/BaseSpeed": "TSTAT_GATHER_ALL",
    "Vision/Range": "TSTAT_VISION",
    "TerritoryInfluence/Radius": "TSTAT_TERRITORY_RADIUS",
    "Attack/Capture/Capture": "TSTAT_CAPTURE_ATTACK",
    "Player/MaxPopulation": "TSTAT_MAX_POP",
    "Promotion/RequiredXp": "TSTAT_REQUIRED_XP",
    "Researcher/TechCostMultiplier/food": "TSTAT_TECH_COST_FOOD",
    "Researcher/TechCostMultiplier/wood": "TSTAT_TECH_COST_WOOD",
    "Researcher/TechCostMultiplier/stone": "TSTAT_TECH_COST_STONE",
    "Researcher/TechCostMultiplier/metal": "TSTAT_TECH_COST_METAL",
    "Looter/Resource/food": "TSTAT_LOOT_FOOD",
    "Looter/Resource/wood": "TSTAT_LOOT_WOOD",
    "Looter/Resource/stone": "TSTAT_LOOT_STONE",
    "Looter/Resource/metal": "TSTAT_LOOT_METAL",
}

# 0 A.D. classes with no TechClass bit, as alternatives of classes RTS0 has.
# Human: every unit but siege engines and ships (dogs and elephants are Human too).
# Economic: template_structure_economic's children, the dock and the corral.
EXTRA_ALIASES = {
    "Human": ["Unit !Siege !Ship"],
    "Economic": ["Storehouse", "Farmstead", "Market", "Dock", "Corral"],
    # Nastasen's Gift of Warships: units/kush/ship_arrow is Kush's only ArrowShip, and the
    # aura reaches only its Kushite owner, who trains no other civ's ships.
    "KushTrireme": ["ArrowShip"],
    # Arakamani's Follower of Apedemak, which reaches only its Kushite owner: the Kushite temple
    # (structures/kush/temple.xml) is the Temple of Apedemak, and the Apedemak Temple Guard
    # (units/kush/champion_infantry_apedemak, an axeman) is Kush's only melee champion
    # infantry that is no swordsman (the Amun guard is).
    "TempleOfApedemak": ["Temple"],
    "ApedemakGuard": ["Champion Infantry Melee !Swordsman"],
}

# Auras.js affectedPlayers -> AuraPlayers bits (aura_manager.h).
PLAYERS = {"Player": 1, "Ally": 11, "MutualAlly": 3, "Enemy": 4}  # 8: by the owner's own stance


def list_files(src: Source, prefix: str) -> list[str]:
    """Every file under `prefix` in the public mod, as paths relative to it."""
    if src.zip:
        return [n for n in src.zip.namelist() if n.startswith(prefix)]
    out = []
    for dirpath, _, files in os.walk(os.path.join(src.root, prefix)):
        for f in files:
            out.append(os.path.relpath(os.path.join(dirpath, f), src.root))
    return out


def class_expr(tokens: str) -> str | None:
    need, forbid = [], []
    tokens = " ".join(CLASS_ALIASES.get(t, t) for t in tokens.split())
    for tok in tokens.split():
        neg = tok.startswith("!")
        tok = tok[1:] if neg else tok
        if tok not in CLASSES:
            if not neg:
                return None
            continue
        (forbid if neg else need).append("TC_" + tok.upper())
    return f"{{{' | '.join(need) or '0'}, {' | '.join(forbid) or '0'}}}"


def alternatives(affects) -> list[str] | None:
    """The TechClassExpr rows of an affects list ([] = everything, None = nothing RTS0 has)."""
    if not affects:
        return []
    if isinstance(affects, str):
        affects = [affects]
    out: list[str] = []
    for a in affects:
        # A class with no bit expands to its alternatives, each ANDed with the rest.
        variants = [""]
        for tok in a.split():
            subs = EXTRA_ALIASES.get(tok, [tok])
            variants = [(v + " " + s).strip() for v in variants for s in subs]
        for v in variants:
            e = class_expr(v)
            if e and e not in out:
                out.append(e)
    return out or None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--public", required=True, help="0 A.D. public.zip or unzipped public mod dir")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "src", "simulation",
                                                  "aura_modifier_data.h"))
    args = ap.parse_args()
    src = Source(args.public)

    names = []
    for civ in ["athen", "spart", "pers", "rome", "cart", "mace", "brit", "gaul",
                "iber", "maur", "kush", "ptol", "sele", "han", "germ"]:
        for suffix in ["", "_1", "_2", "_3"]:
            if src.read(f"{AURA_DIR}/{civ}_catafalque{suffix}.json"):
                names.append(f"{civ}_catafalque{suffix}")
        # 0.28's Germanic catafalque names its aura germ_1 (units/germ/catafalque.xml).
        if src.read(f"{AURA_DIR}/{civ}_1.json"):
            names.append(f"{civ}_1")
    hero_dir = "simulation/data/auras/units/heroes"
    hero_names = sorted(os.path.basename(r)[:-5] for r in list_files(src, hero_dir + "/") if r.endswith(".json"))

    auras: list[str] = []
    mods: list[str] = []
    skipped: list[str] = []
    max_alts = 1
    for name, hero in [(n, False) for n in names] + [(n, True) for n in hero_names]:
        j = json.loads(src.read(f"{hero_dir if hero else AURA_DIR}/{name}.json"))
        if j.get("type") not in ("global", "player"):
            continue  # range auras: civ_roster_data.h / AuraManager
        if hero and not any(m["value"].startswith(("Cost/", "Researcher/")) for m in j.get("modifications", [])):
            continue
        players = 0
        for p in j.get("affectedPlayers") or ["Player"]:
            players |= PLAYERS.get(p, 0)
        begin = len(mods)
        for m in j.get("modifications", []):
            if hero and not m["value"].startswith(("Cost/", "Researcher/")):
                continue
            stat = EXTRA_STATS.get(m["value"]) or stat_of(m["value"])
            if not stat:
                skipped.append(f"{name}: {m['value']}")
                continue
            if "multiply" in m:
                op, val = "TOP_MUL", float(m["multiply"])
            elif "add" in m:
                op, val = "TOP_ADD", float(m["add"])
            else:
                skipped.append(f"{name}: {m['value']}")
                continue
            aff = None if j.get("type") == "player" else j.get("affects")
            alts = alternatives(aff)
            if alts is None:
                skipped.append(f"{name}: affects {aff}")
                continue
            max_alts = max(max_alts, len(alts))
            mods.append((stat, op, val, alts, name))
        if hero and len(mods) == begin:
            continue  # only classes RTS0 has no TechClass for
        auras.append(f"\t\t{{\"{name}\", {players}, {begin}, {len(mods) - begin}}},")

    # Range auras with a RequiredXp multiplier, anywhere under auras/.
    xp_auras: list[tuple[str, float]] = []
    build_auras: list[tuple[str, float, bool]] = []
    arrow_auras: list[tuple[str, float, float]] = []
    for rel in sorted(list_files(src, "simulation/data/auras/")):
        if not rel.endswith(".json"):
            continue
        j = json.loads(src.read(rel))
        if j.get("type") == "garrison":
            mul = {m["value"]: float(m["multiply"]) for m in j.get("modifications", [])
                   if m["value"].startswith("BuildingAI/") and "multiply" in m}
            if mul:
                arrow_auras.append((os.path.basename(rel)[:-5], mul.get("BuildingAI/GarrisonArrowMultiplier", 1.0),
                                    mul.get("BuildingAI/MaxArrowCount", 1.0)))
        if j.get("type") != "range":
            continue
        for m in j.get("modifications", []):
            if m["value"] == "Promotion/RequiredXp" and "multiply" in m:
                xp_auras.append((os.path.basename(rel)[:-5], float(m["multiply"])))
            if m["value"] == "Builder/Rate" and "multiply" in m:
                build_auras.append((os.path.basename(rel)[:-5], float(m["multiply"]), bool(j.get("stackable"))))

    # Range auras on Capturable/CapturePoints or Capturable/GarrisonRegenRate (multiply),
    # anywhere under auras/, and the RTS0 structures that project them (their own Auras).
    cap_stats = {"Capturable/CapturePoints": "TSTAT_CAPTURE_POINTS",
                 "Capturable/GarrisonRegenRate": "TSTAT_GARRISON_REGEN"}
    cap_auras: list[tuple[str, str, float, float, int, bool]] = []
    cap_paths: dict[str, int] = {}
    for rel in sorted(list_files(src, "simulation/data/auras/")):
        if not rel.endswith(".json"):
            continue
        j = json.loads(src.read(rel))
        if j.get("type") != "range":
            continue
        affects = j.get("affects") or []
        if not any(c in ("Structure", "CivilCentre", "CivCentre") for c in affects):
            continue
        players = 0
        for pl in j.get("affectedPlayers") or ["Player"]:
            players |= PLAYERS.get(pl, 0)
        civic_centre = "Structure" not in affects
        for m in j.get("modifications", []):
            if m["value"] in cap_stats and "multiply" in m:
                key = rel[len("simulation/data/auras/"):-5]
                cap_paths[key] = len(cap_auras)
                cap_auras.append((os.path.basename(key), cap_stats[m["value"]], float(m["multiply"]),
                                  float(j.get("radius", 0)), players, civic_centre))
    # Range auras on units' Attack/Capture/Capture (multiply and add), anywhere under auras/.
    capatk_auras: list[tuple[str, float, float, float, int]] = []
    for rel in sorted(list_files(src, "simulation/data/auras/")):
        if not rel.endswith(".json"):
            continue
        j = json.loads(src.read(rel))
        if j.get("type") != "range":
            continue
        mul, add = 1.0, 0.0
        for m in j.get("modifications", []):
            if m["value"] == "Attack/Capture/Capture":
                mul *= float(m.get("multiply", 1.0))
                add += float(m.get("add", 0.0))
        if mul == 1.0 and add == 0.0:
            continue
        players = 0
        for pl in j.get("affectedPlayers") or ["Player"]:
            players |= PLAYERS.get(pl, 0)
        capatk_auras.append((os.path.basename(rel)[:-5], mul, add, float(j.get("radius", 0)), players))
    # Range auras on Looter/Resource/<res> (multiply and add) and on Loot/* (replace 0).
    loot_res = ["food", "wood", "stone", "metal"]
    looter_auras: list[tuple[str, list[float], list[float], float, int]] = []
    no_loot_auras: list[tuple[str, float, int]] = []
    # Range auras on ResourceGatherer/Rates/<type>.<subtype>, per GatherKind (simulation.h).
    gather_keys = ["food.grain", "food.fruit", "food.meat", "food.fish", "wood.tree", "stone.rock", "metal.ore"]
    gather_auras: list[tuple[str, list[float], float, int]] = []
    # Range auras adding to Attack/Ranged/MaxRange and Attack/Melee/MaxRange (Boiorix).
    range_add_auras: list[tuple[str, float, float, float, int]] = []
    for rel in sorted(list_files(src, "simulation/data/auras/")):
        if not rel.endswith(".json"):
            continue
        j = json.loads(src.read(rel))
        if j.get("type") != "range":
            continue
        players = 0
        for pl in j.get("affectedPlayers") or ["Player"]:
            players |= PLAYERS.get(pl, 0)
        name = os.path.basename(rel)[:-5]
        mul, add = [1.0] * 4, [0.0] * 4
        zero = False
        rates = [1.0] * len(gather_keys)
        range_add = {"Ranged": 0.0, "Melee": 0.0}
        for m in j.get("modifications", []):
            v = m["value"]
            if v in ("Attack/Ranged/MaxRange", "Attack/Melee/MaxRange") and "add" in m:
                range_add[v.split("/")[1]] += float(m["add"])
            if v.startswith("ResourceGatherer/Rates/") and v.split("/")[-1] in gather_keys and "multiply" in m:
                rates[gather_keys.index(v.split("/")[-1])] *= float(m["multiply"])
            if v.startswith("Looter/Resource/") and v.split("/")[-1] in loot_res:
                r = loot_res.index(v.split("/")[-1])
                mul[r] *= float(m.get("multiply", 1.0))
                add[r] += float(m.get("add", 0.0))
            elif v.startswith("Loot/") and m.get("replace") == 0:
                zero = True
        if mul != [1.0] * 4 or add != [0.0] * 4:
            looter_auras.append((name, mul, add, float(j.get("radius", 0)), players))
        if zero:
            no_loot_auras.append((name, float(j.get("radius", 0)), players))
        if rates != [1.0] * len(gather_keys):
            gather_auras.append((name, rates, float(j.get("radius", 0)), players))
        if range_add["Ranged"] or range_add["Melee"]:
            range_add_auras.append((name, range_add["Ranged"], range_add["Melee"], float(j.get("radius", 0)), players))
    # Unit auras' AuraManager effects of every kind (the roster keeps the strongest one).
    import extract_0ad_rosters as ros  # noqa: E402
    effect_rows: list[tuple[str, str, float, float]] = []
    for rel in sorted(list_files(src, "simulation/data/auras/units/")):
        if not rel.endswith(".json"):
            continue
        j = json.loads(src.read(rel))
        if j.get("type") not in ("range", "global"):
            continue
        a = {"type": j["type"], "radius": float(j.get("radius", 0)),
             "players": j.get("affectedPlayers", ["Player"]), "modifications": j.get("modifications", [])}
        effects = ros.rts0_aura_effects(a)
        if len(effects) < 2:
            continue  # one kind: the roster row has it
        for kind, value, rng in effects:
            effect_rows.append((os.path.basename(rel)[:-5], kind, float(value), float(rng)))
    cap_structures: list[tuple[str, int, str]] = []
    import extract_0ad_attack as atk  # noqa: E402
    for bld, name in atk.BUILDINGS + atk.civ_buildings():
        data = src.read(f"simulation/templates/{name}.xml")
        if data is None:
            continue
        e = ET.fromstring(data).find("Auras")
        for tok in (e.text.split() if e is not None and e.text else []):
            if tok in cap_paths:
                cap_structures.append((bld, cap_paths[tok], name))

    lines = []
    w = lines.append
    w("// GENERATED by tools/extract_0ad_aura_modifiers.py from 0 A.D. 0.28")
    w("// simulation/data/auras/**/*.json (CC BY-SA 3.0, Wildfire Games). Do not edit by hand.")
    w("#ifndef RTSO_AURA_MODIFIER_DATA_H")
    w("#define RTSO_AURA_MODIFIER_DATA_H")
    w("")
    w("// Included from sim_types.h after tech_data.h and civ_roster_data.h.")
    w("")
    w(f"constexpr int RELIC_AURA_MAX_ALTS = {max_alts};")
    w("")
    w("// Global auras whose modifications are player modifiers (Simulation::compute_tech_mods):")
    w("// Capture the Relic catafalques while a relic is owned, and heroes' cost and research")
    w("// auras while the hero lives.")
    w("// One modification of a global relic aura: an entity matches when its classes")
    w("// match any alternative (none = every entity of the affected players).")
    w("struct RelicAuraModifier {")
    w("\tuint8_t stat; // TechStat")
    w("\tuint8_t op;   // TechOp")
    w("\tfloat value;")
    w("\tuint8_t alt_count;")
    w("\tTechClassExpr alts[RELIC_AURA_MAX_ALTS];")
    w("};")
    w("")
    w("struct RelicGlobalAura {")
    w("\tconst char *id;       // civ_hero_aura id")
    w("\tuint8_t players;      // AuraPlayers bits (Auras.js affectedPlayers)")
    w("\tuint16_t mod_begin;   // into relic_aura_modifiers()")
    w("\tuint16_t mod_count;")
    w("};")
    w("")
    w(f"constexpr uint16_t RELIC_GLOBAL_AURA_COUNT = {len(auras)};")
    w("")
    w("inline const RelicAuraModifier *relic_aura_modifiers() {")
    w(f"\tstatic const RelicAuraModifier kMods[{max(1, len(mods))}] = {{")
    for stat, op, val, alts, name in mods:
        a = ", ".join(alts) if alts else "{0, 0}"
        w(f"\t\t{{{stat}, {op}, {val!r}f, {len(alts)}, {{{a}}}}}, // {name}")
    w("\t};")
    w("\treturn kMods;")
    w("}")
    w("")
    w("inline const RelicGlobalAura *relic_global_auras() {")
    w(f"\tstatic const RelicGlobalAura kAuras[RELIC_GLOBAL_AURA_COUNT] = {{")
    lines.extend(auras)
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("// Range auras multiplying Promotion/RequiredXp (AURA_REQUIRED_XP sources).")
    w("struct RequiredXpRangeAura { const char *id; float multiply; };")
    w(f"constexpr uint16_t REQUIRED_XP_RANGE_AURA_COUNT = {len(xp_auras)};")
    w("inline const RequiredXpRangeAura *required_xp_range_auras() {")
    w(f"\tstatic const RequiredXpRangeAura kAuras[{max(1, len(xp_auras))}] = {{")
    for name, mul in xp_auras:
        w(f"\t\t{{\"{name}\", {mul!r}f}},")
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("// Range auras multiplying Capturable/CapturePoints or GarrisonRegenRate of the structures")
    w("// they reach (stat TSTAT_CAPTURE_POINTS or TSTAT_GARRISON_REGEN); radius in 0 A.D. metres,")
    w("// civic_centre: only Civil Centres (the aura's affects name no other Structure).")
    w("struct CaptureRangeAura { const char *id; uint8_t stat; float multiply; float radius; uint8_t players; bool civic_centre; };")
    w(f"constexpr uint16_t CAPTURE_RANGE_AURA_COUNT = {len(cap_auras)};")
    w("inline const CaptureRangeAura *capture_range_auras() {")
    w(f"\tstatic const CaptureRangeAura kAuras[{max(1, len(cap_auras))}] = {{")
    for name, kind, mul, radius, players, cc in cap_auras:
        w(f"\t\t{{\"{name}\", {kind}, {mul!r}f, {radius!r}f, {players}, {'true' if cc else 'false'}}},")
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("// Range auras on Attack/Capture/Capture of the units they reach (AURA_CAPTURE_MUL and")
    w("// AURA_CAPTURE_ADD sources): ModifiersManager applies value x multiply + add; radius in")
    w("// 0 A.D. metres. Which units each reaches is the hero aura's own affects (civ_roster_data.h).")
    w("struct CaptureAttackRangeAura { const char *id; float multiply; float add; float radius; uint8_t players; };")
    w(f"constexpr uint16_t CAPTURE_ATTACK_RANGE_AURA_COUNT = {len(capatk_auras)};")
    w("inline const CaptureAttackRangeAura *capture_attack_range_auras() {")
    w(f"\tstatic const CaptureAttackRangeAura kAuras[{max(1, len(capatk_auras))}] = {{")
    for name, mul, add, radius, players in capatk_auras:
        w(f"\t\t{{\"{name}\", {mul!r}f, {add!r}f, {radius!r}f, {players}}},")
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("// Range auras on Looter/Resource of the units they reach (AURA_LOOTER sources): a kill's")
    w("// loot is the victim's Loot x multiply + add (Looter.js Collect), per resource in the")
    w("// order food, wood, stone, metal; radius in 0 A.D. metres.")
    w("struct LooterRangeAura { const char *id; float multiply[4]; float add[4]; float radius; uint8_t players; };")
    w(f"constexpr uint16_t LOOTER_RANGE_AURA_COUNT = {len(looter_auras)};")
    w("inline const LooterRangeAura *looter_range_auras() {")
    w(f"\tstatic const LooterRangeAura kAuras[{max(1, len(looter_auras))}] = {{")
    for name, mul, add, radius, players in looter_auras:
        ms = ", ".join(f"{x!r}f" for x in mul)
        ad = ", ".join(f"{x!r}f" for x in add)
        w(f"\t\t{{\"{name}\", {{{ms}}}, {{{ad}}}, {radius!r}f, {players}}},")
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("// Range auras on ResourceGatherer/Rates of the units they reach (AURA_GATHER_RATE sources,")
    w("// value = row): a gather rate x multiply per GatherKind (grain, fruit, meat, fish, wood,")
    w("// stone, metal); radius in 0 A.D. metres.")
    w("struct GatherRateRangeAura { const char *id; float multiply[7]; float radius; uint8_t players; };")
    w(f"constexpr uint16_t GATHER_RATE_RANGE_AURA_COUNT = {len(gather_auras)};")
    w("inline const GatherRateRangeAura *gather_rate_range_auras() {")
    w(f"\tstatic const GatherRateRangeAura kAuras[{max(1, len(gather_auras))}] = {{")
    for name, rates, radius, players in gather_auras:
        rs = ", ".join(f"{x!r}f" for x in rates)
        w(f"\t\t{{\"{name}\", {{{rs}}}, {radius!r}f, {players}}},")
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("// Range auras adding to the MaxRange of the units they reach (AURA_RANGE_ADD sources,")
    w("// value = row): 0 A.D. metres added to a ranged and to a melee attack; radius in 0 A.D. metres.")
    w("struct RangeAddRangeAura { const char *id; float ranged; float melee; float radius; uint8_t players; };")
    w(f"constexpr uint16_t RANGE_ADD_RANGE_AURA_COUNT = {len(range_add_auras)};")
    w("inline const RangeAddRangeAura *range_add_range_auras() {")
    w(f"\tstatic const RangeAddRangeAura kAuras[{max(1, len(range_add_auras))}] = {{")
    for name, ranged, melee, radius, players in range_add_auras:
        w(f"\t\t{{\"{name}\", {ranged!r}f, {melee!r}f, {radius!r}f, {players}}},")
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("// Range auras replacing Loot (xp and resources) with 0 on the entities they reach")
    w("// (AURA_NO_LOOT sources): those give their killer nothing.")
    w("struct NoLootRangeAura { const char *id; float radius; uint8_t players; };")
    w(f"constexpr uint16_t NO_LOOT_RANGE_AURA_COUNT = {len(no_loot_auras)};")
    w("inline const NoLootRangeAura *no_loot_range_auras() {")
    w(f"\tstatic const NoLootRangeAura kAuras[{max(1, len(no_loot_auras))}] = {{")
    for name, radius, players in no_loot_auras:
        w(f"\t\t{{\"{name}\", {radius!r}f, {players}}},")
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("// Every AuraManager effect of the unit auras with more than one kind (extract_0ad_rosters")
    w("// rts0_aura_effects): civ_roster_data.h's CivHeroAura keeps one kind, and rebuild_auras")
    w("// registers the rows of the other kinds from here. range in RTS0 units.")
    w("struct AuraEffectRow { const char *id; uint8_t kind; float value; float range; };")
    w(f"constexpr uint16_t AURA_EFFECT_ROW_COUNT = {len(effect_rows)};")
    w("inline const AuraEffectRow *aura_effect_rows() {")
    w(f"\tstatic const AuraEffectRow kRows[{max(1, len(effect_rows))}] = {{")
    for name, kind, value, rng in effect_rows:
        w(f"\t\t{{\"{name}\", {ros.AURA_KIND[kind]} /* {kind} */, {round(value, 4)!r}f, {round(rng, 4)!r}f}},")
    w("\t};")
    w("\treturn kRows;")
    w("}")
    w("")
    w("// Structures projecting a capture range aura: BuildingType, capture_range_auras() index.")
    w("struct CaptureStructureAura { uint8_t building; uint16_t aura; };")
    w(f"constexpr uint16_t CAPTURE_STRUCTURE_AURA_COUNT = {len(cap_structures)};")
    w("inline const CaptureStructureAura *capture_structure_auras() {")
    w(f"\tstatic const CaptureStructureAura kAuras[{max(1, len(cap_structures))}] = {{")
    for bld, k, name in cap_structures:
        w(f"\t\t{{{bld}, {k}}}, // {name}")
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("// Range auras multiplying Builder/Rate (AURA_BUILD sources). A stackable one")
    w("// applies once per source (Auras.js \"stackable\"), the others once however many reach.")
    w("struct BuilderRateRangeAura { const char *id; float multiply; bool stackable; };")
    w(f"constexpr uint16_t BUILDER_RATE_RANGE_AURA_COUNT = {len(build_auras)};")
    w("inline const BuilderRateRangeAura *builder_rate_range_auras() {")
    w(f"\tstatic const BuilderRateRangeAura kAuras[{max(1, len(build_auras))}] = {{")
    for name, mul, stack in build_auras:
        w(f"\t\t{{\"{name}\", {mul!r}f, {'true' if stack else 'false'}}},")
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("// Garrison auras on BuildingAI: while a unit carrying one is garrisoned, the structure or")
    w("// siege tower holding it multiplies GarrisonArrowMultiplier and MaxArrowCount.")
    w("struct GarrisonArrowAura { const char *id; float arrow_multiply; float max_multiply; };")
    w(f"constexpr uint16_t GARRISON_ARROW_AURA_COUNT = {len(arrow_auras)};")
    w("inline const GarrisonArrowAura *garrison_arrow_auras() {")
    w(f"\tstatic const GarrisonArrowAura kAuras[{max(1, len(arrow_auras))}] = {{")
    for name, arrows, mx in arrow_auras:
        w(f"\t\t{{\"{name}\", {arrows!r}f, {mx!r}f}},")
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("#endif // RTSO_AURA_MODIFIER_DATA_H")
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {args.out}: {len(auras)} global relic auras, {len(mods)} modifiers, {len(xp_auras)} RequiredXp range auras, "
          f"{len(capatk_auras)} capture attack range auras, {len(looter_auras)} looter and "
          f"{len(no_loot_auras)} no-loot range auras, {len(gather_auras)} gather-rate and {len(range_add_auras)} MaxRange range auras, {len(build_auras)} Builder/Rate range auras, "
          f"{len(arrow_auras)} garrison arrow auras")
    for s in skipped:
        print("  not modelled: " + s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
