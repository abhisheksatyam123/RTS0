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
The 0.28 regen runs --keep-trainers --previous-0ad with --append taking the row list in
/mnt/project-files/d-civ-rosters/roster-append-028.txt followed by
",rome/champion_infantry_swordsman_02,rome/champion_infantry_swordsman_03,
rome/champion_infantry_swordsman_04,rome/champion_infantry_swordsman_05,athen/hero_xenophon,
spart/hero_leonidas_300,pers/arstibara,han/siege_shoushe,plane" (rows 507-514 and 544, added
after that file was last written) and --retire the patterns in roster-retire-028.txt.
"""

import argparse
import fnmatch
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

# RTS0's playable civs in Civilization enum order (sim_types.h). Han and the Germans
# are appended so saved games and network messages keep their civ ids; the Germans
# are 15 because 14 is CIV_GAIA, the civ saved on Gaia's structures (save v62).
CIVS = ["athen", "spart", "pers", "rome", "cart", "mace", "brit", "gaul",
        "iber", "maur", "kush", "ptol", "sele", "han", "germ"]
CIV_ENUM = ["CIV_ATHENIANS", "CIV_SPARTANS", "CIV_PERSIANS", "CIV_ROMANS",
            "CIV_CARTHAGINIANS", "CIV_MACEDONIANS", "CIV_BRITONS", "CIV_GAULS",
            "CIV_IBERIANS", "CIV_MAURYANS", "CIV_KUSHITES", "CIV_PTOLEMIES",
            "CIV_SELEUCIDS", "CIV_HAN", "CIV_GERMANS"]
CIV_ENUM_OF = dict(zip(CIVS, CIV_ENUM))
CIV_GAIA = 14
CIV_INDEX = {c: (i if i < CIV_GAIA else i + 1) for i, c in enumerate(CIVS)}
CIV_COUNT = len(CIVS) + 1  # with the CIV_GAIA slot
CIV_SLOTS = [next((c for c in CIVS if CIV_INDEX[c] == i), None) for i in range(CIV_COUNT)]  # None = CIV_GAIA

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
    "UNIT_TYPE_TECH":       (0, 0.0, 0.0, 0.0, 1.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    "UNIT_WAGON":           (200, 1.78, 1.0, 0.0, 1.0, 0, 4, 4, 10, 50, 50, 0, 0, 1, 12, 0, 0, 0),
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
    "UNIT_TYPE_TECH": "",  # a queued technology's ProductionOrder: no template, empty rows
    "UNIT_WAGON": "template_unit_support_wagon",
}

# RTS0's male gatherer has no 0 A.D. counterpart in most civs; every civ keeps it.
RTS0_ONLY = ("UNIT_VILLAGER",)

# 0 A.D. structure (file stem, without trailing _N upgrade suffix) to the RTS0
# BuildingType that trains its units. Buildings RTS0 lacks fall back to the
# closest one RTS0 has; the 0 A.D. name stays in the data for later remaps.
# Gaia structures whose Trainer lists "units/{civ}/..." for their captor:
# template stem -> structure kind (TRAINER_BUILDING key).
# (civ, BuildingType, unit) a captured foreign structure adds (for --check output).
CAPTURED_TRAINERS = set()
GAIA_TRAINERS = {"merc_camp_egyptian": "mercenary_camp", "trading_post_desert": "trading_post"}
# Gaia shrines (mixins/shrine.xml) train "units/{native}/support_healer_e": the Elite Healer
# of the shrine's own civ (Identity/Civ), whoever holds it.
GAIA_NATIVE_TRAINERS = ("shrine_celtic", "shrine_chinese", "shrine_egyptian", "shrine_greek_01",
                        "shrine_greek_02", "shrine_italian", "shrine_kushite", "shrine_persian")

TRAINER_BUILDING = {
    "civil_centre": "BLD_TOWN_CENTER", "military_colony": "BLD_TOWN_CENTER",
    "civil_centre_court": "BLD_TOWN_CENTER", "palace": "BLD_TOWN_CENTER",
    "prytaneion": "BLD_TOWN_CENTER", "tachara": "BLD_TOWN_CENTER",
    "house": "BLD_HOUSE", "barracks": "BLD_BARRACKS", "stable": "BLD_STABLE",
    "range": "BLD_RANGE", "elephant_stable": "BLD_ELEPHANT_STABLE",
    "gymnasium": "BLD_BARRACKS", "mercenary_camp": "BLD_BARRACKS",
    # cart/embassy (all seven mercenaries) is in no Builder list: each embassy trains its own.
    "embassy_celtic": "BLD_EMBASSY",
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
    "shrine": "BLD_SHRINE", "trading_post": "BLD_TRADING_POST",
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
    ("BLD_GEROUSIA", ["gerousia"], "gerousia", "gerousia"),
    ("BLD_SYSSITON", ["syssiton"], "syssiton", "syssiton"),
    ("BLD_TACHARA", ["tachara"], "tachara", "tachara"),
    ("BLD_ARMY_CAMP", ["army_camp"], "army_camp", "army_camp"),
    ("BLD_AMPHITHEATER", ["amphitheater_pompeii"], "amphitheater", "amphitheater"),
    ("BLD_LIBRARY", ["library"], "library", "library"),
    ("BLD_KENNEL", ["kennel"], "kennel", "kennel"),
    ("BLD_ASSEMBLY", ["assembly"], "assembly", "assembly"),
    # The Iberian monument; the Germans' Cerannus totem projects the same aura (iber_monument).
    ("BLD_MONUMENT", ["monument", "totem"], "monument", "monument"),
    ("BLD_PALACE", ["palace"], "palace", "palace"),
    ("BLD_PILLAR", ["pillar_ashoka"], "pillar", "pillar"),
    ("BLD_PYRAMID_LARGE", ["pyramid_large"], "pyramid_large", "pyramid_large"),
    ("BLD_PYRAMID_SMALL", ["pyramid_small"], "pyramid_small", "pyramid_small"),
    ("BLD_MERCENARY_CAMP", ["mercenary_camp"], "mercenary_camp", "mercenary_camp"),
    ("BLD_LIGHTHOUSE", ["lighthouse"], "lighthouse", "lighthouse"),
    ("BLD_MILITARY_COLONY", ["military_colony"], "military_colony", "military_colony"),
    ("BLD_ACADEMY", ["academy"], "academy", "academy"),
    ("BLD_MINISTRY", ["ministry"], "imperial_ministry", "ministry"),
    ("BLD_CAMP_BLEMMYE", ["camp_blemmye"], "camp_blemmye", "camp_blemmye"),
    ("BLD_CAMP_NOBA", ["camp_noba"], "camp_noba", "camp_noba"),
    ("BLD_TEMPLE_AMUN", ["temple_amun"], "temple_amun", "temple_amun"),
    ("BLD_TEMPLE_VESTA", ["temple_vesta"], "temple_vesta", "temple_vesta"),
    ("BLD_TEMPLE_MARS", ["temple_mars"], "temple_mars", "temple_mars"),
    ("BLD_ICE_HOUSE", ["ice_house"], "ice_house", "ice_house"),
    ("BLD_SUPER_DOCK", ["super_dock"], "super_dock", "super_dock"),
    # Reached only by an Upgrade (UPGRADE_ONLY): the Tachara's four tribute forms and
    # the Carthaginian apartment.
    ("BLD_TACHARA_FOOD", ["tachara_food"], "tachara", "tachara"),
    ("BLD_TACHARA_WOOD", ["tachara_wood"], "tachara", "tachara"),
    ("BLD_TACHARA_STONE", ["tachara_stone"], "tachara", "tachara"),
    ("BLD_TACHARA_METAL", ["tachara_metal"], "tachara", "tachara"),
    ("BLD_APARTMENT", ["apartment"], "apartment", "house"),
    # The Han Imperial Court (a Civic Center upgrade) and Great Archery Tower (a defense
    # tower upgrade): RTS0 treats them as their base type wherever the role matters
    # (is_civic_centre, is_defense_tower).
    ("BLD_IMPERIAL_COURT", ["civil_centre_court"], "imperial_court", "imperial_court"),
    ("BLD_TOWER_GREAT", ["defense_tower_great"], "tower_great", "tower_great"),
    # The palisade gate (structures/palisades_gate, a palisades_long Upgrade; drawn with the
    # palisade until it has its own model). Generic: every civ that lays palisades has it.
    ("BLD_PALISADE_GATE", ["palisades_gate"], "palisade", "palisade"),
    # Carthage's Iberian and Italic embassies (BLD_EMBASSY is the Celtic one), each training
    # its own mercenaries.
    ("BLD_EMBASSY_IBERIAN", ["embassy_iberian"], "embassy_iberian", "embassy"),
    ("BLD_EMBASSY_ITALIC", ["embassy_italic"], "embassy_italic", "embassy"),
    # The Roman siege wall (rome/wallset_siege): its segments and towers are one type, read from
    # siege_wall_long (sim_types.h wallset_info has each piece's values); drawn with the stone
    # wall until it has its own model.
    ("BLD_SIEGE_WALL", ["siege_wall_long"], "wall_segment", "wall"),
    # Its gate (a siege_wall_long Upgrade), drawn with the stone gate until it has its own model.
    ("BLD_SIEGE_WALL_GATE", ["siege_wall_gate"], "gate", "gate"),
    # The Briton crannog: a Civic Center laid on the shore (is_civic_centre).
    ("BLD_CRANNOG", ["crannog"], "crannog", "crannog"),
    # The Germans' Great Hall (Town phase): their champions and the ram train there.
    ("BLD_GREAT_HALL", ["great_hall"], "great_hall", "great_hall"),
    # The Germans' Wagon Encampment and its fortified Upgrade (a Colony that claims no territory),
    # drawn with 0.28's encampment actors (the portrait is still the army camp's).
    ("BLD_WAGON_ENCAMPMENT", ["encampment"], "encampment", "army_camp"),
    ("BLD_WAGON_ENCAMPMENT_FORTIFIED", ["encampment_fortified"], "encampment_fortified", "army_camp"),
    # Gaia structures maps place (GAIA_UNIQUE): nobody builds them; captured, never destroyed.
    # The type's row is read from the first template; each placed one keeps its template's civ.
    ("BLD_SHRINE", ["shrine_greek_01"], "shrine", "temple"),
    ("BLD_TRADING_POST", ["trading_post_desert"], "trading_post", "market"),
    # Structures only maps place (MAP_ONLY): no Builder list names them. The bolt and artillery
    # towers, the royal stoa, the Persian inn, the Roman tent and triumphal arch are civ
    # templates (their row is read from the first civ's); the Hellenic stoa is generic.
    ("BLD_TOWER_BOLT", ["tower_bolt"], "tower_bolt", "tower_bolt"),
    ("BLD_TOWER_ARTILLERY", ["tower_artillery"], "tower_artillery", "tower_artillery"),
    ("BLD_STOA", ["royal_stoa"], "greek_stoa_great", "stoa"),
    ("BLD_HELLENIC_STOA", ["hellenic_stoa"], "greek_stoa", "stoa"),
    ("BLD_INN", ["inn"], "inn", "house"),
    ("BLD_TENT", ["tent"], "tent", "house"),
    ("BLD_TRIUMPHAL_ARCH", ["arch"], "triumphal_arch", "arch"),
    # More of them: the Persian Gate of All Nations and warehouse, the Gallic tavern, the
    # Mauryan rampart tower, the Kushite shrine and the Han old fortress and Laozi gate.
    ("BLD_HALL", ["hall"], "pers_hall", "palace"),
    ("BLD_WAREHOUSE", ["warehouse"], "warehouse", "storehouse"),
    ("BLD_TAVERN", ["tavern"], "tavern", "house"),
    ("BLD_TOWER_DOUBLE", ["tower_double"], "maur_tower_double", "tower"),
    ("BLD_KUSH_SHRINE", ["shrine"], "kush_shrine", "temple"),
    ("BLD_FORTRESS_OLD", ["fortress_old"], "han_fortress_old", "fortress"),
    ("BLD_LAOZI_GATE", ["laozigate"], "laozi_gate", "temple"),
    # The Ptolemaic Temple of Isis (ptol/temple_2, City phase, one per player): the
    # Ptolemaic heroes train there only. Drawn with the civ's temple until its actor is ported.
    ("BLD_TEMPLE_ISIS", ["temple_2"], "temple", "temple"),
    # The Gaia mills and rotary mills maps place (structures/mill_*, {brit,gaul}/rotarymill,
    # mixins/rotary_mill.xml): captured, never destroyed; their farmstead_60 aura speeds the
    # grain of the owner's workers nearby. Drawn with the farmstead of the template's civ.
    ("BLD_MILL", ["mill_greek"], "farmstead", "farmstead"),
]
# Numbered stems that are a building of their own rather than an upgrade's _N form.
KEEP_NUMBERED_STEMS = {"temple_2"}
# Unique WallSet pieces whose phase is their WallSet's (wallset_siege needs phase_city).
WALLSET_PHASE = {"BLD_SIEGE_WALL": "wallset_siege", "BLD_SIEGE_WALL_GATE": "wallset_siege"}
# WallSet pieces keep one cell like the stone wall's: their turned footprint is their length
# (sim_types.h wallset_info), not a square.
ONE_CELL = {"BLD_SIEGE_WALL", "BLD_SIEGE_WALL_GATE"}
# Civ structures named by Identity/SpecificName rather than a GenericName they share.
SPECIFIC_NAME = {"BLD_EMBASSY_IBERIAN", "BLD_EMBASSY_ITALIC", "BLD_INN", "BLD_WAREHOUSE"}
# Structures no Builder list names: only an Upgrade reaches them.
# (Carthage lays its apartment directly too: mixins/builder.xml lists {civ}/apartment.)
UPGRADE_ONLY = {"BLD_TACHARA_FOOD", "BLD_TACHARA_WOOD", "BLD_TACHARA_STONE", "BLD_TACHARA_METAL",
                "BLD_IMPERIAL_COURT", "BLD_TOWER_GREAT", "BLD_PALISADE_GATE", "BLD_SIEGE_WALL_GATE",
                "BLD_WAGON_ENCAMPMENT_FORTIFIED"}
# Generic structures (structures/<stem>.xml) in UNIQUE_BUILDINGS -> the generic BuildingType
# whose civs have them too.
GENERIC_UNIQUE = {"BLD_PALISADE_GATE": "BLD_PALISADE"}
# Gaia structures in UNIQUE_BUILDINGS (structures/<stem>.xml, mixins/shrine.xml and
# mixins/trading_post.xml): no civ builds them.
GAIA_UNIQUE = {"BLD_SHRINE", "BLD_TRADING_POST", "BLD_HELLENIC_STOA", "BLD_MILL"}
# Civ structures in UNIQUE_BUILDINGS that only maps place: no civ builds them.
MAP_ONLY = {"BLD_TOWER_BOLT", "BLD_TOWER_ARTILLERY", "BLD_STOA", "BLD_INN", "BLD_TENT", "BLD_TRIUMPHAL_ARCH",
            "BLD_HALL", "BLD_WAREHOUSE", "BLD_TAVERN", "BLD_TOWER_DOUBLE", "BLD_KUSH_SHRINE", "BLD_FORTRESS_OLD",
            "BLD_LAOZI_GATE"}
# Generic structures a civ structure's Upgrade can start from (structures/cart/house,
# structures/han/civil_centre, structures/han/defense_tower).
UPGRADE_FROM_GENERIC = {"house": "BLD_HOUSE", "civil_centre": "BLD_TOWN_CENTER", "defense_tower": "BLD_TOWER"}
# Units of a civ-specific structure train only there, as in 0 A.D. (Petra builds
# these structures since #105).
for _enum, _stems, _model, _portrait in UNIQUE_BUILDINGS:
    for _st in _stems:
        TRAINER_BUILDING[_st] = _enum
TRAINER_BUILDING.update({"imperial_ministry": "BLD_MINISTRY", "ministry": "BLD_MINISTRY"})
# "shrine" names the Gaia shrines' Trainer (GAIA_NATIVE_TRAINERS); kush/shrine trains nothing.
TRAINER_BUILDING["shrine"] = "BLD_SHRINE"
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
    "theater": ["BLD_THEATER"], "embassy_celtic": ["BLD_EMBASSY"],
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
                    masks[bld] = masks.get(bld, 0) | (1 << CIV_INDEX[civ])
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
        # --previous-0ad: a template this tree lacks is read from the previous one
        # (--keep-trainers keeps rows whose template the release dropped).
        self.fallback = TemplateLoader(PREVIOUS_SIM_ROOT) if PREVIOUS_SIM_ROOT and PREVIOUS_SIM_ROOT != sim_root else None

    def own_exists(self, name):
        return os.path.isfile(os.path.join(self.root, name.split("|")[-1] + ".xml"))

    def exists(self, name):
        return self.own_exists(name) or (self.fallback is not None and self.fallback.exists(name))

    def load(self, name):
        if name in self.cache:
            return self.cache[name]
        if self.fallback is not None and not self.own_exists(name) and self.fallback.exists(name):
            node = self.fallback.load(name)
            self.cache[name] = node
            return node
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

    def own_tokens(self, name, path):
        """Tokens a template file lists itself under path, not its parents' (empty for mixins)."""
        f = os.path.join(self.root, name + ".xml")
        if "|" in name or not os.path.isfile(f):
            return []
        e = ET.parse(f).getroot().find(path)
        return e.text.split() if e is not None and e.text else []

    def apply_mixin(self, node, mixin):
        """Overlay mixins/<mixin>.xml, after its own parent mixins (e.g. merc_inf -> mercenary)."""
        elem = ET.parse(os.path.join(self.root, "mixins", mixin + ".xml")).getroot()
        parent = elem.attrib.get("parent")
        if parent:
            for m in reversed(parent.split("|")):
                self.apply_mixin(node, m)
        merge_into(node, elem)


def generic_template(loader, cls):
    """The GENERIC template of an RTS0 UnitType, or an empty one where there is none:
    UNIT_TYPE_TECH has no template, and a tree from before the Germans (2024) has no
    template_unit_support_wagon."""
    name = GENERIC[cls]
    return loader.load(name) if name and loader.exists(name) else Node()

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
    hack = pierce = crush = fire = 0.0
    rng = rep = 0.0
    if atk is not None:
        hack = atk.num("Damage/Hack")
        pierce = atk.num("Damage/Pierce")
        crush = atk.num("Damage/Crush")
        fire = atk.num("Damage/Fire")  # fire ships (civ_attack_data.h keeps it apart)
        rng = atk.num("MaxRange")
        rep = atk.num("RepeatTime") / 1000.0
    carry = 0.0
    caps = t.get("ResourceGatherer/Capacities")
    if caps is not None:
        carry = max([_safe(c.text) for c in caps.children.values()] or [0.0])
    return {
        "hp": t.num("Health/Max"),
        # UnitMotionFlying.GetWalkSpeed is its MaxSpeed (units/plane).
        "speed": t.num("UnitMotion/WalkSpeed") or t.num("UnitMotionFlying/MaxSpeed"),
        "vision": t.num("Vision/Range"),
        "range": rng,
        "damage": hack + pierce + crush + fire,
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
        "fire_dmg": fire,
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
    # Vision/Range on the ranged-range scale (0 = no own vision, e.g. some fauna-like units).
    out["vision"] = raw.get("vision", 0.0) * RANGED_RANGE_SCALE
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
    if t.value("UnitMotionFlying/MaxSpeed", ""):
        # units/plane (Flight Demo's P-51): no classes of its own; a ranged engine that neither
        # packs, gathers nor promotes, so the bolt shooter's class, flagged ROSTER_FLYING.
        return "UNIT_BOLT_SHOOTER"
    if "Minister" in c:
        return "UNIT_MINISTER"  # units/han/support_minister
    if "Relic" in c:
        return "UNIT_CATAFALQUE"  # template_unit_catafalque (Capture the Relic)
    if "Wagon" in c:
        return "UNIT_WAGON"  # template_unit_support_wagon (Germans)
    if "Hero" in c:
        return "UNIT_HERO"
    if "FemaleCitizen" in c or ("Civilian" in c and stem.startswith("support_civilian")):
        return "UNIT_FEMALE_CITIZEN"  # 0.28's Civilians take the Female Citizens' place
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
        if t.value("Attack/Ranged/Damage/Fire"):
            # template_unit_siege_flamethrower (the Theban fire-raiser): a ranged siege engine
            # firing one Fire projectile, no splash, so the bolt shooter's class; its row's
            # CombatProfile carries the Fire damage, as the fire ships' do.
            return "UNIT_BOLT_SHOOTER"
        return None
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


def _tech_class_table(path):
    """TC_ name (without TC_) -> bit, from tech_data.h's TechClass enum and its
    TechClasses constants from bit 64 up."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    table = {m.group(1): int(m.group(2)) for m in re.finditer(r"^\tTC_([A-Z0-9]+) = 1ull << (\d+),", text, re.M)}
    table.update({m.group(1): int(m.group(2)) for m in re.finditer(
        r"^inline constexpr TechClasses TC_([A-Z0-9]+) = tc_bit\((\d+)\);", text, re.M)})
    return table


def tech_classes_literal(bits):
    """A TechClasses initializer for a class mask: a plain 64-bit literal while
    only bits 0..63 are set."""
    if bits >> 64:
        return "TechClasses{0x%xull, 0x%xull}" % (bits & ((1 << 64) - 1), bits >> 64)
    return "0x%xull" % bits


def tech_class_bits(classes):
    """0 A.D. classes as a sim_types.h TechClass mask (bits from tech_data.h)."""
    global _TECH_CLASSES
    if _TECH_CLASSES is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "simulation", "tech_data.h")
        _TECH_CLASSES = _tech_class_table(path)
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


RANK_SUFFIX = (("_b", 1), ("_a", 2), ("_e", 3))


def unit_upgrades(loader, name, civ):
    """0 A.D. Upgrade choices of a unit row and of its rank templates (the Roman
    Centurion upgrade sits on infantry_*_e): [(entity, from rank or 0 for any,
    {resource: cost}, time, phase)]. Upgrade.js DetermineUpgrades drops missing entities."""
    out = []
    base = re.sub(r"_[bae]$", "", name)
    variants = [(base + suf, rank) for suf, rank in RANK_SUFFIX] if base != name else [(name, 0)]
    for tname, rank in variants:
        if not loader.exists(tname):
            continue
        up = loader.load(tname).get("Upgrade")
        if up is None:
            continue
        for choice in sorted(up.children):
            c = up.children[choice]
            ent = (c.value("Entity") or "").replace("{civ}", civ).replace("{native}", civ)
            if not loader.exists(ent):
                continue
            cost = {r: int(c.num("Cost/" + r)) for r in ("food", "wood", "stone", "metal") if c.num("Cost/" + r)}
            phase = 1
            for tech in c.tokens("Requirements/Techs"):
                phase = max(phase, PHASE_OF_TECH.get(tech, 1))
            out.append((ent, rank, cost, c.num("Time"), phase))
    return out

# ---------------------------------------------------------------------------
# Roster walk
# ---------------------------------------------------------------------------

def structure_kind(stem):
    if stem in KEEP_NUMBERED_STEMS:
        return stem
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
        "UnitMotion/WalkSpeed": "speed", "Vision/Range": "vision", "Health/Max": "hp", "Cost/BuildTime": "train_time",
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
    raw["damage"] = raw["hack_dmg"] + raw["pierce_dmg"] + raw["crush_dmg"] + raw["fire_dmg"]
    return applied


# 0 A.D. structure classes in civ bonus "affects" lists, as RTS0 BuildingTypes.
BUILDING_CLASS = {
    "Structure": BUILDING_BITS,
    "Wall": ["BLD_WALL_SEGMENT", "BLD_GATE", "BLD_WALL_TOWER"],
    "Tower": ["BLD_TOWER", "BLD_SENTRY_TOWER", "BLD_TOWER_GREAT"], "Field": ["BLD_FIELD"],
    "Storehouse": ["BLD_STOREHOUSE"],
    "House": ["BLD_HOUSE"],
    "Economic": ["BLD_STOREHOUSE", "BLD_FARMSTEAD", "BLD_MARKET", "BLD_CORRAL", "BLD_HOUSE",
                 "BLD_TRADING_POST",  # trading_post_* inherit template_structure_economic_market
                 "BLD_MILL"],  # mill_greek: rotary_mill|template_structure_economic_farmstead
    "CivCentre": ["BLD_TOWN_CENTER", "BLD_IMPERIAL_COURT", "BLD_CRANNOG", "BLD_MILITARY_COLONY"], "Naval": ["BLD_DOCK"], "Wonder": ["BLD_WONDER"],
    # military_colony inherits template_structure_civic_civil_centre; the temples of Vesta, Mars,
    # Amun and Isis (ptol/temple_2) template_structure_civic_temple; han/academy and the
    # mercenary camps template_structure_military_barracks.
    "CivilCentre": ["BLD_TOWN_CENTER", "BLD_IMPERIAL_COURT", "BLD_CRANNOG", "BLD_MILITARY_COLONY"],
    "Temple": ["BLD_TEMPLE", "BLD_TEMPLE_VESTA", "BLD_TEMPLE_MARS", "BLD_TEMPLE_AMUN", "BLD_TEMPLE_ISIS",
               "BLD_KUSH_SHRINE", "BLD_SHRINE"], "Forge": ["BLD_FORGE"],
    "Barracks": ["BLD_BARRACKS", "BLD_ACADEMY", "BLD_MERCENARY_CAMP"], "Stable": ["BLD_STABLE"], "Arsenal": ["BLD_ARSENAL"],
}
BUILDING_STAT = {
    "Health/Max": "CIV_BONUS_BUILDING_HP", "Cost/BuildTime": "CIV_BONUS_BUILDING_BUILD_TIME",
    "Cost/Resources/wood": "CIV_BONUS_BUILDING_WOOD_COST",
    "Cost/Resources/stone": "CIV_BONUS_BUILDING_STONE_COST",
    "Capturable/CapturePoints": "CIV_BONUS_BUILDING_CAPTURE_POINTS",
    "Loot/wood": "CIV_BONUS_BUILDING_LOOT_WOOD",
    "Researcher/TechCostMultiplier/time": "CIV_BONUS_BUILDING_RESEARCH_TIME",
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
            if mult is None and v == "Researcher/TechCostMultiplier/time":
                mult = m.get("replace")  # the multiplier defaults to 1, so replace is multiply
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
                    # ModifiersManager adds "add" after the multipliers (Stolen Arms: -5 metal).
                    entries.append(("TEAM_BONUS_UNIT_COST_ADD" if "add" in m else "TEAM_BONUS_UNIT_COST", res, [], units))
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
             "AURA_HEALTH": 5, "AURA_VISION": 6, "AURA_HEAL_AMOUNT": 7, "AURA_VISION_ADD": 18,
             "AURA_ATTACK_RATE": 19}
MULTIPLY_AURAS = ("AURA_ATTACK", "AURA_SPEED", "AURA_HEALTH", "AURA_VISION")
GLOBAL_AURA_RANGE = 1000.0


# 0 A.D. Auras.js affectedPlayers -> AuraPlayers bits in sim_types.h: 1 = the
# owner, 2 = the owner's other allies, 4 = enemies (GetAllies() includes the
# owner itself; ExclusiveMutualAlly does not).
AURA_PLAYER_BITS = {"Player": 1, "Ally": 11, "MutualAlly": 3, "ExclusiveMutualAlly": 2, "Enemy": 4}  # 8: by the owner's own stance


def aura_players_mask(a):
    mask = 0
    for p in a.get("players", ["Player"]):
        mask |= AURA_PLAYER_BITS.get(p, 0)
    return mask


def rts0_aura_effect(a):
    """Strongest effect of a 0 A.D. aura that RTS0's AuraManager can model:
    (kind, value, range) or None. Auras on enemies keep their strongest
    penalty (an attack or speed multiplier below 1, negative armour)."""
    best = None
    for kind, value, gain in aura_modifications(a):
        if gain <= 0:
            continue
        if best is None or gain > best[3]:
            best = (kind, value, None, gain)
    if best is None:
        return None
    return best[0], best[1], aura_range(a)


def aura_range(a):
    return a["radius"] * RANGED_RANGE_SCALE if a["type"] == "range" and a["radius"] > 0 else GLOBAL_AURA_RANGE


def rts0_aura_effects(a):
    """Every kind of effect of a 0 A.D. aura that RTS0's AuraManager can model,
    the strongest of each, penalties included: [(kind, value, range)]. A
    structure's aura row per kind (the Kushite large pyramid's attack and armour)."""
    best = {}
    for kind, value, gain in aura_modifications(a):
        if gain != 0 and (kind not in best or abs(gain) > abs(best[kind][1])):
            best[kind] = (value, gain)
    return [(k, best[k][0], aura_range(a)) for k in AURA_KIND if k in best]


def aura_modifications(a):
    """(kind, value, gain) for each modification RTS0 models; gain > 0 helps
    the affected units (an aura on enemies helps its owner by hurting them)."""
    enemy = aura_players_mask(a) == 4
    out = []
    for m in a.get("modifications", []):
        v = m.get("value", "")
        kind = value = None
        if v.startswith("Attack/") and "/Damage/" in v and "multiply" in m:
            kind, value = "AURA_ATTACK", m["multiply"]
        elif v.startswith("Attack/") and v.endswith("/RepeatTime") and "multiply" in m and m["multiply"] > 0:
            kind, value = "AURA_ATTACK_RATE", m["multiply"]
        elif v == "UnitMotion/WalkSpeed" and "multiply" in m:
            kind, value = "AURA_SPEED", m["multiply"]
        elif v == "Health/Max" and "multiply" in m:
            kind, value = "AURA_HEALTH", m["multiply"]
        elif v == "Vision/Range" and "multiply" in m:
            kind, value = "AURA_VISION", m["multiply"]
        elif v == "Vision/Range" and "add" in m:
            kind, value = "AURA_VISION_ADD", m["add"]
        elif v.startswith("Resistance/Entity/Damage/") and "add" in m:
            kind, value = "AURA_ARMOR", m["add"]
        elif v in ("Health/IdleRegenRate", "Health/RegenRate") and "add" in m:
            kind, value = "AURA_HEAL", m["add"]
        elif v == "Heal/Health" and "add" in m:
            kind, value = "AURA_HEAL_AMOUNT", m["add"]
        elif v == "ResourceGatherer/BaseSpeed" and "multiply" in m and m["multiply"] > 1:
            kind, value = "AURA_GATHER", 1.0 - 1.0 / m["multiply"]
        if kind is None:
            continue
        gain = value - 1.0 if kind in MULTIPLY_AURAS else value
        if kind == "AURA_ATTACK_RATE":  # a shorter RepeatTime helps
            gain = 1.0 / value - 1.0
        if enemy and kind in MULTIPLY_AURAS + ("AURA_ARMOR", "AURA_HEAL", "AURA_HEAL_AMOUNT", "AURA_VISION_ADD"):
            gain = -gain
        out.append((kind, value, gain))
    return out


RENUMBER = False  # --renumber: lay rows out afresh (changes saved template ids)
# --keep-trainers: every row civ_roster_data.h has keeps its id (also rows the release
# dropped, read from --previous-0ad with their old trainers and phase) and new rows are
# left out unless --append names them, so a release's changes land without moving the
# template ids saves store. Rows the release has take its stats, trainers and phase.
KEEP_TRAINERS = False
PREVIOUS_SIM_ROOT = None
# Building types added after the kept layout, with their structures/{civ}/ template and the
# type whose Trainer list theirs adds to: --keep-trainers adds them to the kept trainers where
# the release trains the unit there, or where the civ has the template and the kept trainers
# hold that type (0.28's crannog adds ships to the civil centre's list).
KEEP_TRAINERS_NEW_BUILDINGS = {"BLD_CRANNOG": ("crannog", "BLD_TOWN_CENTER")}
# --append: new rows --keep-trainers still adds (after every existing row, so no id moves).
APPEND_ROWS = set()
# Templates only 0.28's maps or Regicide place (scenario heroes, Xenophon and the 300's Leonidas, whom
# Regicide.js draws with every Hero template of the civ, the Samnite, Kardakes and pirate units,
# Juggernaut, Noldor and Viking ships): no structure trains them, so the civ walk never reaches
# them, and a map spawning one fell back to its owner's generic unit of the class. Each is a
# row of its own (ROSTER_MAP_ONLY, no trainers), after every existing row (name them in
# --append): a units/<civ>/ template goes in its civ's slot, a template without a civ
# (template_unit.xml's Identity/Civ gaia) in CIV_GAIA's. The map's owner fields it as it is,
# whatever its own civ: 0.28's scenarios give a Roman player Samnite mercenaries.
# An _a/_e template stays its _b row at that rank (Simulation::resolve_map_template).
MAP_CIV = "gaia"
MAP_ROWS = (
    "units/athen/hero_xenophon", "units/athen/infantry_marine_archer_b",
    "units/brit/hero_boudicca_cavalry_javelineer", "units/brit/hero_boudicca_sword", "units/brit/hero_cunobelin_infantry",
    "units/cart/champion_pikeman",
    "units/gaul/hero_vercingetorix_infantry",
    "units/han/cavalry_crossbowman_b", "units/han/champion_infantry_archer_academy",
    "units/han/hero_han_xin", "units/han/hero_liu_bang", "units/han/hero_wei_qing", "units/han/hero_wei_qing_horse",
    "units/han/siege_shoushe",
    "units/iber/hero_indibil_infantry",
    "units/kush/hero_amanirenas_infantry", "units/kush/hero_harsiotef",
    "units/mace/hero_craterus", "units/mace/hero_philip_ii_pike", "units/mace/hero_pyrrhus_i",
    "units/maur/hero_ashoka_infantry", "units/maur/hero_bindusara_infantry", "units/maur/hero_chandragupta_infantry",
    "units/pers/arstibara", "units/pers/hero_xerxes_i_chariot", "units/pers/kardakes_hoplite", "units/pers/kardakes_skirmisher",
    "units/ptol/champion_juggernaut",
    "units/rome/hero_marcellus_infantry", "units/rome/hero_maximus_infantry", "units/rome/hero_scipio_infantry",
    "units/rome/infantry_swordsman_slave",
    # The Marian legionaries (Egypt 3v3, Gallic Fields, Extinct Volcano): _02 promotes to _03
    # at 150 xp and _03 to the Centurion (_04) at 300; the Praetorian Guard (_05) never promotes.
    "units/rome/champion_infantry_swordsman_02", "units/rome/champion_infantry_swordsman_03",
    "units/rome/champion_infantry_swordsman_04", "units/rome/champion_infantry_swordsman_05",
    "units/spart/champion_infantry_pike", "units/spart/hero_leonidas_300",
    "units/merc_black_cloak", "units/noldor_warship", "units/plane",
    "units/pirates/ship_arrow", "units/pirates/ship_merchant", "units/pirates/ship_scout",
    "units/samnite_skirmisher", "units/samnite_spearman", "units/samnite_swordsman",
    "units/viking_longship",
    # Survival of the Fittest's Gaia champions (Survival's champion pool), placed by its trigger.
    "units/merc_thorakites", "units/merc_thureophoros", "units/theb_sacred_band", "units/theb_siege_fireraiser",
    "units/thesp_melanochitones",
)


def map_row_civ(name):
    civ = name.split("/")[1] if name.count("/") >= 2 else ""
    return civ if civ in CIVS else MAP_CIV


# --retire: kept rows (fnmatch patterns) the release no longer trains: they keep their ids for
# saves but lose their trainers (0.28's Civilians replace the Female Citizens).
RETIRE_ROWS = []
PREVIOUS_PROMOTION_ONLY_FIRST = None  # set from the checked-in header with --keep-trainers


def previous_layout():
    """id -> (trainer BuildingType names, phase) from the checked-in civ_roster_data.h."""
    if not os.path.exists(OUT_HEADER):
        return {}
    with open(OUT_HEADER, encoding="utf-8") as fh:
        text = fh.read()
    out = {}
    for m in re.finditer(r'^\t\t\{"([^"]+)", "[^"]*", "[^"]*", CIV_[A-Z]+, UNIT_[A-Z_]+, (\d), [^,]*, ([^\n]*?),\n',
                         text, re.M):
        out[m.group(1)] = (re.findall(r"bld_bit\((BLD_[A-Z_0-9]+)\)", m.group(3)), int(m.group(2)))
    return out


def previous_promotion_only_first():
    if not os.path.exists(OUT_HEADER):
        return None
    with open(OUT_HEADER, encoding="utf-8") as fh:
        m = re.search(r"CIV_ROSTER_PROMOTION_ONLY_FIRST = (\d+);", fh.read())
    return int(m.group(1)) if m else None


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
    if KEEP_TRAINERS:
        keep = set(prev) | APPEND_ROWS
        added = [r["id"] for r in rows if r["id"] not in keep]
        if added:
            print("--keep-trainers leaves out new rows: " + ", ".join(added), file=sys.stderr)
        rows = [r for r in rows if r["id"] in keep]
        by_id = {r["id"]: r for r in rows}
    gone = [i for i in prev if i not in by_id]
    if gone:
        sys.exit("roster rows gone from 0 A.D. (%s): re-run with --renumber and bump the save version"
                 % ", ".join(gone))
    seen = set(prev)
    return [by_id[i] for i in prev] + [r for r in rows if r["id"] not in seen]


def keep_previous_aura_ids(auras, rows):
    """--keep-trainers: saved aura sources store civ_hero_aura_table() indices, so
    the checked-in table's auras keep their rows (an aura the release dropped
    stays as an inert row, kind 255, affecting nobody) and new auras go after them."""
    with open(OUT_HEADER, encoding="utf-8") as fh:
        text = fh.read()
    sec = text[text.index("kAuras[CIV_HERO_AURA_COUNT]"):]
    sec = sec[:sec.index("\n\t};")]
    prev = [(m.group(1), int(m.group(2))) for m in re.finditer(
        r'^\t\t\{"([^"]+)", "(?:[^"\\]|\\.)*", "(?:[^"\\]|\\.)*", [^,]+, (\d+),', sec, re.M)]
    key = lambda a: (a["id"], AURA_KIND[a["rts0"][0]] if a.get("rts0") else 255)  # noqa: E731
    pool = list(range(len(auras)))
    order, retired = [], []
    for pid, pkind in prev:
        hit = next((i for i in pool if key(auras[i]) == (pid, pkind)), None)
        if hit is None:
            hit = next((i for i in pool if auras[i]["id"] == pid), None)
        if hit is None:
            retired.append(pid)
            auras.append({"id": pid, "name": "", "description": "", "type": "range", "radius": 0.0,
                          "affects": [], "players": [], "modifications": [], "rts0": None})
            hit = len(auras) - 1
        else:
            pool.remove(hit)
        order.append(hit)
    order += pool
    if retired:
        print("--keep-trainers keeps dropped auras as inert rows: " + ", ".join(retired), file=sys.stderr)
    remap = {old: new for new, old in enumerate(order)}
    for r in rows:
        r["auras"] = [remap[i] for i in r["auras"]]
    return [auras[i] for i in order]


def collect(sim_root):
    loader = TemplateLoader(sim_root)
    civ_json = {}
    for civ in CIVS:
        with open(os.path.join(sim_root, "data", "civs", civ + ".json"), encoding="utf-8") as f:
            civ_json[civ] = json.load(f)

    generic = {}
    for cls in GENERIC:
        generic[cls] = raw_stats(generic_template(loader, cls))

    bonuses = load_civ_bonuses(sim_root)
    prev_layout = previous_layout() if KEEP_TRAINERS else {}
    rows = []
    auras = []
    skipped = []
    for civ in CIVS + [MAP_CIV]:  # MAP_CIV: MAP_ROWS without a civ, after every civ
        native = civ
        sdir = os.path.join(loader.root, "structures", civ)
        trainers = {}  # unit template -> {buildings}, structure phase
        # The civ's own structures, then Gaia ones that train "{civ}" units for
        # whoever captures them (the mercenary camp).
        structs = []
        if civ != MAP_CIV:
            structs = [("structures/%s/%s" % (civ, fn[:-4]), structure_kind(fn[:-4]))
                       for fn in sorted(os.listdir(sdir)) if fn.endswith(".xml")]
            structs += [("structures/" + stem, kind) for stem, kind in sorted(GAIA_TRAINERS.items())]
            structs += [("structures/" + stem, "shrine") for stem in GAIA_NATIVE_TRAINERS
                        if loader.load("structures/" + stem).value("Identity/Civ") == civ]
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
        # Another civ's structure of a kind this civ has none of (the Roman Army Camp, the
        # Athenian Gymnasium, the Briton Crannog, ...) trains "units/{civ}/..." for whoever
        # captures it (Trainer.js): its type's bit on the rows this civ already trains, at
        # their own phase. Kinds this civ builds keep its own lists.
        if civ != MAP_CIV:
            own = {TRAINER_BUILDING.get(kind) for _path, kind in structs}
            for other in CIVS:
                odir = os.path.join(loader.root, "structures", other)
                if other == civ or not os.path.isdir(odir):
                    continue
                for fn in sorted(os.listdir(odir)):
                    if not fn.endswith(".xml"):
                        continue
                    bld = TRAINER_BUILDING.get(structure_kind(fn[:-4]))
                    if bld is None or bld in own:
                        continue
                    ostruct = loader.load("structures/%s/%s" % (other, fn[:-4]))
                    for e in ostruct.tokens("Trainer/Entities"):
                        if "{civ}" not in e:
                            continue
                        e = e.replace("{civ}", civ)
                        if e in trainers and trainers[e]["buildings"]:
                            trainers[e]["buildings"].add(bld)
                            CAPTURED_TRAINERS.add((civ, bld, e))
                        elif e not in trainers and loader.exists(e):
                            # A template only the captured structure trains for this civ (the Army
                            # Camp's advanced infantry, the Temple of Amun's elite healer): a row of
                            # its own with that type's bit, at the structure's phase.
                            trainers[e] = {"buildings": {bld}, "kinds": {structure_kind(fn[:-4])},
                                           "phase": phase_of(ostruct)}
                            CAPTURED_TRAINERS.add((civ, bld, e))
        map_only = {ent for ent in MAP_ROWS if map_row_civ(ent) == civ and ent not in trainers and loader.exists(ent)}
        for ent in sorted(map_only):
            trainers[ent] = {"buildings": set(), "kinds": set(), "phase": 9}
        # Units no structure trains but a trained unit promotes into (0 A.D.
        # Promotion/Entity to another unit: Marian Reforms' auxiliary cavalry and
        # Antesignani, the Olympic champion, ...): rows with no trainer.
        pending = sorted(trainers)
        while pending:
            src_name = origin = pending.pop()
            # Upgrade/*/Entity targets (the Immortals' bow) are rows with no trainer too.
            for ent, _rank, _cost, _time, _phase in unit_upgrades(loader, origin, civ):
                if ent not in trainers:
                    trainers[ent] = {"buildings": set(), "kinds": set(), "phase": trainers[origin]["phase"]}
                    pending.append(ent)
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
        if KEEP_TRAINERS:
            for pid, (pblds, pphase) in prev_layout.items():
                ent = "units/" + pid
                if pid.startswith(civ + "/") and ent not in trainers and loader.exists(ent):
                    trainers[ent] = {"buildings": set(pblds), "kinds": set(), "phase": pphase}
        # The civ's catafalque: no structure trains it; Capture the Relic places it for Gaia.
        relic = "units/%s/catafalque" % civ
        if civ != MAP_CIV and loader.exists(relic) and relic not in trainers:
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
                for k in ("range", "damage", "cooldown", "hack_dmg", "pierce_dmg", "crush_dmg", "fire_dmg", "ranged"):
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
            if "Civilian" in c:
                flags.append("ROSTER_CIVILIAN")  # 0.28's template_unit_support_civilian
            if "Citizen" in c and not t.value("ResourceGatherer/MaxDistance", ""):
                flags.append("ROSTER_NO_GATHER")  # mixins/mercenary.xml, Neodamodes: ResourceGatherer disabled
            if t.value("Pack/Time", ""):
                flags.append("ROSTER_PACKABLE")  # 0 A.D. Pack: unpack before firing, pack to move
            if "SiegeTower" in c:
                flags.append("ROSTER_SIEGE_TOWER")
            if t.value("UnitMotionFlying/MaxSpeed", ""):
                flags.append("ROSTER_FLYING")  # UnitMotionFlying: takes off, flies over everything, lands
            if name in map_only:
                flags.append("ROSTER_MAP_ONLY")  # MAP_ROWS: only maps place it
            if "Relic" in c:
                flags.append("ROSTER_RELIC")
            hero_auras = []
            own_auras = loader.own_tokens(name, "Auras")
            if re.search(r"_[ae]$", name):  # an _a/_e rank keeps its _b's own aura (gaul/support_healer_e: celtic_healer)
                own_auras = own_auras + loader.own_tokens(re.sub(r"_[ae]$", "_b", name), "Auras")
            for aname in t.tokens("Auras"):
                apath = os.path.join(sim_root, "data", "auras", aname + ".json")
                if not os.path.isfile(apath):
                    continue
                with open(apath, encoding="utf-8") as f:
                    a = json.load(f)
                aid = aname.rsplit("/", 1)[-1]
                if not aid.startswith(civ + "_") and not (aname.startswith("units/") and aname in own_auras):
                    continue  # generic auras (garrison, formation) apply to every unit of a kind; a
                    # unit's own (the centurion's Combat Leadership, a Celtic healer's) are its
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
                    # Auras.js: an aura whose requiredTechnology is not researched stays off; 0.28's
                    # Seeress' Human Sacrifice names "healer_aura", which no technology is.
                    "modifications": [] if a.get("requiredTechnology") and not os.path.exists(os.path.join(
                        sim_root, "data", "technologies", a["requiredTechnology"] + ".json")) else a.get("modifications", []),
                })
                auras[-1]["rts0"] = rts0_aura_effect(auras[-1])
            if len(hero_auras) > 3:  # three slots: range auras before garrison ones (the minister has four)
                hero_auras.sort(key=lambda i: auras[i]["type"] == "garrison")
            rid = name.split("units/", 1)[-1]
            # A trained row's phase is its own Identity/Requirements (Trainer.js checks only the
            # unit's; the structure's phase gates building it, not training at a captured one).
            # Rows no structure trains take the phase of the unit they promote or upgrade from.
            phase = phase_of(t) if trainers[name]["buildings"] else \
                max(phase_of(t), trainers[name]["phase"] if trainers[name]["phase"] < 9 else 1)
            blds = sorted(trainers[name]["buildings"], key=BUILDING_BITS.index)
            # --keep-trainers keeps a row's id; its trainers and phase are the release's while the
            # release has its template (0.28 trains Spartan heroes at the Gerousia, the Legionary
            # only by promotion). Saves store template ids, never trainers.
            if KEEP_TRAINERS and any(fnmatch.fnmatchcase(rid, pat) for pat in RETIRE_ROWS):
                blds = []
            rows.append({
                "id": rid,
                "civ": civ,
                "class": cls,
                "name": t.value("Identity/GenericName", ""),
                "specific_name": t.value("Identity/SpecificName", ""),
                "phase": phase,
                "trainers": blds,
                "trained_at_0ad": sorted(trainers[name]["kinds"]),
                "flags": flags,
                "unlock_tech": unlock_tech_of(t),
                "auras": hero_auras,
                "civ_bonuses": civ_bonus_ids,
                "classes": sorted(c),
                "stats": stats,
                "upgrades": unit_upgrades(loader, name, civ),
                # Population/Bonus (the Germans' wagon house); only rows that have one carry it.
                **({"pop_bonus": int(t.num("Population/Bonus"))} if t.num("Population/Bonus") else {}),
            })
        # RTS0-only classes every civ (or a named civ) keeps.
        for cls in RTS0_ONLY:
            if civ == MAP_CIV:
                break
            if any(r["civ"] == civ and r["class"] == cls for r in rows):
                continue
            base = dict(zip(RTS0_FIELDS, RTS0_CLASSES[cls]))
            label = "Villager"
            stats = {k: base[k] for k in RTS0_FIELDS}
            stats["pop"] = int(stats["pop"])
            stats["carry"] = int(stats["carry"])
            stats["vision"] = 0.0  # unit_vision_range() of its class
            for r in ("food", "wood", "gold", "stone"):
                stats[r] = int(stats[r])
            rows.append({
                "id": "%s/rts0_%s" % (civ, cls[5:].lower()),
                "civ": civ, "class": cls, "name": label, "specific_name": "",
                "phase": 1,
                "trainers": ["BLD_TOWN_CENTER"] + [  # and a civ's own civil centre that trains citizens
                    bld for bld, (tmpl, base) in KEEP_TRAINERS_NEW_BUILDINGS.items()
                    if base == "BLD_TOWN_CENTER" and loader.exists("structures/%s/%s" % (civ, tmpl))],
                "trained_at_0ad": [], "flags": ["ROSTER_CITIZEN"],
                "classes": ["Citizen", "Human", "Organic", "Unit", "Worker"],
                "auras": [], "stats": stats,
            })
    # Units only reached by promotion (no trainer) go after every civ's block, so
    # the template ids of trainable rows, which saves store, stay where they were.
    rows = [r for r in rows if r["trainers"]] + [r for r in rows if not r["trainers"]]
    rows = keep_previous_ids(rows)
    # Civ structures' range and global auras on units (Auras.js), after the unit
    # auras: one row per effect kind, listed per BuildingType (civ_building_auras).
    for enum, stems, _model, _portrait in UNIQUE_BUILDINGS:
        for civ in CIVS:
            for st in stems:
                path = "structures/%s/%s" % (civ, st)
                if not loader.exists(path):
                    continue
                for aname in loader.load(path).tokens("Auras"):
                    apath = os.path.join(sim_root, "data", "auras", aname + ".json")
                    if not os.path.isfile(apath):
                        continue
                    with open(apath, encoding="utf-8") as fh:
                        a = json.load(fh)
                    affects = a.get("affects", [])
                    if a.get("type") not in ("range", "global") or "Structure" in affects or "Player" in affects:
                        continue  # garrison/turret auras and those on structures are code (simulation.cpp)
                    aid = aname.rsplit("/", 1)[-1]
                    for kind, value, rng in rts0_aura_effects(a):
                        known = next((x for x in auras if x["id"] == aid and x["rts0"][0] == kind), None)
                        if known is not None:
                            if enum not in known["buildings"]:
                                known["buildings"].append(enum)
                            continue
                        auras.append({
                            "id": aid, "name": a.get("auraName", aname), "description": a.get("auraDescription", ""),
                            "type": a["type"], "radius": float(a.get("radius", 0)), "affects": affects,
                            "players": a.get("affectedPlayers", ["Player"]),
                            "modifications": a.get("modifications", []),
                            "rts0": (kind, value, rng), "buildings": [enum],
                        })
    if KEEP_TRAINERS:
        auras = keep_previous_aura_ids(auras, rows)
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
    w("\t\t// bitset of civ_unit_template_table() rows the aura affects, whether it affects structures")
    for a in auras:
        bits = [0] * words
        for i in a["affects_rows"]:
            bits[i // 64] |= 1 << (i % 64)
        bitset = "{" + ", ".join("0x%016xull" % b for b in bits) + "}"
        radius = a["radius"] if a["type"] == "range" else 0.0
        eff = a.get("rts0")
        kind, value, rng = (AURA_KIND[eff[0]], eff[1], eff[2]) if eff else (255, 0.0, 0.0)
        structures = "true" if "Structure" in a["affects"] else "false"
        w("\t\t{%s, %s, %s, %s, %d, %s, %s, %d, %s, %s}," % (cstr(a["id"]), cstr(a["name"]), cstr(a["description"]),
                                                         f(radius), kind, f(value), f(rng), aura_players_mask(a), bitset,
                                                         structures))
    w("\t};")
    w("\treturn kAuras;")
    w("}")
    w("")
    w("// civ_hero_aura_table() rows a finished structure of this BuildingType projects")
    w("// (0 A.D. structures/<civ>/*.xml Auras), ending in -1.")
    w("inline const int16_t *civ_building_auras(uint8_t type) {")
    w("\tstatic const int16_t kNone[1] = {-1};")
    by_bld = {}
    for i, a in enumerate(auras):
        for bld in a.get("buildings", []):
            by_bld.setdefault(bld, []).append(i)
    w("\tswitch (type) {")
    for bld in sorted(by_bld, key=lambda b: [u[0] for u in UNIQUE_BUILDINGS].index(b)):
        ids = by_bld[bld] + [-1]
        w("\t\tcase %s: { static const int16_t k[%d] = {%s}; return k; }" % (bld, len(ids), ", ".join(map(str, ids))))
    w("\t\tdefault: return kNone;")
    w("\t}")
    w("}")
    w("")
    w("// 0 A.D. Upgrade on units (Upgrade.js): from row, rank the from unit needs (0 = any),")
    w("// to row, food/wood/gold(metal)/stone cost, time in seconds, phase the choice requires.")
    index = {"units/" + r["id"]: i for i, r in enumerate(rows)}
    ups = []
    for i, r in enumerate(rows):
        for ent, rank, cost, secs, phase in r.get("upgrades", []):
            if ent in index:
                ups.append((i, rank, index[ent], cost, secs, phase))
    w("inline constexpr uint16_t CIV_UNIT_UPGRADE_COUNT = %d;" % len(ups))
    w("inline const CivUnitUpgrade *civ_unit_upgrade_table() {")
    w("\tstatic const CivUnitUpgrade kUpgrades[CIV_UNIT_UPGRADE_COUNT + 1] = {")
    for i, rank, to, cost, secs, phase in ups:
        w("\t\t{%d, %d, %d, %d, %d, %d, %d, %s, %d}, // %s -> %s" % (
            i, rank, to, cost.get("food", 0), cost.get("wood", 0), cost.get("metal", 0), cost.get("stone", 0),
            f(secs), phase, rows[i]["id"], rows[to]["id"]))
    w("\t\t{0xFFFF, 0, 0xFFFF, 0, 0, 0, 0, 0.0f, 0},")
    w("\t};")
    w("\treturn kUpgrades;")
    w("}")
    w("")
    w("// 0 A.D. Population/Bonus on units (Population.js: added to the owner's population")
    w("// bonuses while the unit exists): row, bonus.")
    bonus_rows = [(i, r["pop_bonus"]) for i, r in enumerate(rows) if r.get("pop_bonus")]
    w("inline constexpr uint16_t CIV_UNIT_POP_BONUS_COUNT = %d;" % len(bonus_rows))
    w("inline const CivUnitPopBonus *civ_unit_pop_bonus_table() {")
    w("\tstatic const CivUnitPopBonus kBonuses[CIV_UNIT_POP_BONUS_COUNT + 1] = {")
    for i, b in bonus_rows:
        w("\t\t{%d, %d}, // %s" % (i, b, rows[i]["id"]))
    w("\t\t{0xFFFF, 0},")
    w("\t};")
    w("\treturn kBonuses;")
    w("}")
    w("")
    w("inline const CivUnitTemplate *civ_unit_template_table() {")
    w("\tstatic const CivUnitTemplate kTable[CIV_UNIT_TEMPLATE_COUNT] = {")
    w("\t\t// id, name, specific name, civ, class, phase, flags, trainers,")
    w("\t\t// hp, speed, range, damage, cooldown, hack/pierce/crush dmg, hack/pierce/crush armour,")
    w("\t\t// food, wood, gold, stone, pop, train_time, carry, aura slots (-1 = none), unlock tech,")
    w("\t\t// TechClass mask of the template's 0 A.D. classes, Vision/Range x0.28")
    for r in rows:
        s = r["stats"]
        flags = " | ".join(r["flags"]) or "0"
        trainers = " | ".join("bld_bit(%s)" % b for b in r["trainers"]) or "BldMask{}"
        a = r["auras"] + [-1, -1, -1]
        w("\t\t{%s, %s, %s, %s, %s, %d, %s, %s," % (
            cstr(r["id"]), cstr(r["name"]), cstr(r["specific_name"]),
            CIV_ENUM_OF.get(r["civ"], "CIV_GAIA"), r["class"], r["phase"], flags, trainers))
        w("\t\t %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %d, %d, %d, %d, %d, %s, %d, %d, %d, %d, %s, %s, %s}," % (
            f(s["hp"]), f(s["speed"]), f(s["range"]), f(s["damage"]), f(s["cooldown"]),
            f(s["hack_dmg"]), f(s["pierce_dmg"]), f(s["crush_dmg"]),
            f(s["hack_armor"]), f(s["pierce_armor"]), f(s["crush_armor"]),
            s["food"], s["wood"], s["gold"], s["stone"], s["pop"], f(s["train_time"]),
            s["carry"], a[0], a[1], a[2], r.get("unlock_tech", "TECH_NONE"), tech_classes_literal(tech_class_bits(r["classes"])),
            f(s["vision"])))
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
        civs = " | ".join("civ_bit(%s)" % CIV_ENUM_OF[c] for c in b["civs"] if c in CIVS) or "0"
        if len(b["civs"]) > 1:
            civs = "(uint16_t)(%s)" % civs
        blds = " | ".join("bld_bit(%s)" % x for x in mask) or "BldMask{}"
        w("\t\t{%s, %s, %s, %s, %s, %s}," % (civs, stat, blds, f(mult), cstr(b["id"]), cstr(b["tooltip"])))
    w("\t\t{0, 0, {}, 1.0f, \"\", \"\"}, // sentinel")
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
        blds = " | ".join("bld_bit(%s)" % x for x in t["buildings"]) or "BldMask{}"
        w("\t\t{%s, %s, %d, %s, %s, %s, %s, %s, %s, %s}," % (
            CIV_ENUM_OF[t["civ"]], t["stat"], t["res"], "true" if t["allies_only"] else "false",
            blds, f(t["value"]), cstr(t["id"]), cstr(t["name"]), cstr(t["description"]), bitset))
    w("\t\t{0, 0, -1, false, {}, 1.0f, \"\", \"\", \"\", {}}, // sentinel")
    w("\t};")
    w("\treturn kTeam;")
    w("}")
    w("")
    w("// Each civ's 0 A.D. bonuses as one line of text for the civ picker.")
    w("inline const char *civ_bonus_text(uint8_t civ) {")
    w("\tstatic const char *kText[CIV_COUNT] = {")
    for slot in range(CIV_COUNT):
        civ = next((c for c in CIVS if CIV_INDEX[c] == slot), None)
        if civ is None:
            w("\t\t\"\", // CIV_GAIA")
            continue
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
    if KEEP_TRAINERS and PREVIOUS_PROMOTION_ONLY_FIRST is not None:
        # Retired rows (--retire) lose their trainers but stay in their civ's block.
        trainable = PREVIOUS_PROMOTION_ONLY_FIRST
    for i in range(1, trainable):
        assert CIV_INDEX[rows[i]["civ"]] >= CIV_INDEX[rows[i - 1]["civ"]], "civ blocks out of order"
    w("inline constexpr uint16_t CIV_ROSTER_PROMOTION_ONLY_FIRST = %d;" % trainable)
    w("inline const uint16_t *civ_roster_offsets() {")
    # A civ's block starts after every earlier civ's block; CIV_GAIA's, and the block of a
    # civ whose rows were all appended later (the Germans), are empty.
    offs = [sum(1 for r in rows[:trainable] if CIV_INDEX[r["civ"]] < slot) for slot in range(CIV_COUNT)]
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
            "enum": CIV_ENUM_OF[civ],
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
        "units": [{k: v for k, v in r.items() if k != "upgrades"} for r in rows],
        "auras": [{k: v for k, v in a.items() if k != "affects_rows"} for a in auras],
    }
    return json.dumps(doc, indent=1, ensure_ascii=False, sort_keys=False) + "\n"


def builder_named(loader):
    """Every structure template some civ's units can build: their 0 A.D. Builder/Entities
    lists with {civ} and {native} resolved (mixins/builder.xml plus the civs' own extras,
    such as structures/ptol/library)."""
    named = set()
    for civ in CIVS:
        udir = os.path.join(loader.root, "units", civ)
        for fn in sorted(os.listdir(udir)):
            if fn.endswith(".xml"):
                for tok in loader.load("units/%s/%s" % (civ, fn[:-4])).tokens("Builder/Entities"):
                    named.add(tok.replace("{civ}", civ).replace("{native}", civ))
    return named


def unique_building_rows(sim_root, generic_masks=None):
    """BuildingTypeInfo rows, civ masks and phases for UNIQUE_BUILDINGS, read
    from each structure's 0 A.D. template the way the generic rows in
    sim_types.h were: Health, Cost (metal -> gold), GarrisonHolder,
    Population/Bonus, Capturable, TerritoryDecay; BuildTime x0.25; footprint
    2 cells when wider than 15 m."""
    loader = TemplateLoader(sim_root)
    named = builder_named(loader)
    out = []
    for enum, stems, model, portrait in UNIQUE_BUILDINGS:
        civs, t, src = [], None, None
        for civ in CIVS:
            for st in stems:
                name = "structures/%s/%s" % (civ, st)
                if loader.exists(name):
                    # A civ has the structure when its Builder lists name it (an Upgrade
                    # reaches UPGRADE_ONLY ones): 0.28's ptol/mercenary_camp, mace and sele
                    # libraries, rome/amphitheater_pompeii and temple_mars and germ/totem
                    # are templates only maps place.
                    wallset = "structures/%s/%s" % (civ, WALLSET_PHASE.get(enum, ""))
                    if name in named or wallset in named or enum in UPGRADE_ONLY:
                        civs.append(civ)
                    if t is None:
                        t, src = loader.load(name), name
                    break
        if enum in GENERIC_UNIQUE:
            name = "structures/%s" % stems[0]
            mask = (generic_masks or {}).get(GENERIC_UNIQUE[enum], 0)
            civs = [c for c in CIVS if mask >> CIV_INDEX[c] & 1]
            t, src = loader.load(name), name
        if enum in MAP_ONLY:
            civs = []
        if enum in GAIA_UNIQUE:
            civs, src = [], "structures/%s" % stems[0]
            t = loader.load(src)
        if t is None:
            raise SystemExit("no 0 A.D. template for %s" % enum)
        fp = t.get("Footprint/Square")
        width = float(fp.attrs.get("width", 0)) if fp is not None else 0.0
        if width == 0.0:
            c = t.get("Footprint/Circle")
            width = 2.0 * float(c.attrs.get("radius", 0)) if c is not None else 0.0
        out.append({
            "enum": enum, "src": src, "civs": civs,
            # The three Carthaginian embassies share GenericName "Embassy": tell them apart.
            "name": (t.value("Identity/SpecificName", "") if enum in SPECIFIC_NAME else "")
                    or t.value("Identity/GenericName", "") or t.value("Identity/SpecificName", ""),
            "generic": t.value("Identity/GenericName", ""),
            "model": model, "portrait": portrait,
            "hp": t.num("Health/Max", 1000.0),
            "foot": 2 if width > 15.0 and enum not in ONE_CELL else 1,
            "food": int(t.num("Cost/Resources/food")), "wood": int(t.num("Cost/Resources/wood")),
            "gold": int(t.num("Cost/Resources/metal")), "stone": int(t.num("Cost/Resources/stone")),
            "pop": int(t.num("Population/Bonus")), "garrison": int(t.num("GarrisonHolder/Max")),
            "build_time": t.num("Cost/BuildTime", 100.0) * 0.25,
            # Capturable disable="" (rome/arch): never captured.
            "capture": t.num("Capturable/CapturePoints", 500.0) if t.get("Capturable") is not None else 0.0,
            "regen": t.num("Capturable/RegenRate", 5.0),
            "garrison_regen": t.num("Capturable/GarrisonRegenRate", 1.0),
            "decay": t.num("TerritoryDecay/DecayRate", 0.0),  # disable="" (Cothon): no decay,
            "phase": (phase_of(loader.load(src.rsplit("/", 1)[0] + "/" + WALLSET_PHASE[enum]))
                      if enum in WALLSET_PHASE else phase_of(t)),
            # TerritoryInfluence roots on RTS0's scale (0 A.D. civil centre 140 m -> 32).
            "territory": (t.num("TerritoryInfluence/Radius") * 32.0 / 140.0
                          if t.value("TerritoryInfluence/Root") == "true" else 0.0),
            # Vision/Range on RTS0's scale (x0.3, never below the 18 every structure gets).
            "vision": max(18.0, t.num("Vision/Range") * 0.3),
            # Every resource: a food-only dropsite (the mills) is is_food_dropsite's.
            "dropsite": set(t.tokens("ResourceDropsite/Types")) >= {"food", "wood", "stone", "metal"},
            # Loot/* a destroyed or captured structure yields (Looter.js), metal -> gold.
            "loot": [int(t.num("Loot/" + k)) for k in ("xp", "food", "wood", "stone", "metal")],
        })
    return out


def civ_building_upgrades(sim_root):
    """0 A.D. Upgrade choices of the civs' own structures whose source and target
    are RTS0 BuildingTypes: [(from enum, to enum, {civ}, cost, time, phase, tooltip)].
    Generic upgrades (sentry tower, wall to gate) stay in sim_types.h building_upgrade()."""
    loader = TemplateLoader(sim_root)
    enum_of = dict(UPGRADE_FROM_GENERIC)
    for enum, stems, _model, _portrait in UNIQUE_BUILDINGS:
        for st in stems:
            enum_of[st] = enum
    found = {}
    for civ in CIVS:
        sdir = os.path.join(loader.root, "structures", civ)
        for fn in sorted(os.listdir(sdir)):
            stem = fn[:-4]
            if not fn.endswith(".xml") or stem not in enum_of:
                continue
            up = loader.load("structures/%s/%s" % (civ, stem)).get("Upgrade")
            if up is None:
                continue
            for choice in sorted(up.children):
                c = up.children[choice]
                ent = (c.value("Entity") or "").replace("{civ}", civ).replace("{native}", civ)
                to = enum_of.get(ent.rsplit("/", 1)[-1])
                if not to or not loader.exists(ent):
                    continue
                phase = 1
                for tech in c.tokens("Requirements/Techs"):
                    phase = max(phase, PHASE_OF_TECH.get(tech, 1))
                key = (enum_of[stem], to)
                cost = tuple(int(c.num("Cost/" + r)) for r in ("food", "wood", "metal", "stone"))
                if key in found:
                    found[key][2].add(civ)
                    continue
                found[key] = [enum_of[stem], to, {civ}, cost, c.num("Time"), phase, c.value("Tooltip", "")]
    order = BUILDING_BITS.index
    return sorted(found.values(), key=lambda u: (order(u[0]), order(u[1])))


def emit_buildings(rows, generic_masks, upgrades=()):
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
    full = sum(1 << CIV_INDEX[c] for c in CIVS)
    for enum in BUILDING_BITS[:BUILDING_BITS.index(rows[0]["enum"])]:
        mask = generic_masks.get(enum, 0)
        if mask != full:
            w("\t\tcase %s: return 0x%04x; // %s" % (enum, mask, " ".join(c for c in CIVS if mask >> CIV_INDEX[c] & 1) or "none"))
    for r in rows:
        mask = 0
        for c in r["civs"]:
            mask |= 1 << CIV_INDEX[c]
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
    w("// True for a structure only an Upgrade reaches (the Tachara's tribute forms, the apartment):")
    w("// no civ lays its foundation.")
    w("inline bool civ_building_upgrade_only(uint8_t type) {")
    w("\treturn %s;" % (" || ".join("type == %s" % r["enum"] for r in rows if r["enum"] in UPGRADE_ONLY) or "false"))
    w("}")
    w("")
    w("// 0 A.D. Upgrade choices of the civs' own structures (structures/<civ>/*.xml):")
    w("// from, civs (bit per Civilization), to, food/wood/gold(metal)/stone cost, seconds,")
    w("// phase the choice requires, tooltip. building_upgrade() lists them before the generic ones.")
    w("struct CivBuildingUpgrade {")
    w("\tuint8_t from;")
    w("\tuint16_t civs;")
    w("\tuint8_t to;")
    w("\tint16_t food_cost, wood_cost, gold_cost, stone_cost;")
    w("\tfloat time;")
    w("\tuint8_t phase;")
    w("\tconst char *tooltip;")
    w("};")
    w("inline constexpr int CIV_BUILDING_UPGRADE_COUNT = %d;" % len(upgrades))
    w("inline const CivBuildingUpgrade *civ_building_upgrade_table() {")
    w("\tstatic const CivBuildingUpgrade kUpgrades[CIV_BUILDING_UPGRADE_COUNT + 1] = {")
    for frm, to, civs, cost, secs, phase, tip in upgrades:
        mask = 0
        for c in civs:
            mask |= 1 << CIV_INDEX[c]
        w("\t\t{%s, 0x%04x, %s, %d, %d, %d, %d, %s, %d, %s}," % (frm, mask, to, cost[0], cost[1], cost[2], cost[3],
                                                             f(secs), phase, cstr(tip)))
    w("\t\t{BLD_TYPE_COUNT, 0, BLD_TYPE_COUNT, 0, 0, 0, 0, 0.0f, 1, nullptr},")
    w("\t};")
    w("\treturn kUpgrades;")
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
    ap.add_argument("--keep-trainers", action="store_true",
                    help="refresh stats only: keep every row's trainers and phase, add no rows")
    ap.add_argument("--append", default="",
                    help="with --keep-trainers: comma-separated new row ids to add after the existing rows")
    ap.add_argument("--retire", default="",
                    help="with --keep-trainers: comma-separated row id patterns (fnmatch) the release no longer trains")
    ap.add_argument("--previous-0ad", dest="prev_zad",
                    help="older 0 A.D. tree for templates the release dropped (with --keep-trainers)")
    args = ap.parse_args()
    global RENUMBER, KEEP_TRAINERS, PREVIOUS_SIM_ROOT, APPEND_ROWS, RETIRE_ROWS, PREVIOUS_PROMOTION_ONLY_FIRST
    RENUMBER = args.renumber
    KEEP_TRAINERS = args.keep_trainers
    APPEND_ROWS = {x for x in args.append.split(",") if x}
    RETIRE_ROWS = [x for x in args.retire.split(",") if x]
    if args.keep_trainers:
        PREVIOUS_PROMOTION_ONLY_FIRST = previous_promotion_only_first()
    if args.prev_zad:
        PREVIOUS_SIM_ROOT = os.path.join(args.prev_zad, "binaries", "data", "mods", "public", "simulation")
    sim_root = os.path.join(args.zad, "binaries", "data", "mods", "public", "simulation")
    rows, auras, skipped, civ_json, bonuses, team = collect(sim_root)
    header = emit_header(rows, auras, civ_json, bonuses, team)
    js = emit_json(rows, auras, civ_json, bonuses)
    masks = generic_build_masks(sim_root)
    bld = emit_buildings(unique_building_rows(sim_root, masks), masks, civ_building_upgrades(sim_root))
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
