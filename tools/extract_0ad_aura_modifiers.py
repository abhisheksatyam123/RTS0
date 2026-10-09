#!/usr/bin/env python3
"""Generate src/simulation/aura_modifier_data.h from 0 A.D.'s auras.

Reads simulation/data/auras/ from 0 A.D.'s public mod (0.28):
- units/catafalques/*.json: each "global" or "player" aura becomes a list of player
  modifiers that Simulation::compute_tech_mods folds in for every player the aura
  reaches while a relic carrying it is owned (Capture the Relic). Their "range" auras
  stay with the AuraManager (civ_roster_data.h).
- every "range" aura with a Promotion/RequiredXp modification (Han Xin's Northern
  Army, the centurion): its multiplier, registered as an AURA_REQUIRED_XP source.

Usage:
    python3 tools/extract_0ad_aura_modifiers.py --public PATH [--out src/simulation/aura_modifier_data.h]

PATH is either 0 A.D.'s public.zip or a directory it was unzipped into.
The 0 A.D. data is CC BY-SA 3.0, (C) Wildfire Games; see docs/0AD_ATTRIBUTION.md.
"""

from __future__ import annotations

import argparse
import json
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
}

# Auras.js affectedPlayers -> AuraPlayers bits (aura_manager.h).
PLAYERS = {"Player": 1, "Ally": 3, "MutualAlly": 3, "Enemy": 4}


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
                "iber", "maur", "kush", "ptol", "sele", "han"]:
        for suffix in ["", "_1", "_2", "_3"]:
            if src.read(f"{AURA_DIR}/{civ}_catafalque{suffix}.json"):
                names.append(f"{civ}_catafalque{suffix}")

    auras: list[str] = []
    mods: list[str] = []
    skipped: list[str] = []
    max_alts = 1
    for name in names:
        j = json.loads(src.read(f"{AURA_DIR}/{name}.json"))
        if j.get("type") not in ("global", "player"):
            continue  # range auras: civ_roster_data.h / AuraManager
        players = 0
        for p in j.get("affectedPlayers") or ["Player"]:
            players |= PLAYERS.get(p, 0)
        begin = len(mods)
        for m in j.get("modifications", []):
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
        auras.append(f"\t\t{{\"{name}\", {players}, {begin}, {len(mods) - begin}}},")

    # Range auras with a RequiredXp multiplier, anywhere under auras/.
    xp_auras: list[tuple[str, float]] = []
    for rel in sorted(list_files(src, "simulation/data/auras/")):
        if not rel.endswith(".json"):
            continue
        j = json.loads(src.read(rel))
        if j.get("type") != "range":
            continue
        for m in j.get("modifications", []):
            if m["value"] == "Promotion/RequiredXp" and "multiply" in m:
                xp_auras.append((os.path.basename(rel)[:-5], float(m["multiply"])))

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
    w("#endif // RTSO_AURA_MODIFIER_DATA_H")
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {args.out}: {len(auras)} global relic auras, {len(mods)} modifiers, {len(xp_auras)} RequiredXp range auras")
    for s in skipped:
        print("  not modelled: " + s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
