#!/usr/bin/env python3
"""Generate RTS0's fauna, treasure and ruins tables from 0 A.D.'s Gaia templates.

Reads, under binaries/data/mods/public/simulation/templates:
  * gaia/fauna_*.xml (61 animals) through their template_unit_fauna_* parents
  * gaia/treasure/*.xml (template_gaia_treasure, Treasure.js)
  * gaia/ruins/*.xml (template_gaia_ruins, a ResourceSupply of stone or metal)
  * structures/*.xml whose parent is plain template_structure (fences, columns, obelisks,
    benches): scenery that maps give to Gaia

Template inheritance (parent chains, op="mul", disable="", tokens) is resolved by
extract_0ad_rosters.TemplateLoader, the same loader the civ rosters use.

Stats are 0 A.D.'s own (health, damage, armour levels, repeat time, food,
treasure amounts, collect time). Distances and speeds use the roster tool's
scales, because RTS0's world is smaller than 0 A.D.'s:
  * walk speed x SPEED_SCALE
  * melee reach x MELEE_RANGE_SCALE
  * vision, flee and roam distances x RANGED_RANGE_SCALE (an archer's 60 m -> ~17)

Usage:
  python3 -I tools/extract_0ad_fauna.py --0ad PATH/TO/0ad [--check]

Writes src/simulation/gaia_data.h. --check regenerates into memory and fails if
the checked-in file differs.
"""

import argparse
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_rosters import (  # noqa: E402
    CIVS, CIV_COUNT, CIV_ENUM_OF, CIV_SLOTS, TemplateLoader, SPEED_SCALE, MELEE_RANGE_SCALE, RANGED_RANGE_SCALE)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_HEADER = os.path.join(REPO, "src", "simulation", "gaia_data.h")

# RTS0 kept these four ids before the port (maps, rms_generator and saves use them).
FIRST = ["fauna_deer", "fauna_boar", "fauna_sheep", "fauna_wolf"]

STANCES = ["passive", "skittish", "passive-defensive", "defensive", "aggressive", "violent"]

# Animals whose 0 A.D. actor tools/extract_0ad_fauna_models.py converts get that
# model (fauna_<actor>.glb). The rest use the models extract_0ad_assets.py made
# (assets/models/units/<id>.glb), picked by template name below.
from extract_0ad_fauna_models import MODELS as ACTOR_MODELS  # noqa: E402

MODEL_BY_ACTOR = {spec[0]: model_id for model_id, spec in ACTOR_MODELS.items()}

MODEL = {
    "bear": "bear", "boar": "boar", "camel": "camel", "cattle": "cow", "chicken": "chicken",
    "crocodile": "crocodile", "deer": "deer", "dog_mastiff": "mastiff_gaia",
    "dog_wolfhound": "wolfhound", "donkey": "donkey", "elephant_african_bush": "elephant_bush",
    "elephant_african_infant": "elephant_bush", "elephant_asian": "elephant_forest",
    "elephant_asian_infant": "elephant_forest", "elephant_north_african": "elephant_forest",
    "fox": "fox", "gazelle": "gazelle", "giraffe": "giraffe", "goat": "goat",
    "hippopotamus": "boar", "horse": "donkey", "lion": "lion", "lioness": "lioness",
    "muskox": "cow", "peacock": "chicken", "pig": "pig", "piglet": "pig", "rabbit": "rabbit",
    "rhinoceros": "boar", "shark": "crocodile", "sheep": "sheep", "tiger": "lion",
    "walrus": "boar", "whale": "crocodile", "wildebeest": "cow", "wolf": "wolf", "zebra": "zebra",
}


# 0.28 actors that need their own RTS0 model although the template name maps elsewhere.
MODEL_BY_ZIP_ACTOR = {
    "fauna/sheep3.xml": "sheep_gaia",  # Gaia sheep: sheep1 without the player-colour patch
}


def model_for(stem, actor):
    if actor in MODEL_BY_ZIP_ACTOR:
        return MODEL_BY_ZIP_ACTOR[actor]
    if actor in MODEL_BY_ACTOR:
        return MODEL_BY_ACTOR[actor]
    key = stem[len("fauna_"):]
    best = None
    for k, v in MODEL.items():
        if key.startswith(k) and (best is None or len(k) > len(best[0])):
            best = (k, v)
    if best is None:
        raise SystemExit(f"no RTS0 model for {stem}: add it to MODEL")
    return best[1]


def r1(x):
    return round(x, 3)


def fauna_rows(loader):
    stems = sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(loader.root, "gaia", "fauna_*.xml")))
    order = FIRST + [s for s in stems if s not in FIRST]
    rows = []
    for stem in order:
        t = loader.load("gaia/" + stem)
        classes = t.tokens("Identity/Classes")
        stance = t.value("UnitAI/DefaultStance", "passive")
        if stance not in STANCES:
            raise SystemExit(f"{stem}: unknown stance {stance}")
        melee = t.get("Attack/Melee")
        hack = pierce = crush = rng = rep = prep = 0.0
        if melee is not None:
            hack = melee.num("Damage/Hack")
            pierce = melee.num("Damage/Pierce")
            crush = melee.num("Damage/Crush")
            rng = melee.num("MaxRange")
            rep = melee.num("RepeatTime") / 1000.0
            prep = melee.num("PrepareTime", 0.0) / 1000.0
        supply = t.get("ResourceSupply")
        food = int(supply.num("Max")) if supply is not None else 0
        rtype = supply.value("Type", "") if supply is not None else ""
        flags = []
        if "Domestic" in classes:
            flags.append("FAUNA_F_DOMESTIC")
        if "SeaCreature" in classes or t.value("UnitMotion/PassabilityClass", "").startswith("ship"):
            flags.append("FAUNA_F_NAVAL")
        if stem.endswith("_trainable"):
            flags.append("FAUNA_F_TRAINABLE")
        if supply is not None and t.value("ResourceSupply/KillBeforeGather", "false") == "true":
            flags.append("FAUNA_F_KILL_BEFORE_GATHER")
        if rtype == "food.fish":
            flags.append("FAUNA_F_FISH")
        if t.value("UnitMotion/PassabilityClass", "") == "large":
            flags.append("FAUNA_F_LARGE")
        rows.append({
            "stem": stem,
            "name": t.value("Identity/GenericName", stem),
            "model": model_for(stem, t.value("VisualActor/Actor", "")),
            "hp": t.num("Health/Max"),
            "speed": r1(t.num("UnitMotion/WalkSpeed") * SPEED_SCALE),
            "run": r1(t.num("UnitMotion/RunMultiplier", 1.0)),
            "food": food,
            "gatherers": int(supply.num("MaxGatherers")) if supply is not None else 0,
            "stance": "FAUNA_STANCE_" + stance.upper().replace("-", "_"),
            "flags": " | ".join(flags) if flags else "0",
            "vision": r1(t.num("Vision/Range") * RANGED_RANGE_SCALE),
            "flee": r1(t.num("UnitAI/FleeDistance") * RANGED_RANGE_SCALE),
            "roam": r1(t.num("UnitAI/RoamDistance") * RANGED_RANGE_SCALE),
            "roam_min": t.num("UnitAI/RoamTimeMin") / 1000.0,
            "roam_max": t.num("UnitAI/RoamTimeMax") / 1000.0,
            "feed_min": t.num("UnitAI/FeedTimeMin") / 1000.0,
            "feed_max": t.num("UnitAI/FeedTimeMax") / 1000.0,
            "hack": hack, "pierce": pierce, "crush": crush,
            "range": r1(rng * MELEE_RANGE_SCALE), "repeat": rep, "prepare": prep,
            "hack_armor": t.num("Resistance/Entity/Damage/Hack"),
            "pierce_armor": t.num("Resistance/Entity/Damage/Pierce"),
            "crush_armor": t.num("Resistance/Entity/Damage/Crush"),
            "xp": t.num("Loot/xp"),
            "footprint": round(footprint_radius(t), 3),
            "accel": r1(t.num("UnitMotion/Acceleration") * SPEED_SCALE),
            "corral_food": corral_food(loader, t),
            "cost": [int(t.num("Cost/Resources/" + r)) for r in ("food", "wood", "stone", "metal")],
            "build_time": t.num("Cost/BuildTime"),
        })
    return rows


def corral_food(loader, t):
    """Food a garrisoned animal adds to its corral's ResourceTrickle (structures/corral_garrison_*.json)."""
    food = 0.0
    for aura in t.tokens("Auras"):
        if not aura.startswith("structures/corral_garrison_"):
            continue
        with open(os.path.join(os.path.dirname(loader.root), "data", "auras", aura + ".json")) as fh:
            mods = json.load(fh)["modifications"]
        food += sum(m.get("add", 0.0) for m in mods if m["value"] == "ResourceTrickle/Rates/food")
    return food


def corral_rows(loader):
    """The corral's ResourceTrickle interval (seconds), garrison size, and each civ's Trainer list."""
    t = loader.load("template_structure_resource_corral")
    trains = []
    for civ in CIVS:
        name = f"structures/{civ}/corral"
        c = loader.load(name if loader.exists(name) else "template_structure_resource_corral")
        ents = [e.replace("{civ}", civ) for e in c.tokens("Trainer/Entities")]
        for e in ents:
            if not e.startswith("gaia/fauna_"):
                raise SystemExit(f"{name}: corral trains {e}, not an animal")
        trains.append([e[len("gaia/"):] for e in ents])
    return t.num("ResourceTrickle/Interval") / 1000.0, int(t.num("GarrisonHolder/Max")), trains


RES = {"food": "RES_FOOD", "wood": "RES_WOOD", "stone": "RES_STONE", "metal": "RES_GOLD"}


def treasure_rows(loader):
    rows = []
    for p in sorted(glob.glob(os.path.join(loader.root, "gaia", "treasure", "*.xml"))):
        stem = os.path.basename(p)[:-4]
        t = loader.load("gaia/treasure/" + stem)
        res = t.get("Treasure/Resources")
        amounts = {k: int(res.num(k)) for k in RES} if res is not None else {}
        st = t.get("Obstruction/Static")
        actor = os.path.basename(t.value("VisualActor/Actor", "barrel_a.xml"))[:-4]
        rows.append({
            "stem": stem,
            "model": "treasure_" + (actor[len("treasure_"):] if actor.startswith("treasure_") else actor),
            "name": t.value("Identity/SpecificName", t.value("Identity/GenericName", stem)),
            "collect": t.num("Treasure/CollectTime") / 1000.0,
            "amounts": [amounts.get(k, 0) for k in ("food", "wood", "stone", "metal")],
            "width": float(st.attrs.get("width", 1)) if st is not None else 1.0,
            "depth": float(st.attrs.get("depth", 1)) if st is not None else 1.0,
        })
    return rows


def ruins_rows(loader):
    rows = []
    for p in sorted(glob.glob(os.path.join(loader.root, "gaia", "ruins", "*.xml"))):
        stem = os.path.basename(p)[:-4]
        t = loader.load("gaia/ruins/" + stem)
        rtype = t.value("ResourceSupply/Type", "stone.ruins").split(".")[0]
        rows.append({
            "stem": stem,
            "name": t.value("Identity/SpecificName", stem),
            "res": RES[rtype],
            "amount": int(t.num("ResourceSupply/Max")),
            "gatherers": int(t.num("ResourceSupply/MaxGatherers")),
            "width": float(t.get("Obstruction/Static").attrs["width"]),
            "depth": float(t.get("Obstruction/Static").attrs["depth"]),
        })
    return rows


# Scenery types are saved by index (Simulation::scenery_, save v29), so the ones RTS0 shipped
# keep their place and templates new in a later 0 A.D. release go after them.
SCENERY_ORDER = [
    "bench", "column_doric", "column_doric_fallen", "column_doric_fallen_b", "fence_long",
    "fence_rock_long", "fence_rock_medium", "fence_rock_short", "fence_rock_stub", "fence_short",
    "fence_stone", "fence_stone_long", "fence_stone_medium", "fence_stone_short", "fence_stone_stub",
    "obelisk", "sele_colonnade", "table_rectangle", "table_square",
]


def scenery_rows(loader):
    rows = []
    for p in sorted(glob.glob(os.path.join(loader.root, "structures", "*.xml"))):
        with open(p, encoding="utf-8") as fh:
            if 'parent="template_structure"' not in fh.read():
                continue
        stem = os.path.basename(p)[:-4]
        t = loader.load("structures/" + stem)
        st = t.get("Obstruction/Static")
        loot = t.get("Loot")
        amounts = {k: int(loot.num(k)) for k in RES} if loot is not None else {}
        cap = t.get("Capturable")
        rows.append({
            "stem": stem,
            "name": t.value("Identity/SpecificName", t.value("Identity/GenericName", stem)),
            "hp": t.num("Health/Max"),
            "width": float(st.attrs.get("width", 1)) if st is not None else 1.0,
            "depth": float(st.attrs.get("depth", 1)) if st is not None else 1.0,
            "height": t.num("Footprint/Height", 4.0),
            "loot": [amounts.get(k, 0) for k in ("food", "wood", "stone", "metal")],
            "capture": 0 if cap is None or "disable" in cap.attrs else int(cap.num("CapturePoints")),
            "wooden": amounts.get("wood", 0) > 0 or stem in ("bench", "fence_long", "fence_short") or stem.startswith("fence_wicker"),
        })
    rank = {n: i for i, n in enumerate(SCENERY_ORDER)}
    return sorted(rows, key=lambda r: (rank.get(r["stem"], len(SCENERY_ORDER)), r["stem"]))


def footprint_radius(t):
    """Footprint radius in 0 A.D. metres; a square counts as the circle of the same area
    (as extract_0ad_attack.py's footprint_of, which DelayedDamage hits test against)."""
    c = t.get("Footprint/Circle")
    if c is not None and "radius" in c.attrs:
        return float(c.attrs["radius"])
    q = t.get("Footprint/Square")
    if q is not None and "width" in q.attrs:
        return (float(q.attrs["width"]) * float(q.attrs["depth"]) / 3.141592653589793) ** 0.5
    return 0.0


def f(x):
    s = repr(float(x))
    return s + "f"


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit(fauna, treasures, ruins, scenery, corral):
    out = []
    w = out.append
    w("// Generated by tools/extract_0ad_fauna.py from 0 A.D.'s Gaia templates")
    w("// (binaries/data/mods/public/simulation/templates/gaia/{fauna_*,treasure/*,ruins/*}.xml).")
    w("// Do not edit by hand: change the tool and regenerate. 0 A.D. data is CC BY-SA 3.0,")
    w("// see docs/0AD_ATTRIBUTION.md.")
    w("#ifndef RTSO_GAIA_DATA_H")
    w("#define RTSO_GAIA_DATA_H")
    w("")
    w("#include <cstdint>")
    w("")
    w("namespace rtso {")
    w("")
    w("// RTS0 metres per 0 A.D. metre for vision, flee and roam distances (RANGED_RANGE_SCALE).")
    w(f"static constexpr float FAUNA_DISTANCE_SCALE = {f(RANGED_RANGE_SCALE)};")
    w("")
    w("enum FaunaType : uint8_t {")
    for i, r in enumerate(fauna):
        w(f"\t{r['stem'].upper()} = {i},")
    w(f"\tFAUNA_COUNT = {len(fauna)}")
    w("};")
    w("")
    w("// UnitAI.js g_Stances, the ones 0 A.D.'s animals use.")
    w("enum FaunaStance : uint8_t {")
    for i, s in enumerate(STANCES):
        w(f"\tFAUNA_STANCE_{s.upper().replace('-', '_')} = {i},")
    w("};")
    w("")
    w("enum FaunaFlags : uint8_t {")
    w("\tFAUNA_F_DOMESTIC = 1,           // Identity class Domestic (herdable livestock)")
    w("\tFAUNA_F_NAVAL = 2,              // SeaCreature: swims, ship-small passability")
    w("\tFAUNA_F_TRAINABLE = 4,          // *_trainable: the corral's player-owned copy")
    w("\tFAUNA_F_KILL_BEFORE_GATHER = 8, // ResourceSupply/KillBeforeGather")
    w("\tFAUNA_F_FISH = 16,              // food.fish (whales) instead of food.meat")
    w("\tFAUNA_F_LARGE = 32,             // UnitMotion/PassabilityClass large (bulls, adult elephants)")
    w("};")
    w("")
    w("struct FaunaTypeInfo {")
    w("\tconst char *template_name; // gaia/<template_name>.xml")
    w("\tconst char *name;          // Identity/GenericName")
    w("\tconst char *model_id;      // assets/models/units/<model_id>.glb")
    w("\tfloat max_hp;")
    w("\tfloat speed;               // walk speed, RTS0 units/s")
    w("\tfloat run_mult;            // UnitMotion/RunMultiplier (fleeing and chasing)")
    w("\tint food_amount;           // ResourceSupply/Max (0: not huntable)")
    w("\tuint8_t max_gatherers;")
    w("\tuint8_t stance;            // FaunaStance")
    w("\tuint8_t flags;             // FaunaFlags")
    w("\tfloat vision;              // Vision/Range: aggressive animals attack, skittish flee, inside it")
    w("\tfloat flee_distance;       // UnitAI/FleeDistance")
    w("\tfloat roam_distance;       // UnitAI/RoamDistance")
    w("\tfloat roam_time_min, roam_time_max, feed_time_min, feed_time_max; // seconds")
    w("\tfloat hack_damage, pierce_damage, crush_damage; // Attack/Melee/Damage")
    w("\tfloat attack_range;        // Attack/Melee/MaxRange")
    w("\tfloat attack_cooldown;     // Attack/Melee/RepeatTime, seconds")
    w("\tfloat attack_prepare;      // Attack/Melee/PrepareTime, seconds: the first hit waits this long")
    w("\tfloat hack_armor, pierce_armor, crush_armor; // Resistance levels (0.9^level)")
    w("\tfloat loot_xp;")
    w("\tfloat footprint;          // Footprint radius in 0 A.D. metres (a square as the circle of equal area)")
    w("\tfloat acceleration;       // UnitMotion/Acceleration in RTS0 units/s^2 (0.28's m/s^2 x the speed scale)")
    w("\tbool is_domestic() const { return flags & FAUNA_F_DOMESTIC; }")
    w("\tbool is_naval() const { return flags & FAUNA_F_NAVAL; }")
    w("\tbool is_large() const { return flags & FAUNA_F_LARGE; }")
    w("\tfloat attack_damage() const { return hack_damage + pierce_damage + crush_damage; }")
    w("\t// UnitAI.IsDangerousAnimal: an animal whose stance targets visible enemies and that can attack.")
    w("\tbool is_dangerous() const {")
    w("\t\treturn attack_damage() > 0.0f && (stance == FAUNA_STANCE_DEFENSIVE || stance == FAUNA_STANCE_AGGRESSIVE || stance == FAUNA_STANCE_VIOLENT);")
    w("\t}")
    w("};")
    w("")
    w("inline const FaunaTypeInfo &fauna_type_info(uint8_t ftype) {")
    w("\tstatic const FaunaTypeInfo kTable[FAUNA_COUNT] = {")
    for r in fauna:
        w("\t\t{" + ", ".join([
            cstr(r["stem"]), cstr(r["name"]), cstr(r["model"]), f(r["hp"]), f(r["speed"]), f(r["run"]),
            str(r["food"]), str(r["gatherers"]), r["stance"], r["flags"], f(r["vision"]), f(r["flee"]),
            f(r["roam"]), f(r["roam_min"]), f(r["roam_max"]), f(r["feed_min"]), f(r["feed_max"]),
            f(r["hack"]), f(r["pierce"]), f(r["crush"]), f(r["range"]), f(r["repeat"]), f(r["prepare"]),
            f(r["hack_armor"]), f(r["pierce_armor"]), f(r["crush_armor"]), f(r["xp"]), f(r["footprint"]), f(r["accel"])]) + "},")
    w("\t};")
    w("\treturn kTable[ftype < FAUNA_COUNT ? ftype : 0];")
    w("}")
    w("")
    w("// Template stem (\"fauna_bear_brown\", or \"bear_brown\") to FaunaType; FAUNA_COUNT if unknown.")
    w("inline uint8_t fauna_type_by_name(const char *s) {")
    w("\tif (!s) return FAUNA_COUNT;")
    w("\tauto eq = [](const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; };")
    w("\tconst char *bare = s;")
    w("\tif (s[0] == 'f' && s[1] == 'a' && s[2] == 'u' && s[3] == 'n' && s[4] == 'a' && s[5] == '_') bare = s + 6;")
    w("\tfor (uint8_t i = 0; i < FAUNA_COUNT; ++i)")
    w("\t\tif (eq(fauna_type_info(i).template_name + 6, bare)) return i;")
    w("\treturn FAUNA_COUNT;")
    w("}")
    w("")
    w("// The corral (template_structure_resource_corral.xml): ResourceTrickle/Interval and")
    w("// GarrisonHolder/Max, and the food each garrisoned animal adds to the corral's trickle")
    w("// every interval (its structures/corral_garrison_{small,medium,large} aura, stackable).")
    w(f"inline constexpr float CORRAL_TRICKLE_INTERVAL = {f(corral[0])};")
    w(f"inline constexpr int CORRAL_GARRISON_MAX = {corral[1]};")
    w("")
    stems = [r["stem"] for r in fauna]
    longest = max(len(x) for x in corral[2])
    w("// The animals each civ's corral trains (structures/<civ>/corral.xml Trainer/Entities),")
    w("// indexed by Civilization: " + ", ".join(CIV_ENUM_OF[c] if c else "CIV_GAIA (none)" for c in CIV_SLOTS) + ".")
    w(f"inline constexpr int CORRAL_TRAINABLE_MAX = {longest};")
    w("struct CorralTrainable {")
    w("\tuint8_t count;")
    w("\tuint8_t fauna[CORRAL_TRAINABLE_MAX];")
    w("};")
    w(f"inline constexpr CorralTrainable CORRAL_TRAINABLE[{CIV_COUNT}] = {{")
    by_civ = dict(zip(CIVS, corral[2]))
    for civ in CIV_SLOTS:
        ents = by_civ[civ] if civ else []
        w(f"\t{{{len(ents)}, {{{', '.join(e.upper() for e in ents)}}}}}, // {civ or 'gaia'}")
        for e in ents:
            if e not in stems:
                raise SystemExit(f"{civ} corral trains unknown {e}")
    w("};")
    w("")
    w("// What training an animal costs (Cost/Resources: food, wood, stone, metal) and takes (Cost/BuildTime, s).")
    w("struct FaunaTrainCost {")
    w("\tint16_t res[4];")
    w("\tfloat time;")
    w("};")
    w("")
    w("inline const FaunaTrainCost &fauna_train_cost(uint8_t ftype) {")
    w("\tstatic constexpr FaunaTrainCost kCost[FAUNA_COUNT] = {")
    for r in fauna:
        c = r["cost"]
        w(f"\t\t{{{{{c[0]}, {c[1]}, {c[2]}, {c[3]}}}, {f(r['build_time'])}}}, // {r['stem']}")
    w("\t};")
    w("\treturn kCost[ftype < FAUNA_COUNT ? ftype : 0];")
    w("}")
    w("")
    w("inline float fauna_corral_food(uint8_t ftype) {")
    w("\tstatic constexpr float kFood[FAUNA_COUNT] = {" + ", ".join(f(r["corral_food"]) for r in fauna) + "};")
    w("\treturn ftype < FAUNA_COUNT ? kFood[ftype] : 0.0f;")
    w("}")
    w("")
    w("// Treasure.js: what a treasure gives (food, wood, stone, metal) and how long it takes to collect.")
    w("enum TreasureType : uint8_t {")
    for i, r in enumerate(treasures):
        w(f"\tTREASURE_{r['stem'].upper()} = {i},")
    w(f"\tTREASURE_TYPE_COUNT = {len(treasures)}")
    w("};")
    w("")
    w("struct TreasureTypeInfo {")
    w("\tconst char *template_name; // gaia/treasure/<template_name>.xml")
    w("\tconst char *name;")
    w("\tfloat collect_time;        // Treasure/CollectTime, seconds")
    w("\tint amount[4];             // food, wood, stone, metal")
    w("\tfloat width, depth;       // Obstruction/Static in metres")
    w("\tconst char *model_id;     // assets/models/props/<model_id>.glb (VisualActor, extract_0ad_assets.py ZIP_PROPS)")
    w("};")
    w("")
    w("inline const TreasureTypeInfo &treasure_type_info(uint8_t t) {")
    w("\tstatic const TreasureTypeInfo kTable[TREASURE_TYPE_COUNT] = {")
    for r in treasures:
        a = r["amounts"]
        w(f"\t\t{{{cstr(r['stem'])}, {cstr(r['name'])}, {f(r['collect'])}, {{{a[0]}, {a[1]}, {a[2]}, {a[3]}}}, {f(r['width'])}, {f(r['depth'])}, {cstr(r['model'])}}},")
    w("\t};")
    w("\treturn kTable[t < TREASURE_TYPE_COUNT ? t : 0];")
    w("}")
    w("")
    w("// By template stem (\"food_barrel\"), as maps name them; TREASURE_TYPE_COUNT if unknown.")
    w("inline uint8_t treasure_type_by_name(const char *s) {")
    w("\tif (!s) return TREASURE_TYPE_COUNT;")
    w("\tauto eq = [](const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; };")
    w("\tfor (uint8_t i = 0; i < TREASURE_TYPE_COUNT; ++i)")
    w("\t\tif (eq(treasure_type_info(i).template_name, s)) return i;")
    w("\treturn TREASURE_TYPE_COUNT;")
    w("}")
    w("")
    w("// The plain treasure of one resource (rmgen placePlayersNomad: gaia/treasure/{food_bin,wood,stone,metal}).")
    w("inline uint8_t treasure_type_for_resource(uint8_t res_type) {")
    w("\tswitch (res_type) {")
    w("\t\tcase RES_FOOD: return TREASURE_FOOD_BIN;")
    w("\t\tcase RES_WOOD: return TREASURE_WOOD;")
    w("\t\tcase RES_STONE: return TREASURE_STONE;")
    w("\t\tdefault: return TREASURE_METAL;")
    w("\t}")
    w("}")
    w("")
    w("// template_gaia_ruins: ruins are a resource supply (stone.ruins, or metal for the Isis statue).")
    w("struct RuinsTypeInfo {")
    w("\tconst char *template_name; // gaia/ruins/<template_name>.xml")
    w("\tconst char *name;")
    w("\tuint8_t res_type;          // ResourceType")
    w("\tint amount;                // ResourceSupply/Max")
    w("\tuint8_t max_gatherers;")
    w("\tfloat width, depth;       // Obstruction/Static (metres), along the entity's x and z axes")
    w("};")
    w("")
    w(f"static constexpr uint8_t RUINS_TYPE_COUNT = {len(ruins)};")
    w("")
    w("inline const RuinsTypeInfo &ruins_type_info(uint8_t t) {")
    w("\tstatic const RuinsTypeInfo kTable[RUINS_TYPE_COUNT] = {")
    for r in ruins:
        w(f"\t\t{{{cstr(r['stem'])}, {cstr(r['name'])}, {r['res']}, {r['amount']}, {r['gatherers']}, {f(r['width'])}, {f(r['depth'])}}},")
    w("\t};")
    w("\treturn kTable[t < RUINS_TYPE_COUNT ? t : 0];")
    w("}")
    w("")
    w("inline uint8_t ruins_type_by_name(const char *s) {")
    w("\tif (!s) return RUINS_TYPE_COUNT;")
    w("\tauto eq = [](const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; };")
    w("\tfor (uint8_t i = 0; i < RUINS_TYPE_COUNT; ++i)")
    w("\t\tif (eq(ruins_type_info(i).template_name, s)) return i;")
    w("\treturn RUINS_TYPE_COUNT;")
    w("}")
    w("")
    w("// Gaia scenery structures (structures/fence_*, column_*, obelisk, bench, table_*): obstructions")
    w("// with health that maps place. Sizes are 0 A.D. metres.")
    w("struct SceneryTypeInfo {")
    w("\tconst char *template_name; // structures/<template_name>.xml")
    w("\tconst char *name;")
    w("\tfloat max_hp;")
    w("\tfloat width, depth;       // Obstruction/Static, along the entity's x and z axes")
    w("\tfloat height;             // Footprint/Height")
    w("\tint loot[4];              // food, wood, stone, metal")
    w("\tint capture_points;       // Capturable/CapturePoints, 0 if not capturable")
    w("\tbool wooden;              // a wooden fence, bench or table: drawn as wood, not stone")
    w("};")
    w("")
    w(f"static constexpr uint8_t SCENERY_TYPE_COUNT = {len(scenery)};")
    w("")
    w("inline const SceneryTypeInfo &scenery_type_info(uint8_t t) {")
    w("\tstatic const SceneryTypeInfo kTable[SCENERY_TYPE_COUNT] = {")
    for r in scenery:
        lt = r["loot"]
        w(f"\t\t{{{cstr(r['stem'])}, {cstr(r['name'])}, {f(r['hp'])}, {f(r['width'])}, {f(r['depth'])}, {f(r['height'])}, "
          f"{{{lt[0]}, {lt[1]}, {lt[2]}, {lt[3]}}}, {r['capture']}, {'true' if r['wooden'] else 'false'}}},")
    w("\t};")
    w("\treturn kTable[t < SCENERY_TYPE_COUNT ? t : 0];")
    w("}")
    w("")
    w("inline uint8_t scenery_type_by_name(const char *s) {")
    w("\tif (!s) return SCENERY_TYPE_COUNT;")
    w("\tauto eq = [](const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; };")
    w("\tfor (uint8_t i = 0; i < SCENERY_TYPE_COUNT; ++i)")
    w("\t\tif (eq(scenery_type_info(i).template_name, s)) return i;")
    w("\treturn SCENERY_TYPE_COUNT;")
    w("}")
    w("")
    w("} // namespace rtso")
    w("")
    w("#endif")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zeroad", required=True)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    sim_root = os.path.join(a.zeroad, "binaries", "data", "mods", "public", "simulation")
    loader = TemplateLoader(sim_root)
    text = emit(fauna_rows(loader), treasure_rows(loader), ruins_rows(loader), scenery_rows(loader), corral_rows(loader))
    if a.check:
        with open(OUT_HEADER) as fh:
            if fh.read() != text:
                print(f"{OUT_HEADER} is out of date; rerun tools/extract_0ad_fauna.py")
                return 1
        print("gaia_data.h is up to date")
        return 0
    with open(OUT_HEADER, "w") as fh:
        fh.write(text)
    print(f"wrote {OUT_HEADER}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
