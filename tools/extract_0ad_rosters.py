#!/usr/bin/env python3
"""Generate RTS0's per-civ unit rosters and heroes from 0 A.D.'s simulation templates.

0 A.D. gives every civ its own units: Athenian Hoplites, Spartan Skiritai,
Han crossbowmen, and so on. RTS0's simulation has 23 behaviour classes
(UnitType). This tool reads 0 A.D.'s entity templates, works out what each
civ's buildings train, and emits one roster row per civ unit, tagged with the
RTS0 class that drives its behaviour.

Template semantics follow 0 A.D.'s CTemplateLoader / CParamNode:
  * parent="a|b|c": load c, overlay mixins/b.xml, then mixins/a.xml
  * child nodes merge into parent nodes; op="add|mul|mul_round" adjusts the
    parent's number; disable="" removes a node; replace="" drops the parent's
    subtree; merge="" only applies if the parent has the node
  * datatype="tokens" lists merge; "-x" removes token x
  * {civ} and {native} in Trainer lists are replaced by the owner's civ code

Stats are 0 A.D.'s own: health, damage, armour levels, attack repeat time,
costs, population and train time are copied as they are (both games apply
armour as 0.9^level). Only distances and speeds are converted, because RTS0's
world is smaller than 0 A.D.'s:
  * walk speed x SPEED_SCALE (RTS0 units/s per 0 A.D. m/s)
  * ranged attack range x RANGED_RANGE_SCALE (an archer's 60 m becomes ~17)
  * melee reach x MELEE_RANGE_SCALE (larger, since RTS0 units are wider
    relative to the map, and melee must reach past collision radii)
The scales are the medians of RTS0's current class table against 0 A.D.'s
generic template for each class. Carry capacity is the one stat left in RTS0
terms (class value scaled by 0 A.D.'s difference) because RTS0's gather rates
are tuned to it.

Usage:
  python3 -I tools/extract_0ad_rosters.py --0ad PATH/TO/0ad [--check]

PATH/TO/0ad is a checkout of https://github.com/0ad/0ad (only
binaries/data/mods/public/simulation is read). Writes
src/simulation/civ_roster_data.h and data/civs/rosters.json.
--check regenerates into memory and fails if the checked-in files differ.
"""

import argparse
import json
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_HEADER = os.path.join(REPO, "src", "simulation", "civ_roster_data.h")
OUT_JSON = os.path.join(REPO, "data", "civs", "rosters.json")
OUT_BUILDINGS = os.path.join(REPO, "src", "simulation", "civ_building_data.h")

# RTS0 Civilization enum order (sim_types.h). Han is appended last so saved
# games and network messages keep their civ ids.
CIVS = ["athen", "spart", "pers", "rome", "cart", "mace", "brit", "gaul",
        "iber", "maur", "kush", "ptol", "sele", "han"]
CIV_ENUM = ["CIV_ATHENIANS", "CIV_SPARTANS", "CIV_PERSIANS", "CIV_ROMANS",
            "CIV_CARTHAGINIANS", "CIV_MACEDONIANS", "CIV_BRITONS", "CIV_GAULS",
            "CIV_IBERIANS", "CIV_MAURYANS", "CIV_KUSHITES", "CIV_PTOLEMIES",
            "CIV_SELEUCIDS", "CIV_HAN"]

# RTS0 UnitType classes with today's sim_types.h stats (unit_type_info):
# hp, speed, range, damage, cooldown, carry, hack/pierce/crush armour,
# food/wood/gold/stone, pop, train_time, hack/pierce/crush damage.
RTS0_CLASSES = {
    "UNIT_VILLAGER":        (80, 3.0, 1.6, 5.0, 1.0, 20, 2, 4, 15, 50, 0, 0, 0, 1, 10, 5, 0, 0),
    "UNIT_SWORDSMAN":       (100, 3.4, 2.0, 8.0, 0.75, 0, 3, 3, 15, 0, 30, 60, 0, 1, 14, 8, 0, 0),
    "UNIT_ARCHER":          (80, 3.3, 16.0, 7.2, 1.25, 0, 1, 1, 15, 0, 50, 40, 0, 1, 12, 0, 7.2, 0),
    "UNIT_SPEARMAN":        (100, 3.2, 2.4, 8.5, 1.0, 0, 3, 3, 15, 30, 40, 20, 0, 1, 12, 4.25, 4.25, 0),
    "UNIT_CAVALRY":         (180, 5.5, 2.2, 11.5, 1.25, 0, 3, 3, 15, 50, 0, 80, 0, 2, 18, 11.5, 0, 0),
    "UNIT_TRADER":          (120, 4.2, 1.0, 0.0, 1.0, 50, 2, 2, 10, 60, 60, 0, 0, 1, 12, 0, 0, 0),
    "UNIT_CATAPULT":        (220, 2.0, 24.0, 45.0, 3.5, 0, 2, 5, 20, 0, 150, 100, 50, 3, 28, 0, 0, 45),
    "UNIT_HERO":            (350, 4.2, 2.8, 18.0, 0.8, 0, 6, 6, 25, 200, 0, 200, 0, 3, 30, 12, 6, 0),
    "UNIT_TRIREME":         (600, 4.5, 20.0, 32.0, 1.0, 0, 5, 8, 20, 0, 200, 150, 0, 3, 35, 0, 32, 0),
    "UNIT_ELEPHANT":        (900, 3.4, 2.5, 35.0, 1.4, 0, 5, 8, 20, 250, 0, 200, 0, 4, 40, 20, 0, 15),
    "UNIT_SIEGE_RAM":       (850, 2.2, 2.4, 75.0, 2.5, 0, 2, 12, 15, 0, 200, 100, 0, 3, 25, 0, 0, 75),
    "UNIT_HEALER":          (90, 3.2, 12.0, 0.0, 1.0, 0, 1, 1, 10, 80, 0, 60, 0, 1, 14, 0, 0, 0),
    "UNIT_CHARIOT":         (280, 5.8, 14.0, 14.0, 1.1, 0, 4, 4, 20, 80, 100, 60, 0, 2, 22, 4, 10, 0),
    "UNIT_WAR_DOG":         (75, 5.2, 1.2, 9.0, 0.7, 0, 1, 2, 8, 60, 0, 0, 0, 1, 8, 9, 0, 0),
    "UNIT_BOLT_SHOOTER":    (200, 2.2, 28.0, 40.0, 3.0, 0, 2, 4, 15, 0, 120, 80, 0, 2, 22, 0, 40, 0),
    "UNIT_FISHING_BOAT":    (150, 3.8, 1.5, 0.0, 1.0, 30, 1, 2, 10, 0, 75, 0, 0, 1, 12, 0, 0, 0),
    "UNIT_MERCHANT_SHIP":   (300, 4.0, 1.0, 0.0, 1.0, 100, 3, 4, 15, 0, 120, 40, 0, 1, 18, 0, 0, 0),
    "UNIT_CARNYX":          (140, 3.5, 2.0, 7.0, 1.0, 0, 3, 3, 15, 70, 40, 30, 0, 1, 15, 7, 0, 0),
    "UNIT_FEMALE_CITIZEN":  (65, 3.2, 1.4, 4.0, 1.0, 25, 1, 3, 10, 40, 0, 0, 0, 1, 8, 4, 0, 0),
    "UNIT_WORKER_ELEPHANT": (600, 3.2, 2.0, 12.0, 1.2, 100, 4, 6, 20, 180, 80, 0, 0, 2, 25, 8, 0, 4),
    "UNIT_ARCHER_MAIDEN":   (95, 3.6, 17.0, 8.5, 1.1, 0, 2, 2, 15, 40, 60, 40, 0, 1, 14, 0, 8.5, 0),
    "UNIT_CAMEL":           (190, 5.2, 2.4, 12.0, 1.2, 0, 3, 3, 15, 60, 0, 70, 0, 2, 16, 12, 0, 0),
    "UNIT_SCYTHED_CHARIOT": (320, 5.6, 2.2, 24.0, 1.0, 0, 5, 5, 25, 100, 80, 120, 0, 3, 26, 18, 0, 6),
    "UNIT_CATAFALQUE":      (1000, 1.76, 1.0, 0.0, 1.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    "UNIT_MINISTER":        (200, 3.204, 1.8, 10.0, 1.0, 0, 2, 4, 6, 100, 0, 100, 0, 1, 15, 10, 0, 0),
}
RTS0_FIELDS = ("hp", "speed", "range", "damage", "cooldown", "carry",
               "hack_armor", "pierce_armor", "crush_armor",
               "food", "wood", "gold", "stone", "pop", "train_time",
               "hack_dmg", "pierce_dmg", "crush_dmg")

# 0 A.D. generic template per RTS0 class; used to scale carry capacity.
GENERIC = {
    "UNIT_VILLAGER": "template_unit_support_slave",
    "UNIT_SWORDSMAN": "template_unit_infantry_melee_swordsman",
    "UNIT_ARCHER": "template_unit_infantry_ranged_archer",
    "UNIT_SPEARMAN": "template_unit_infantry_melee_spearman",
    "UNIT_CAVALRY": "template_unit_cavalry_melee_swordsman",
    "UNIT_TRADER": "template_unit_support_trader",
    "UNIT_CATAPULT": "template_unit_siege_stonethrower",
    "UNIT_HERO": "template_unit_hero_infantry_swordsman",
    "UNIT_TRIREME": "template_unit_ship_warship_arrow",
    "UNIT_ELEPHANT": "template_unit_champion_elephant_melee",
    "UNIT_SIEGE_RAM": "template_unit_siege_ram",
    "UNIT_HEALER": "template_unit_support_healer",
    "UNIT_CHARIOT": "units/brit/cavalry_javelineer_b",
    "UNIT_WAR_DOG": "template_unit_dog",
    "UNIT_BOLT_SHOOTER": "template_unit_siege_boltshooter",
    "UNIT_FISHING_BOAT": "template_unit_ship_fishing",
    "UNIT_MERCHANT_SHIP": "template_unit_ship_merchant",
    "UNIT_CARNYX": "template_unit_champion_infantry_trumpeter",
    "UNIT_FEMALE_CITIZEN": "template_unit_support_female_citizen",
    "UNIT_WORKER_ELEPHANT": "template_unit_support_elephant",
    "UNIT_ARCHER_MAIDEN": "template_unit_infantry_ranged_archer",
    "UNIT_CAMEL": "template_unit_cavalry_ranged_archer",
    "UNIT_SCYTHED_CHARIOT": "template_unit_champion_cavalry_swordsman",
    "UNIT_MINISTER": "units/han/support_minister",
    "UNIT_CATAFALQUE": "template_unit_catafalque",
}

# RTS0's male gatherer has no 0 A.D. counterpart in most civs; every civ keeps it.
RTS0_ONLY = ("UNIT_VILLAGER",)

# 0 A.D. structure (file stem, without trailing _N upgrade suffix) to the RTS0
# BuildingType that trains its units. Buildings RTS0 lacks fall back to the
# closest one RTS0 has; the 0 A.D. name stays in the data for later remaps.
# Gaia structures whose Trainer lists "units/{civ}/..." for their captor:
# template stem -> structure kind (TRAINER_BUILDING key).
GAIA_TRAINERS = {"merc_camp_egyptian": "mercenary_camp"}

TRAINER_BUILDING = {
    "civil_centre": "BLD_TOWN_CENTER", "military_colony": "BLD_TOWN_CENTER",
    "civil_centre_court": "BLD_TOWN_CENTER", "palace": "BLD_TOWN_CENTER",
    "prytaneion": "BLD_TOWN_CENTER", "tachara": "BLD_TOWN_CENTER",
    "house": "BLD_HOUSE", "barracks": "BLD_BARRACKS", "stable": "BLD_STABLE",
    "range": "BLD_RANGE", "elephant_stable": "BLD_ELEPHANT_STABLE",
    "gymnasium": "BLD_BARRACKS", "mercenary_camp": "BLD_BARRACKS",
    "embassy": "BLD_EMBASSY", "embassy_celtic": "BLD_EMBASSY",
    "embassy_iberian": "BLD_EMBASSY", "embassy_italic": "BLD_EMBASSY",
    "army_camp": "BLD_BARRACKS", "academy": "BLD_BARRACKS",
    "imperial_court": "BLD_TOWN_CENTER", "imperial_ministry": "BLD_TOWN_CENTER",
    "arsenal": "BLD_ARSENAL", "workshop": "BLD_ARSENAL",
    "fortress": "BLD_FORTRESS", "temple": "BLD_TEMPLE",
    "temple_amun": "BLD_TEMPLE", "temple_apedemak": "BLD_TEMPLE",
    "pyramid_large": "BLD_TEMPLE", "pyramid_small": "BLD_TEMPLE",
    "market": "BLD_MARKET", "dock": "BLD_DOCK", "shipyard": "BLD_DOCK",
    "corral": "BLD_CORRAL", "wonder": "BLD_WONDER",
    "syssiton": "BLD_TOWN_CENTER", "assembly": "BLD_TOWN_CENTER",
    "tachara_food": "BLD_TOWN_CENTER", "tachara_wood": "BLD_TOWN_CENTER",
    "tachara_stone": "BLD_TOWN_CENTER", "tachara_metal": "BLD_TOWN_CENTER",
    "crannog": "BLD_TOWN_CENTER", "ministry": "BLD_TOWN_CENTER",
    "apartment": "BLD_HOUSE", "apartment_block": "BLD_HOUSE",
    "house_a": "BLD_HOUSE", "house_b": "BLD_HOUSE",
    "warehouse": "BLD_HOUSE", "inn": "BLD_HOUSE", "tavern": "BLD_HOUSE",
    "kennel": "BLD_BARRACKS", "amphitheater_pompeii": "BLD_BARRACKS",
    "camp_blemmye": "BLD_BARRACKS", "camp_noba": "BLD_BARRACKS",
    "practice_range": "BLD_RANGE",
    "temple_mars": "BLD_TEMPLE", "temple_vesta": "BLD_TEMPLE", "tophet": "BLD_TEMPLE",
}
# Civ-specific 0 A.D. structures RTS0 models as their own BuildingType
# (appended after BLD_EMBASSY): enum, 0 A.D. file stems, model id, portrait id.
UNIQUE_BUILDINGS = [
    ("BLD_GYMNASIUM", ["gymnasium"], "gymnasium", "gymnasium"),
    ("BLD_PRYTANEION", ["prytaneion"], "prytaneion", "prytaneion"),
    ("BLD_GEROUSIA", ["gerousia"], "great_hall", "gerousia"),
    ("BLD_SYSSITON", ["syssiton"], "great_hall", "syssiton"),
    ("BLD_TACHARA", ["tachara"], "palace", "tachara"),
    ("BLD_ARMY_CAMP", ["army_camp"], "barracks", "army_camp"),
    ("BLD_AMPHITHEATER", ["amphitheater_pompeii"], "amphitheater", "amphitheater"),
    ("BLD_LIBRARY", ["library"], "library", "library"),
    ("BLD_KENNEL", ["kennel"], "kennel", "kennel"),
    ("BLD_ASSEMBLY", ["assembly"], "great_hall", "assembly"),
    ("BLD_MONUMENT", ["monument"], "pyramid_minor", "monument"),
    ("BLD_PALACE", ["palace"], "palace", "palace"),
    ("BLD_PILLAR", ["pillar_ashoka"], "pyramid_minor", "pillar"),
    ("BLD_PYRAMID_LARGE", ["pyramid_large"], "pyramid_large", "pyramid_large"),
    ("BLD_PYRAMID_SMALL", ["pyramid_small"], "pyramid_small", "pyramid_small"),
    ("BLD_MERCENARY_CAMP", ["mercenary_camp"], "barracks", "mercenary_camp"),
    ("BLD_LIGHTHOUSE", ["lighthouse"], "lighthouse", "lighthouse"),
    ("BLD_MILITARY_COLONY", ["military_colony"], "town_center", "military_colony"),
    ("BLD_ACADEMY", ["academy"], "academy", "academy"),
    ("BLD_MINISTRY", ["ministry"], "imperial_ministry", "ministry"),
    ("BLD_CAMP_BLEMMYE", ["camp_blemmye"], "barracks", "camp_blemmye"),
    ("BLD_CAMP_NOBA", ["camp_noba"], "barracks", "camp_noba"),
    ("BLD_TEMPLE_AMUN", ["temple_amun"], "temple", "temple_amun"),
    ("BLD_TEMPLE_VESTA", ["temple_vesta"], "temple", "temple_vesta"),
    ("BLD_TEMPLE_MARS", ["temple_mars"], "temple", "temple_mars"),
    ("BLD_ICE_HOUSE", ["ice_house"], "farmstead", "ice_house"),
    ("BLD_SUPER_DOCK", ["super_dock"], "dock", "super_dock"),
]
# Units of a civ-specific structure train only there, as in 0 A.D. (Petra builds
# these structures since #105).
for _enum, _stems, _model, _portrait in UNIQUE_BUILDINGS:
    for _st in _stems:
        TRAINER_BUILDING[_st] = _enum
TRAINER_BUILDING.update({"tachara_food": "BLD_TACHARA", "tachara_wood": "BLD_TACHARA",
                         "tachara_stone": "BLD_TACHARA", "tachara_metal": "BLD_TACHARA",
                         "imperial_ministry": "BLD_MINISTRY", "ministry": "BLD_MINISTRY"})
# 0 A.D. Builder/Entities structure kinds -> the generic RTS0 BuildingTypes they
# let a civ lay (a stone wallset brings its towers and gates).
BUILDER_BUILDING = {
    "civil_centre": ["BLD_TOWN_CENTER"], "house": ["BLD_HOUSE"], "apartment": ["BLD_HOUSE"],
    "barracks": ["BLD_BARRACKS"], "storehouse": ["BLD_STOREHOUSE"], "defense_tower": ["BLD_TOWER"],
    "farmstead": ["BLD_FARMSTEAD"], "market": ["BLD_MARKET"], "fortress": ["BLD_FORTRESS"],
    "temple": ["BLD_TEMPLE"], "wonder": ["BLD_WONDER"], "dock": ["BLD_DOCK"],
    "wallset_stone": ["BLD_WALL_SEGMENT", "BLD_WALL_TOWER", "BLD_GATE"], "corral": ["BLD_CORRAL"],
    "field": ["BLD_FIELD"], "stable": ["BLD_STABLE"], "range": ["BLD_RANGE"], "forge": ["BLD_FORGE"],
    "arsenal": ["BLD_ARSENAL"], "sentry_tower": ["BLD_SENTRY_TOWER"], "outpost": ["BLD_OUTPOST"],
    "wallset_palisade": ["BLD_PALISADE"], "elephant_stable": ["BLD_ELEPHANT_STABLE"],
    "theater": ["BLD_THEATER"], "embassy_celtic": ["BLD_EMBASSY"], "embassy_iberian": ["BLD_EMBASSY"],
    "embassy_italic": ["BLD_EMBASSY"],
}


def generic_build_masks(sim_root):
    """Civ bit mask per generic BuildingType: the civs whose units' 0 A.D.
    Builder/Entities lists name such a structure (template files that exist)."""
    loader = TemplateLoader(sim_root)
    masks = {}
    for civ in CIVS:
        udir = os.path.join(loader.root, "units", civ)
        for fn in sorted(os.listdir(udir)):
            if not fn.endswith(".xml"):
                continue
            for tok in loader.load("units/%s/%s" % (civ, fn[:-4])).tokens("Builder/Entities"):
                tok = tok.replace("{civ}", civ).replace("{native}", civ)
                if not loader.exists(tok):
                    continue
                for bld in BUILDER_BUILDING.get(structure_kind(tok.rsplit("/", 1)[1]), []):
                    masks[bld] = masks.get(bld, 0) | (1 << CIVS.index(civ))
    return masks


BUILDING_BITS = ["BLD_TOWN_CENTER", "BLD_HOUSE", "BLD_BARRACKS", "BLD_STOREHOUSE",
                 "BLD_TOWER", "BLD_FARMSTEAD", "BLD_MARKET", "BLD_FORTRESS",
                 "BLD_GATE", "BLD_TEMPLE", "BLD_WONDER", "BLD_DOCK",
                 "BLD_WALL_SEGMENT", "BLD_CORRAL", "BLD_FIELD", "BLD_STABLE",
                 "BLD_RANGE", "BLD_FORGE", "BLD_ARSENAL", "BLD_SENTRY_TOWER",
                 "BLD_OUTPOST", "BLD_PALISADE", "BLD_WALL_TOWER", "BLD_ELEPHANT_STABLE",
                 "BLD_THEATER", "BLD_EMBASSY"] + [u[0] for u in UNIQUE_BUILDINGS]  # BuildingType order, every id

PHASE_OF_TECH = {
    "phase_village": 1, "unlock_females_house": 1,
    "phase_town": 2, "equine_transports": 2, "iphicratean_reforms": 2,
    "phase_city": 3, "roman_reforms": 3,
    "unlock_champion_infantry": 3, "unlock_champion_cavalry": 3,
    "unlock_champion_chariots": 3, "unlock_champion_elephants": 3,
}

# ---------------------------------------------------------------------------
# Template loading (CParamNode semantics)
# ---------------------------------------------------------------------------

class Node:
    __slots__ = ("text", "children", "attrs")

    def __init__(self):
        self.text = None
        self.children = {}
        self.attrs = {}

    def copy(self):
        n = Node()
        n.text = self.text
        n.attrs = dict(self.attrs)
        n.children = {k: v.copy() for k, v in self.children.items()}
        return n

    def get(self, path):
        node = self
        for part in path.split("/"):
            node = node.children.get(part)
            if node is None:
                return None
        return node

    def value(self, path, default=None):
        n = self.get(path)
        if n is None or n.text is None:
            return default
        return n.text.strip()

    def num(self, path, default=0.0):
        v = self.value(path)
        try:
            return float(v) if v not in (None, "") else default
        except ValueError:
            return default

    def tokens(self, path):
        v = self.value(path)
        return v.split() if v else []


def _fmt(x):
    if abs(x - round(x)) < 1e-9:
        return str(int(round(x)))
    return repr(x)


def merge_into(base, elem):
    """Overlay ElementTree element `elem` onto Node `base` (CParamNode::ApplyLayer)."""
    for child in elem:
        if not isinstance(child.tag, str):
            continue
        tag = child.tag
        if "disable" in child.attrib:
            base.children.pop(tag, None)
            continue
        if "merge" in child.attrib and tag not in base.children:
            continue
        if "replace" in child.attrib or tag not in base.children:
            base.children[tag] = Node()
        node = base.children[tag]
        text = (child.text or "").strip()
        op = child.attrib.get("op")
        if child.attrib.get("datatype") == "tokens":
            old = [] if "replace" in child.attrib else (node.text or "").split()
            for tok in text.split():
                if tok.startswith("-"):
                    old = [t for t in old if t != tok[1:]]
                elif tok not in old:
                    old.append(tok)
            node.text = " ".join(old)
        elif op in ("add", "mul", "mul_round"):
            # CParamNode::ApplyLayer: a missing value counts as 0 (ToFixed of "").
            try:
                a = float(node.text) if node.text not in (None, "") else 0.0
                b = float(text)
                r = a + b if op == "add" else a * b
                if op == "mul_round":
                    r = float(round(r))
                node.text = _fmt(r)
            except ValueError:
                node.text = text
        elif text:
            node.text = text
        elif node.text is None:
            node.text = ""
        for k, v in child.attrib.items():
            if k not in ("op", "merge", "replace", "disable", "datatype"):
                node.attrs[k] = v
        merge_into(node, child)


class TemplateLoader:
    def __init__(self, sim_root):
        self.root = os.path.join(sim_root, "templates")
        self.cache = {}

    def exists(self, name):
        return os.path.isfile(os.path.join(self.root, name + ".xml"))

    def load(self, name):
        if name in self.cache:
            return self.cache[name]
        if "|" in name:
            mixin, rest = name.split("|", 1)
            node = self.load(rest).copy()
            self.apply_mixin(node, mixin)
        else:
            elem = ET.parse(os.path.join(self.root, name + ".xml")).getroot()
            parent = elem.attrib.get("parent")
            node = self.load(parent).copy() if parent else Node()
            merge_into(node, elem)
        self.cache[name] = node
        return node

    def apply_mixin(self, node, mixin):
        """Overlay mixins/<mixin>.xml, after its own parent mixins (e.g. merc_inf -> mercenary)."""
        elem = ET.parse(os.path.join(self.root, "mixins", mixin + ".xml")).getroot()
        parent = elem.attrib.get("parent")
        if parent:
            for m in reversed(parent.split("|")):
                self.apply_mixin(node, m)
        merge_into(node, elem)

# ---------------------------------------------------------------------------
# Unit stat extraction
# ---------------------------------------------------------------------------

def primary_attack(t):
    """Ranged attack if the unit has one, else melee. Returns (node, ranged)."""
    for kind, ranged in (("Ranged", True), ("Melee", False)):
        a = t.get("Attack/" + kind)
        if a is not None and a.get("Damage") is not None:
            return a, ranged
    return None, False


def raw_stats(t):
    """0 A.D. numbers in RTS0_FIELDS order (where they exist)."""
    atk, ranged = primary_attack(t)
    hack = pierce = crush = 0.0
    rng = rep = 0.0
    if atk is not None:
        hack = atk.num("Damage/Hack")
        pierce = atk.num("Damage/Pierce")
        crush = atk.num("Damage/Crush")
        rng = atk.num("MaxRange")
        rep = atk.num("RepeatTime") / 1000.0
    carry = 0.0
    caps = t.get("ResourceGatherer/Capacities")
    if caps is not None:
        carry = max([_safe(c.text) for c in caps.children.values()] or [0.0])
    return {
        "hp": t.num("Health/Max"),
        "speed": t.num("UnitMotion/WalkSpeed"),
        "range": rng,
        "damage": hack + pierce + crush,
        "cooldown": rep,
        "carry": carry,
        "hack_armor": t.num("Resistance/Entity/Damage/Hack"),
        "pierce_armor": t.num("Resistance/Entity/Damage/Pierce"),
        "crush_armor": t.num("Resistance/Entity/Damage/Crush"),
        "food": t.num("Cost/Resources/food"),
        "wood": t.num("Cost/Resources/wood"),
        "gold": t.num("Cost/Resources/metal"),
        "stone": t.num("Cost/Resources/stone"),
        "pop": t.num("Cost/Population"),
        "train_time": t.num("Cost/BuildTime"),
        "hack_dmg": hack,
        "pierce_dmg": pierce,
        "crush_dmg": crush,
        "ranged": ranged,
    }


def _safe(text):
    try:
        return float((text or "").strip())
    except ValueError:
        return 0.0


SPEED_SCALE = 0.356
RANGED_RANGE_SCALE = 0.28
MELEE_RANGE_SCALE = 0.6


def convert(cls, raw, gen):
    base = dict(zip(RTS0_FIELDS, RTS0_CLASSES[cls]))
    out = {f: raw[f] for f in ("hp", "cooldown", "hack_dmg", "pierce_dmg", "crush_dmg",
                               "hack_armor", "pierce_armor", "crush_armor",
                               "food", "wood", "gold", "stone", "train_time")}
    for r in ("food", "wood", "gold", "stone"):
        out[r] = int(round(out[r]))
    out["pop"] = int(round(raw["pop"]))
    out["speed"] = raw["speed"] * SPEED_SCALE
    out["damage"] = raw["damage"]
    if raw["damage"] > 0:
        out["range"] = raw["range"] * (RANGED_RANGE_SCALE if raw["ranged"] else MELEE_RANGE_SCALE)
    else:
        # No attack (healers, traders, boats): keep the class's interaction range.
        out["range"] = base["range"]
        out["cooldown"] = base["cooldown"]
    if base["carry"] > 0 and gen["carry"] > 0 and raw["carry"] > 0:
        out["carry"] = int(round(base["carry"] * raw["carry"] / gen["carry"]))
    else:
        out["carry"] = int(base["carry"])
    if out["hp"] <= 0:
        out["hp"] = base["hp"]
    return out


# ---------------------------------------------------------------------------
# Class mapping
# ---------------------------------------------------------------------------

def classes_of(t):
    return set(t.tokens("Identity/Classes")) | set(t.tokens("Identity/VisibleClasses"))


def rts0_class(name, t, civ):
    """RTS0 UnitType for a 0 A.D. unit template, or None to leave it out."""
    c = classes_of(t)
    stem = name.rsplit("/", 1)[-1]
    if "Minister" in c:
        return "UNIT_MINISTER"  # units/han/support_minister
    if "Relic" in c:
        return "UNIT_CATAFALQUE"  # template_unit_catafalque (Capture the Relic)
    if "Hero" in c:
        return "UNIT_HERO"
    if "FemaleCitizen" in c:
        return "UNIT_FEMALE_CITIZEN"
    if "Ship" in c:
        if "FishingBoat" in c or "fishing" in stem:
            return "UNIT_FISHING_BOAT"
        if "Trader" in c or "merchant" in stem:
            return "UNIT_MERCHANT_SHIP"
        if "Warship" in c or "Arrow" in c or "ship_" in stem:
            return "UNIT_TRIREME"
        return None
    if "Trader" in c:
        return "UNIT_TRADER"
    if "Healer" in c:
        return "UNIT_HEALER"
    if "Siege" in c:
        if "Ram" in c:
            return "UNIT_SIEGE_RAM"
        if "StoneThrower" in c:
            return "UNIT_CATAPULT"
        if "BoltShooter" in c:
            return "UNIT_BOLT_SHOOTER"
        if "SiegeTower" in c:
            return "UNIT_BOLT_SHOOTER"  # fires its own Bow attack; flagged ROSTER_SIEGE_TOWER
        return None  # flamethrowers have no RTS0 class yet
    if "trumpeter" in stem:
        return "UNIT_CARNYX"
    if "Dog" in c:
        return "UNIT_WAR_DOG"
    if "Elephant" in c:
        if "Worker" in c or "support" in stem:
            return "UNIT_WORKER_ELEPHANT"
        return "UNIT_ELEPHANT"
    if "Chariot" in c or "chariot" in stem:
        if "Scythed" in (t.value("Identity/GenericName") or ""):
            return "UNIT_SCYTHED_CHARIOT"
        return "UNIT_CHARIOT"
    if "Camel" in c or "camel" in stem:
        return "UNIT_CAMEL"
    if "Cavalry" in c:
        return "UNIT_CAVALRY"
    if "Infantry" in c:
        if "maiden" in stem:
            return "UNIT_ARCHER_MAIDEN"
        if "Ranged" in c:
            return "UNIT_ARCHER"
        if c & {"Spearman", "Pikeman"}:
            return "UNIT_SPEARMAN"
        if "Melee" in c:
            return "UNIT_SWORDSMAN"
        return None
    if "Worker" in c and "support_slave" in stem:
        return "UNIT_VILLAGER"
    return None


_TECH_ENUMS = None


def tech_enums():
    """0 A.D. technology name -> RTS0 TechnologyId enum, read from tech_data.h."""
    global _TECH_ENUMS
    if _TECH_ENUMS is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "simulation", "tech_data.h")
        _TECH_ENUMS = {}
        with open(path, encoding="utf-8") as f:
            for m in re.finditer(r"^\s*(TECH_[A-Z0-9_]+) = \d+, // (\S+)", f.read(), re.M):
                _TECH_ENUMS.setdefault(m.group(2), m.group(1))
    return _TECH_ENUMS


_TECH_CLASSES = None
# Newer 0 A.D. templates call older technologies' "Civilian" class "Support".
TECH_CLASS_ALIASES = {"Support": "Civilian"}


def tech_class_bits(classes):
    """0 A.D. classes as a sim_types.h TechClass mask (bits from tech_data.h)."""
    global _TECH_CLASSES
    if _TECH_CLASSES is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "simulation", "tech_data.h")
        with open(path, encoding="utf-8") as f:
            _TECH_CLASSES = {m.group(1): int(m.group(2))
                             for m in re.finditer(r"^\tTC_([A-Z0-9]+) = 1ull << (\d+),", f.read(), re.M)}
    bits = 0
    for c in list(classes) + [TECH_CLASS_ALIASES[c] for c in classes if c in TECH_CLASS_ALIASES]:
        if c.upper() in _TECH_CLASSES:
            bits |= 1 << _TECH_CLASSES[c.upper()]
    return bits


def unlock_tech_of(t):
    """The non-phase technology 0 A.D.'s Identity/Requirements names (e.g.
    unlock_champion_infantry), as an RTS0 enum, or TECH_NONE."""
    for tech in t.tokens("Identity/Requirements/Techs"):
        if tech.startswith("phase_"):
            continue
        enum = tech_enums().get(tech)
        if enum:
            return enum
    return "TECH_NONE"


def phase_of(t):
    phase = 1
    for tech in t.tokens("Identity/Requirements/Techs"):
        phase = max(phase, PHASE_OF_TECH.get(tech, 1))
    return phase

# ---------------------------------------------------------------------------
# Roster walk
# ---------------------------------------------------------------------------

def structure_kind(stem):
    stem = re.sub(r"_\d+$", "", stem)
    return stem


# ---------------------------------------------------------------------------
# Civ bonuses (simulation/data/technologies/civbonuses/*.json, auto-researched)
# ---------------------------------------------------------------------------

def req_civs(req):
    if not isinstance(req, dict):
        return set()
    out = set()
    if "civ" in req:
        out.add(req["civ"])
    for key in ("any", "all"):
        for sub in req.get(key, []):
            out |= req_civs(sub)
    return out


def affects_match(affects, classes):
    if not affects:
        return False
    for entry in affects:
        ok = True
        for tok in entry.split():
            if tok.startswith("!"):
                if tok[1:] in classes:
                    ok = False
            elif tok not in classes:
                ok = False
        if ok:
            return True
    return False


def load_civ_bonuses(sim_root):
    bdir = os.path.join(sim_root, "data", "technologies", "civbonuses")
    out = []
    for fn in sorted(os.listdir(bdir)):
        if not fn.endswith(".json"):
            continue
        with open(os.path.join(bdir, fn), encoding="utf-8") as f:
            d = json.load(f)
        if not d.get("autoResearch"):
            continue
        out.append({
            "id": fn[:-5],
            "name": d.get("genericName", fn[:-5]),
            "tooltip": d.get("tooltip", ""),
            "civs": sorted(req_civs(d.get("requirements"))),
            "affects": d.get("affects") or [],
            "modifications": d.get("modifications", []),
        })
    return out


def _modify(value, mod):
    if "multiply" in mod:
        return value * mod["multiply"]
    if "add" in mod:
        return value + mod["add"]
    if "replace" in mod:
        return mod["replace"]
    return value


def apply_unit_bonuses(raw, classes, civ, bonuses):
    """Fold a civ's auto-researched unit bonuses into raw 0 A.D. stats.
    Returns the ids of the bonuses that applied."""
    kind = "Ranged" if raw["ranged"] else "Melee"
    paths = {
        "UnitMotion/WalkSpeed": "speed", "Health/Max": "hp", "Cost/BuildTime": "train_time",
        "Cost/Resources/food": "food", "Cost/Resources/wood": "wood",
        "Cost/Resources/metal": "gold", "Cost/Resources/stone": "stone",
        "Attack/%s/Damage/Hack" % kind: "hack_dmg", "Attack/%s/Damage/Pierce" % kind: "pierce_dmg",
        "Attack/%s/Damage/Crush" % kind: "crush_dmg", "Attack/%s/RepeatTime" % kind: "cooldown",
        "Resistance/Entity/Damage/Hack": "hack_armor", "Resistance/Entity/Damage/Pierce": "pierce_armor",
        "Resistance/Entity/Damage/Crush": "crush_armor",
    }
    applied = []
    for b in bonuses:
        if civ not in b["civs"] or not affects_match(b["affects"], classes):
            continue
        hit = False
        for m in b["modifications"]:
            field = paths.get(m.get("value"))
            if field is None:
                continue
            raw[field] = _modify(raw[field], m)
            hit = True
        if hit:
            applied.append(b["id"])
    raw["damage"] = raw["hack_dmg"] + raw["pierce_dmg"] + raw["crush_dmg"]
    return applied


# 0 A.D. structure classes in civ bonus "affects" lists, as RTS0 BuildingTypes.
BUILDING_CLASS = {
    "Structure": BUILDING_BITS,
    "Wall": ["BLD_WALL_SEGMENT", "BLD_GATE", "BLD_WALL_TOWER"],
    "Tower": ["BLD_TOWER", "BLD_SENTRY_TOWER"], "Field": ["BLD_FIELD"],
    "Storehouse": ["BLD_STOREHOUSE"],
    "House": ["BLD_HOUSE"],
    "Economic": ["BLD_STOREHOUSE", "BLD_FARMSTEAD", "BLD_MARKET", "BLD_CORRAL", "BLD_HOUSE"],
    "CivCentre": ["BLD_TOWN_CENTER"], "Naval": ["BLD_DOCK"], "Wonder": ["BLD_WONDER"],
    "CivilCentre": ["BLD_TOWN_CENTER"], "Temple": ["BLD_TEMPLE"], "Forge": ["BLD_FORGE"],
    "Barracks": ["BLD_BARRACKS"], "Stable": ["BLD_STABLE"], "Arsenal": ["BLD_ARSENAL"],
}
BUILDING_STAT = {
    "Health/Max": "CIV_BONUS_BUILDING_HP", "Cost/BuildTime": "CIV_BONUS_BUILDING_BUILD_TIME",
    "Cost/Resources/wood": "CIV_BONUS_BUILDING_WOOD_COST",
    "Cost/Resources/stone": "CIV_BONUS_BUILDING_STONE_COST",
    "Capturable/CapturePoints": "CIV_BONUS_BUILDING_CAPTURE_POINTS",
}


def building_mask(affects):
    out = set()
    for entry in affects:
        include = None
        exclude = set()
        for tok in entry.split():
            if tok.startswith("!"):
                exclude |= set(BUILDING_CLASS.get(tok[1:], []))
            elif tok in BUILDING_CLASS:
                include = set(BUILDING_CLASS[tok]) if include is None else include & set(BUILDING_CLASS[tok])
            else:
                include = set()
        if include:
            out |= include - exclude
    return sorted(out, key=BUILDING_BITS.index)


def player_bonus_rows(bonuses):
    """Bonuses that act on a civ's buildings or the player rather than units."""
    rows = []
    for b in bonuses:
        for m in b["modifications"]:
            v = m.get("value")
            mult = m.get("multiply")
            if mult is None:
                continue
            if v == "Player/MaxPopulation":
                rows.append((b, "CIV_BONUS_POP_CAP", [], mult))
            elif v in BUILDING_STAT:
                mask = building_mask(b["affects"])
                if mask:
                    rows.append((b, BUILDING_STAT[v], mask, mult))
    return rows


# 0 A.D. resource names as RTS0 ResourceType values (RES_WOOD 0, RES_GOLD 1, RES_STONE 2, RES_FOOD 3).
RES_INDEX = {"wood": 0, "metal": 1, "stone": 2, "food": 3}


def load_team_bonuses(sim_root, rows):
    """0 A.D. team bonuses (data/auras/teambonuses/<civ>_player_teambonus.json) as
    (civ, stat, res, allies_only, buildings, unit rows, value, id, name, description).
    Each one is a global aura on the player that reaches the owner and its allies."""
    tdir = os.path.join(sim_root, "data", "auras", "teambonuses")
    out = []
    for civ in CIVS:
        path = os.path.join(tdir, civ + "_player_teambonus.json")
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as fh:
            d = json.load(fh)
        affects = d.get("affects") or []
        allies_only = "ExclusiveMutualAlly" in (d.get("affectedPlayers") or [])
        units = [i for i, r in enumerate(rows) if affects_match(affects, set(r["classes"]))]
        blds = building_mask(affects)
        base = (d.get("auraName", ""), d.get("auraDescription", ""))
        for m in d.get("modifications", []):
            v = m.get("value", "")
            if "multiply" in m:
                value = m["multiply"]
            elif "replace" in m:
                value = float(m["replace"])  # Spartan heroes: cost replaced by 0
            else:
                value = m.get("add", 0.0)
            parts = v.split("/")
            entries = []
            if v.startswith("Cost/Resources/"):
                res = RES_INDEX[parts[2]]
                if units:
                    entries.append(("TEAM_BONUS_UNIT_COST", res, [], units))
                if blds:
                    entries.append(("TEAM_BONUS_BUILDING_COST", res, blds, []))
            elif v == "Cost/BuildTime":
                if units:
                    entries.append(("TEAM_BONUS_UNIT_TRAIN_TIME", -1, [], units))
                if blds:
                    entries.append(("TEAM_BONUS_BUILDING_BUILD_TIME", -1, blds, []))
            elif v.startswith("Researcher/TechCostMultiplier/") and blds:
                if parts[2] == "time":
                    entries.append(("TEAM_BONUS_TECH_TIME", -1, blds, []))
                else:
                    entries.append(("TEAM_BONUS_TECH_COST", RES_INDEX[parts[2]], blds, []))
            elif v == "ResourceTrickle/Rates/food":
                entries.append(("TEAM_BONUS_FOOD_TRICKLE", RES_INDEX["food"], [], []))
            elif v == "Market/InternationalBonus":
                entries.append(("TEAM_BONUS_TRADE_INTERNATIONAL", -1, [], []))
            for stat, res, bmask, urows in entries:
                out.append({"civ": civ, "stat": stat, "res": res, "allies_only": allies_only,
                            "buildings": bmask, "units": urows, "value": value,
                            "id": civ + "_player_teambonus", "name": base[0], "description": base[1]})
    return out


# RTS0 AuraManager kinds (aura_manager.h AuraType).
AURA_KIND = {"AURA_GATHER": 0, "AURA_HEAL": 1, "AURA_ATTACK": 2, "AURA_SPEED": 3, "AURA_ARMOR": 4,
             "AURA_HEALTH": 5, "AURA_VISION": 6}
MULTIPLY_AURAS = ("AURA_ATTACK", "AURA_SPEED", "AURA_HEALTH", "AURA_VISION")
GLOBAL_AURA_RANGE = 1000.0


# 0 A.D. Auras.js affectedPlayers -> AuraPlayers bits in sim_types.h: 1 = the
# owner, 2 = the owner's other allies, 4 = enemies (GetAllies() includes the
# owner itself; ExclusiveMutualAlly does not).
AURA_PLAYER_BITS = {"Player": 1, "Ally": 3, "MutualAlly": 3, "ExclusiveMutualAlly": 2, "Enemy": 4}


def aura_players_mask(a):
    mask = 0
    for p in a.get("players", ["Player"]):
        mask |= AURA_PLAYER_BITS.get(p, 0)
    return mask


def rts0_aura_effect(a):
    """Strongest effect of a 0 A.D. aura that RTS0's AuraManager can model:
    (kind, value, range) or None. Auras on enemies keep their strongest
    penalty (an attack or speed multiplier below 1, negative armour)."""
    enemy = aura_players_mask(a) == 4
    best = None
    for m in a.get("modifications", []):
        v = m.get("value", "")
        kind = value = None
        if v.startswith("Attack/") and "/Damage/" in v and "multiply" in m:
            kind, value = "AURA_ATTACK", m["multiply"]
        elif v.startswith("Attack/") and v.endswith("/RepeatTime") and "multiply" in m and m["multiply"] > 0:
            kind, value = "AURA_ATTACK", 1.0 / m["multiply"]
        elif v == "UnitMotion/WalkSpeed" and "multiply" in m:
            kind, value = "AURA_SPEED", m["multiply"]
        elif v == "Health/Max" and "multiply" in m:
            kind, value = "AURA_HEALTH", m["multiply"]
        elif v == "Vision/Range" and "multiply" in m:
            kind, value = "AURA_VISION", m["multiply"]
        elif v.startswith("Resistance/Entity/Damage/") and "add" in m:
            kind, value = "AURA_ARMOR", m["add"]
        elif v in ("Health/IdleRegenRate", "Health/RegenRate") and "add" in m:
            kind, value = "AURA_HEAL", m["add"]
        elif v.startswith("ResourceGatherer/") and "multiply" in m and m["multiply"] > 1:
            kind, value = "AURA_GATHER", 1.0 - 1.0 / m["multiply"]
        if kind is None:
            continue
        gain = value - 1.0 if kind in MULTIPLY_AURAS else value
        if enemy and kind in MULTIPLY_AURAS + ("AURA_ARMOR",):
            gain = -gain
        if gain <= 0:
            continue
        if best is None or gain > best[3]:
            best = (kind, value, None, gain)
    if best is None:
        return None
    rng = a["radius"] * RANGED_RANGE_SCALE if a["type"] == "range" and a["radius"] > 0 else GLOBAL_AURA_RANGE
    return best[0], best[1], rng


RENUMBER = False  # --renumber: lay rows out afresh (changes saved template ids)


def keep_previous_ids(rows):
    """Saves, replays and lockstep commands store roster template ids, so rows the
    previous civ_roster_data.h had keep their ids; new rows go after all of them
    (a new trainable row lands after the promotion-only ones; civ_roster_ids()
    in sim_types.h adds it to its civ)."""
    if RENUMBER or not os.path.exists(OUT_HEADER):
        return rows
    with open(OUT_HEADER, encoding="utf-8") as fh:
        prev = re.findall(r'^\t\t\{"([^"]+)", "[^"]*", "[^"]*", CIV_[A-Z]+, UNIT_', fh.read(), re.M)
    by_id = {r["id"]: r for r in rows}
    gone = [i for i in prev if i not in by_id]
    if gone:
        sys.exit("roster rows gone from 0 A.D. (%s): re-run with --renumber and bump the save version"
                 % ", ".join(gone))
    seen = set(prev)
    return [by_id[i] for i in prev] + [r for r in rows if r["id"] not in seen]


def collect(sim_root):
    loader = TemplateLoader(sim_root)
    civ_json = {}
    for civ in CIVS:
        with open(os.path.join(sim_root, "data", "civs", civ + ".json"), encoding="utf-8") as f:
            civ_json[civ] = json.load(f)

    generic = {}
    for cls, tmpl in GENERIC.items():
        generic[cls] = raw_stats(loader.load(tmpl))

    bonuses = load_civ_bonuses(sim_root)
    rows = []
    auras = []
    skipped = []
    for civ in CIVS:
        native = civ
        sdir = os.path.join(loader.root, "structures", civ)
        trainers = {}  # unit template -> {buildings}, structure phase
        # The civ's own structures, then Gaia ones that train "{civ}" units for
        # whoever captures them (the mercenary camp).
        structs = [("structures/%s/%s" % (civ, fn[:-4]), structure_kind(fn[:-4]))
                   for fn in sorted(os.listdir(sdir)) if fn.endswith(".xml")]
        structs += [("structures/" + stem, kind) for stem, kind in sorted(GAIA_TRAINERS.items())]
        for path, kind in structs:
            s = loader.load(path)
            ents = s.tokens("Trainer/Entities")
            if not ents:
                continue
            bld = TRAINER_BUILDING.get(kind)
            if bld is None:
                skipped.append("%s structure %s (no RTS0 building)" % (civ, path.rsplit("/", 1)[1]))
                continue
            sphase = phase_of(s)
            for e in ents:
                e = e.replace("{civ}", civ).replace("{native}", native)
                if not loader.exists(e):
                    continue
                slot = trainers.setdefault(e, {"buildings": set(), "kinds": set(), "phase": 9})
                slot["buildings"].add(bld)
                slot["kinds"].add(kind)
                slot["phase"] = min(slot["phase"], sphase)
        # Units no structure trains but a trained unit promotes into (0 A.D.
        # Promotion/Entity to another unit: Marian Reforms' auxiliary cavalry and
        # Antesignani, the Olympic champion, ...): rows with no trainer.
        pending = sorted(trainers)
        while pending:
            src_name = origin = pending.pop()
            node, seen = loader.load(src_name), set()
            while node.value("Promotion/Entity") and src_name not in seen:
                seen.add(src_name)
                tgt = node.value("Promotion/Entity").replace("{civ}", node.value("Identity/Civ") or civ)
                if not loader.exists(tgt):
                    break
                rank_base = lambda n: re.sub(r"_[abe](_dock)?$", "", n)  # noqa: E731
                if rank_base(tgt) != rank_base(src_name) and rank_base(tgt) not in {rank_base(n) for n in trainers}:
                    base = re.sub(r"_[ae]$", "_b", tgt)
                    tgt = base if loader.exists(base) else tgt
                    if tgt not in trainers:
                        trainers[tgt] = {"buildings": set(), "kinds": set(), "phase": trainers[origin]["phase"]}
                        pending.append(tgt)
                    break
                src_name, node = tgt, loader.load(tgt)
        # The civ's catafalque: no structure trains it; Capture the Relic places it for Gaia.
        relic = "units/%s/catafalque" % civ
        if loader.exists(relic) and relic not in trainers:
            trainers[relic] = {"buildings": set(), "kinds": set(), "phase": 9}
        for name in sorted(trainers):
            t = loader.load(name)
            cls = rts0_class(name, t, civ)
            if cls is None:
                skipped.append("%s unit %s (no RTS0 class: %s)" % (
                    civ, name, " ".join(sorted(classes_of(t)))))
                continue
            raw = raw_stats(t)
            if name.endswith("_packed") and loader.exists(name[:-len("_packed")] + "_unpacked"):
                unpacked = raw_stats(loader.load(name[:-len("_packed")] + "_unpacked"))
                for k in ("range", "damage", "cooldown", "hack_dmg", "pierce_dmg", "crush_dmg", "ranged"):
                    raw[k] = unpacked[k]
            c = classes_of(t)
            civ_bonus_ids = apply_unit_bonuses(raw, c, civ, bonuses)
            stats = convert(cls, raw, generic[cls])
            flags = []
            if "Hero" in c:
                flags.append("ROSTER_HERO")
            if "Champion" in c:
                flags.append("ROSTER_CHAMPION")
            if raw["ranged"]:
                flags.append("ROSTER_RANGED")
            if "Citizen" in c:
                flags.append("ROSTER_CITIZEN")
            if "Mercenary" in c:
                flags.append("ROSTER_MERCENARY")
            if "Citizen" in c and not t.value("ResourceGatherer/MaxDistance", ""):
                flags.append("ROSTER_NO_GATHER")  # mixins/mercenary.xml, Neodamodes: ResourceGatherer disabled
            if t.value("Pack/Time", ""):
                flags.append("ROSTER_PACKABLE")  # 0 A.D. Pack: unpack before firing, pack to move
            if "SiegeTower" in c:
                flags.append("ROSTER_SIEGE_TOWER")
            if "Relic" in c:
                flags.append("ROSTER_RELIC")
            hero_auras = []
            for aname in t.tokens("Auras"):
                apath = os.path.join(sim_root, "data", "auras", aname + ".json")
                if not os.path.isfile(apath):
                    continue
                with open(apath, encoding="utf-8") as f:
                    a = json.load(f)
                aid = aname.rsplit("/", 1)[-1]
                if not aid.startswith(civ + "_"):
                    continue  # generic auras (garrison, formation) apply to every unit of a kind
                known = next((i for i, x in enumerate(auras) if x["id"] == aid), None)
                if known is not None:
                    hero_auras.append(known)
                    continue
                hero_auras.append(len(auras))
                auras.append({
                    "id": aid,
                    "name": a.get("auraName", aname),
                    "description": a.get("auraDescription", ""),
                    "type": a.get("type", "range"),
                    "radius": float(a.get("radius", 0)),
                    "affects": a.get("affects", []),
                    "players": a.get("affectedPlayers", ["Player"]),
                    "modifications": a.get("modifications", []),
                })
                auras[-1]["rts0"] = rts0_aura_effect(auras[-1])
            if len(hero_auras) > 3:  # three slots: range auras before garrison ones (the minister has four)
                hero_auras.sort(key=lambda i: auras[i]["type"] == "garrison")
            rows.append({
                "id": name.split("units/", 1)[-1],
                "civ": civ,
                "class": cls,
                "name": t.value("Identity/GenericName", ""),
                "specific_name": t.value("Identity/SpecificName", ""),
                "phase": max(phase_of(t), trainers[name]["phase"] if trainers[name]["phase"] < 9 else 1),
                "trainers": sorted(trainers[name]["buildings"], key=BUILDING_BITS.index),
                "trained_at_0ad": sorted(trainers[name]["kinds"]),
                "flags": flags,
                "unlock_tech": unlock_tech_of(t),
                "auras": hero_auras,
                "civ_bonuses": civ_bonus_ids,
                "classes": sorted(c),
                "stats": stats,
            })
        # RTS0-only classes every civ (or a named civ) keeps.
        for cls in RTS0_ONLY:
            if any(r["civ"] == civ and r["class"] == cls for r in rows):
                continue
            base = dict(zip(RTS0_FIELDS, RTS0_CLASSES[cls]))
            label = "Villager"
            stats = {k: base[k] for k in RTS0_FIELDS}
            stats["pop"] = int(stats["pop"])
            stats["carry"] = int(stats["carry"])
            for r in ("food", "wood", "gold", "stone"):
                stats[r] = int(stats[r])
            rows.append({
                "id": "%s/rts0_%s" % (civ, cls[5:].lower()),
                "civ": civ, "class": cls, "name": label, "specific_name": "",
                "phase": 1,
                "trainers": ["BLD_TOWN_CENTER"],
                "trained_at_0ad": [], "flags": ["ROSTER_CITIZEN"],
                "classes": ["Citizen", "Human", "Organic", "Unit", "Worker"],
                "auras": [], "stats": stats,
            })
    # Units only reached by promotion (no trainer) go after every civ's block, so
    # the template ids of trainable rows, which saves store, stay where they were.
    rows = [r for r in rows if r["trainers"]] + [r for r in rows if not r["trainers"]]
    rows = keep_previous_ids(rows)
    # Which roster units each aura affects (0 A.D. "affects" class lists).
    for a in auras:
        a["affects_rows"] = [i for i, r in enumerate(rows) if affects_match(a["affects"], set(r["classes"]))]
    team = load_team_bonuses(sim_root, rows)
    return rows, auras, skipped, civ_json, bonuses, team

# ---------------------------------------------------------------------------
# Emitters
# ---------------------------------------------------------------------------

def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def f(x):
    s = "%.4g" % x
    if "." not in s and "e" not in s:
        s += ".0"
    return s + "f"


def emit_header(rows, auras, civ_json, bonuses, team):
    out = []
    w = out.append
    w("// GENERATED by tools/extract_0ad_rosters.py from 0 A.D. simulation templates.")
    w("// Do not edit by hand; rerun the tool. 0 A.D. data is CC BY-SA 3.0 (Wildfire Games).")
    w("#ifndef RTSO_CIV_ROSTER_DATA_H")
    w("#define RTSO_CIV_ROSTER_DATA_H")
    w("")
    w("// Included from sim_types.h inside namespace rtso.")
    w("")
    w("inline constexpr uint16_t CIV_UNIT_TEMPLATE_COUNT = %d;" % len(rows))
    w("inline constexpr uint16_t CIV_HERO_AURA_COUNT = %d;" % len(auras))
    words = (len(rows) + 63) // 64
    w("inline constexpr uint16_t CIV_TEMPLATE_WORDS = %d; // uint64 words in a per-template bitset" % words)
    w("")
    w("inline const CivHeroAura *civ_hero_aura_table() {")
    w("\tstatic const CivHeroAura kAuras[CIV_HERO_AURA_COUNT] = {")
    w("\t\t// id, name, description, radius (0 A.D. metres; 0 = global),")
    w("\t\t// RTS0 AuraManager effect: kind (AuraType, 255 = none modelled), value, range,")
    w("\t\t// affected players (AuraPlayers bits: 1 owner, 2 other allies, 4 enemies),")
    w("\t\t// bitset of civ_unit_template_table() rows the aura affects")
    for a in auras:
        bits = [0] * words
        for i in a["affects_rows"]:
            bits[i // 64] |= 1 << (i % 64)
        bitset = "{" + ", ".join("0x%016xull" % b for b in bits) + "}"
        radius = a["radius"] if a["type"] == "range" else 0.0
        eff = a.get("rts0")
        kind, value, rng = (AURA_KIND[eff[0]], eff[1], eff[2]) if eff else (255, 0.0, 0.0)
        w("\t\t{%s, %s, %s, %s, %d, %s, %s, %d, %s}," % (cstr(a["id"]), cstr(a["name"]), cstr(a["description"]),
                                                     f(radius), kind, f(value), f(rng), aura_players_mask(a), bitset))
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("inline const CivUnitTemplate *civ_unit_template_table() {")
    w("\tstatic const CivUnitTemplate kTable[CIV_UNIT_TEMPLATE_COUNT] = {")
    w("\t\t// id, name, specific name, civ, class, phase, flags, trainers,")
    w("\t\t// hp, speed, range, damage, cooldown, hack/pierce/crush dmg, hack/pierce/crush armour,")
    w("\t\t// food, wood, gold, stone, pop, train_time, carry, aura slots (-1 = none), unlock tech,")
    w("\t\t// TechClass mask of the template's 0 A.D. classes")
    for r in rows:
        s = r["stats"]
        flags = " | ".join(r["flags"]) or "0"
        trainers = " | ".join("bld_bit(%s)" % b for b in r["trainers"]) or "0"
        if len(r["trainers"]) > 1:
            trainers = "(uint64_t)(%s)" % trainers
        a = r["auras"] + [-1, -1, -1]
        w("\t\t{%s, %s, %s, %s, %s, %d, %s, %s," % (
            cstr(r["id"]), cstr(r["name"]), cstr(r["specific_name"]),
            CIV_ENUM[CIVS.index(r["civ"])], r["class"], r["phase"], flags, trainers))
        w("\t\t %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %d, %d, %d, %d, %d, %s, %d, %d, %d, %d, %s, 0x%xull}," % (
            f(s["hp"]), f(s["speed"]), f(s["range"]), f(s["damage"]), f(s["cooldown"]),
            f(s["hack_dmg"]), f(s["pierce_dmg"]), f(s["crush_dmg"]),
            f(s["hack_armor"]), f(s["pierce_armor"]), f(s["crush_armor"]),
            s["food"], s["wood"], s["gold"], s["stone"], s["pop"], f(s["train_time"]),
            s["carry"], a[0], a[1], a[2], r.get("unlock_tech", "TECH_NONE"), tech_class_bits(r["classes"])))
    w("\t};")
    w("\treturn kTable;")
    w("}")
    w("")
    prow = player_bonus_rows(bonuses)
    w("inline constexpr uint16_t CIV_PLAYER_BONUS_COUNT = %d;" % len(prow))
    w("")
    w("// 0 A.D. auto-researched civ bonuses that act on buildings or the player.")
    w("// Bonuses on units are already folded into civ_unit_template_table().")
    w("inline const CivPlayerBonus *civ_player_bonus_table() {")
    w("\tstatic const CivPlayerBonus kBonuses[CIV_PLAYER_BONUS_COUNT + 1] = {")
    w("\t\t// civ mask, stat, buildings, multiplier, 0 A.D. bonus id, tooltip")
    for b, stat, mask, mult in prow:
        civs = " | ".join("civ_bit(%s)" % CIV_ENUM[CIVS.index(c)] for c in b["civs"] if c in CIVS) or "0"
        if len(b["civs"]) > 1:
            civs = "(uint16_t)(%s)" % civs
        blds = " | ".join("bld_bit(%s)" % x for x in mask) or "0"
        if len(mask) > 1:
            blds = "(uint64_t)(%s)" % blds
        w("\t\t{%s, %s, %s, %s, %s, %s}," % (civs, stat, blds, f(mult), cstr(b["id"]), cstr(b["tooltip"])))
    w("\t\t{0, 0, 0, 1.0f, \"\", \"\"}, // sentinel")
    w("\t};")
    w("\treturn kBonuses;")
    w("}")
    w("")
    w("inline constexpr uint16_t CIV_TEAM_BONUS_COUNT = %d;" % len(team))
    w("")
    w("// 0 A.D. team bonuses (data/auras/teambonuses): each reaches the civ's player and")
    w("// its allies (allies only when allies_only is set).")
    w("inline const CivTeamBonus *civ_team_bonus_table() {")
    w("\tstatic const CivTeamBonus kTeam[CIV_TEAM_BONUS_COUNT + 1] = {")
    w("\t\t// civ, stat, resource (-1 = n/a), allies only, buildings, value, 0 A.D. aura id, name, description,")
    w("\t\t// bitset of civ_unit_template_table() rows it affects")
    for t in team:
        bits = [0] * words
        for i in t["units"]:
            bits[i // 64] |= 1 << (i % 64)
        bitset = "{" + ", ".join("0x%016xull" % b for b in bits) + "}"
        blds = " | ".join("bld_bit(%s)" % x for x in t["buildings"]) or "0"
        if len(t["buildings"]) > 1:
            blds = "(uint64_t)(%s)" % blds
        w("\t\t{%s, %s, %d, %s, %s, %s, %s, %s, %s, %s}," % (
            CIV_ENUM[CIVS.index(t["civ"])], t["stat"], t["res"], "true" if t["allies_only"] else "false",
            blds, f(t["value"]), cstr(t["id"]), cstr(t["name"]), cstr(t["description"]), bitset))
    w("\t\t{0, 0, -1, false, 0, 1.0f, \"\", \"\", \"\", {}}, // sentinel")
    w("\t};")
    w("\treturn kTeam;")
    w("}")
    w("")
    w("// Each civ's 0 A.D. bonuses as one line of text for the civ picker.")
    w("inline const char *civ_bonus_text(uint8_t civ) {")
    w("\tstatic const char *kText[CIV_COUNT] = {")
    for civ in CIVS:
        parts = ["%s: %s" % (b.get("Name", ""), b.get("Description", ""))
                 for b in civ_json[civ].get("CivBonuses", [])]
        named = {b.get("Name") for b in civ_json[civ].get("CivBonuses", [])}
        parts += ["%s: %s" % (b["name"], b["tooltip"]) for b in bonuses
                  if civ in b["civs"] and b["name"] not in named]
        w("\t\t%s," % cstr(" ".join(p if p.endswith(".") else p + "." for p in parts)))
    w("\t};")
    w("\treturn kText[civ < CIV_COUNT ? civ : 0];")
    w("}")
    w("")
    # Per-civ ranges into the table (rows are grouped by civ).
    # Rows no structure trains (units only reached by promotion) follow the last
    # civ's block, from CIV_ROSTER_PROMOTION_ONLY_FIRST to the table end.
    w("// [first, last) rows of civ_unit_template_table() for each civ. Rows from")
    w("// CIV_ROSTER_PROMOTION_ONLY_FIRST on are units no structure trains, reached only by")
    w("// promotion (0 A.D. Promotion/Entity), after every civ's block so that adding one")
    w("// never moves a trainable row's template id. Trainable rows added later follow them")
    w("// (template ids never move); civ_roster_ids() gives a civ's block and its added rows.")
    trainable = next((i for i, r in enumerate(rows) if not r["trainers"]), len(rows))
    for i in range(1, trainable):
        assert CIVS.index(rows[i]["civ"]) >= CIVS.index(rows[i - 1]["civ"]), "civ blocks out of order"
    w("inline constexpr uint16_t CIV_ROSTER_PROMOTION_ONLY_FIRST = %d;" % trainable)
    w("inline const uint16_t *civ_roster_offsets() {")
    offs = []
    for i, civ in enumerate(CIVS):
        first = next(j for j, r in enumerate(rows) if r["civ"] == civ)
        offs.append(first)
    offs.append(trainable)
    w("\tstatic const uint16_t kOffsets[CIV_COUNT + 1] = {%s};" % ", ".join(str(o) for o in offs))
    w("\treturn kOffsets;")
    w("}")
    w("")
    w("#endif")
    w("")
    return "\n".join(out)


def emit_json(rows, auras, civ_json, bonuses):
    civs = []
    for civ in CIVS:
        j = civ_json[civ]
        civs.append({
            "code": civ,
            "enum": CIV_ENUM[CIVS.index(civ)],
            "bonuses": [{"name": b.get("Name", ""), "description": b.get("Description", "")}
                        for b in j.get("CivBonuses", [])],
            "team_bonuses": [{"name": b.get("Name", ""), "description": b.get("Description", "")}
                             for b in j.get("TeamBonuses", [])],
            "units": [r["id"] for r in rows if r["civ"] == civ],
            "auto_bonuses": [{"id": b["id"], "name": b["name"], "tooltip": b["tooltip"]}
                             for b in bonuses if civ in b["civs"]],
        })
    doc = {
        "generator": "tools/extract_0ad_rosters.py",
        "license": "0 A.D. data, CC BY-SA 3.0, Wildfire Games",
        "civs": civs,
        "units": rows,
        "auras": [{k: v for k, v in a.items() if k != "affects_rows"} for a in auras],
    }
    return json.dumps(doc, indent=1, ensure_ascii=False, sort_keys=False) + "\n"


def unique_building_rows(sim_root):
    """BuildingTypeInfo rows, civ masks and phases for UNIQUE_BUILDINGS, read
    from each structure's 0 A.D. template the way the generic rows in
    sim_types.h were: Health, Cost (metal -> gold), GarrisonHolder,
    Population/Bonus, Capturable, TerritoryDecay; BuildTime x0.25; footprint
    2 cells when wider than 15 m."""
    loader = TemplateLoader(sim_root)
    out = []
    for enum, stems, model, portrait in UNIQUE_BUILDINGS:
        civs, t, src = [], None, None
        for civ in CIVS:
            for st in stems:
                name = "structures/%s/%s" % (civ, st)
                if loader.exists(name):
                    civs.append(civ)
                    if t is None:
                        t, src = loader.load(name), name
                    break
        if t is None:
            raise SystemExit("no 0 A.D. template for %s" % enum)
        fp = t.get("Footprint/Square")
        width = float(fp.attrs.get("width", 0)) if fp is not None else 0.0
        if width == 0.0:
            c = t.get("Footprint/Circle")
            width = 2.0 * float(c.attrs.get("radius", 0)) if c is not None else 0.0
        out.append({
            "enum": enum, "src": src, "civs": civs,
            "name": t.value("Identity/GenericName", "") or t.value("Identity/SpecificName", ""),
            "generic": t.value("Identity/GenericName", ""),
            "model": model, "portrait": portrait,
            "hp": t.num("Health/Max", 1000.0),
            "foot": 2 if width > 15.0 else 1,
            "food": int(t.num("Cost/Resources/food")), "wood": int(t.num("Cost/Resources/wood")),
            "gold": int(t.num("Cost/Resources/metal")), "stone": int(t.num("Cost/Resources/stone")),
            "pop": int(t.num("Population/Bonus")), "garrison": int(t.num("GarrisonHolder/Max")),
            "build_time": t.num("Cost/BuildTime", 100.0) * 0.25,
            "capture": t.num("Capturable/CapturePoints", 500.0),
            "regen": t.num("Capturable/RegenRate", 5.0),
            "garrison_regen": t.num("Capturable/GarrisonRegenRate", 1.0),
            "decay": t.num("TerritoryDecay/DecayRate", 0.0),  # disable="" (Cothon): no decay,
            "phase": phase_of(t),
            # TerritoryInfluence roots on RTS0's scale (0 A.D. civil centre 140 m -> 32).
            "territory": (t.num("TerritoryInfluence/Radius") * 32.0 / 140.0
                          if t.value("TerritoryInfluence/Root") == "true" else 0.0),
            # Vision/Range on RTS0's scale (x0.3, never below the 18 every structure gets).
            "vision": max(18.0, t.num("Vision/Range") * 0.3),
            "dropsite": bool(t.tokens("ResourceDropsite/Types")),
            # Loot/* a destroyed or captured structure yields (Looter.js), metal -> gold.
            "loot": [int(t.num("Loot/" + k)) for k in ("xp", "food", "wood", "stone", "metal")],
        })
    return out


def emit_buildings(rows, generic_masks):
    out = []
    w = out.append
    w("// GENERATED by tools/extract_0ad_rosters.py from 0 A.D. simulation/templates/structures/")
    w("// (CC BY-SA 3.0, Wildfire Games). Do not edit by hand.")
    w("#ifndef RTSO_CIV_BUILDING_DATA_H")
    w("#define RTSO_CIV_BUILDING_DATA_H")
    w("")
    w("// Included from sim_types.h after BuildingType. Civ-specific structures that")
    w("// RTS0 models as their own BuildingType, appended after BLD_EMBASSY.")
    w("inline constexpr uint8_t CIV_BUILDING_FIRST = %s;" % rows[0]["enum"])
    w("")
    w("// BuildingTypeInfo rows for building_type_info(), in BuildingType order.")
    w("#define RTSO_CIV_BUILDING_INFO_ROWS \\")
    for r in rows:
        w("\t\t{%s, %s, %s, %s, %d, %d, %d, %d, %d, %d, %d, %s, %s, %s, %s, %s}, /* %s */ \\" % (
            cstr(r["name"]), cstr(r["model"]), cstr(r["portrait"]), f(r["hp"]), r["foot"],
            r["food"], r["wood"], r["gold"], r["stone"], r["pop"], r["garrison"], f(r["build_time"]),
            f(r["capture"]), f(r["regen"]), f(r["garrison_regen"]), f(r["decay"]), r["src"]))
    w("")
    w("")
    w("// LootInfo rows (xp, food, wood, stone, metal) for building_loot(), in BuildingType order.")
    w("#define RTSO_CIV_BUILDING_LOOT_ROWS \\")
    for r in rows:
        w("\t\t{%d, %d, %d, %d, %d}, /* %s */ \\" % tuple(r["loot"] + [r["src"]]))
    w("")
    w("")
    w("// Civilizations (bit per Civilization) that may build a structure: the civ-specific")
    w("// ones, and generic ones only for the civs whose 0 A.D. Builder lists name them.")
    w("inline uint16_t civ_building_civ_mask(uint8_t type) {")
    w("\tswitch (type) {")
    full = (1 << len(CIVS)) - 1
    for enum in BUILDING_BITS[:BUILDING_BITS.index(rows[0]["enum"])]:
        mask = generic_masks.get(enum, 0)
        if mask != full:
            w("\t\tcase %s: return 0x%04x; // %s" % (enum, mask, " ".join(c for i, c in enumerate(CIVS) if mask >> i & 1) or "none"))
    for r in rows:
        mask = 0
        for c in r["civs"]:
            mask |= 1 << CIVS.index(c)
        w("\t\tcase %s: return 0x%04x; // %s" % (r["enum"], mask, " ".join(r["civs"])))
    w("\t\tdefault: return 0xFFFF;")
    w("\t}")
    w("}")
    w("")
    w("// 0 A.D. Identity/Requirements phase of a civ-specific structure (0 = not one).")
    w("inline uint8_t civ_building_phase(uint8_t type) {")
    w("\tswitch (type) {")
    for r in rows:
        w("\t\tcase %s: return %d;" % (r["enum"], r["phase"]))
    w("\t\tdefault: return 0;")
    w("\t}")
    w("}")
    w("")
    w("// TerritoryInfluence radius of a civ-specific territory root (0 = not a root).")
    w("inline float civ_building_territory_radius(uint8_t type) {")
    w("\tswitch (type) {")
    for r in rows:
        if r["territory"] > 0:
            w("\t\tcase %s: return %s;" % (r["enum"], f(round(r["territory"], 1))))
    w("\t\tdefault: return 0.0f;")
    w("\t}")
    w("}")
    w("")
    w("// Vision range of a civ-specific structure (0 = the default structure vision).")
    w("inline float civ_building_vision(uint8_t type) {")
    w("\tswitch (type) {")
    for r in rows:
        if r["vision"] > 18.0:
            w("\t\tcase %s: return %s;" % (r["enum"], f(round(r["vision"], 1))))
    w("\t\tdefault: return 0.0f;")
    w("\t}")
    w("}")
    w("")
    w("// True for a civ-specific structure that takes every resource (ResourceDropsite).")
    w("inline bool civ_building_is_dropsite(uint8_t type) {")
    drops = [r["enum"] for r in rows if r["dropsite"]]
    w("\treturn %s;" % (" || ".join("type == %s" % e for e in drops) or "false"))
    w("}")
    w("")
    w("#endif // RTSO_CIV_BUILDING_DATA_H")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--0ad", dest="zad", required=True, help="0 A.D. source checkout")
    ap.add_argument("--check", action="store_true", help="fail if outputs are stale")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--renumber", action="store_true",
                    help="lay the rows out afresh instead of keeping civ_roster_data.h's template ids")
    args = ap.parse_args()
    global RENUMBER
    RENUMBER = args.renumber
    sim_root = os.path.join(args.zad, "binaries", "data", "mods", "public", "simulation")
    rows, auras, skipped, civ_json, bonuses, team = collect(sim_root)
    header = emit_header(rows, auras, civ_json, bonuses, team)
    js = emit_json(rows, auras, civ_json, bonuses)
    bld = emit_buildings(unique_building_rows(sim_root), generic_build_masks(sim_root))
    if args.verbose:
        for s in skipped:
            print("skipped:", s, file=sys.stderr)
    if args.check:
        stale = []
        for path, text in ((OUT_HEADER, header), (OUT_JSON, js), (OUT_BUILDINGS, bld)):
            if not os.path.isfile(path) or open(path, encoding="utf-8").read() != text:
                stale.append(os.path.relpath(path, REPO))
        if stale:
            print("stale: " + ", ".join(stale), file=sys.stderr)
            return 1
        return 0
    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    with open(OUT_HEADER, "w", encoding="utf-8") as fh:
        fh.write(header)
    with open(OUT_JSON, "w", encoding="utf-8") as fh:
        fh.write(js)
    with open(OUT_BUILDINGS, "w", encoding="utf-8") as fh:
        fh.write(bld)
    per_civ = {c: sum(1 for r in rows if r["civ"] == c) for c in CIVS}
    heroes = {c: sum(1 for r in rows if r["civ"] == c and "ROSTER_HERO" in r["flags"]) for c in CIVS}
    print("%d unit templates, %d hero auras, %d skipped" % (len(rows), len(auras), len(skipped)))
    for c in CIVS:
        print("  %-6s %3d units, %d heroes" % (c, per_civ[c], heroes[c]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
