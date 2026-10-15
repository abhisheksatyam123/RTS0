#!/usr/bin/env python3
"""Generate src/simulation/tech_data.h from 0 A.D.'s technology tree.

Reads 0 A.D.'s public mod (0.28): simulation/data/technologies/*.json for each
technology's cost, research time, requirements and modifications, and
simulation/templates/template_structure_*.xml for which structure researches
it. Only technologies researched at one of RTS0's generic building types are
kept. Modifications RTS0's simulation can apply become TechEffect rows; the
others are dropped (the tech stays researchable for whatever effects remain).
Civ-only technologies of the civs' own structures (structures/<civ>/*.xml, e.g.
the Spartan Gerousia's Agoge) are appended after those, at RTS0's civ-specific
building types (ids 26-45) or the generic type the civ's variant inherits from.
Technologies with no effect RTS0 can apply are kept only when they unlock a unit.

Usage:
    python3 tools/extract_0ad_techs.py --public PATH [--out src/simulation/tech_data.h] [--append]

--append keeps every technology already in --out (ids and rows unchanged, even if
PATH is a different 0 A.D. version) and adds only the civ-only ones it lacks.

PATH is either 0 A.D.'s public.zip or a directory it was unzipped into.
The 0 A.D. data is CC BY-SA 3.0, (C) Wildfire Games; see docs/0AD_ATTRIBUTION.md.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import xml.etree.ElementTree as ET
import zipfile

# VisionSharing bribes (unlock_spies, spy_counter): no TechEffect rows, the simulation reads them.
SPY_TECHS = ("unlock_spies", "spy_counter")
# Read by name: Cartography (Simulation::shares_los, Player/sharedLos), Glorious Expansion
# (Simulation::pop_limit, the wonder's Player/MaxPopulation aura) and Athenian Long Walls
# (build_restriction, Wall BuildRestrictions/Territory "own neutral"), and Equine Transports
# (unit_trainer_data.h: the Persian triremes' cavalry require it).
NAMED_TECHS = SPY_TECHS + ("unlock_shared_los", "wonder_population_cap", "long_walls", "equine_transports")

# RTS0's playable civs in Civilization order (sim_types.h) by 0 A.D. civ code; civ id 14 is
# CIV_GAIA, so the Germans are bit 15.
CIVS = ["athen", "spart", "pers", "rome", "cart", "mace", "brit", "gaul",
        "iber", "maur", "kush", "ptol", "sele", "han", "germ"]
CIV_BIT = {c: (i if i < 14 else i + 1) for i, c in enumerate(CIVS)}
ALL_CIVS = sum(1 << b for b in CIV_BIT.values())

# RTS0 BuildingType -> 0 A.D. generic structure template.
BUILDINGS = [
    ("BLD_TOWN_CENTER", "template_structure_civic_civil_centre"),
    ("BLD_HOUSE", "template_structure_civic_house"),
    ("BLD_BARRACKS", "template_structure_military_barracks"),
    ("BLD_STOREHOUSE", "template_structure_economic_storehouse"),
    ("BLD_TOWER", "template_structure_defensive_tower_stone"),
    ("BLD_FARMSTEAD", "template_structure_economic_farmstead"),
    ("BLD_MARKET", "template_structure_economic_market"),
    ("BLD_FORTRESS", "template_structure_military_fortress"),
    ("BLD_TEMPLE", "template_structure_civic_temple"),
    ("BLD_WONDER", "template_structure_wonder"),
    ("BLD_DOCK", "template_structure_military_dock"),
    ("BLD_CORRAL", "template_structure_resource_corral"),
    ("BLD_STABLE", "template_structure_military_stable"),
    ("BLD_FORGE", "template_structure_military_forge"),
    ("BLD_ARSENAL", "template_structure_military_arsenal"),
    ("BLD_SENTRY_TOWER", "template_structure_defensive_tower_sentry"),
    ("BLD_OUTPOST", "template_structure_defensive_outpost"),
]

# Technologies kept under the legacy RTS0 ids 1..7 (AI and UI refer to them).
LEGACY = [
    ("TECH_PHASE_TOWN", "phase_town_generic"),
    ("TECH_PHASE_CITY", "phase_city_generic"),
    ("TECH_FARMING_1", "gather_farming_plows"),
    ("TECH_WOODCUTTING_1", "gather_lumbering_ironaxes"),
    ("TECH_MINING_1", "gather_mining_wedgemallet"),
    ("TECH_WEAPONS_1", "soldier_attack_melee_01"),
    ("TECH_ARMOR_1", "soldier_resistance_hack_01"),
]

# 0 A.D. class tokens RTS0 knows (sim_types.h TechClass). Unknown tokens in an
# "affects" expression make it never match (required) or are ignored (!negated).
CLASSES = [
    "Unit", "Organic", "Worker", "Citizen", "Civilian", "FemaleCitizen", "Soldier",
    "CitizenSoldier", "Infantry", "Cavalry", "Melee", "Ranged", "Archer", "Spearman",
    "Swordsman", "Champion", "Hero", "Elephant", "Siege", "Ram", "BoltShooter",
    "StoneThrower", "Ship", "Warship", "ArrowShip", "FishingBoat", "Trader", "Healer",
    "Chariot", "Dog", "Structure", "CivCentre", "House", "Barracks", "Stable", "Tower",
    "SentryTower", "StoneTower", "Fortress", "Wall", "Temple", "Wonder", "Dock", "Market",
    "Storehouse", "Farmstead", "Field", "Corral", "Forge", "Arsenal", "Outpost", "Civic",
    # Bits 53-55 were Village, Town and City, which no technology or code tests: 0.28's
    # naval technologies need these three instead (Naval Ballistae, Burning Pitch, Lookouts).
    "Defensive", "NavalSiege", "Ignited", "ScoutShip", "Domestic",
    # Appended for civ-only technologies (bits 57..62).
    "Javelineer", "Immortal", "Crossbowman", "Mercenary", "SiegeTower", "Minister",
    "IceHouse",  # bit 63 (Subterranean Aqueducts)
    "Wagon",  # bit 64 (Wagon Trains)
    "Colony",  # bit 65 (Wagon Trains)
    "Auxiliary",  # bit 66 (Marian Reforms: "Soldier !Auxiliary")
    "TempleOfApedemak",  # bit 67 (Arakamani's Follower of Apedemak: the Kushite temple)
    "Palisade",  # bit 68 (Themistoclean Walls: "Wall" or "Palisade")
]
assert len(CLASSES) <= 128, "TechClasses is a 128-bit mask"

# The class-set type tech_data.h opens with: 128 bits, so 0 A.D. classes past the
# first 64 (TechClass enum values) get bits too. Built from a uint64_t, a TechClass
# or 0; combines with | & ~; tests with `if (c & TC_X)`, any() or != 0.
TECH_CLASSES_STRUCT = r"""// A set of 0 A.D. classes (TechClass bits 0..63 in lo, 64..127 in hi).
struct TechClasses {
	uint64_t lo = 0, hi = 0;
	constexpr TechClasses() = default;
	constexpr TechClasses(uint64_t l) : lo(l) {}
	constexpr TechClasses(uint64_t l, uint64_t h) : lo(l), hi(h) {}
	constexpr bool any() const { return (lo | hi) != 0; }
	constexpr explicit operator bool() const { return any(); }
	constexpr bool contains(TechClasses o) const { return (lo & o.lo) == o.lo && (hi & o.hi) == o.hi; }
	constexpr bool intersects(TechClasses o) const { return ((lo & o.lo) | (hi & o.hi)) != 0; }
	friend constexpr TechClasses operator|(TechClasses a, TechClasses b) { return {a.lo | b.lo, a.hi | b.hi}; }
	friend constexpr TechClasses operator&(TechClasses a, TechClasses b) { return {a.lo & b.lo, a.hi & b.hi}; }
	friend constexpr TechClasses operator~(TechClasses a) { return {~a.lo, ~a.hi}; }
	friend constexpr bool operator==(TechClasses a, TechClasses b) { return a.lo == b.lo && a.hi == b.hi; }
	friend constexpr bool operator!=(TechClasses a, TechClasses b) { return !(a == b); }
	constexpr TechClasses &operator|=(TechClasses o) { lo |= o.lo; hi |= o.hi; return *this; }
	constexpr TechClasses &operator&=(TechClasses o) { lo &= o.lo; hi &= o.hi; return *this; }
};
constexpr TechClasses tc_bit(int i) { return i < 64 ? TechClasses(1ull << i, 0) : TechClasses(0, 1ull << (i - 64)); }""".split("\n")


# 0 A.D. classes RTS0 has no bit for, spelled as classes it has. Exact for the
# civs that research them: NavalRam (warship_ramming_speed) is a melee warship
# for every civ in its civ list (the melee fire ships are brit, gaul and iber).
# RTS0's TC_CIVILIAN is 0.28's Support (every support unit); 0.28's own Civilian class is only
# on template_unit_support_civilian (and RTS0's villager): a Support Worker that is no
# FemaleCitizen. CivilCentre is the VisibleClass that rides with CivCentre.
CLASS_ALIASES = {"NavalRam": "Warship Melee", "Civilian": "Civilian Worker !FemaleCitizen",
                 "CivilCentre": "CivCentre", "!CivilCentre": "!CivCentre"}

# Civ-only technologies left out on purpose.
CIV_TECH_SKIP = {
    "unlock_females_house",  # newer 0 A.D. name of unlock_civilians_house_generic (id 77)
}

# RTS0 civ-specific BuildingType -> 0 A.D. structures/<civ>/<stem>.xml stems
# (sim_types.h ids 26-45, see tools/extract_0ad_rosters.py UNIQUE_BUILDINGS).
CIV_STRUCTURES = {
    "gymnasium": "BLD_GYMNASIUM", "prytaneion": "BLD_PRYTANEION",
    "gerousia": "BLD_GEROUSIA", "syssiton": "BLD_SYSSITON", "tachara": "BLD_TACHARA",
    "army_camp": "BLD_ARMY_CAMP", "amphitheater_pompeii": "BLD_AMPHITHEATER",
    "library": "BLD_LIBRARY", "kennel": "BLD_KENNEL", "assembly": "BLD_ASSEMBLY",
    "monument": "BLD_MONUMENT", "palace": "BLD_PALACE", "pillar_ashoka": "BLD_PILLAR",
    "pyramid_large": "BLD_PYRAMID_LARGE", "pyramid_small": "BLD_PYRAMID_SMALL",
    "mercenary_camp": "BLD_MERCENARY_CAMP", "lighthouse": "BLD_LIGHTHOUSE",
    "military_colony": "BLD_MILITARY_COLONY", "academy": "BLD_ACADEMY",
    "ministry": "BLD_MINISTRY", "ice_house": "BLD_ICE_HOUSE",
}

GATHER_KINDS = {"food.grain": "GATHER_GRAIN", "food.fruit": "GATHER_FRUIT",
                "food.meat": "GATHER_MEAT", "food.fish": "GATHER_FISH",
                "wood.tree": "GATHER_WOOD", "stone.rock": "GATHER_STONE",
                "metal.ore": "GATHER_METAL"}


# Modifications RTS0 applies outside tech_data.h's effect table: promotion_data.h
# (tools/extract_0ad_promotion.py) reads Promotion/RequiredXp from the techs here.
APPLIED_ELSEWHERE = {"Promotion/RequiredXp"}


def stat_of(path: str):
    """Map a 0 A.D. modification path to (TechStat, op hint) or None."""
    m = re.fullmatch(r"ResourceGatherer/Rates/(.+)", path)
    if m and m.group(1) in GATHER_KINDS:
        return "TSTAT_" + GATHER_KINDS[m.group(1)]
    m = re.fullmatch(r"ResourceTrickle/Rates/(food|wood|stone|metal)", path)
    if m:
        return "TSTAT_TRICKLE_" + m.group(1).upper()
    if re.fullmatch(r"ResourceGatherer/Capacities/(food|wood|stone|metal)", path):
        return "TSTAT_CARRY_" + path.rsplit("/", 1)[1].upper()
    m = re.fullmatch(r"Attack/(Melee|Ranged)/Damage/(Hack|Pierce|Crush|Fire)", path)
    if m:
        return "TSTAT_" + m.group(1).upper() + "_" + m.group(2).upper()
    m = re.fullmatch(r"Loot/(food|wood|stone|metal)", path)
    if m:  # what the unit itself yields when killed (Sacrificial Ritual, Celtic Auxiliaries)
        return "TSTAT_UNIT_LOOT_" + m.group(1).upper()
    m = re.fullmatch(r"Resistance/Entity/Damage/(Hack|Pierce|Crush)", path)
    if m:
        return "TSTAT_ARMOR_" + m.group(1).upper()
    return {
        "Health/Max": "TSTAT_HEALTH",
        "UnitMotion/WalkSpeed": "TSTAT_SPEED",
        "Population/Bonus": "TSTAT_POP_BONUS",
        "Trader/GainMultiplier": "TSTAT_TRADE_GAIN",
        "Attack/Ranged/MaxRange": "TSTAT_RANGE",
        "Cost/BuildTime": "TSTAT_BUILD_TIME",
        "Heal/Range": "TSTAT_HEAL_RANGE",
        "Heal/Interval": "TSTAT_HEAL_INTERVAL",
        "BuildingAI/DefaultArrowCount": "TSTAT_ARROWS",
        "BuildingAI/MaxArrowCount": "TSTAT_ARROWS",
        "Researcher/TechCostMultiplier/time": "TSTAT_RESEARCH_TIME",
        "Pack/Time": "TSTAT_PACK_TIME",
        "GarrisonHolder/BuffHeal": "TSTAT_GARRISON_HEAL",
        "Health/RegenRate": "TSTAT_REGEN",
        "Health/IdleRegenRate": "TSTAT_IDLE_REGEN",
        "Capturable/CapturePoints": "TSTAT_CAPTURE_POINTS",
        "Capturable/GarrisonRegenRate": "TSTAT_GARRISON_REGEN",
        "TerritoryInfluence/Radius": "TSTAT_TERRITORY_RADIUS",
        "Cost/Resources/food": "TSTAT_COST_FOOD",
        "Cost/Resources/wood": "TSTAT_COST_WOOD",
        "Cost/Resources/stone": "TSTAT_COST_STONE",
        "Cost/Resources/metal": "TSTAT_COST_METAL",
        "Market/InternationalBonus": "TSTAT_TRADE_INTERNATIONAL",
        "Trainer/BatchTimeModifier": "TSTAT_BATCH_TIME",
        "Vision/Range": "TSTAT_VISION",
        "Attack/Ranged/Projectile/Spread": "TSTAT_SPREAD",
        "BuildingAI/GarrisonArrowMultiplier": "TSTAT_GARRISON_ARROWS",
        "Attack/Ranged/MinRange": "TSTAT_MIN_RANGE",
        "TerritoryDecay/DecayRate": "TSTAT_TERRITORY_DECAY",
    }.get(path)


class Source:
    def __init__(self, path: str):
        self.zip = zipfile.ZipFile(path) if path.endswith(".zip") else None
        self.root = path

    def read(self, rel: str) -> bytes | None:
        if self.zip:
            try:
                return self.zip.read(rel)
            except KeyError:
                return None
        p = os.path.join(self.root, rel)
        return open(p, "rb").read() if os.path.exists(p) else None


def template_researchables(src: Source, name: str) -> list[str]:
    """Researcher/Technologies of a template, following parent templates."""
    data = src.read(f"simulation/templates/{name}.xml")
    if data is None:
        data = src.read(f"simulation/templates/mixins/{name}.xml")
    if data is None:
        return []
    root = ET.fromstring(data)
    techs: list[str] = []
    for parent in reversed((root.get("parent") or "").split("|")):
        if parent:
            techs = template_researchables(src, parent) or techs
    node = root.find("Researcher/Technologies")
    if node is not None and node.text:
        own = node.text.split()
        if node.get("datatype") == "tokens" and node.get("replace") is None:
            for t in own:
                if t.startswith("-"):
                    techs = [x for x in techs if x != t[1:]]
                elif t not in techs:
                    techs.append(t)
        else:
            techs = own
    return techs


def template_parents(src: Source, name: str) -> list[str]:
    """`name` and every template it inherits from, nearest first."""
    data = src.read(f"simulation/templates/{name}.xml")
    if data is None:
        return []
    out = [name]
    for parent in (ET.fromstring(data).get("parent") or "").split("|"):
        if parent:
            out += template_parents(src, parent)
    return out


def civ_structure_researchers(src: Source) -> dict[str, str]:
    """Civ-only technologies researched at structures/<civ>/*.xml, by the RTS0
    BuildingType of that structure (a civ-specific one, or the generic one the
    civ's variant inherits from)."""
    generic = {tpl: bld for bld, tpl in BUILDINGS}
    out: dict[str, str] = {}
    for civ in CIVS:
        prefix = f"simulation/templates/structures/{civ}/"
        if src.zip:
            names = sorted(n[len(prefix):-4] for n in src.zip.namelist()
                           if n.startswith(prefix) and n.endswith(".xml"))
        else:
            d = os.path.join(src.root, prefix)
            names = sorted(f[:-4] for f in os.listdir(d) if f.endswith(".xml")) if os.path.isdir(d) else []
        for stem in names:
            tpl = f"structures/{civ}/{stem}"
            bld = next((b for k, b in CIV_STRUCTURES.items() if stem == k or stem.startswith(k + "_")), None)
            if bld is None:
                bld = next((generic[p] for p in template_parents(src, tpl) if p in generic), None)
            if bld is None:
                continue
            for t in template_researchables(src, tpl):
                for name in pair_members(src, t.replace("{civ}", civ)):
                    out.setdefault(name, bld)
    return out


def pair_members(src: Source, name: str) -> list[str]:
    """A researcher lists a pair_*.json as one entry; the player picks its "top" or
    its "bottom" technology, and researching one forbids the other."""
    if not name.startswith("pair_"):
        return [name]
    data = src.read(f"simulation/data/technologies/{name}.json")
    j = json.loads(data) if data else {}
    return [j[k] for k in ("top", "bottom") if k in j]


def unit_unlock_techs(src: Source) -> set[str]:
    """Technologies named by a unit's Identity/Requirements/Techs (they unlock it)."""
    out: set[str] = set()
    pat = re.compile(rb"<Requirements>\s*<Techs[^>]*>([^<]*)</Techs>")
    if src.zip:
        names = [n for n in src.zip.namelist() if n.startswith("simulation/templates/units/") and n.endswith(".xml")]
    else:
        base = os.path.join(src.root, "simulation/templates/units")
        names = [os.path.relpath(os.path.join(r, f), src.root) for r, _, fs in os.walk(base) for f in fs if f.endswith(".xml")]
    for n in names:
        for m in pat.finditer(src.read(n) or b""):
            out.update(m.group(1).decode().split())
    return out


def civ_mask(req) -> int:
    """Civs (bitmask over CIVS) for which a requirements tree can hold."""
    full = ALL_CIVS
    if req is None:
        return full
    if isinstance(req, list):
        req = {"all": req}
    if "all" in req:
        m = full
        for r in req["all"]:
            m &= civ_mask(r)
        return m
    if "any" in req:
        m = 0
        for r in req["any"]:
            m |= civ_mask(r)
        return m
    if "civ" in req:
        return 1 << CIV_BIT[req["civ"]] if req["civ"] in CIVS else 0
    if "notciv" in req:
        return full & ~(1 << CIV_BIT[req["notciv"]]) if req["notciv"] in CIVS else full
    return full


def tech_reqs(req, out: list[str]) -> None:
    if req is None:
        return
    if isinstance(req, list):
        for r in req:
            tech_reqs(r, out)
        return
    for key in ("all", "any"):
        if key in req:
            for r in req[key]:
                tech_reqs(r, out)
    if "tech" in req:
        out.append(req["tech"])


def c_str(s: str) -> str:
    return json.dumps(s, ensure_ascii=False)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--public", required=True, help="0 A.D. public.zip or unzipped public mod dir")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "src", "simulation", "tech_data.h"))
    ap.add_argument("--append", action="store_true",
                    help="keep --out's existing technologies (ids, rows) and append only the civ-only ones it lacks")
    ap.add_argument("--add-techs", default="",
                    help="with --append: comma-separated generic technologies to append as well")
    ap.add_argument("--add-stats", default="",
                    help="with --append: comma-separated TechStats newly mapped in stat_of(); every kept "
                         "technology also gets its modifications of those stats from --public")
    ap.add_argument("--add-classes", default="",
                    help="with --append: comma-separated 0 A.D. classes newly spelled in CLASSES or "
                         "CLASS_ALIASES; every kept technology gains rows for its affects entries naming them")
    args = ap.parse_args()
    src = Source(args.public)

    researcher: dict[str, str] = {}
    for bld, tpl in BUILDINGS:
        for t in template_researchables(src, tpl):
            t = t.replace("{civ}", "generic")
            researcher.setdefault(t, bld)

    def load(name):
        data = src.read(f"simulation/data/technologies/{name}.json")
        return json.loads(data) if data else None

    order = [name for _, name in LEGACY]
    for name in sorted(researcher):
        if name in order or name.startswith("pair_"):
            continue
        j = load(name)
        if not j or not j.get("cost") or "researchTime" not in j:
            continue
        mods = [m for m in j.get("modifications", []) if stat_of(m["value"])]
        # The spy techs work through Simulation::cmd_bribe (Player/SpyCostMultiplier there too).
        if not mods and not name.startswith(("unlock_champion_", "unlock_civilians_")) and name not in NAMED_TECHS:
            continue  # nothing RTS0 can apply yet (shared LOS, spread, ...)
        if civ_mask(j.get("requirements")) == 0:
            continue  # only civs RTS0 doesn't have
        order.append(name)
    # Civ-only technologies of civ-specific structures (and of civ variants of
    # the generic ones), appended so the ids above stay the same.
    full = ALL_CIVS
    unlockers = unit_unlock_techs(src)
    civ_researcher = civ_structure_researchers(src)
    civ_only: list[str] = []
    for name in sorted(civ_researcher):
        if name in order or name in CIV_TECH_SKIP or name.startswith(("pair_", "phase_")):
            continue
        j = load(name)
        # A pair member that only unlocks units may be free and instant (the Seleucid army
        # reforms have neither cost nor researchTime).
        if not j or ((not j.get("cost") or "researchTime" not in j) and not (j.get("pair") and name in unlockers)):
            continue
        mask = civ_mask(j.get("requirements"))
        if mask in (0, full):
            continue  # all-civ techs belong to the generic buildings above
        mods = [m for m in j.get("modifications", []) if stat_of(m["value"]) or m["value"] in APPLIED_ELSEWHERE]
        if not mods and name not in unlockers:
            continue
        order.append(name)
        civ_only.append(name)
        researcher.setdefault(name, civ_researcher[name])
    # Keep the ids an existing tech_data.h already gave (saves and lockstep store them);
    # technologies new to it get the next ids.
    previous = []
    if os.path.exists(args.out):
        with open(args.out, encoding="utf-8") as f:
            previous = re.findall(r"^\tTECH_\w+ = \d+, // (\S+)$", f.read(), re.M)
    kept = set(order)
    order = [n for n in previous if n in kept] + [n for n in order if n not in previous]
    if len(order) + 1 > 255:
        print("too many technologies for uint8_t ids", file=sys.stderr)
        return 1

    # --append: the technologies already in --out keep their ids and rows
    # verbatim (they may come from another 0 A.D. version), and only the
    # civ-only ones they lack are generated and appended after them.
    kept_ids: list[tuple[str, str]] = []
    kept_affects: list[str] = []
    kept_effects: list[str] = []
    kept_techs: list[str] = []
    emit = order
    if args.append:
        text = open(args.out, encoding="utf-8").read()

        def block(start: str) -> list[str]:
            body = text.split(start, 1)[1].split("\n\t};", 1)[0]
            return [l for l in body.split("\n")[2:] if l.strip()]  # skip the header and fixed first row
        kept_ids = re.findall(r"^\t(TECH_[A-Z0-9_]+) = \d+, // (\S+)(?: .*)?$", text, re.M)
        kept_affects = block("static const TechClassExpr kAffects[] = {")
        kept_effects = block("static const TechEffect kEffects[] = {")
        kept_techs = block("static const TechInfo kTable[TECH_COUNT] = {")
        kept = {name for _, name in kept_ids}
        emit = [name for name in civ_only if name not in kept]
        # --add-techs: generic technologies newly portable (a stat stat_of() now maps).
        extra = [n for n in args.add_techs.split(",") if n]
        missing = [n for n in extra if n not in order]
        if missing:
            print("--add-techs: not researchable or nothing RTS0 applies: " + ",".join(missing), file=sys.stderr)
            return 1
        emit += [n for n in extra if n not in kept and n not in emit]
        if args.add_stats or args.add_classes:
            new_stats = set(filter(None, args.add_stats.split(",")))
            new_classes = set(filter(None, args.add_classes.split(",")))
            pending_kept = (kept_effects, kept_techs, new_stats, new_classes)
        order = [name for _, name in kept_ids] + emit
        if len(order) + 1 > 255:
            print("too many technologies for uint8_t ids", file=sys.stderr)
            return 1

    if not (args.append and (args.add_stats or args.add_classes)):
        pending_kept = None
    ids: dict[str, str | None] = {name: None for name in order}
    for sym, name in kept_ids:
        ids[name] = sym
    for (sym, name) in LEGACY:
        ids[name] = sym
    for name in order:
        if ids[name] is None:
            ids[name] = "TECH_" + re.sub(r"[^A-Z0-9]", "_", name.upper())

    affects_rows: list[tuple[str, ...]] = []
    effects: list[str] = []
    techs: list[str] = []

    def class_expr(tokens: str) -> str | None:
        need, forbid = [], []
        tokens = " ".join(CLASS_ALIASES.get(t, t) for t in tokens.split())
        for tok in tokens.split():
            neg = tok.startswith("!")
            tok = tok[1:] if neg else tok
            if tok not in CLASSES:
                if not neg:
                    return None  # can never match an RTS0 entity
                continue
            (forbid if neg else need).append("TC_" + tok.upper())
        n = " | ".join(need) or "0"
        f = " | ".join(forbid) or "0"
        return f"{{{n}, {f}}}"

    def affects_index(aff) -> tuple[int, int]:
        if not aff:
            return (0, 0)
        if isinstance(aff, str):
            aff = [aff]
        rows = [r for r in (class_expr(a) for a in aff) if r]
        if not rows:
            return (0, -1)
        key = tuple(rows)
        start = 0
        for k in affects_rows:
            if k == key:
                break
            start += len(k)
        else:
            affects_rows.append(key)
        return (len(kept_affects) + start + 1, len(rows))  # slot 0 is reserved for "no filter"

    def effect_lines(name, j, only=None, classes=None):
        """Effect rows of technology j: only the stats in `only`, and with `classes`
        only the affects entries that name one of those classes."""
        out = []
        tech_aff = j.get("affects")
        for m in j.get("modifications", []):
            stat = stat_of(m["value"])
            if not stat or (only is not None and stat not in only):
                continue
            if "multiply" in m:
                op, val = "TOP_MUL", float(m["multiply"])
            elif "add" in m:
                op, val = "TOP_ADD", float(m["add"])
            elif "replace" in m and isinstance(m["replace"], (int, float)):
                op, val = "TOP_SET", float(m["replace"])
            else:
                continue
            aff = m.get("affects", tech_aff)
            if classes is not None:
                entries = [aff] if isinstance(aff, str) else (aff or [])
                aff = [a for a in entries if classes & set(a.split())]
                if not aff:
                    continue
            if stat.startswith("TSTAT_TRICKLE_") and aff in (["Player"], "Player"):
                aff = None  # template_player.xml's ResourceTrickle: an unfiltered trickle row
            if stat == "TSTAT_TRADE_INTERNATIONAL" and aff in (["Trade"], "Trade"):
                aff = ["Market", "Dock"]  # the Trade class: markets and docks (Market component)
            a_start, a_count = affects_index(aff)
            if a_count < 0:
                continue
            out.append(f"\t\t{{{stat}, {op}, {a_start}, {a_count}, {val!r}f}}, // {name}")
        return out

    # --add-stats / --add-classes: each kept technology keeps its effect rows and
    # gains rows for the newly mapped stats or classes; effect_begin/effect_count
    # are renumbered.
    if pending_kept:
        k_effects, k_techs, new_stats, new_classes = pending_kept
        rebuilt_effects: list[str] = []
        rebuilt_techs: list[str] = []
        row_re = re.compile(r", (\d+), (\d+), (\"[^\"]*\")\},$")
        for row in k_techs:
            if row.strip().startswith("//"):  # hand-written notes stay where they are
                rebuilt_techs.append(row)
                continue
            mo = row_re.search(row)
            begin, count, tname = int(mo.group(1)), int(mo.group(2)), mo.group(3)[1:-1]
            nb = len(rebuilt_effects)
            rebuilt_effects.extend(k_effects[begin:begin + count])
            j = load(tname)
            head = row[:mo.start()]
            if j:
                if new_stats:
                    rebuilt_effects.extend(effect_lines(tname, j, new_stats))
                if new_classes:
                    # A stat the kept rows already change keeps their value (they may
                    # come from another 0 A.D. version), so the tooltip stays true.
                    old = {}
                    for e in k_effects[begin:begin + count]:
                        em = re.match(r"\t\t\{(\w+), (\w+), \d+, \d+, ([^}]+)\}", e)
                        old[(em.group(1), em.group(2))] = em.group(3)
                    for e in effect_lines(tname, j, None, new_classes):
                        em = re.match(r"(\t\t\{(\w+), (\w+), \d+, \d+, )([^}]+)(\}.*)", e)
                        rebuilt_effects.append(em.group(1) + old.get((em.group(2), em.group(3)), em.group(4)) + em.group(5))
                # A technology this run appends that a kept one supersedes becomes
                # its prerequisite (Bronze Ram after Flanking Maneuvers).
                if j.get("supersedes") in emit:
                    sup = "TECH_" + re.sub(r"[^A-Z0-9]", "_", j["supersedes"].upper())
                    head = re.sub(r", TECH_NONE(, 0x[0-9a-f]+)$", r", " + sup + r"\1", head)
            rebuilt_techs.append(head + f", {nb}, {len(rebuilt_effects) - nb}, \"{tname}\"}},")
        kept_effects[:] = rebuilt_effects
        kept_techs[:] = rebuilt_techs

    for name in emit:
        j = load(name)
        cost = j.get("cost") or {}
        reqs: list[str] = []
        tech_reqs(j.get("requirements"), reqs)
        phase = 1
        prereq = "TECH_NONE"
        for r in reqs:
            if r == "phase_village":
                phase = max(phase, 1)
            elif r.startswith("phase_town"):
                phase = max(phase, 2)
            elif r.startswith("phase_city"):
                phase = max(phase, 3)
            elif r in ids:
                prereq = ids[r]
        # "supersedes": the technology replaces an earlier one in its slot, so
        # that one has to be researched first (Silk Road after Advanced Handicraft).
        if prereq == "TECH_NONE" and j.get("supersedes") in ids:
            prereq = ids[j["supersedes"]]
        if name == "phase_town_generic":
            phase = 1
        elif name == "phase_city_generic":
            phase = 2
        cmask = civ_mask(j.get("requirements"))
        e_begin = len(kept_effects) + len(effects)
        effects.extend(effect_lines(name, j))
        e_count = len(kept_effects) + len(effects) - e_begin
        desc = j.get("tooltip", "").replace("\n", " ")
        bld = researcher.get(name, "BLD_TOWN_CENTER")
        if name in ("phase_town_generic", "phase_city_generic"):
            bld = "BLD_TOWN_CENTER"
        techs.append(
            f"\t\t{{{c_str(j.get('genericName', name))}, {c_str(desc)}, "
            f"{int(cost.get('food', 0))}, {int(cost.get('wood', 0))}, {int(cost.get('metal', 0))}, "
            f"{int(cost.get('stone', 0))}, {float(j.get('researchTime', 0))!r}f, {phase}, {bld}, "
            f"{prereq}, 0x{cmask:04x}, {e_begin}, {e_count}, {c_str(name)}}},")

    lines = []
    w = lines.append
    w("// GENERATED by tools/extract_0ad_techs.py from 0 A.D. 0.28")
    w("// simulation/data/technologies/*.json (CC BY-SA 3.0, Wildfire Games). Do not edit by hand.")
    w("#ifndef RTSO_TECH_DATA_H")
    w("#define RTSO_TECH_DATA_H")
    w("")
    w("// Included from sim_types.h after BuildingType and Civilization.")
    w("")
    w("enum TechnologyId : uint8_t {")
    w("\tTECH_NONE = 0,")
    for i, name in enumerate(order):
        w(f"\t{ids[name]} = {i + 1}, // {name}")
    w(f"\tTECH_COUNT = {len(order) + 1}")
    w("};")
    w("")
    for line in TECH_CLASSES_STRUCT:
        w(line)
    w("")
    w("// Bits 0..63; 0 A.D. classes from bit 64 up are TechClasses constants below.")
    w("enum TechClass : uint64_t {")
    for i, c in enumerate(CLASSES[:64]):
        w(f"\tTC_{c.upper()} = 1ull << {i},")
    w("};")
    for i, c in enumerate(CLASSES[64:], 64):
        w(f"inline constexpr TechClasses TC_{c.upper()} = tc_bit({i});")
    w("")
    w("// One 0 A.D. \"affects\" alternative: every `need` class and no `forbid` class.")
    w("struct TechClassExpr { TechClasses need; TechClasses forbid; };")
    w("")
    w("inline const TechClassExpr *tech_affects_table() {")
    w("\tstatic const TechClassExpr kAffects[] = {")
    w("\t\t{0, 0}, // unused: affects index 0 means \"no filter\"")
    lines.extend(kept_affects)
    for row in affects_rows:
        for expr in row:
            w(f"\t\t{expr},")
    w("\t};")
    w("\treturn kAffects;")
    w("}")
    w("")
    w("inline const TechEffect *tech_effects_table() {")
    w("\tstatic const TechEffect kEffects[] = {")
    w("\t\t{TSTAT_NONE, TOP_MUL, 0, 0, 1.0f},")
    lines.extend(kept_effects)
    for e in effects:
        w(e)
    w("\t};")
    w("\treturn kEffects + 1;")
    w("}")
    w(f"constexpr int TECH_EFFECT_COUNT = {len(kept_effects) + len(effects)};")
    w("")
    w("inline const TechInfo &tech_info(uint8_t tech_id) {")
    w("\tstatic const TechInfo kTable[TECH_COUNT] = {")
    w("\t\t{\"None\", \"\", 0, 0, 0, 0, 0.0f, 0, BLD_TOWN_CENTER, TECH_NONE, 0, 0, 0, \"\"},")
    lines.extend(kept_techs)
    lines.extend(techs)
    w("\t};")
    w("\tif (tech_id >= TECH_COUNT) return kTable[TECH_NONE];")
    w("\treturn kTable[tech_id];")
    w("}")
    w("")
    # 0 A.D. pair_*.json: each technology's "pair" partner (Researcher offers the two
    # side by side; researching or queueing one removes the other).
    pairs: dict[str, str] = {}
    for name in order:
        j = load(name) or {}
        if "pair" not in j:
            continue
        pj = json.loads(src.read(f"simulation/data/technologies/{j['pair']}.json") or b"{}")
        other = pj.get("bottom") if pj.get("top") == name else pj.get("top")
        if other in ids:
            pairs[name] = other
    w("// The technology paired with `tech_id` (0 A.D. pair_*.json), or TECH_NONE.")
    w("inline uint8_t tech_pair_partner(uint8_t tech_id) {")
    w("\tswitch (tech_id) {")
    for name, other in pairs.items():
        w(f"\t\tcase {ids[name]}: return {ids[other]};")
    w("\t\tdefault: return TECH_NONE;")
    w("\t}")
    w("}")
    w("")
    # globalscripts/Technologies.js DeriveTechnologyRequirements: "supersedes" is one
    # more required technology (Researcher.js shows a chain's next step only).
    w("// The technology `tech_id` supersedes (0 A.D. \"supersedes\", a required technology), or TECH_NONE.")
    w("inline uint8_t tech_supersedes(uint8_t tech_id) {")
    w("\tswitch (tech_id) {")
    for name in order:
        sup = (load(name) or {}).get("supersedes")
        if sup in ids and not sup.startswith("phase_"):
            w(f"\t\tcase {ids[name]}: return {ids[sup]};")
    w("\t\tdefault: return TECH_NONE;")
    w("\t}")
    w("}")
    w("")
    w("#endif // RTSO_TECH_DATA_H")
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {args.out}: {len(order)} technologies ({len(emit)} generated), "
          f"{len(kept_effects) + len(effects)} effects")
    return 0


if __name__ == "__main__":
    sys.exit(main())
