#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games).

Extract game-ready assets from 0 A.D.'s public mod archive into RTS0's asset folders.

Pipeline (mirrors 0ad source/graphics/ModelDef.cpp + ps/FileIo.cpp formats):
  public.zip (.pmd meshes, .xml actors, .cached.dds skins)
    -> parse PMD v1-v5 binary meshes (bind-pose static geometry + prop points)
    -> compose actor XML trees (variants/groups/shared files/props)
    -> bake player-color (alpha-mask formula from shaders/glsl/model_common.fs)
       and AO multiply into albedo PNGs
    -> emit single-file GLB (embedded PNG) + manifest.json

Usage: python3 tools/extract_0ad_assets.py [--zip PATH] [--out assets]
Requires: Pillow
"""
# ===========================================================================
# C2 HAN (Chinese) civ — unit/building coverage & fallback matrix
# ===========================================================================
# 0 A.D. art roots used by this civ (verified against public.zip via
# `--list-actors`; no `chinese/` tree exists in the archive — 0 A.D. names the
# civ folder "han" under both roots):
#     art/actors/units/han/*.xml         people, mounts, chariots, siege
#     art/actors/structures/han/*.xml     pagoda/timber civic set, hulls
#     art/actors/props/units/han/*.xml   mangonel/shoushe sub-meshes only
#
# Prior to this batch "han" was absent from CIV_ART_FOLDERS, so register_civ_
# unit_assets()/register_civ_building_assets() never probed those folders and
# every Han unit fell through to the generic mesh at load time.
#
# UNIT MATRIX — RTS0 UnitTypes (23) -> Han mesh  (unique = Han-specific art,
#                                                 fallback = borrowed civil)
# ---------------------------------------------------------------------------
#  UNIT ID            HAN MESH                              STATUS
#  villager            citizen_male.xml                      unique   height 1.70
#  female_citizen      female_citizen.xml                    unique   height 1.70
#  swordsman           infantry_swordsman_c.xml              unique   height 1.80
#  spearman            infantry_spearman_c.xml               unique   height 1.80
#  archer              infantry_archer_c.xml                 unique   height 1.78
#  cavalry             cavalry_spearman_c_m.xml              unique   height 2.20
#  hero                hero_infantry_han_xin.xml            unique   height 1.90
#  healer              healer.xml                            unique   height 1.70
#  trader              trader.xml                            unique   height 1.90
#  fishing_boat        structures/han/fishing_ship.xml       unique   width  3.50
#  trireme             structures/han/trireme.xml            unique   width  7.00
#  chariot             chariot_archer_d.xml                  unique   width  2.80
#  merchant_ship       structures/han/merchant_ship.xml      unique   width  5.50
#  catapult            (none — Han uses mangonel)            FALLBACK generic
#  bolt_shooter        (none — Han uses mangonel/shoushe)    FALLBACK generic
#  siege_ram           (none — Han has no battering ram)     FALLBACK generic
#  elephant            (none — Han has no war elephant)      FALLBACK generic
#  worker_elephant     (none — Han uses corral/stable)       FALLBACK generic
#  war_dog             (none — no Han dog unit)              FALLBACK generic
#  camel               (none — no Han camel)                 FALLBACK generic
#  scythed_chariot     (none — Han chariots are archer only)  FALLBACK generic
#  carnyx              (none — no Han war-trumpet unit)      FALLBACK generic
#  archer_maiden       (none — Han women are citizens only)  FALLBACK generic
#  => 14 unique, 10 fallback (Han-specific equivalents exported separately
#     as siege_mangonel / siege_shoushe / halberdman / crossbowman /
#     cavalry_crossbow / minister so the Han roster is not thin).
#
# BUILDING MATRIX — RTS0 BuildingTypes (14) + common civ buildings
# ---------------------------------------------------------------------------
#  BUILDING ID         HAN MESH                              STATUS
#  town_center         structures/han/civil_centre.xml       unique   width 6.20
#  house               structures/han/house.xml              unique   width 2.40
#  barracks            structures/han/barracks.xml           unique   width 4.00
#  storehouse          structures/han/storehouse.xml         unique   width 3.40
#  farmstead           structures/han/farmstead.xml          unique   width 3.20
#  market              structures/han/market.xml             unique   width 4.20
#  temple              structures/han/temple.xml             unique   width 5.00
#  fortress            structures/han/fortress.xml           unique   width 6.00
#  gate                structures/han/wall_gate.xml          unique   width 3.80
#  dock                structures/han/dock.xml                unique   width 4.80
#  wall / wall_segment structures/han/wall_long.xml          unique   width 2.00
#  wall_tower / tower  structures/han/wall_tower.xml          unique   width 2.20
#  corral              structures/han/corral.xml             unique   width 3.60
#  wonder              structures/han/wonder.xml             unique   width 6.50
#  --- Han-only civic extras with no RTS0 BuildingType id (exported anyway) ---
#  shrine / academy / arsenal / pagoda / imperial_court / imperial_ministry /
#  tower_small / tower_large / tower_great / forge / outpost
#  => 14/14 BuildingTypes unique; 0 structure fallbacks.
# ===========================================================================

# ===========================================================================
# C3 MAURYA (Indian) civ — unit/building coverage & fallback matrix
# ===========================================================================
# 0 A.D. art roots used by this civ (verified against public.zip via
# `--list-actors`; 0 A.D. names the civ folder "mauryas" under both roots):
#     art/actors/units/mauryas/*.xml       people, elephantry, chariots
#     art/actors/structures/mauryas/*.xml   stupa/palace civic set, hulls
#     art/actors/props/structures/mauryas/*.xml  *_struct_b / *_palisade
#         sub-meshes only — these are component props pulled in by the parent
#         actors above and are NOT standalone actors, so they are deliberately
#         not registered as assets (extracting them yields no renderable mesh).
#
# "maur" was already present in CIV_CORE_ACTORS/CIV_ART_FOLDERS (unlike "han",
# which had to be added in the C2 batch), so the civ auto-registration pass was
# already emitting the 36 core `maur_*` ids. This batch adds the Mauryan
# roster WIDTH: the a/b/e infantry colour variants, the full elephantry line,
# the javelinist/swordsman cavalry variants, the four extra heroes, and the
# stupa civic set that the core pass could not reach.
#
# UNIT MATRIX — RTS0 UnitTypes (23) -> Mauryan mesh (unique = Mauryan art)
# ---------------------------------------------------------------------------
#  UNIT ID            MAURYA MESH                            STATUS
#  villager            citizen_male.xml                      unique   height 1.70
#  female_citizen      female_citizen.xml                    unique   height 1.70
#  swordsman           infantry_swordsman_c.xml              unique   height 1.80
#  spearman            infantry_spearman_b.xml               unique   height 1.80
#  archer              infantry_archer_c.xml                 unique   height 1.78
#  cavalry             cavalry_swordsman_b_m.xml              unique   height 2.20
#  hero                hero_infantry_archer_ashoka.xml       unique   height 1.90
#  healer              healer.xml                            unique   height 1.70
#  trader              trader.xml                            unique   height 1.90
#  elephant            elephantry_archer_e_m.xml             unique   width  3.40
#  worker_elephant     support_elephant.xml                   unique   width  3.60
#  chariot             chariot_archer_c_m.xml                 unique   width  2.90
#  fishing_boat        structures/mauryas/fishing_ship.xml    unique   width  3.50
#  trireme             structures/mauryas/trireme.xml         unique   width  7.00
#  --- Mauryan-only roster (exported this batch, no generic UnitType) ---
#  maceman             infantry_maceman_c.xml                 unique   height 1.85
#  hero_ashoka         hero_infantry_archer_ashoka.xml       unique   height 1.90
#  hero_bindusara      hero_infantry_swordsman_bindusara.xml  unique   height 1.90
#  hero_chandragupta   hero_infantry_swordsman_chandragupta.xml unique height 1.90
#  hero_chanakya       hero_healer_chanakya.xml              unique   height 1.80
#  hero_elephant       hero_elephant_chandragupta_m.xml       unique   width  3.60
#  hero_chariot        hero_chariot_archer_ashoka_d.xml      unique   width  3.00
#  a/b/e colour variants of swordsman / spearman / archer / cavalry,
#  elephantry_archer a_m/b_m/d/r1, elephantry_c_d, support_elephant_d.
#  siege_ram           structures/mauryas/siege_ram.xml        unique   width  3.60
#  catapult            (none — Mauryas are a bow/elephant civ)  FALLBACK generic
#  bolt_shooter        (none — no Mauryan bolt shooter)        FALLBACK generic
#  scythed_chariot     (none — no scythed chariot actor)      FALLBACK generic
#  war_dog             (none — no Mauryan dog unit)           FALLBACK generic
#  camel               (none — Mauryas fight on elephants)    FALLBACK generic
#  carnyx              (none — no Mauryan war-trumpet unit)   FALLBACK generic
#  archer_maiden       (none — shares infantry_archer_c.xml)  FALLBACK (maur_archer)
#  => 14 unique, 9 fallback (of which archer_maiden reuses a real Mauryan
#     archer mesh via maur_archer rather than the generic one).
#
# BUILDING MATRIX — RTS0 BuildingTypes (14) + common civ buildings
# ---------------------------------------------------------------------------
#  BUILDING ID         MAURYA MESH                            STATUS
#  town_center         structures/mauryas/civil_centre.xml    unique   width 6.20
#  house               structures/mauryas/house.xml           unique   width 2.40
#  barracks            structures/mauryas/barracks.xml        unique   width 4.00
#  storehouse          structures/mauryas/storehouse.xml      unique   width 3.40
#  farmstead           structures/mauryas/farmstead.xml       unique   width 3.20
#  market              structures/mauryas/market.xml          unique   width 4.20
#  temple              structures/mauryas/temple.xml          unique   width 5.00
#  fortress            structures/mauryas/fortress.xml        unique   width 6.00
#  gate                structures/mauryas/wall_gate.xml       unique   width 3.80
#  dock                structures/mauryas/dock.xml            unique   width 4.80
#  wall / wall_segment structures/mauryas/wall_long.xml       unique   width 2.00
#  wall_tower / tower  structures/mauryas/wall_tower.xml      unique   width 2.20
#  corral              structures/mauryas/corral.xml          unique   width 3.60
#  wonder              structures/mauryas/big_stupa.xml       unique   width 6.50
#  --- Mauryan civic set added this batch ---
#  blacksmith          structures/mauryas/blacksmith.xml      unique   width 4.00
#  workshop            structures/mauryas/workshop.xml        unique   width 4.60
#  archery_range       structures/mauryas/range.xml           unique   width 3.80
#  stable              structures/mauryas/stable.xml          unique   width 4.20
#  stable_elephant     structures/mauryas/stable_elephant.xml unique   width 4.60
#  palace              structures/mauryas/palace.xml          unique   width 6.00
#  outpost             structures/mauryas/outpost.xml         unique   width 2.00
#  scout_tower         structures/mauryas/scout_tower.xml     unique   width 2.00
#  wooden_tower        structures/mauryas/wooden_tower.xml    unique   width 2.00
#  tower_double        structures/mauryas/tower_double.xml    unique   width 2.60
#  super_dock          structures/mauryas/dock.xml            unique   width 5.00
#  bireme              structures/mauryas/bireme.xml          unique   width 6.00
#  => 14/14 BuildingTypes unique; 0 structure fallbacks.
# ===========================================================================

# ===========================================================================
# C4 KUSH (Nubian) civ — unit/building coverage & fallback matrix
# ===========================================================================
# 0 A.D. art roots used by this civ (verified against public.zip via
# `--list-actors`; 0 A.D. names the civ folder "kushites" under both roots):
#     art/actors/units/kushites/*.xml       people, horse/camel/elephantry
#     art/actors/structures/kushites/*.xml  Nubian civic set, hulls
#     art/actors/props/structures/kushites/*.xml  *_statue / *_paintings /
#         *_sail / *_oar sub-meshes only — component props pulled in by the
#         parent actors above, NOT standalone actors, so they are deliberately
#         not registered as assets (extracting them yields no renderable mesh).
#
# "kush" was already present in CIV_CORE_ACTORS/CIV_ART_FOLDERS, so the civ
# auto-registration pass was already emitting the 32 core `kush_*` ids (same
# situation as C3 "maur"). This batch adds the Kushite roster WIDTH: the
# Nubian/Blemmyan/mercenary colour lines, the full javelin/spear horse line,
# the four named heroes, and the Nubian civic set the core pass could not
# reach (blacksmith/workshop/range/stable, pyramids, stone tower, bireme).
#
# UNIT MATRIX — RTS0 UnitTypes (23) -> Kushite mesh (unique = Kushite art)
# ---------------------------------------------------------------------------
#  UNIT ID            KUSH MESH                               STATUS
#  villager            citizen_male.xml                       unique   height 1.70
#  female_citizen      female_citizen.xml                     unique   height 1.70
#  swordsman           infantry_swordsman_c.xml               unique   height 1.80
#  spearman            infantry_spearman_b.xml                unique   height 1.80
#  archer              infantry_archer_c.xml                  unique   height 1.78
#  cavalry             cavalry_spearman_c_m.xml               unique   height 2.20
#  hero                hero_infantry_archer_amanirenas.xml    unique   height 1.90
#  healer              healer.xml                             unique   height 1.70
#  trader              trader.xml                             unique   height 1.90
#  camel               camel_javelinist_b_m.xml               unique   height 2.40
#  elephant            elephantry_c_m.xml                     unique   width  3.40
#  chariot             hero_chariot_archer_d.xml              unique   width  3.00
#  fishing_boat        structures/kushites/fishing_boat.xml    unique   width  3.50
#  trireme             structures/kushites/trireme.xml         unique   width  7.00
#  merchant_ship       structures/kushites/merchant_sail.xml  unique   width  5.50
#  --- Kushite-only roster (exported this batch, no generic UnitType) --
#  maceman             infantry_maceman_{a,b,e}.xml            unique   height 1.85
#  javelin_merc        infantry_javelinist_{a,b,e}.xml         unique   height 1.78
#  pikeman_merc        infantry_pikeman_{a,b,e}.xml           unique   height 1.80
#  cav_javelinist      cavalry_javelinist_{a,b,e}_m.xml       unique   height 2.20
#  cav_spearman        cavalry_spearman_{a,e}_m.xml            unique   height 2.20
#  hero_chariot        hero_chariot_archer_amanirenas_m.xml   unique   width  3.00
#  hero_cav_harsiotef  hero_cavalry_spearman_harsiotef_m.xml  unique   height 2.20
#  hero_cav_nastasen   hero_cavalry_spearman_nastasen_m.xml   unique   height 2.20
#  hero_elephant       hero_elephantry_arakamani_m.xml        unique   width  3.40
#  --- a/b/e colour variants of swordsman / spearman / archer, mercenary
#      maceman/pikeman/javelinist lines, camel + hero variant sets. --
#  catapult            (none — Kushites field no artillery)      FALLBACK generic
#  bolt_shooter        (none — no Kushite bolt shooter)         FALLBACK generic
#  siege_ram           (none — Kushites are a horse civ)        FALLBACK generic
#  scythed_chariot     (none — no scythed chariot actor)       FALLBACK generic
#  war_dog             (none — no Kushite dog unit)            FALLBACK generic
#  worker_elephant     (none — no support elephant actor)       FALLBACK generic
#  archer_maiden       (none — shares infantry_archer_c.xml)   FALLBACK (kush_archer)
#  => 14 unique, 9 fallback (of which archer_maiden reuses a real Kushite
#     archer mesh via kush_archer rather than the generic one).
#
# BUILDING MATRIX — RTS0 BuildingTypes (14) + common civ buildings
# ---------------------------------------------------------------------------
#  BUILDING ID         KUSH MESH                               STATUS
#  town_center         structures/kushites/civic_centre_kush.xml unique width 6.20
#  house               structures/kushites/house.xml            unique   width 2.40
#  barracks            structures/kushites/barracks.xml         unique   width 4.00
#  storehouse          structures/kushites/storehouse.xml       unique   width 3.40
#  farmstead           structures/kushites/farmstead.xml        unique   width 3.20
#  market              structures/kushites/market.xml           unique   width 4.20
#  temple              structures/kushites/temple.xml           unique   width 5.00
#  fortress            structures/kushites/fortress.xml         unique   width 6.00
#  gate                structures/kushites/wall_gate.xml        unique   width 3.80
#  dock                structures/kushites/dock.xml             unique   width 4.80
#  wall / wall_segment structures/kushites/wall_long.xml        unique   width 2.00
#  wall_tower / tower  structures/kushites/wall_tower.xml       unique   width 2.20
#  corral              structures/kushites/corral.xml           unique   width 3.60
#  wonder              structures/kushites/wonder.xml           unique   width 6.50
#  --- Kushite civic set added this batch --
#  blacksmith          structures/kushites/blacksmith.xml       unique   width 4.00
#  workshop            structures/kushites/workshop.xml         unique   width 4.60
#  archery_range       structures/kushites/range.xml            unique   width 3.80
#  stable              structures/kushites/stable.xml           unique   width 4.20
#  stable_elephant     structures/kushites/stable_elephant.xml  unique   width 4.60
#  pyramid_large       structures/kushites/pyramid_large.xml    unique   width 6.50
#  pyramid_small       structures/kushites/pyramid_small.xml    unique   width 4.00
#  defense_tower_stone structures/kushites/defense_tower_stone.xml unique width 2.20
#  shrine              structures/kushites/shrine.xml           unique   width 4.60
#  bireme              structures/kushites/bireme.xml           unique   width 6.00
#  => 14/14 BuildingTypes unique; 0 structure fallbacks.
# ===========================================================================

import argparse
import hashlib
import io
import json
import math
import os
import struct
import sys
import zipfile
import xml.etree.ElementTree as ET

try:
    from PIL import Image, ImageChops
except ImportError:
    sys.exit("Pillow required: pip install pillow")

DEFAULT_ZIP = os.path.join(os.path.dirname(__file__), "..", "..", "0ad",
                           "binaries", "data", "mods", "public", "public.zip")

# ---------------------------------------------------------------- config ----

# id -> (actor path inside zip, fit mode, target meters, tint)
#   fit: 'height' scales uniformly so AABB height == target;
#        'width' scales so max(AABB.x, AABB.z) == target.
#   tint: None | (r,g,b) applied through 0ad player-color alpha mask.
ASSETS = {
    # All 23 UnitTypes
    "villager":         ("art/actors/units/athenians/female_citizen.xml",      "height", 1.70, (0.36, 0.62, 0.94)),
    "swordsman":        ("art/actors/units/athenians/infantry_swordsman_c.xml","height", 1.80, (0.92, 0.32, 0.28)),
    "archer":           ("art/actors/units/athenians/infantry_archer_a.xml",   "height", 1.80, (0.36, 0.62, 0.94)),
    "spearman":         ("art/actors/units/athenians/infantry_spearman_b.xml", "height", 1.80, (0.36, 0.62, 0.94)),
    "cavalry":          ("art/actors/units/athenians/cavalry_swordsman_b_m.xml","height", 1.90, (0.36, 0.62, 0.94)),
    "trader":           ("art/actors/units/athenians/trader.xml",              "height", 1.90, (0.36, 0.62, 0.94)),
    "catapult":         ("art/actors/units/carthaginians/siege_lithobolos_med.xml", "width", 2.60, (0.92, 0.32, 0.28)),
    "hero":             ("art/actors/units/spartans/hero_infantry_spearman_leonidas.xml", "height", 1.85, (0.92, 0.32, 0.28)),
    "trireme":          ("art/actors/structures/athenians/trireme.xml",        "width",  7.00, (0.36, 0.62, 0.94)),
    # ObjectEntry.pluck 'projectile' — flying bolt / arrow / stone for LaunchProjectile
    "bolt_projectile":  ("art/actors/props/units/weapons/bolt_tower.xml",      "width",  1.40, None),
    "arrow_projectile": ("art/actors/props/units/weapons/arrow_front.xml",     "width",  1.10, None),
    "stone_projectile": ("art/actors/props/units/siege_artillery/stone_projectile_med.xml", "width", 0.55, None),
    "elephant":         ("art/actors/units/carthaginians/elephantry_c_m.xml",  "height", 3.20, (0.92, 0.32, 0.28)),
    "ram":              ("art/actors/units/romans/siege_ram.xml",              "width",  3.00, None),
    "siege_ram":        ("art/actors/units/romans/siege_ram.xml",              "width",  3.00, None),
    "healer":           ("art/actors/units/athenians/healer.xml",              "height", 1.70, (0.92, 0.90, 0.82)),
    "chariot":          ("art/actors/units/britons/chariot_javelinist_c_m.xml","width",  2.80, (0.36, 0.62, 0.94)),
    "war_dog":          ("art/actors/units/britons/war_dog_mastiff_a.xml",     "height", 0.90, None),
    "bolt_shooter":     ("art/actors/units/hellenes/siege_oxybeles.xml",       "width",  2.20, (0.36, 0.62, 0.94)),
    "fishing_boat":     ("art/actors/structures/athenians/fishing_boat.xml",   "width",  3.50, (0.36, 0.62, 0.94)),
    # Carthaginian kart hull/sail/cargo are 0-bone. Celtic merchant has a 21-bone
    # sail (celt_merchant_sail + idle/move/run) so Walk/Run actually flutter.
    "merchant_ship":    ("art/actors/structures/celts/merchant_ship.xml",      "width",5.50, (0.36, 0.62, 0.94)),
    "female_citizen":   ("art/actors/units/athenians/female_citizen.xml",      "height", 1.68, (0.36, 0.62, 0.94)),
    "carnyx":           ("art/actors/units/gauls/infantry_carnyx_c.xml",       "height", 1.95, (0.36, 0.62, 0.94)),
    "worker_elephant":  ("art/actors/units/mauryas/support_elephant.xml",      "height", 2.90, (0.36, 0.62, 0.94)),
    "archer_maiden":    ("art/actors/units/mauryas/infantry_archer_c.xml",     "height", 1.70, (0.36, 0.62, 0.94)),
    "camel":            ("art/actors/units/ptolemies/camel_archer_a_m.xml",    "height", 2.40, (0.36, 0.62, 0.94)),
    "scythed_chariot":  ("art/actors/units/persians/chariot_archer_a_m.xml",   "width",  2.90, (0.92, 0.32, 0.28)),

    # Fauna
    "deer":             ("art/actors/fauna/deer.xml",                          "height", 1.40, None),
    "boar":             ("art/actors/fauna/boar.xml",                          "height", 1.10, None),
    "wolf":             ("art/actors/fauna/wolf.xml",                          "height", 1.00, None),
    "sheep":            ("art/actors/fauna/sheep1.xml",                        "height", 0.90, None),
    # Gaia sheep: 0.28 gaia/fauna_sheep uses sheep3 (same mesh, no player-colour patch).
    "sheep_gaia":       ("art/actors/fauna/sheep3.xml",                        "height", 0.90, None),

    # 14 BuildingTypes + common military/civ structures
    "town_center":      ("art/actors/structures/hellenes/civic_centre_new.xml","width",  6.20, (0.36, 0.62, 0.94)),
    "house":            ("art/actors/structures/romans/house.xml",             "width",  2.40, (0.36, 0.62, 0.94)),
    "barracks":         ("art/actors/structures/athenians/barracks.xml",        "width",  4.00, (0.36, 0.62, 0.94)),
    "storehouse":       ("art/actors/structures/hellenes/storehouse.xml",      "width",  3.40, (0.36, 0.62, 0.94)),
    "wall_tower":       ("art/actors/structures/hellenes/wall_tower.xml",      "width",  2.20, (0.36, 0.62, 0.94)),
    "tower":            ("art/actors/structures/hellenes/wall_tower.xml",      "width",  2.20, (0.36, 0.62, 0.94)),
    "farmstead":        ("art/actors/structures/hellenes/farmstead.xml",       "width",  3.20, (0.36, 0.62, 0.94)),
    "market":           ("art/actors/structures/hellenes/market.xml",          "width",  4.20, (0.36, 0.62, 0.94)),
    "fortress":         ("art/actors/structures/athenians/fortress.xml",       "width",  6.00, (0.36, 0.62, 0.94)),
    "gate":             ("art/actors/structures/athenians/wall_gate_struct.xml","width", 3.80, (0.36, 0.62, 0.94)),
    "temple":           ("art/actors/structures/hellenes/temple_epic.xml",     "width",  5.00, (0.36, 0.62, 0.94)),
    "wonder":           ("art/actors/structures/carthaginians/wonder.xml",     "width",  6.50, (0.36, 0.62, 0.94)),
    "dock":             ("art/actors/structures/athenians/dock.xml",           "width",  4.80, (0.36, 0.62, 0.94)),
    "wall":             ("art/actors/structures/athenians/wall_long.xml",      "width",  2.00, (0.36, 0.62, 0.94)),
    "wall_segment":     ("art/actors/structures/athenians/wall_long.xml",      "width",  2.00, (0.36, 0.62, 0.94)),
    "corral":           ("art/actors/structures/carthaginians/corral.xml",     "width",  3.60, (0.36, 0.62, 0.94)),
    "range":            ("art/actors/structures/hellenes/range.xml",           "width",  3.80, (0.36, 0.62, 0.94)),
    "archery_range":    ("art/actors/structures/hellenes/range.xml",           "width",  3.80, (0.36, 0.62, 0.94)),
    "stable":           ("art/actors/structures/hellenes/stable.xml",          "width",  4.20, (0.36, 0.62, 0.94)),
    "stables":          ("art/actors/structures/hellenes/stable.xml",          "width",  4.20, (0.36, 0.62, 0.94)),
    "ruins":            ("art/actors/structures/hellenes/temple_epic.xml",     "width",  2.50, None),
    "foundation_small": ("art/actors/structures/fndn_4x4.xml",                 "width",  2.40, None),
    "foundation_large": ("art/actors/structures/fndn_7x7.xml",                 "width",  6.20, None),
    "foundation_3x3":   ("art/actors/structures/fndn_3x3.xml",                 "width",  2.40, None),
    "foundation_3x3_tower": ("art/actors/structures/fndn_3x3_tower.xml",       "width",  2.40, None),
    "foundation_5x5":   ("art/actors/structures/fndn_5x5.xml",                 "width",  3.60, None),
    "foundation_6x6":   ("art/actors/structures/fndn_6x6.xml",                 "width",  4.40, None),
    "foundation_8x8":   ("art/actors/structures/fndn_8x8.xml",                 "width",  6.20, None),
    "foundation_4x6":   ("art/actors/structures/fndn_4x6.xml",                 "width",  5.00, None),
    "foundation_wall":  ("art/actors/structures/fndn_9x3_wall.xml",            "width",  4.00, None),
    "foundation_dock":  ("art/actors/structures/fndn_4x4_dock.xml",            "width",  4.80, None),
    "scaffold_small":   ("art/actors/props/structures/construction/scaf_4x4.xml", "width", 2.40, None),
    "scaffold_large":   ("art/actors/props/structures/construction/scaf_8x8.xml", "width", 6.20, None),
    "rubble_small":     ("art/actors/structures/destruct_stone_3x3.xml",       "width",  2.40, None),
    "rubble_large":     ("art/actors/structures/destruct_stone_6x6.xml",       "width",  6.20, None),
    "athen_town_center_rubble": ("art/actors/structures/destruct_hele_cc.xml", "width", 6.20, None),
    "spart_town_center_rubble": ("art/actors/structures/destruct_hele_cc.xml", "width", 6.20, None),
    "mace_town_center_rubble":  ("art/actors/structures/destruct_hele_cc.xml", "width", 6.20, None),
    "sele_town_center_rubble":  ("art/actors/structures/destruct_hele_cc.xml", "width", 6.20, None),
    "ptol_town_center_rubble":  ("art/actors/structures/destruct_hele_cc.xml", "width", 6.20, None),
    "rome_town_center_rubble":  ("art/actors/structures/destruct_rome_cc.xml", "width", 6.20, None),
    "brit_town_center_rubble":  ("art/actors/structures/destruct_brit_cc.xml", "width", 6.20, None),
    "gaul_town_center_rubble":  ("art/actors/structures/destruct_gaul_civic_center.xml", "width", 6.20, None),
    "pers_town_center_rubble":  ("art/actors/structures/destruct_pers_cc.xml", "width", 6.20, None),
    "cart_town_center_rubble":  ("art/actors/structures/destruct_kart_cc.xml", "width", 6.20, None),
    "iber_town_center_rubble":  ("art/actors/structures/destruct_iber_cc.xml", "width", 6.20, None),
    "maur_town_center_rubble":  ("art/actors/structures/destruct_maur_cc.xml", "width", 6.20, None),
    "kush_town_center_rubble":  ("art/actors/structures/destruct_stone_6x6.xml", "width", 6.20, None),

    # Vegetation / Resources
    "tree":             ("art/actors/flora/trees/oak_new.xml",                 "height", 4.50, None),
    "pine":             ("art/actors/flora/trees/pine.xml",                    "height", 4.80, None),
    "palm":             ("art/actors/flora/trees/palm_cretan_date.xml",        "height", 4.00, None),
    "gold_vein":        ("art/actors/geology/metalmine_granite_greek.xml",     "width",  1.80, None),
    "stone_mine":       ("art/actors/geology/stonemine_granite.xml",           "width",  2.00, None),
    "fruit_bush":       ("art/actors/props/flora/berry_bush.xml",              "height", 1.20, None),
    # G7-H2 batch: HIGH-VISIBILITY map fauna (animated) + landmarks/civic extras (static) + flora/geology variety.
    # Chosen = biggest convertible classes still missing: gaia animals used on maps,
    # wonder/landmark structures, athenian civic set, extra trees. Civ-skin duplicates skipped.
    "goat":             ("art/actors/fauna/goat.xml",                            "height", 0.90, None),
    "zebra":            ("art/actors/fauna/zebra.xml",                           "height", 1.40, None),
    "chicken":          ("art/actors/fauna/chicken.xml",                         "height", 0.50, None),
    "crocodile":        ("art/actors/fauna/crocodile.xml",                       "height", 0.80, None),
    "giraffe":          ("art/actors/fauna/giraffe_adult.xml",                   "height", 4.20, None),
    "rabbit":           ("art/actors/fauna/rabbit1.xml",                         "height", 0.40, None),
    "gazelle":          ("art/actors/fauna/gazelle.xml",                         "height", 1.20, None),
    "bear":             ("art/actors/fauna/bear_brown.xml",                      "height", 1.60, None),
    "cow":              ("art/actors/fauna/cow.xml",                             "height", 1.40, None),
    "pig":              ("art/actors/fauna/pig1.xml",                            "height", 0.80, None),
    "lion":             ("art/actors/fauna/lion.xml",                            "height", 1.20, None),
    "lioness":          ("art/actors/fauna/lioness.xml",                         "height", 1.10, None),
    "donkey":           ("art/actors/fauna/donkey.xml",                          "height", 1.20, None),
    "fox":              ("art/actors/fauna/fox_red.xml",                         "height", 0.60, None),
    "hawk":             ("art/actors/fauna/hawk.xml",                            "height", 0.60, None),
    # Props of 0.28 structures' "researching"/"training" variants (data/production_fx.json):
    # the German forge's forger and the German temple's circling crows, at 0 A.D. metres.
    "germ_forger":      ("art/actors/units/germans/forger.xml",                  "scale",  1.00, None),
    "crow_circle":      ("art/actors/fauna/crow_circle.xml",                     "scale",  1.00, None),
    "pyramid":          ("art/actors/special/pyramid.xml",                       "width",  8.00, None),
    "pyramid_minor":    ("art/actors/special/pyramid_minor.xml",                 "width",  5.00, None),
    "ishtar_gate":      ("art/actors/special/pers_ishtar_gate.xml",              "width",  6.00, None),
    "greek_stoa":       ("art/actors/special/greek_stoa.xml",                    "width",  5.00, None),
    "greek_stoa_great": ("art/actors/special/greek_stoa_great.xml",               "width",  6.50, None),
    "theater":          ("art/actors/structures/athenians/theater.xml",          "width",  6.00, None),
    "gymnasium":        ("art/actors/structures/athenians/gymnasium.xml",        "width",  5.50, None),
    "stoa":             ("art/actors/structures/athenians/stoa_painted.xml",     "width",  5.00, None),
    "prytaneion":       ("art/actors/structures/athenians/prytaneion.xml",        "width",  5.00, None),
    "propylaea":        ("art/actors/structures/athenians/propylaea.xml",         "width",  4.50, None),
    "lighthouse":       ("art/actors/structures/ptolemies/lighthouse.xml",       "width",  4.00, None),
    "oak":              ("art/actors/flora/trees/oak.xml",                       "height", 5.00, None),
    "acacia":           ("art/actors/flora/trees/acacia.xml",                    "height", 5.50, None),
    "baobab":           ("art/actors/flora/trees/baobab.xml",                    "height", 6.50, None),
    "bamboo":           ("art/actors/flora/trees/bamboo.xml",                    "height", 4.50, None),
    "apple":            ("art/actors/flora/trees/apple.xml",                     "height", 3.50, None),
    # G7-M batch: siege engines (animated, distinct per-engine silhouettes) +
    # towers/walls/docks + production/civic statics still missing from manifest.
    # Mesh bones == PSA keys == skeleton bones verified per id (see UNIT_CFG).
    "siege_ballista":       ("art/actors/units/romans/siege_ballista.xml",        "width",  2.20, (0.36, 0.62, 0.94)),
    "siege_ballista_med":   ("art/actors/units/romans/siege_ballista_med.xml",    "width",  2.40, (0.36, 0.62, 0.94)),
    "siege_lithobolos":     ("art/actors/units/hellenes/siege_lithobolos.xml",    "width",  2.60, (0.36, 0.62, 0.94)),
    "siege_lithobolos_light": ("art/actors/units/hellenes/siege_lithobolos_light.xml", "width", 2.20, (0.36, 0.62, 0.94)),
    "siege_oxybeles":       ("art/actors/units/hellenes/siege_oxybeles.xml",      "width",  2.20, (0.36, 0.62, 0.94)),
    "siege_polybolos":      ("art/actors/units/ptolemies/siege_polybolos.xml",    "width",  2.40, (0.36, 0.62, 0.94)),
    "siege_scorpio":        ("art/actors/units/romans/siege_scorpio.xml",         "width",  2.20, (0.36, 0.62, 0.94)),
    "siege_onager":         ("art/actors/units/romans/siege_onager.xml",          "width",  2.60, (0.36, 0.62, 0.94)),
    "siege_mangonel":       ("art/actors/units/han/siege_mangonel.xml",            "width",  3.00, (0.36, 0.62, 0.94)),
    "siege_shoushe":        ("art/actors/units/han/siege_shoushe.xml",            "width",  2.60, (0.36, 0.62, 0.94)),
    "siege_tower":          ("art/actors/structures/hellenes/siege_tower.xml",    "width",  3.00, (0.36, 0.62, 0.94)),
    "siege_wall":           ("art/actors/structures/romans/siege_wall.xml",       "width",  3.00, None),
    "ballista_tower":       ("art/actors/structures/persians/ballista_tower.xml", "width",  2.50, None),
    "sentry_tower":         ("art/actors/structures/germans/sentry_tower.xml",    "width",  2.00, None),
    "defense_tower":        ("art/actors/structures/germans/defense_tower.xml",   "width",  2.50, None),
    "outpost":              ("art/actors/structures/hellenes/outpost.xml",       "width",  2.00, None),
    "super_dock":           ("art/actors/structures/carthaginians/super_dock.xml","width",  5.50, None),
    "blacksmith":           ("art/actors/structures/hellenes/blacksmith.xml",    "width",  4.20, None),
    "workshop":             ("art/actors/structures/hellenes/workshop.xml",       "width",  4.20, None),
    "arsenal":              ("art/actors/structures/han/arsenal.xml",            "width",  5.00, None),
    "library":              ("art/actors/structures/macedonians/library.xml",    "width",  4.50, None),
    "amphitheater":         ("art/actors/structures/romans/amphitheater_pompeii.xml", "width", 8.00, None),
    "triumphal_arch":       ("art/actors/structures/romans/triumphal_arch.xml",  "width",  4.00, None),
    "mausoleum":            ("art/actors/structures/global/mausoleum.xml",        "width",  5.00, None),
    "shrine":               ("art/actors/structures/han/shrine.xml",              "width",  3.00, None),
    "palace":               ("art/actors/structures/carthaginians/palace.xml",   "width",  6.00, None),
    "embassy":              ("art/actors/structures/carthaginians/embassy.xml",  "width",  3.50, None),
    "academy":              ("art/actors/structures/han/academy.xml",            "width",  5.00, None),
    "great_hall":           ("art/actors/structures/germans/great_hall.xml",     "width",  5.00, None),
    # G7-Q batch: warship hulls (bireme/quinquereme/pentekonter + nordic/celtic
    # boats + han merchants) + smith/forge/farm-field/harbor statics + hound,
    # elephant and chariot variants. pentekonter is the one animated hull
    # (3-bone mesh, 3-key PSAs, ship_one_row skeleton); the rest are 0-bone
    # statics. Hound/elephant/chariot triples verified mesh==skeleton==PSA.
    "bireme":               ("art/actors/structures/carthaginians/bireme.xml",  "width",  6.00, None),
    "bireme_rome":          ("art/actors/structures/romans/bireme.xml",         "width",  6.00, None),
    "quinquereme":          ("art/actors/structures/carthaginians/quinquereme.xml", "width", 7.50, None),
    "quinquereme_rome":     ("art/actors/structures/romans/quinquereme.xml",    "width",  7.50, None),
    "pentekonter":          ("art/actors/structures/hellenes/pentekonter.xml",   "width",  6.00, (0.36, 0.62, 0.94)),
    "trireme_han":          ("art/actors/structures/han/trireme.xml",           "width",  7.00, None),
    "bireme_han":           ("art/actors/structures/han/bireme.xml",            "width",  6.00, None),
    "ship_knarr":           ("art/actors/structures/germans/ship_knarr.xml",    "width",  5.00, None),
    "ship_snekkja":         ("art/actors/structures/germans/ship_snekkja.xml",  "width",  5.50, None),
    "ship_karvi":           ("art/actors/structures/germans/ship_karvi.xml",    "width",  4.50, None),
    "skiff":                ("art/actors/structures/celts/skiff.xml",           "width",  3.00, None),
    "rowboat":              ("art/actors/structures/celts/rowboat.xml",         "width",  3.00, None),
    "merchant_han":         ("art/actors/structures/han/merchant_ship.xml",     "width",  5.50, None),
    "fishing_han":          ("art/actors/structures/han/fishing_ship.xml",      "width",  3.50, None),
    "blacksmith_brit":      ("art/actors/structures/britons/blacksmith.xml",   "width",  4.20, None),
    "blacksmith_kart":      ("art/actors/structures/carthaginians/blacksmith.xml", "width", 4.20, None),
    "forge_germ":           ("art/actors/structures/germans/forge.xml",         "width",  4.20, None),
    "forge_han":            ("art/actors/structures/han/forge.xml",              "width",  4.50, None),
    "field_plot":           ("art/actors/structures/plot_field.xml",            "width",  4.00, None),
    "orchard_plot":         ("art/actors/structures/plot_orchard.xml",          "width",  4.00, None),
    "crannog":              ("art/actors/structures/britons/crannog.xml",       "width",  5.00, None),
    "wooden_tower":         ("art/actors/structures/britons/wooden_tower.xml",  "width",  2.50, None),
    "scout_tower":          ("art/actors/structures/britons/scout_tower.xml",   "width",  2.00, None),
    "tower_bolt":           ("art/actors/structures/carthaginians/tower_bolt.xml", "width", 2.50, None),
    "tower_artillery":      ("art/actors/structures/carthaginians/tower_artillery.xml", "width", 3.00, None),
    "stable_elephant":      ("art/actors/structures/carthaginians/stable_elephant.xml", "width", 5.00, None),
    "kennel":               ("art/actors/structures/britons/kennel.xml",        "width",  3.50, None),
    "wolfhound":            ("art/actors/units/britons/wolfhound.xml",          "height", 0.90, None),
    "mastiff_gaia":         ("art/actors/fauna/mastiff.xml",                   "height", 0.90, None),
    "elephant_bush":        ("art/actors/fauna/elephant_african_bush.xml",     "height", 3.20, (0.92, 0.32, 0.28)),
    "elephant_forest":      ("art/actors/fauna/elephant_african_forest.xml",   "height", 3.20, (0.92, 0.32, 0.28)),
    "chariot_han":          ("art/actors/units/han/chariot_archer_m.xml",       "width",  2.80, (0.36, 0.62, 0.94)),

    # ---------------------------------------------------------------------
    # C2 HAN (Chinese) civ batch. 0 A.D. ships the Han roster under two art
    # roots — art/actors/units/han/*.xml (people, mounts, chariots, siege) and
    # art/actors/structures/han/*.xml (the pagoda/timber civic set). None of it
    # was reachable before: "han" was absent from CIV_ART_FOLDERS, so the civ
    # probe never looked in those folders and ModelLibrary silently fell back to
    # the generic (Greek/Hellenic) meshes.
    #
    # Fit conventions copied from the rows above:
    #   infantry/support : 'height' 1.70 (villager/female/healer per task spec)
    #   mounted/chariot   : 'height' 2.20 for cavalry, 'width' 2.80 for chariots
    #   structures/siege : 'width'  (0 A.D. .pmd units are ~1 unit == 1 m here)
    # Tint: (0.36, 0.62, 0.94) player blue — Han actors are tinted with the same
    # alpha-mask path as every other row above, so the civ reads as a recolour of
    # the same pipeline rather than a new bake path.
    # ---------------------------------------------------------------------
    # --- Han infantry / support (height 1.70-1.80, matches the other civs) ---
    "han_villager_male":     ("art/actors/units/han/citizen_male.xml",       "height", 1.70, (0.36, 0.62, 0.94)),
    "han_villager_female":   ("art/actors/units/han/female_citizen.xml",    "height", 1.70, (0.36, 0.62, 0.94)),
    "han_citizen_female":    ("art/actors/units/han/citizen_female.xml",    "height", 1.70, (0.36, 0.62, 0.94)),
    "han_swordsman":         ("art/actors/units/han/infantry_swordsman_c.xml", "height", 1.80, (0.36, 0.62, 0.94)),
    "han_spearman":          ("art/actors/units/han/infantry_spearman_c.xml",  "height", 1.80, (0.36, 0.62, 0.94)),
    "han_halberdman":        ("art/actors/units/han/infantry_halberdman_b.xml", "height", 1.85, (0.36, 0.62, 0.94)),
    "han_archer":            ("art/actors/units/han/infantry_archer_c.xml",    "height", 1.78, (0.36, 0.62, 0.94)),
    "han_crossbowman":       ("art/actors/units/han/infantry_crossbowman_c.xml", "height", 1.78, (0.36, 0.62, 0.94)),
    "han_healer":            ("art/actors/units/han/healer.xml",                "height", 1.70, (0.92, 0.90, 0.82)),
    "han_trader":            ("art/actors/units/han/trader.xml",                "height", 1.90, (0.36, 0.62, 0.94)),
    "han_minister":          ("art/actors/units/han/minister.xml",              "height", 1.72, (0.92, 0.90, 0.82)),
    "han_fisherman":         ("art/actors/units/han/fisherman.xml",             "height", 1.70, (0.36, 0.62, 0.94)),
    # --- Han heroes (the three 0 A.D. hero_infantry_* actors) ---
    "han_hero_han_xin":      ("art/actors/units/han/hero_infantry_han_xin.xml",      "height", 1.90, (0.92, 0.32, 0.28)),
    "han_hero_wei_qing":     ("art/actors/units/han/hero_infantry_wei_qing.xml",     "height", 1.90, (0.92, 0.32, 0.28)),
    "han_hero_liu_bang":     ("art/actors/units/han/hero_infantry_liu_bang.xml",     "height", 1.90, (0.92, 0.32, 0.28)),
    "han_hero_cav_han_xin":  ("art/actors/units/han/hero_cavalry_spearman_han_xin_m.xml",    "height", 2.20, (0.92, 0.32, 0.28)),
    "han_hero_cav_wei_qing": ("art/actors/units/han/hero_cavalry_crossbowman_wei_qing_m.xml", "height", 2.20, (0.92, 0.32, 0.28)),
    "han_hero_cav_liu_bang": ("art/actors/units/han/hero_cavalry_swordsman_liu_bang_m.xml",    "height", 2.20, (0.92, 0.32, 0.28)),
    "han_hero_chariot_han":  ("art/actors/units/han/hero_chariot_crossbowman_d.xml", "width",  2.90, (0.92, 0.32, 0.28)),
    # --- Han mounted / chariot (cavalry height 2.20 per task spec, chariots width) ---
    "han_cavalry_spearman":  ("art/actors/units/han/cavalry_spearman_c_m.xml",  "height", 2.20, (0.36, 0.62, 0.94)),
    "han_cavalry_swordsman": ("art/actors/units/han/cavalry_swordsman_b_m.xml", "height", 2.20, (0.36, 0.62, 0.94)),
    "han_cavalry_crossbow":  ("art/actors/units/han/cavalry_crossbowman_c_m.xml", "height", 2.20, (0.36, 0.62, 0.94)),
    "han_cavalry_archer":    ("art/actors/units/han/cavalry_archer_b_m.xml",    "height", 2.20, (0.36, 0.62, 0.94)),
    "han_chariot_archer":    ("art/actors/units/han/chariot_archer_d.xml",      "width",  2.80, (0.36, 0.62, 0.94)),
    "han_chariot_general":   ("art/actors/units/han/chariot_archer_h_01.xml",   "width",  2.80, (0.36, 0.62, 0.94)),
    # --- Han siege (already partly present as siege_mangonel / siege_shoushe;
    #     the packed sub-meshes are the ones the unit actors actually compose) ---
    "han_siege_mangonel_packed": ("art/actors/units/han/siege_mangonel_packed.xml", "width", 3.00, (0.36, 0.62, 0.94)),
    "han_siege_mangonel_pivot":  ("art/actors/units/han/siege_mangonel_pivot_packed.xml", "width", 3.00, (0.36, 0.62, 0.94)),
    # --- Han structures: civic core (all 'width', sizes copied from the
    #     Hellenic/Athenian rows above so the RTS0 building footprints line up) ---
    "han_town_center":       ("art/actors/structures/han/civil_centre.xml",   "width", 6.20, (0.36, 0.62, 0.94)),
    "han_house":             ("art/actors/structures/han/house.xml",          "width", 2.40, (0.36, 0.62, 0.94)),
    "han_barracks":          ("art/actors/structures/han/barracks.xml",       "width", 4.00, (0.36, 0.62, 0.94)),
    "han_storehouse":        ("art/actors/structures/han/storehouse.xml",     "width", 3.40, (0.36, 0.62, 0.94)),
    "han_farmstead":         ("art/actors/structures/han/farmstead.xml",      "width", 3.20, (0.36, 0.62, 0.94)),
    "han_market":            ("art/actors/structures/han/market.xml",         "width", 4.20, (0.36, 0.62, 0.94)),
    "han_temple":            ("art/actors/structures/han/temple.xml",         "width", 5.00, (0.36, 0.62, 0.94)),
    "han_temple_shrine":     ("art/actors/structures/han/shrine.xml",         "width", 3.00, (0.36, 0.62, 0.94)),
    "han_range":             ("art/actors/structures/han/range.xml",          "width", 3.80, (0.36, 0.62, 0.94)),
    "han_stable":            ("art/actors/structures/han/stable.xml",         "width", 4.20, (0.36, 0.62, 0.94)),
    "han_fortress":          ("art/actors/structures/han/fortress.xml",       "width", 6.00, (0.36, 0.62, 0.94)),
    "han_fortress_old":      ("art/actors/structures/han/fortress_old.xml",   "width", 6.00, (0.36, 0.62, 0.94)),
    "han_academy":           ("art/actors/structures/han/academy.xml",        "width", 5.00, (0.36, 0.62, 0.94)),
    "han_arsenal":           ("art/actors/structures/han/arsenal.xml",        "width", 5.00, (0.36, 0.62, 0.94)),
    "han_pagoda":            ("art/actors/structures/han/pagoda.xml",         "width", 5.50, (0.36, 0.62, 0.94)),
    "han_imperial_court":    ("art/actors/structures/han/imperial_court.xml", "width", 6.00, (0.36, 0.62, 0.94)),
    "han_imperial_ministry": ("art/actors/structures/han/imperial_ministry.xml", "width", 5.50, (0.36, 0.62, 0.94)),
    "han_wonder":            ("art/actors/structures/han/wonder.xml",         "width", 6.50, (0.36, 0.62, 0.94)),
    # --- Han defences / walls / dock ---
    "han_wall_tower":        ("art/actors/structures/han/wall_tower.xml",     "width", 2.20, (0.36, 0.62, 0.94)),
    "han_tower_small":       ("art/actors/structures/han/tower_small.xml",    "width", 2.00, (0.36, 0.62, 0.94)),
    "han_tower_large":       ("art/actors/structures/han/tower_large.xml",    "width", 2.60, (0.36, 0.62, 0.94)),
    "han_tower_great":       ("art/actors/structures/han/tower_great.xml",     "width", 3.20, (0.36, 0.62, 0.94)),
    "han_wall_long":         ("art/actors/structures/han/wall_long.xml",      "width", 2.00, (0.36, 0.62, 0.94)),
    "han_wall_short":        ("art/actors/structures/han/wall_short.xml",     "width", 1.00, (0.36, 0.62, 0.94)),
    "han_wall_medium":       ("art/actors/structures/han/wall_medium.xml",    "width", 1.50, (0.36, 0.62, 0.94)),
    "han_wall_gate":         ("art/actors/structures/han/wall_gate.xml",      "width", 3.80, (0.36, 0.62, 0.94)),
    "han_outpost":           ("art/actors/structures/han/outpost.xml",        "width", 2.00, (0.36, 0.62, 0.94)),
    "han_dock":              ("art/actors/structures/han/dock.xml",           "width", 4.80, (0.36, 0.62, 0.94)),
    "han_corral":            ("art/actors/structures/han/corral.xml",         "width", 3.60, (0.36, 0.62, 0.94)),
    "han_forge":             ("art/actors/structures/han/forge.xml",          "width", 4.50, (0.36, 0.62, 0.94)),
    # --- Han naval hulls (static, 'width'; sail/rig animates via rider clips) ---
    "han_trireme":           ("art/actors/structures/han/trireme.xml",        "width", 7.00, None),
    "han_bireme":            ("art/actors/structures/han/bireme.xml",         "width", 6.00, None),
    "han_merchant_ship":     ("art/actors/structures/han/merchant_ship.xml",  "width", 5.50, None),
    "han_fishing_ship":      ("art/actors/structures/han/fishing_ship.xml",   "width", 3.50, None),
    "han_fireship":          ("art/actors/structures/han/fireship.xml",       "width", 6.50, None),
    "han_towership":         ("art/actors/structures/han/towership.xml",      "width", 6.50, None),
    # ---------------------------------------------------------------------
    # --- Mauryan infantry / support (height 1.70-1.85, matches other civs) ---
    "maur_mace_inf":         ("art/actors/units/mauryas/infantry_maceman_c.xml", "height", 1.85, (0.36, 0.62, 0.94)),
    "maur_swordsman_a":      ("art/actors/units/mauryas/infantry_swordsman_a.xml", "height", 1.8, (0.36, 0.62, 0.94)),
    "maur_swordsman_b":      ("art/actors/units/mauryas/infantry_swordsman_b.xml", "height", 1.8, (0.36, 0.62, 0.94)),
    "maur_swordsman_e":      ("art/actors/units/mauryas/infantry_swordsman_e.xml", "height", 1.8, (0.36, 0.62, 0.94)),
    "maur_spearman_a":       ("art/actors/units/mauryas/infantry_spearman_a.xml", "height", 1.8, (0.36, 0.62, 0.94)),
    "maur_spearman_e":       ("art/actors/units/mauryas/infantry_spearman_e.xml", "height", 1.8, (0.36, 0.62, 0.94)),
    "maur_archer_a":         ("art/actors/units/mauryas/infantry_archer_a.xml", "height", 1.78, (0.36, 0.62, 0.94)),
    "maur_archer_b":         ("art/actors/units/mauryas/infantry_archer_b.xml", "height", 1.78, (0.36, 0.62, 0.94)),
    "maur_archer_e":         ("art/actors/units/mauryas/infantry_archer_e.xml", "height", 1.78, (0.36, 0.62, 0.94)),
    "maur_archer_fire":      ("art/actors/units/mauryas/infantry_archer_c_fire.xml", "height", 1.78, (0.36, 0.62, 0.94)),
    "maur_citizen_female":   ("art/actors/units/mauryas/citizen_female.xml", "height", 1.7, (0.36, 0.62, 0.94)),
    "maur_fisherman":        ("art/actors/units/mauryas/fisherman.xml", "height", 1.7, (0.36, 0.62, 0.94)),
    # --- Mauryan cavalry (height 2.20, rider `_m` actor as the base mesh) ---
    "maur_cav_jav_a":        ("art/actors/units/mauryas/cavalry_javelinist_a_m.xml", "height", 2.20, (0.36, 0.62, 0.94)),
    "maur_cav_jav_b":        ("art/actors/units/mauryas/cavalry_javelinist_b_m.xml", "height", 2.20, (0.36, 0.62, 0.94)),
    "maur_cav_jav_e":        ("art/actors/units/mauryas/cavalry_javelinist_e_m.xml", "height", 2.20, (0.36, 0.62, 0.94)),
    "maur_cav_sword_a":      ("art/actors/units/mauryas/cavalry_swordsman_a_m.xml", "height", 2.20, (0.36, 0.62, 0.94)),
    "maur_cav_sword_e":      ("art/actors/units/mauryas/cavalry_swordsman_e_m.xml", "height", 2.20, (0.36, 0.62, 0.94)),
    # --- Mauryan elephantry / chariot (width; too tall for a 'height' fit) ---
    "maur_eleph_archer_a":   ("art/actors/units/mauryas/elephantry_archer_a_m.xml", "width",  3.2, (0.36, 0.62, 0.94)),
    "maur_eleph_archer_b":   ("art/actors/units/mauryas/elephantry_archer_b_m.xml", "width",  3.2, (0.36, 0.62, 0.94)),
    "maur_eleph_archer_d":   ("art/actors/units/mauryas/elephantry_archer_d.xml", "width",  3.4, (0.36, 0.62, 0.94)),
    "maur_eleph_archer_r1":  ("art/actors/units/mauryas/elephantry_archer_r1.xml", "width",  3.4, (0.36, 0.62, 0.94)),
    "maur_elephantry_c":     ("art/actors/units/mauryas/elephantry_c_d.xml", "width",  3.4, (0.36, 0.62, 0.94)),
    "maur_support_elephant": ("art/actors/units/mauryas/support_elephant_d.xml", "width",  3.6, (0.36, 0.62, 0.94)),
    "maur_chariot_archer_c": ("art/actors/units/mauryas/chariot_archer_c_d.xml", "width",  2.9, (0.36, 0.62, 0.94)),
    # --- Mauryan heroes (Ashoka / Bindusara / Chandragupta / Chanakya) ---
    "maur_hero_ashoka":      ("art/actors/units/mauryas/hero_infantry_archer_ashoka.xml", "height",  1.9, (0.92, 0.32, 0.28)),
    "maur_hero_bindusara":   ("art/actors/units/mauryas/hero_infantry_swordsman_bindusara.xml", "height",  1.9, (0.92, 0.32, 0.28)),
    "maur_hero_chandragupta":("art/actors/units/mauryas/hero_infantry_swordsman_chandragupta.xml", "height",  1.9, (0.92, 0.32, 0.28)),
    "maur_hero_chanakya":    ("art/actors/units/mauryas/hero_healer_chanakya.xml", "height",  1.8, (0.92, 0.90, 0.82)),
    "maur_hero_elephant":    ("art/actors/units/mauryas/hero_elephant_chandragupta_m.xml", "width",  3.6, (0.92, 0.32, 0.28)),
    "maur_hero_chariot":     ("art/actors/units/mauryas/hero_chariot_archer_ashoka_d.xml", "width",  3.0, (0.92, 0.32, 0.28)),
    # --- Mauryan structures (width; stupa/palace civic set) ---
    "maur_blacksmith":       ("art/actors/structures/mauryas/blacksmith.xml", "width",  4.0, (0.36, 0.62, 0.94)),
    "maur_workshop":         ("art/actors/structures/mauryas/workshop.xml", "width",  4.6, (0.36, 0.62, 0.94)),
    "maur_range":            ("art/actors/structures/mauryas/range.xml", "width",  3.8, (0.36, 0.62, 0.94)),
    "maur_stable":           ("art/actors/structures/mauryas/stable.xml", "width",  4.2, (0.36, 0.62, 0.94)),
    "maur_stable_elephant":  ("art/actors/structures/mauryas/stable_elephant.xml", "width",  4.6, (0.36, 0.62, 0.94)),
    "maur_palace":           ("art/actors/structures/mauryas/palace.xml", "width",  6.0, (0.36, 0.62, 0.94)),
    "maur_outpost":          ("art/actors/structures/mauryas/outpost.xml", "width",  2.0, (0.36, 0.62, 0.94)),
    "maur_scout_tower":      ("art/actors/structures/mauryas/scout_tower.xml", "width",  2.0, (0.36, 0.62, 0.94)),
    "maur_wooden_tower":     ("art/actors/structures/mauryas/wooden_tower.xml", "width",  2.0, (0.36, 0.62, 0.94)),
    "maur_tower_double":     ("art/actors/structures/mauryas/tower_double.xml", "width",  2.6, (0.36, 0.62, 0.94)),
    "maur_bireme":           ("art/actors/structures/mauryas/bireme.xml", "width",  6.0, (0.36, 0.62, 0.94)),
    "maur_super_dock":       ("art/actors/structures/mauryas/dock.xml", "width",  5.0, (0.36, 0.62, 0.94)),
    # --- Kushite infantry (Nubian/Blemmyan/mercenary colour lines; height 1.78-1.85) ---
    "kush_swordsman_a":            ("art/actors/units/kushites/infantry_swordsman_a.xml",         "height", 1.8, (0.36, 0.62, 0.94)),
    "kush_swordsman_b":            ("art/actors/units/kushites/infantry_swordsman_b.xml",         "height", 1.8, (0.36, 0.62, 0.94)),
    "kush_swordsman_e":            ("art/actors/units/kushites/infantry_swordsman_e.xml",         "height", 1.8, (0.36, 0.62, 0.94)),
    "kush_spearman_a":             ("art/actors/units/kushites/infantry_spearman_a.xml",          "height", 1.8, (0.36, 0.62, 0.94)),
    "kush_spearman_e":             ("art/actors/units/kushites/infantry_spearman_e.xml",          "height", 1.8, (0.36, 0.62, 0.94)),
    "kush_archer_a":               ("art/actors/units/kushites/infantry_archer_a.xml",            "height", 1.78, (0.36, 0.62, 0.94)),
    "kush_archer_b":               ("art/actors/units/kushites/infantry_archer_b.xml",            "height", 1.78, (0.36, 0.62, 0.94)),
    "kush_archer_e":               ("art/actors/units/kushites/infantry_archer_e.xml",            "height", 1.78, (0.36, 0.62, 0.94)),
    "kush_maceman_a":              ("art/actors/units/kushites/infantry_maceman_a.xml",           "height", 1.85, (0.36, 0.62, 0.94)),
    "kush_maceman_b":              ("art/actors/units/kushites/infantry_maceman_b.xml",           "height", 1.85, (0.36, 0.62, 0.94)),
    "kush_maceman_e":              ("art/actors/units/kushites/infantry_maceman_e.xml",           "height", 1.85, (0.36, 0.62, 0.94)),
    "kush_pikeman_a":              ("art/actors/units/kushites/infantry_pikeman_a.xml",           "height", 1.8, (0.36, 0.62, 0.94)),
    "kush_pikeman_b":              ("art/actors/units/kushites/infantry_pikeman_b.xml",           "height", 1.8, (0.36, 0.62, 0.94)),
    "kush_pikeman_e":              ("art/actors/units/kushites/infantry_pikeman_e.xml",           "height", 1.8, (0.36, 0.62, 0.94)),
    "kush_javelinist_a":           ("art/actors/units/kushites/infantry_javelinist_a.xml",        "height", 1.78, (0.36, 0.62, 0.94)),
    "kush_javelinist_b":           ("art/actors/units/kushites/infantry_javelinist_b.xml",        "height", 1.78, (0.36, 0.62, 0.94)),
    "kush_javelinist_e":           ("art/actors/units/kushites/infantry_javelinist_e.xml",        "height", 1.78, (0.36, 0.62, 0.94)),
    "kush_cav_jav_a":              ("art/actors/units/kushites/cavalry_javelinist_a_m.xml",       "height", 2.2, (0.36, 0.62, 0.94)),
    "kush_cav_jav_b":              ("art/actors/units/kushites/cavalry_javelinist_b_m.xml",       "height", 2.2, (0.36, 0.62, 0.94)),
    "kush_cav_jav_e":              ("art/actors/units/kushites/cavalry_javelinist_e_m.xml",       "height", 2.2, (0.36, 0.62, 0.94)),
    "kush_cav_spear_a":            ("art/actors/units/kushites/cavalry_spearman_a_m.xml",         "height", 2.2, (0.36, 0.62, 0.94)),
    # --- Kushite mounted (height 2.20; the `_m` actor props its own `_r` rider) ---
    "kush_cav_spear_e":            ("art/actors/units/kushites/cavalry_spearman_e_m.xml",         "height", 2.2, (0.36, 0.62, 0.94)),
    "kush_hero_chariot_amanirenas":("art/actors/units/kushites/hero_chariot_archer_amanirenas_m.xml", "width", 3, (0.92, 0.32, 0.28)),
    "kush_hero_cav_harsiotef":     ("art/actors/units/kushites/hero_cavalry_spearman_harsiotef_m.xml", "height", 2.2, (0.92, 0.32, 0.28)),
    "kush_hero_cav_nastasen":      ("art/actors/units/kushites/hero_cavalry_spearman_nastasen_m.xml", "height", 2.2, (0.92, 0.32, 0.28)),
    "kush_hero_elephant":          ("art/actors/units/kushites/hero_elephantry_arakamani_m.xml",  "width", 3.4, (0.92, 0.32, 0.28)),
    # --- Kushite heroes (Amanirenas chariot, Harsiotef/Nastasen horse, Arakamani elephant) ---
    "kush_blacksmith":             ("art/actors/structures/kushites/blacksmith.xml",              "width", 4, (0.36, 0.62, 0.94)),
    "kush_workshop":               ("art/actors/structures/kushites/workshop.xml",                "width", 4.6, (0.36, 0.62, 0.94)),
    "kush_range":                  ("art/actors/structures/kushites/range.xml",                   "width", 3.8, (0.36, 0.62, 0.94)),
    "kush_stable":                 ("art/actors/structures/kushites/stable.xml",                  "width", 4.2, (0.36, 0.62, 0.94)),
    # --- Kushite structures (width; Nubian civic set, pyramids, stone tower, bireme) ---
    "kush_stable_elephant":        ("art/actors/structures/kushites/stable_elephant.xml",         "width", 4.6, (0.36, 0.62, 0.94)),
    "kush_pyramid_large":          ("art/actors/structures/kushites/pyramid_large.xml",           "width", 6.5, (0.36, 0.62, 0.94)),
    "kush_pyramid_small":          ("art/actors/structures/kushites/pyramid_small.xml",           "width", 4, (0.36, 0.62, 0.94)),
    "kush_defense_tower_stone":    ("art/actors/structures/kushites/defense_tower_stone.xml",     "width", 2.2, (0.36, 0.62, 0.94)),
    "kush_shrine":                 ("art/actors/structures/kushites/shrine.xml",                  "width", 4.6, (0.36, 0.62, 0.94)),
    "kush_bireme":                 ("art/actors/structures/kushites/bireme.xml",                  "width", 6, None),
}

# Per-civ core actors (folder names follow 0 A.D. art/actors/units/*).
# Exported as `{civ_code}_{unit_id}.glb` so each civilization has its own meshes.
CIV_CORE_ACTORS = {
    "athen": {
        "villager": "art/actors/units/athenians/citizen_male.xml",
        "female_citizen": "art/actors/units/athenians/female_citizen.xml",
        "swordsman": "art/actors/units/athenians/infantry_swordsman_c.xml",
        "spearman": "art/actors/units/athenians/infantry_spearman_b.xml",
        "archer": "art/actors/units/athenians/infantry_archer_a.xml",
        "cavalry": "art/actors/units/athenians/cavalry_swordsman_b_m.xml",
        "hero": "art/actors/units/athenians/hero_infantry_swordsman_themistocles.xml",
        "healer": "art/actors/units/athenians/healer.xml",
        "trader": "art/actors/units/athenians/trader.xml",
        "fishing_boat": "art/actors/structures/athenians/fishing_boat.xml",
        "trireme": "art/actors/structures/athenians/trireme.xml",
    },
    "spart": {
        "villager": "art/actors/units/spartans/citizen_male.xml",
        "female_citizen": "art/actors/units/spartans/female_citizen.xml",
        "swordsman": "art/actors/units/spartans/infantry_swordsman_c.xml",
        "spearman": "art/actors/units/spartans/infantry_spearman_c.xml",
        "archer": "art/actors/units/spartans/infantry_slinger_b.xml",
        "cavalry": "art/actors/units/spartans/cavalry_spearman_b_m.xml",
        "hero": "art/actors/units/spartans/hero_infantry_spearman_leonidas.xml",
        "healer": "art/actors/units/spartans/healer.xml",
        "trader": "art/actors/units/spartans/trader.xml",
        "fishing_boat": "art/actors/structures/hellenes/fishing_boat.xml",
        "trireme": "art/actors/structures/spartans/trireme.xml",
    },
    "pers": {
        "villager": "art/actors/units/persians/citizen_male.xml",
        "female_citizen": "art/actors/units/persians/female_citizen.xml",
        "spearman": "art/actors/units/persians/infantry_spearman_c.xml",
        "archer": "art/actors/units/persians/infantry_archer_c.xml",
        "cavalry": "art/actors/units/persians/cavalry_axeman_b_m.xml",
        "hero": "art/actors/units/persians/hero_infantry_archer_xerxes.xml",
        "healer": "art/actors/units/persians/healer.xml",
        "trader": "art/actors/units/persians/trader.xml",
        "fishing_boat": "art/actors/structures/persians/fishship.xml",
        "trireme": "art/actors/structures/persians/trireme.xml",
    },
    "rome": {
        "villager": "art/actors/units/romans/citizen_male.xml",
        "female_citizen": "art/actors/units/romans/female_citizen.xml",
        "swordsman": "art/actors/units/romans/infantry_swordsman_c.xml",
        "spearman": "art/actors/units/romans/infantry_spearman_b.xml",
        "archer": "art/actors/units/romans/infantry_javelinist_b.xml",
        "cavalry": "art/actors/units/romans/cavalry_spearman_b_m.xml",
        "hero": "art/actors/units/romans/hero_infantry_swordsman_scipio.xml",
        "healer": "art/actors/units/romans/healer.xml",
        "trader": "art/actors/units/romans/trader.xml",
        "fishing_boat": "art/actors/structures/hellenes/fishing_boat.xml",
        "trireme": "art/actors/structures/romans/trireme.xml",
    },
    "cart": {
        "villager": "art/actors/units/carthaginians/citizen_male.xml",
        "female_citizen": "art/actors/units/carthaginians/female_citizen.xml",
        "swordsman": "art/actors/units/carthaginians/infantry_swordsman_b.xml",
        "spearman": "art/actors/units/carthaginians/infantry_spearman_c.xml",
        "archer": "art/actors/units/carthaginians/infantry_archer_b.xml",
        "cavalry": "art/actors/units/carthaginians/cavalry_spearman_c_m.xml",
        "hero": "art/actors/units/carthaginians/hero_infantry_swordsman_hannibal.xml",
        "healer": "art/actors/units/carthaginians/healer.xml",
        "trader": "art/actors/units/carthaginians/trader.xml",
        "fishing_boat": "art/actors/structures/hellenes/fishing_boat.xml",
        "trireme": "art/actors/structures/carthaginians/trireme.xml",
    },
    "mace": {
        "villager": "art/actors/units/macedonians/female_citizen.xml",
        "female_citizen": "art/actors/units/macedonians/female_citizen.xml",
        "swordsman": "art/actors/units/macedonians/infantry_swordsman_c_thracian.xml",
        "spearman": "art/actors/units/macedonians/infantry_pikeman_b.xml",
        "archer": "art/actors/units/macedonians/infantry_archer_b.xml",
        "cavalry": "art/actors/units/macedonians/cavalry_spearman_c_m.xml",
        "hero": "art/actors/units/macedonians/hero_infantry_swordsman_alexander.xml",
        "healer": "art/actors/units/hellenes/healer.xml",
        "trader": "art/actors/units/macedonians/trader.xml",
        "fishing_boat": "art/actors/structures/hellenes/fishing_boat.xml",
        "trireme": "art/actors/structures/macedonians/trireme.xml",
    },
    "brit": {
        "villager": "art/actors/units/britons/citizen_male.xml",
        "female_citizen": "art/actors/units/britons/citizen_female.xml",
        "swordsman": "art/actors/units/britons/infantry_swordsman_c.xml",
        "spearman": "art/actors/units/britons/infantry_spearman_c.xml",
        "archer": "art/actors/units/britons/infantry_slinger_b.xml",
        "cavalry": "art/actors/units/britons/cavalry_swordsman_b_m.xml",
        "hero": "art/actors/units/britons/hero_infantry_swordsman_boudicca.xml",
        "healer": "art/actors/units/celts/healer.xml",
        "trader": "art/actors/units/celts/trader.xml",
        "fishing_boat": "art/actors/structures/celts/fishing_boat.xml",
        "trireme": "art/actors/structures/celts/warship.xml",
    },
    "gaul": {
        "villager": "art/actors/units/britons/citizen_male.xml",
        "female_citizen": "art/actors/units/britons/citizen_female.xml",
        "swordsman": "art/actors/units/gauls/infantry_swordsman_c.xml",
        "spearman": "art/actors/units/gauls/infantry_spearman_c.xml",
        "archer": "art/actors/units/gauls/infantry_slinger_b.xml",
        "cavalry": "art/actors/units/gauls/cavalry_swordsman_b_m.xml",
        "hero": "art/actors/units/gauls/hero_infantry_swordsman_vercingetorix.xml",
        "healer": "art/actors/units/celts/healer.xml",
        "trader": "art/actors/units/celts/trader.xml",
        "fishing_boat": "art/actors/structures/celts/fishing_boat.xml",
        "trireme": "art/actors/structures/celts/warship.xml",
    },
    # 0.28's 15th civ (germ). Its art first ships in the 0.28 public.zip.
    "germ": {
        "villager": "art/actors/units/germans/citizen_male.xml",
        "female_citizen": "art/actors/units/germans/citizen_female.xml",
        "swordsman": "art/actors/units/germans/infantry_swordsman_b.xml",
        "spearman": "art/actors/units/germans/infantry_spearman_b.xml",
        "archer": "art/actors/units/germans/infantry_slinger_b.xml",
        "cavalry": "art/actors/units/germans/cavalry_spearman_b_m.xml",
        "hero": "art/actors/units/germans/hero_infantry_swordsman_teutobod.xml",
        "healer": "art/actors/units/germans/healer_b.xml",
        "trader": "art/actors/units/germans/trader.xml",
        "fishing_boat": "art/actors/structures/germans/fishing_boat.xml",
        "trireme": "art/actors/structures/germans/ship_arrow.xml",
        "merchant_ship": "art/actors/structures/germans/ship_karvi.xml",
    },
    "iber": {
        "villager": "art/actors/units/iberians/citizen_male.xml",
        "female_citizen": "art/actors/units/iberians/female_citizen.xml",
        "swordsman": "art/actors/units/iberians/infantry_swordsman_c.xml",
        "spearman": "art/actors/units/iberians/infantry_spearman_b.xml",
        "archer": "art/actors/units/iberians/infantry_slinger_b.xml",
        "cavalry": "art/actors/units/iberians/cavalry_javelinist_c_m.xml",
        "hero": "art/actors/units/iberians/hero_infantry_swordsman_viriato.xml",
        "healer": "art/actors/units/iberians/healer.xml",
        "trader": "art/actors/units/iberians/trader.xml",
        "fishing_boat": "art/actors/structures/iberians/fishing_ship.xml",
        "trireme": "art/actors/structures/iberians/warship.xml",
    },
    "maur": {
        "villager": "art/actors/units/mauryas/citizen_male.xml",
        "female_citizen": "art/actors/units/mauryas/female_citizen.xml",
        "swordsman": "art/actors/units/mauryas/infantry_swordsman_c.xml",
        "spearman": "art/actors/units/mauryas/infantry_spearman_b.xml",
        "archer": "art/actors/units/mauryas/infantry_archer_c.xml",
        "cavalry": "art/actors/units/mauryas/cavalry_swordsman_b_m.xml",
        "hero": "art/actors/units/mauryas/hero_infantry_archer_ashoka.xml",
        "healer": "art/actors/units/mauryas/healer.xml",
        "trader": "art/actors/units/mauryas/trader.xml",
        "fishing_boat": "art/actors/structures/mauryas/fishing_ship.xml",
        "trireme": "art/actors/structures/mauryas/trireme.xml",
        "elephant": "art/actors/units/mauryas/elephantry_archer_e_m.xml",
    },
    "kush": {
        "villager": "art/actors/units/kushites/citizen_male.xml",
        "female_citizen": "art/actors/units/kushites/female_citizen.xml",
        "swordsman": "art/actors/units/kushites/infantry_swordsman_c.xml",
        "spearman": "art/actors/units/kushites/infantry_spearman_b.xml",
        "archer": "art/actors/units/kushites/infantry_archer_c.xml",
        "cavalry": "art/actors/units/kushites/cavalry_spearman_c_m.xml",
        "hero": "art/actors/units/kushites/hero_infantry_archer_amanirenas.xml",
        "healer": "art/actors/units/kushites/healer.xml",
        "trader": "art/actors/units/kushites/trader.xml",
        "fishing_boat": "art/actors/structures/kushites/fishing_boat.xml",
        "trireme": "art/actors/structures/kushites/trireme.xml",
    },
    "ptol": {
        "villager": "art/actors/units/ptolemies/citizen_male.xml",
        "female_citizen": "art/actors/units/ptolemies/female_citizen.xml",
        "swordsman": "art/actors/units/ptolemies/infantry_swordsman_b.xml",
        "spearman": "art/actors/units/ptolemies/infantry_pikeman_c.xml",
        "archer": "art/actors/units/ptolemies/infantry_archer_b.xml",
        "cavalry": "art/actors/units/ptolemies/cavalry_spearman_c_m.xml",
        "hero": "art/actors/units/ptolemies/hero_infantry_archer_cleopatra.xml",
        "healer": "art/actors/units/ptolemies/healer.xml",
        "trader": "art/actors/units/hellenes/trader.xml",
        "fishing_boat": "art/actors/structures/ptolemies/fishing_boat.xml",
        "trireme": "art/actors/structures/ptolemies/trireme.xml",
    },
    "sele": {
        "villager": "art/actors/units/seleucids/citizen_male.xml",
        "female_citizen": "art/actors/units/seleucids/female_citizen.xml",
        "swordsman": "art/actors/units/seleucids/infantry_swordsman_c.xml",
        "spearman": "art/actors/units/seleucids/infantry_pikeman_c.xml",
        "archer": "art/actors/units/seleucids/infantry_archer_b.xml",
        "cavalry": "art/actors/units/seleucids/cavalry_spearman_c_m.xml",
        "hero": "art/actors/units/seleucids/hero_infantry_spearman_seleucid_victor.xml",
        "healer": "art/actors/units/hellenes/healer.xml",
        "trader": "art/actors/units/hellenes/trader.xml",
        "fishing_boat": "art/actors/structures/hellenes/fishing_boat.xml",
        "trireme": "art/actors/structures/seleucids/trireme.xml",
    },
    # Han (Chinese). 0 A.D. has no generic citizen male under a Han-specific
    # name set beyond citizen_male/female_citizen; the roster is Han's own
    # halberd/spear/sword line, crossbow & archer, mounted crossbow/spear/sword,
    # two chariot flavours, three heroes and the minister/ministeress support cast.
    "han": {
        "villager": "art/actors/units/han/citizen_male.xml",
        "female_citizen": "art/actors/units/han/female_citizen.xml",
        "swordsman": "art/actors/units/han/infantry_swordsman_c.xml",
        "spearman": "art/actors/units/han/infantry_spearman_c.xml",
        "archer": "art/actors/units/han/infantry_archer_c.xml",
        "cavalry": "art/actors/units/han/cavalry_spearman_c_m.xml",
        "hero": "art/actors/units/han/hero_infantry_han_xin.xml",
        "healer": "art/actors/units/han/healer.xml",
        "trader": "art/actors/units/han/trader.xml",
        "fishing_boat": "art/actors/structures/han/fishing_ship.xml",
        "trireme": "art/actors/structures/han/trireme.xml",
    },
}

# 0 A.D. art folder names per civ, most specific first. Shared by the unit and
# structure probes (art/actors/units/<folder> and art/actors/structures/<folder>).
CIV_ART_FOLDERS = {
    "athen": ["athenians", "hellenes"],
    "spart": ["spartans", "hellenes"],
    "pers": ["persians"],
    "rome": ["romans"],
    "cart": ["carthaginians"],
    "mace": ["macedonians", "hellenes"],
    "brit": ["britons", "celts"],
    "gaul": ["gauls", "celts"],
    "germ": ["germans", "celts"],
    "iber": ["iberians"],
    "maur": ["mauryas"],
    "kush": ["kushites"],
    "ptol": ["ptolemies"],
    "sele": ["seleucids", "hellenes"],
    "han": ["han"],
}

# 0 A.D. filenames for each RTS0 unit id that CIV_CORE_ACTORS does not spell out
# (siege, chariots, camels, elephants, merchantmen). Probed against each civ's
# folders exactly like BLD_ACTOR_NAMES, so a civ that lacks the unit simply has
# no `{code}_{unit}.glb` and ModelLibrary falls back to the generic mesh.
CIV_UNIT_ACTOR_NAMES = {
    "catapult": ("siege_lithobolos_med.xml", "siege_onager.xml", "siege_lithobolos.xml",
                 "siege_polybolos.xml", "siege_rock.xml", "siege_spear.xml"),
    "siege_ram": ("siege_ram.xml",),
    "ram": ("siege_ram.xml",),
    "bolt_shooter": ("siege_oxybeles.xml", "siege_polybolos.xml",
                     "siege_ballista_med.xml", "siege_ballista.xml"),
    "chariot": ("chariot_javelinist_c_m.xml", "chariot_archer_c_m.xml",
                "chariot_archer_b_m.xml", "chariot_archer_a_m.xml"),
    "scythed_chariot": ("chariot_archer_a_m.xml", "chariot_archer_c_m.xml",
                        "chariot_javelinist_c_m.xml"),
    "war_dog": ("war_dog_mastiff_a.xml", "wolfhound.xml"),
    "camel": ("camel_archer_a_m.xml", "camel_javelinist_b_m.xml", "camel_swordsman_m.xml"),
    "elephant": ("elephantry_archer_e_m.xml", "elephantry_c_m.xml"),
    "worker_elephant": ("support_elephant.xml",),
    "carnyx": ("infantry_carnyx_c.xml",),
    "archer_maiden": ("infantry_archer_c.xml", "infantry_archer_b.xml", "infantry_archer_a.xml"),
    "merchant_ship": ("merchant_ship.xml", "merchantman.xml", "merchant_sail.xml", "merchant.xml"),
    # Han-specific lines 0 A.D. ships under their own names.
    "halberdman": ("infantry_halberdman_b.xml",),
    "crossbowman": ("infantry_crossbowman_c.xml", "infantry_crossbowman_b.xml"),
    "cavalry_crossbowman": ("cavalry_crossbowman_c_m.xml",),
    "cavalry_archer": ("cavalry_archer_b_m.xml",),
    "chariot_archer_han": ("chariot_archer_d.xml", "chariot_archer_m.xml"),
    "hero_cavalry_han": ("hero_cavalry_spearman_han_xin_m.xml",),
    "hero_chariot_han": ("hero_chariot_crossbowman_d.xml",),
    "minister": ("minister.xml",),
    "fisherman": ("fisherman.xml",),
}

# 0 A.D. filenames for each RTS0 building id (first match in civ folders wins).
BLD_ACTOR_NAMES = {
    "town_center": ("civic_centre.xml", "civic_center.xml", "civil_centre.xml", "civil_center.xml"),
    "house": ("house.xml", "homestead.xml"),
    "barracks": ("barracks.xml",),
    "storehouse": ("storehouse.xml",),
    "wall_tower": ("wall_tower.xml", "defense_tower.xml", "defense_tower_stone.xml", "scout_tower.xml"),
    "tower": ("wall_tower.xml", "defense_tower.xml", "defense_tower_stone.xml", "scout_tower.xml"),
    "farmstead": ("farmstead.xml",),
    "market": ("market.xml", "market_newest.xml"),
    "fortress": ("fortress.xml",),
    "gate": ("wall_gate.xml", "wall_gate_struct.xml", "gate.xml"),
    "temple": ("temple.xml",),
    "wonder": ("wonder.xml", "apadana.xml", "capitoline.xml", "big_stupa.xml",
               "pyramid_large.xml", "temple_epic.xml"),
    "dock": ("dock.xml",),
    "wall_segment": ("wall_long.xml",),
    "wall": ("wall_long.xml",),
    "corral": ("corral.xml", "plot_corral.xml"),
}

BLD_GLOBAL_FALLBACK = {
    "town_center": "art/actors/structures/hellenes/civic_centre_new.xml",
    "house": "art/actors/structures/hellenes/house.xml",
    "barracks": "art/actors/structures/athenians/barracks.xml",
    "storehouse": "art/actors/structures/hellenes/storehouse.xml",
    "wall_tower": "art/actors/structures/hellenes/wall_tower.xml",
    "tower": "art/actors/structures/hellenes/wall_tower.xml",
    "farmstead": "art/actors/structures/hellenes/farmstead.xml",
    "market": "art/actors/structures/hellenes/market.xml",
    "fortress": "art/actors/structures/athenians/fortress.xml",
    "gate": "art/actors/structures/hellenes/wall_gate.xml",
    "temple": "art/actors/structures/hellenes/temple.xml",
    "wonder": "art/actors/structures/carthaginians/wonder.xml",
    "dock": "art/actors/structures/athenians/dock.xml",
    "wall_segment": "art/actors/structures/hellenes/wall_long.xml",
    "wall": "art/actors/structures/hellenes/wall_long.xml",
    "corral": "art/actors/structures/hellenes/corral.xml",
}


def _fit_scale(fit, target, dim):
    """"width" and "height" fit that extent to target; "scale" is a fixed factor
    (WallSet pieces keep 0 A.D.'s lengths at 1 unit = 4 m)."""
    if fit == "scale":
        return target
    return (target / max(dim[0], dim[2])) if fit == "width" else (target / max(dim[1], 1e-6))


# 0.28's Germans: structures extract_0ad_structures.py would convert from
# Collada, which a release public.zip does not carry (cached .pmd only). The
# ids follow that tool's s_<folder>_<actor>, mapped in data/building_models.json.
# Structure actors whose 0.28 mesh reaches far below the actor origin (the
# Kushite civic centre's mound, the Hanging Gardens' terraces). RTS0 grounds a
# model at its lowest vertex, so compose_actor cuts the part under 0.28's ground
# plane away: whole triangles below it are dropped and the rest clamped to it.
GROUND_AT_ORIGIN = {
    "art/actors/structures/spartans/civic_center.xml",
    "art/actors/structures/kushites/civic_centre_kush.xml",
    "art/actors/structures/persians/gardens_struct.xml",
    # The fields' own mesh is a hidden prop carrier 27 m down.
    "art/actors/structures/plot_field_medit.xml",
    "art/actors/structures/plot_field_temp.xml",
    "art/actors/structures/rice_paddy.xml",
    # The Ptolemaic market stalls stand on plinths sunk up to 2 m.
    "art/actors/structures/ptolemies/stall_small_a.xml",
    "art/actors/structures/ptolemies/stall_small_b.xml",
    "art/actors/structures/ptolemies/stall_med.xml",
    "art/actors/structures/ptolemies/stall_big.xml",
}
GROUND_CUT_Y = -0.05

ZIP_STRUCTURES = {
    "s_germans_stable":      ("art/actors/structures/germans/stable.xml",   "width", 4.2, None),
    "s_germans_range":       ("art/actors/structures/germans/range.xml",    "width", 3.8, None),
    "s_germans_workshop":    ("art/actors/structures/germans/workshop.xml", "width", 5.0, None),
    "s_germans_forge":       ("art/actors/structures/germans/forge.xml",    "width", 4.2, None),
    "s_germans_wooden_wall_short":  ("art/actors/structures/germans/wooden_wall_short.xml",  "scale", 0.25, None),
    "s_germans_wooden_wall_medium": ("art/actors/structures/germans/wooden_wall_medium.xml", "scale", 0.25, None),
    "s_germans_wooden_wall_long":   ("art/actors/structures/germans/wooden_wall_long.xml",   "scale", 0.25, None),
    "s_germans_wooden_wall_gate":   ("art/actors/structures/germans/wooden_wall_gate.xml",   "scale", 0.25, None),
    # Rome's siege WallSet (rome/wallset_siege): its own long, medium and short
    # segments, tower and gate, at the stone walls' 1 unit = 4 m.
    "s_romans_siege_wall_long":   ("art/actors/structures/romans/siege_wall_long.xml",   "scale", 0.25, None),
    "s_romans_siege_wall_medium": ("art/actors/structures/romans/siege_wall_medium.xml", "scale", 0.25, None),
    "s_romans_siege_wall_short":  ("art/actors/structures/romans/siege_wall_short.xml",  "scale", 0.25, None),
    "s_romans_siege_wall_tower":  ("art/actors/structures/romans/siege_wall_tower.xml",  "scale", 0.25, None),
    "s_romans_siege_wall_gate":   ("art/actors/structures/romans/siege_wall_gate.xml",   "scale", 0.25, None),
    # Carthage's three 0.28 embassies (the generic `embassy` is the older
    # shared actor) and the apartment its houses upgrade to, sized like the
    # embassy and the Carthaginian house they stood in for.
    "s_carthaginians_embassy_celtic":  ("art/actors/structures/carthaginians/embassy_celtic.xml",  "width", 3.5, None),
    "s_carthaginians_embassy_iberian": ("art/actors/structures/carthaginians/embassy_iberian.xml", "width", 3.5, None),
    "s_carthaginians_embassy_italic":  ("art/actors/structures/carthaginians/embassy_italic.xml",  "width", 3.4, None),
    "s_carthaginians_apartment":       ("art/actors/structures/carthaginians/apartment.xml",       "width", 2.7, None),
    # The Defense Tower's 0.28 actors (structures/<civ>/defense_tower.xml), which
    # are not the civs' wall towers; sized like the wall tower they stood in for.
    "s_hellenes_scout_tower": ("art/actors/structures/hellenes/scout_tower.xml", "width", 2.2, None),
    "s_carthaginians_scout_tower": ("art/actors/structures/carthaginians/scout_tower.xml", "width", 2.2, None),
    "s_gauls_scout_tower": ("art/actors/structures/gauls/scout_tower.xml", "width", 2.2, None),
    "s_iberians_scout_tower": ("art/actors/structures/iberians/scout_tower.xml", "width", 2.2, None),
    "s_persians_scout_tower": ("art/actors/structures/persians/scout_tower.xml", "width", 2.2, None),
    "s_romans_scout_tower": ("art/actors/structures/romans/scout_tower.xml", "width", 2.2, None),
    "s_seleucids_scout_tower": ("art/actors/structures/seleucids/scout_tower.xml", "width", 2.2, None),
    "s_ptolemies_defense_tower": ("art/actors/structures/ptolemies/defense_tower.xml", "width", 2.2, None),
    # 0.28's own actors for a Spartan and a Kushite civic centre, the Persian
    # wonder (the Hanging Gardens) and the Carthaginian temple, which drew an
    # older actor (the Kushite one the Hellenic civic centre); sized like those.
    "s_spartans_civic_center":       ("art/actors/structures/spartans/civic_center.xml",       "width", 6.2, None),
    "s_kushites_civic_centre_kush":  ("art/actors/structures/kushites/civic_centre_kush.xml",  "width", 6.2, None),
    "s_persians_gardens_struct":     ("art/actors/structures/persians/gardens_struct.xml",     "width", 6.5, None),
    "s_carthaginians_temple_big":    ("art/actors/structures/carthaginians/temple_big.xml",    "width", 5.0, None),
    # 0.28's fields (structures/<civ>/field.xml): the Mediterranean and temperate
    # propped plots and the Han rice paddy, at 0.28's size (1 unit = 4 m).
    "s_plot_field_medit": ("art/actors/structures/plot_field_medit.xml", "scale", 0.25, None),
    "s_plot_field_temp":  ("art/actors/structures/plot_field_temp.xml",  "scale", 0.25, None),
    "s_rice_paddy":       ("art/actors/structures/rice_paddy.xml",       "scale", 0.25, None),
    # Houses maps place by their own 0.28 template (structures/stall_*,
    # celt_hut, celt_longhouse), drawn by template as "look_*"
    # (data/building_models.json, RtsoWorldView::set_building_look), at 0.28's size.
    "s_ptolemies_stall_small_a": ("art/actors/structures/ptolemies/stall_small_a.xml", "scale", 0.25, None),
    "s_ptolemies_stall_small_b": ("art/actors/structures/ptolemies/stall_small_b.xml", "scale", 0.25, None),
    "s_ptolemies_stall_med":     ("art/actors/structures/ptolemies/stall_med.xml",     "scale", 0.25, None),
    "s_ptolemies_stall_big":     ("art/actors/structures/ptolemies/stall_big.xml",     "scale", 0.25, None),
    "s_celts_hut":               ("art/actors/structures/celts/hut.xml",               "scale", 0.25, None),
    "s_celts_longhouse":         ("art/actors/structures/celts/longhouse.xml",         "scale", 0.25, None),
    # The Wagon Encampment, as wide as the Roman army camp it stood in for, and
    # its fortified Upgrade at the same scale (its palisade rings the wagons).
    "s_germans_encampment":           ("art/actors/structures/germans/encampment.xml",           "width", 4.0, None),
    "s_germans_encampment_fortified": ("art/actors/structures/germans/encampment_fortified.xml", "scale", 0.1146, None),
    # The Gaia shrines and the Celtic trading post maps place (BLD_SHRINE, BLD_TRADING_POST),
    # at the Roman temple of Vesta's scale (the Italian shrine's actor), metres x 0.21.
    "s_hellenes_shrine_01":           ("art/actors/structures/hellenes/shrine_01.xml",            "scale", 0.21, None),
    "s_celts_small_stone_monument":   ("art/actors/structures/celts/small_stone_monument.xml",   "scale", 0.21, None),
    "s_ptolemies_kiosk":              ("art/actors/structures/ptolemies/kiosk.xml",              "scale", 0.21, None),
    "s_persians_shrine":              ("art/actors/structures/persians/shrine.xml",              "scale", 0.21, None),
    "s_celts_tavern":                 ("art/actors/structures/celts/tavern.xml",                 "scale", 0.21, None),
    "s_han_pagoda":                   ("art/actors/structures/han/pagoda.xml",                   "scale", 0.21, None),
    "s_persians_market":              ("art/actors/structures/persians/market.xml",              "scale", 0.21, None),
    "s_persians_alt_building_04":     ("art/actors/props/structures/persians/alt_building_04.xml", "scale", 0.21, None),
    "s_romans_rome_tent":             ("art/actors/props/structures/romans/rome_tent.xml",       "scale", 0.21, None),
    "s_germans_cult_statue":          ("art/actors/structures/germans/cult_statue.xml",          "scale", 0.21, None),
    "s_persians_alt_building_01":     ("art/actors/props/structures/persians/alt_building_01.xml", "scale", 0.21, None),
    # The scaffolds 0.28's "upgrading" variants prop on a structure while it upgrades
    # (tools/extract_upgrade_scaffolds.py places them on each building GLB).
    "scaffold_up_3x3_tower": ("art/actors/props/structures/construction/scaf_3x3_tower.xml", "scale", 0.21, None),
    "scaffold_up_4x4": ("art/actors/props/structures/construction/scaf_4x4.xml", "scale", 0.21, None),
    "scaffold_up_8x8": ("art/actors/props/structures/construction/scaf_8x8.xml", "scale", 0.21, None),
    "scaffold_up_9x3_wall": ("art/actors/props/structures/construction/scaf_9x3_wall.xml", "scale", 0.21, None),
    "scaffold_up_cart_defense_tower": ("art/actors/props/structures/construction/scaf_cart_defense_tower.xml", "scale", 0.21, None),
    "scaffold_up_cart_wall_long": ("art/actors/props/structures/construction/scaf_cart_wall_long.xml", "scale", 0.21, None),
    "scaffold_up_cart_wooden_tower": ("art/actors/props/structures/construction/scaf_cart_wooden_tower.xml", "scale", 0.21, None),
}

# Units only 0.28 has, drawn by RTS0's generic unit models of the same id
# (the Germans' supply wagon and the house wagon it promotes to), as long as
# the template's 14 m obstruction at 1 unit = 4 m.
ZIP_UNITS = {
    "wagon":       ("art/actors/structures/germans/wagon.xml",       "width", 3.5, None),
    "wagon_house": ("art/actors/structures/germans/wagon_house.xml", "width", 3.5, None),
}


def _register_civ_unit(assets, names, code, uid, actor, aid=None):
    """Bind one `{code}_{uid}` asset (or `aid`) to a 0 A.D. actor, inheriting rider setup."""
    if uid not in ASSETS:
        return None
    aid = aid or f"{code}_{uid}"
    _path, fit, target, tint = ASSETS[uid]
    assets[aid] = (actor, fit, target, tint)
    if uid in RIDER_ATTACHMENTS:
        rpath, ap_name, sit = RIDER_ATTACHMENTS[uid][0]
        if actor.endswith("_m.xml"):
            rider = actor[:-6] + "_r.xml"
        elif uid == "trader":
            cand = (actor[:-len("trader.xml")] + "trader_r.xml") if actor.endswith("trader.xml") else None
            if cand and cand in names:
                rider = cand
            elif "camel" in actor or "carthaginians" in actor or "persians" in actor:
                rider = None
                UNIT_CFG[aid] = ("camel", {
                    "Idle": "camel_idle", "Walk": "camel_walk", "Run": "camel_run",
                    "Attack": "camel_attack", "Death": "camel_death"})
            else:
                rider = rpath
        else:
            rider = rpath
        if rider:
            RIDER_ATTACHMENTS[aid] = [(rider, ap_name, sit)]
        else:
            RIDER_ATTACHMENTS.pop(aid, None)
    if uid in RIDER_CLIPS:
        if aid in RIDER_ATTACHMENTS:
            RIDER_CLIPS[aid] = RIDER_CLIPS[uid]
        else:
            RIDER_CLIPS.pop(aid, None)
    return aid


def register_civ_unit_assets(assets, zf=None):
    """Add `{code}_{unit}` entries and copy UNIT_CFG / rider attach from the base unit."""
    names = set(zf.namelist()) if zf is not None else set()
    added = []
    for code, units in CIV_CORE_ACTORS.items():
        for uid, actor in units.items():
            aid = _register_civ_unit(assets, names, code, uid, actor)
            if aid:
                added.append(aid)
    # Siege/chariot/camel/elephant/merchantman: probe the civ's own folders the
    # way buildings do. A civ 0 A.D. never gave the unit is skipped, and
    # ModelLibrary falls back to the generic mesh at load time.
    for code, folders in CIV_ART_FOLDERS.items():
        for uid, filenames in CIV_UNIT_ACTOR_NAMES.items():
            aid = f"{code}_{uid}"
            if uid not in ASSETS or aid in assets:
                continue
            actor = None
            for folder in folders:
                for fn in filenames:
                    for root in ("units", "structures"):
                        path = f"art/actors/{root}/{folder}/{fn}"
                        if path in names:
                            actor = path
                            break
                    if actor:
                        break
                if actor:
                    break
            if actor is None:
                continue
            got = _register_civ_unit(assets, names, code, uid, actor)
            if got:
                added.append(got)
    for aid, row in ZIP_UNITS.items():
        if row[0] in names:
            assets[aid] = row
            added.append(aid)
    _apply_civ_unit_overrides()
    return added


# Template model id -> the RTS0 model kind it is drawn as (spearman, cavalry...),
# and the extracted model whose clips a mesh-only template model borrows.
# The treasures' actors (gaia/treasure/*.xml VisualActor; templates sharing an actor
# share its GLB), written to models/props/. Wrecks are sized like the ships (1 unit =
# 4 m, a trireme is 7 long); the small props at twice that, next to units drawn about
# 1 unit = 1 m tall.
ZIP_PROPS = {
    "treasure_barrel_a": ("art/actors/props/special/eyecandy/barrel_a.xml", "scale", 0.5, None),
    "treasure_barrels_buried": ("art/actors/props/special/eyecandy/barrels_buried.xml", "scale", 0.5, None),
    "treasure_produce_bin_a": ("art/actors/props/special/eyecandy/produce_bin_a.xml", "scale", 0.5, None),
    "treasure_crate_a": ("art/actors/props/special/eyecandy/crate_a.xml", "scale", 0.5, None),
    "treasure_amphorae": ("art/actors/props/special/eyecandy/amphorae.xml", "scale", 0.5, None),
    "treasure_persian_food_big": ("art/actors/props/special/eyecandy/treasure_persian_food_big.xml", "scale", 0.5, None),
    "treasure_persian_food_small": ("art/actors/props/special/eyecandy/treasure_persian_food_small.xml", "scale", 0.5, None),
    "treasure_persian_metal_big": ("art/actors/props/special/eyecandy/treasure_persian_metal_big.xml", "scale", 0.5, None),
    "treasure_persian_metal_small": ("art/actors/props/special/eyecandy/treasure_persian_metal_small.xml", "scale", 0.5, None),
    "treasure_weapons": ("art/actors/props/special/eyecandy/treasure_weapons.xml", "scale", 0.5, None),
    "treasure_shipwreck_ram_side": ("art/actors/props/special/eyecandy/shipwreck_ram_side.xml", "scale", 0.25, None),
    "treasure_barrels_floating": ("art/actors/props/special/eyecandy/barrels_floating.xml", "scale", 0.25, None),
    "treasure_shipwreck_ram_bow": ("art/actors/props/special/eyecandy/shipwreck_ram_bow.xml", "scale", 0.25, None),
    "treasure_shipwreck_sail_boat": ("art/actors/props/special/eyecandy/shipwreck_sail_boat.xml", "scale", 0.25, None),
    "treasure_shipwreck_sail_boat_cut": ("art/actors/props/special/eyecandy/shipwreck_sail_boat_cut.xml", "scale", 0.25, None),
    "treasure_shipwreck_argo": ("art/actors/props/special/eyecandy/shipwreck_argo.xml", "scale", 0.25, None),
    "treasure_standing_stones": ("art/actors/props/special/eyecandy/standing_stones.xml", "scale", 0.5, None),
    "treasure_stone_pile": ("art/actors/props/special/eyecandy/stone_pile.xml", "scale", 0.5, None),
    "treasure_brick_pile_06": ("art/actors/props/special/eyecandy/brick_pile_06.xml", "scale", 0.5, None),
    "treasure_stone_pile_red": ("art/actors/props/special/eyecandy/stone_pile_red.xml", "scale", 0.5, None),
    "treasure_stone_pile_sandstone": ("art/actors/props/special/eyecandy/stone_pile_sandstone.xml", "scale", 0.5, None),
    "treasure_wood_pile": ("art/actors/props/special/eyecandy/wood_pile.xml", "scale", 0.5, None),
    "treasure_golden_fleece": ("art/actors/special/golden_fleece.xml", "scale", 0.5, None),
    "treasure_pegasus": ("art/actors/special/pegasus.xml", "scale", 0.5, None),
    # Mines per random-map biome (maps/random/rmbiome stoneLarge/stoneSmall/metalLarge/metalSmall),
    # drawn by world_view_node.cpp pick_mine_models.
    "mine_stonemine_aegean_large": ("art/actors/geology/stonemine_aegean_large.xml", "scale", 0.25, None),
    "mine_stonemine_aegean_small": ("art/actors/geology/stonemine_aegean_small.xml", "scale", 0.25, None),
    "mine_metal_aegean_round": ("art/actors/geology/metal_aegean_round.xml", "scale", 0.25, None),
    "mine_metal_aegean_small": ("art/actors/geology/metal_aegean_small.xml", "scale", 0.25, None),
    "mine_stonemine_temperate_quarry_01": ("art/actors/geology/stonemine_temperate_quarry_01.xml", "scale", 0.25, None),
    "mine_stonemine_granite": ("art/actors/geology/stonemine_granite.xml", "scale", 0.25, None),
    "mine_metal_temperate_round": ("art/actors/geology/metal_temperate_round.xml", "scale", 0.25, None),
    "mine_metal_temperate_small": ("art/actors/geology/metal_temperate_small.xml", "scale", 0.25, None),
    "mine_metal_temperate_square": ("art/actors/geology/metal_temperate_square.xml", "scale", 0.25, None),
    "mine_stonemine_granite_large": ("art/actors/geology/stonemine_granite_large.xml", "scale", 0.25, None),
    "mine_metalmine_polar_round_snow": ("art/actors/geology/metalmine_polar_round_snow.xml", "scale", 0.25, None),
    "mine_stonemine_temperate_quarry": ("art/actors/geology/stonemine_temperate_quarry.xml", "scale", 0.25, None),
    "mine_stonemine_india_large": ("art/actors/geology/stonemine_india_large.xml", "scale", 0.25, None),
    "mine_stonemine_india_small": ("art/actors/geology/stonemine_india_small.xml", "scale", 0.25, None),
    "mine_metal_india_round": ("art/actors/geology/metal_india_round.xml", "scale", 0.25, None),
    "mine_metal_india_small": ("art/actors/geology/metal_india_small.xml", "scale", 0.25, None),
    "mine_stonemine_savanna_formations": ("art/actors/geology/stonemine_savanna_formations.xml", "scale", 0.25, None),
    "mine_stonemine_savanna_small": ("art/actors/geology/stonemine_savanna_small.xml", "scale", 0.25, None),
    "mine_metal_savanna_round": ("art/actors/geology/metal_savanna_round.xml", "scale", 0.25, None),
    "mine_metal_savanna_small": ("art/actors/geology/metal_savanna_small.xml", "scale", 0.25, None),
    "mine_stonemine_desert_quarry": ("art/actors/geology/stonemine_desert_quarry.xml", "scale", 0.25, None),
    "mine_stonemine_desert_small": ("art/actors/geology/stonemine_desert_small.xml", "scale", 0.25, None),
    "mine_metal_sahara_round": ("art/actors/geology/metal_sahara_round.xml", "scale", 0.25, None),
    "mine_metal_sahara_small": ("art/actors/geology/metal_sahara_small.xml", "scale", 0.25, None),
}

# Trees and fruit with no family model: one per 0 A.D. template (tools/extract_0ad_flora.py,
# flora_data.h), in 0 A.D.'s metres like tools/fetch_0ad_trees.py.
# Looks per flora actor: RtsoWorldView::TREE_VARIANTS.
FLORA_LOOKS = 4

ZIP_FLORA = {
    "flora_fruit_apple": ("art/actors/flora/trees/apple_bloom.xml", "scale", 1.0, None),
    "flora_fruit_banana": ("art/actors/flora/trees/banana.xml", "scale", 1.0, None),
    "flora_fruit_berry_02": ("art/actors/props/flora/berry_bush_02.xml", "scale", 1.0, None),
    "flora_fruit_berry_03": ("art/actors/props/flora/berry_bush_03.xml", "scale", 1.0, None),
    "flora_fruit_berry_04": ("art/actors/props/flora/berry_bush_autumn_01.xml", "scale", 1.0, None),
    "flora_fruit_berry_05": ("art/actors/props/flora/bush_berries_large.xml", "scale", 1.0, None),
    "flora_fruit_date": ("art/actors/flora/trees/palm_date_new_fruit.xml", "scale", 1.0, None),
    "flora_fruit_fig": ("art/actors/flora/trees/fig.xml", "scale", 1.0, None),
    "flora_fruit_grapes": ("art/actors/props/flora/forage_grapes.xml", "scale", 1.0, None),
    "flora_tree_acacia_large": ("art/actors/flora/trees/acacia_large.xml", "scale", 1.0, None),
    "flora_tree_bamboo_dragon": ("art/actors/flora/trees/bamboo_dragon.xml", "scale", 1.0, None),
    "flora_tree_bamboo_single": ("art/actors/flora/trees/bamboo_single.xml", "scale", 1.0, None),
    "flora_tree_baobab_1_sapling": ("art/actors/flora/trees/baobab_new_sapling.xml", "scale", 1.0, None),
    "flora_tree_baobab_2_young": ("art/actors/flora/trees/baobab_new_young.xml", "scale", 1.0, None),
    "flora_tree_baobab_3_mature": ("art/actors/flora/trees/baobab_new.xml", "scale", 1.0, None),
    "flora_tree_baobab_4_dead": ("art/actors/flora/trees/baobab_new_dead.xml", "scale", 1.0, None),
    "flora_tree_bush_badlands": ("art/actors/props/flora/bush_tempe_a.xml", "scale", 1.0, None),
    "flora_tree_bush_mediterranean": ("art/actors/flora/bushes/medit_scrub_01.xml", "scale", 1.0, None),
    "flora_tree_bush_mediterranean_dry": ("art/actors/flora/bushes/medit_scrub_02.xml", "scale", 1.0, None),
    "flora_tree_bush_steppe_01": ("art/actors/props/flora/bush_tempe_underbrush.xml", "scale", 1.0, None),
    "flora_tree_bush_steppe_02": ("art/actors/props/flora/bush_juniper_prickly.xml", "scale", 1.0, None),
    "flora_tree_bush_steppe_03": ("art/actors/props/flora/bush_medit_underbrush.xml", "scale", 1.0, None),
    "flora_tree_bush_temperate": ("art/actors/flora/trees/temperate_bush_biome.xml", "scale", 1.0, None),
    "flora_tree_bush_temperate_autumn": ("art/actors/props/flora/bush_temperate_autumn.xml", "scale", 1.0, None),
    "flora_tree_bush_temperate_winter": ("art/actors/flora/trees/temperate_bush_biome_winter.xml", "scale", 1.0, None),
    "flora_tree_cedar_atlas_1_sapling": ("art/actors/flora/trees/cedar_atlas_sapling.xml", "scale", 1.0, None),
    "flora_tree_cedar_atlas_2_young": ("art/actors/flora/trees/cedar_atlas_young.xml", "scale", 1.0, None),
    "flora_tree_cedar_atlas_3_mature": ("art/actors/flora/trees/cedar_atlas.xml", "scale", 1.0, None),
    "flora_tree_cedar_atlas_4_dead": ("art/actors/flora/trees/cedar_atlas_dead.xml", "scale", 1.0, None),
    "flora_tree_cherry_blossom": ("art/actors/flora/trees/cherryblossom.xml", "scale", 1.0, None),
    "flora_tree_cretan_date_palm_patch": ("art/actors/flora/trees/palm_cretan_date_patch.xml", "scale", 1.0, None),
    "flora_tree_cretan_date_palm_short": ("art/actors/flora/trees/palm_cretan_date_short.xml", "scale", 1.0, None),
    "flora_tree_cretan_date_palm_tall": ("art/actors/flora/trees/palm_cretan_date_tall.xml", "scale", 1.0, None),
    "flora_tree_cypress_wild": ("art/actors/flora/trees/cypress_mediterranean_wild.xml", "scale", 1.0, None),
    "flora_tree_cypress_windswept": ("art/actors/flora/trees/cypress_mediterranean_windswept.xml", "scale", 1.0, None),
    "flora_tree_date_palm": ("art/actors/flora/trees/palm_date_new.xml", "scale", 1.0, None),
    "flora_tree_date_palm_dead": ("art/actors/flora/trees/palm_date_dead.xml", "scale", 1.0, None),
    "flora_tree_elm": ("art/actors/flora/trees/elm.xml", "scale", 1.0, None),
    "flora_tree_elm_dead": ("art/actors/flora/trees/elm_dead.xml", "scale", 1.0, None),
    "flora_tree_euro_beech_aut": ("art/actors/flora/trees/european_beech_aut.xml", "scale", 1.0, None),
    "flora_tree_euro_birch": ("art/actors/flora/trees/euro_birch_tree.xml", "scale", 1.0, None),
    "flora_tree_fir_sapling": ("art/actors/flora/trees/fir_sapling.xml", "scale", 1.0, None),
    "flora_tree_fir_winter": ("art/actors/flora/trees/fir_tree_winter.xml", "scale", 1.0, None),
    "flora_tree_mangrove": ("art/actors/flora/trees/mangrove.xml", "scale", 1.0, None),
    "flora_tree_maple_autumn": ("art/actors/flora/trees/maple_autumn.xml", "scale", 1.0, None),
    "flora_tree_oak_aut": ("art/actors/flora/trees/oak_aut.xml", "scale", 1.0, None),
    "flora_tree_oak_aut_new": ("art/actors/flora/trees/oak_new_aut.xml", "scale", 1.0, None),
    "flora_tree_oak_dead": ("art/actors/flora/trees/oak_dead.xml", "scale", 1.0, None),
    "flora_tree_oak_holly": ("art/actors/flora/trees/oak_holly.xml", "scale", 1.0, None),
    "flora_tree_oak_holly_autumn": ("art/actors/flora/trees/oak_holly_autumn.xml", "scale", 1.0, None),
    "flora_tree_oak_hungarian": ("art/actors/flora/trees/oak_hungarian.xml", "scale", 1.0, None),
    "flora_tree_oak_hungarian_autumn": ("art/actors/flora/trees/oak_hungarian_autumn.xml", "scale", 1.0, None),
    "flora_tree_oak_large": ("art/actors/flora/trees/oak_large.xml", "scale", 1.0, None),
    "flora_tree_palm_areca": ("art/actors/flora/trees/palm_areca.xml", "scale", 1.0, None),
    "flora_tree_palm_doum": ("art/actors/flora/trees/palm_doum.xml", "scale", 1.0, None),
    "flora_tree_palm_palmyra": ("art/actors/flora/trees/palm_palmyra.xml", "scale", 1.0, None),
    "flora_tree_palm_royal": ("art/actors/flora/trees/palm_royal.xml", "scale", 1.0, None),
    "flora_tree_palm_tropical": ("art/actors/flora/trees/palm_tropical.xml", "scale", 1.0, None),
    "flora_tree_pine_black": ("art/actors/flora/trees/pine_black.xml", "scale", 1.0, None),
    "flora_tree_pine_black_dead": ("art/actors/flora/trees/pine_black_dead.xml", "scale", 1.0, None),
    "flora_tree_pine_maritime": ("art/actors/flora/trees/pine_maritime.xml", "scale", 1.0, None),
    "flora_tree_pine_maritime_short": ("art/actors/flora/trees/pine_maritime_short.xml", "scale", 1.0, None),
    "flora_tree_pine_w": ("art/actors/flora/trees/pine_w.xml", "scale", 1.0, None),
    "flora_tree_poplar": ("art/actors/flora/trees/poplar.xml", "scale", 1.0, None),
    "flora_tree_poplar_dead": ("art/actors/flora/trees/poplar_dead.xml", "scale", 1.0, None),
    "flora_tree_poplar_lombardy_autumn": ("art/actors/flora/trees/poplar_autumn.xml", "scale", 1.0, None),
    "flora_tree_poplar_lombardy_dead": ("art/actors/flora/trees/poplar_lombardy_dead.xml", "scale", 1.0, None),
    "flora_tree_senegal_date_palm": ("art/actors/flora/trees/palm_senegal_date.xml", "scale", 1.0, None),
    "flora_tree_strangler": ("art/actors/flora/trees/strangler.xml", "scale", 1.0, None),
    "flora_tree_tamarix": ("art/actors/flora/trees/tamarix.xml", "scale", 1.0, None),
    "flora_tree_temperate": ("art/actors/flora/trees/temperate_forest_biome_tree.xml", "scale", 1.0, None),
    "flora_tree_temperate_autumn": ("art/actors/flora/trees/temperate_forest_biome_tree_autumn.xml", "scale", 1.0, None),
    "flora_tree_temperate_winter": ("art/actors/flora/trees/temperate_forest_biome_tree_winter.xml", "scale", 1.0, None),
    "flora_tree_toona": ("art/actors/flora/trees/tree_tropic.xml", "scale", 1.0, None),
    "flora_tree_tropic_rainforest": ("art/actors/flora/trees/tropic_forest_biome_tree.xml", "scale", 1.0, None),
}

TEMPLATE_BASE = {}
TEMPLATE_DONOR = {}


def register_template_assets(assets, zf):
    """One model per civ roster template whose 0 A.D. actor no extracted model
    uses yet (tools/unit_template_models.py). Returns the per-template plan."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import unit_template_models as utm
    names = set(zf.namelist())
    per_row, new = utm.plan(names, zf.read, assets)
    ranks = utm.rank_plan(names, zf.read, assets, per_row, new)
    for mid, (code, kind, actor) in sorted(new.items()):
        if _register_civ_unit(assets, names, code, kind, actor, aid=mid):
            TEMPLATE_BASE[mid] = kind
            donor = f"{code}_{kind}"
            TEMPLATE_DONOR[mid] = donor if donor in assets else kind
    return per_row, new, ranks


def _same_rig(donor, out_dir, nodes, skins):
    """Whether the extracted `donor` GLB has these skins' joints, by name and
    rest pose, so its clips drive this model exactly."""
    if not donor or not skins:
        return False
    path = None
    for base in (out_dir, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")):
        cand = os.path.join(base, "models", "units", donor + ".glb")
        if os.path.exists(cand):
            path = cand
            break
    if path is None:
        return False
    with open(path, "rb") as f:
        data = f.read()
    jlen = struct.unpack_from("<I", data, 12)[0]
    g = json.loads(data[20:20 + jlen])
    theirs = g.get("skins") or []
    if len(theirs) != len(skins):
        return False
    for mine, other in zip(skins, theirs):
        if len(mine["joints"]) != len(other["joints"]):
            return False
        for a, b in zip(mine["joints"], other["joints"]):
            na, nb = nodes[a], g["nodes"][b]
            if na.get("name") != nb.get("name"):
                return False
            ta, tb = na.get("translation", [0, 0, 0]), nb.get("translation", [0, 0, 0])
            qa, qb = na.get("rotation", [0, 0, 0, 1]), nb.get("rotation", [0, 0, 0, 1])
            if max(abs(x - y) for x, y in zip(ta, tb)) > 1e-3:
                return False
            if min(max(abs(x - y) for x, y in zip(qa, qb)), max(abs(x + y) for x, y in zip(qa, qb))) > 1e-3:
                return False
    return True


def _apply_civ_unit_overrides():
    """Civ actors that do not share the generic UNIT_CFG skeleton/clips."""
    # Mauryan trader: 4-bone maur_ch cart + zebu pair + seated driver.
    UNIT_CFG["maur_trader"] = ("maur_ch", {
        "Idle": "maur_ch_idle", "Walk": "maur_ch_move", "Run": "maur_ch_move",
        "Death": "maur_ch_death"})
    RIDER_ATTACHMENTS["maur_trader"] = [
        ("art/actors/units/mauryas/trader_r.xml", "driver", "maur_trader_rider_idle"),
        ("art/actors/fauna/zebu.xml", "horseright", "zebu_idle"),
        ("art/actors/fauna/zebu.xml", "horseleft", "zebu_idle"),
    ]
    RIDER_CLIPS["maur_trader"] = {
        "Idle": "maur_trader_rider_idle", "Walk": "maur_trader_rider_walk",
        "Run": "maur_trader_rider_walk", "Death": "maur_trader_rider_death",
    }
    # Mauryan hull has no sail DAE. Keep ship_sail clips and inject a sail mesh
    # so idle/move animate the cloth; the 0-bone hull stays origin-locked.
    # Mauryan fishing hull is static; keep the fisherman as the live rider.
    RIDER_ATTACHMENTS["maur_fishing_boat"] = [
        ("art/actors/units/mauryas/fisherman.xml", "fisherman", "fisherman_idle"),
    ]
    # Mauryan war elephant is the elephant archer (howdah crew sit-baked).
    RIDER_ATTACHMENTS["maur_elephant"] = [
        ("art/actors/units/mauryas/elephantry_archer_d.xml", "rider", "elephant_driver_idle"),
        ("art/actors/units/mauryas/elephantry_archer_r1.xml", "rider1", "elephant_archer_ready"),
        ("art/actors/units/mauryas/elephantry_archer_r2.xml", "rider2", "elephant_archer_ready"),
        ("art/actors/units/mauryas/elephantry_archer_r3.xml", "rider3", "elephant_archer_ready"),
    ]
    RIDER_CLIPS.pop("maur_elephant", None)


def register_civ_building_assets(assets, zf):
    """Add `{code}_{building}` entries from each civ's 0 A.D. structure actors."""
    names = set(zf.namelist())
    added = []
    for code, folders in CIV_ART_FOLDERS.items():
        for bid, filenames in BLD_ACTOR_NAMES.items():
            if bid not in ASSETS:
                continue
            actor = None
            for folder in folders:
                for fn in filenames:
                    path = f"art/actors/structures/{folder}/{fn}"
                    if path in names:
                        actor = path
                        break
                if actor:
                    break
            if actor is None:
                actor = BLD_GLOBAL_FALLBACK.get(bid) or ASSETS[bid][0]
            if code == "germ" and bid == "wall_tower":
                actor = "art/actors/structures/germans/wooden_wall_tower.xml"
            aid = f"{code}_{bid}"
            _path, fit, target, tint = ASSETS[bid]
            assets[aid] = (actor, fit, target, tint)
            added.append(aid)
    for aid, row in ZIP_STRUCTURES.items():
        if row[0] in names:
            assets[aid] = row
            added.append(aid)
    return added


# G7-H2 curated batch: HIGH-VISIBILITY classes still missing from the manifest.
# Map fauna (animated gaia animals placed on maps), landmark/civic statics
# (wonders + athenian civic set used on maps), flora variety (biome trees).
# Civ-skin duplicates of already-converted models are deliberately excluded.
G7_CLASSSETS = {
    "g7h2": [
        "goat", "zebra", "chicken", "crocodile", "giraffe", "rabbit",
        "gazelle", "bear", "cow", "pig", "lion", "lioness", "donkey",
        "fox", "hawk",
        "pyramid", "pyramid_minor", "ishtar_gate", "greek_stoa",
        "greek_stoa_great", "theater", "gymnasium", "stoa",
        "prytaneion", "propylaea", "lighthouse",
        "oak", "acacia", "baobab", "bamboo", "apple",
    ],
    "fauna": [
        "goat", "zebra", "chicken", "crocodile", "giraffe", "rabbit",
        "gazelle", "bear", "cow", "pig", "lion", "lioness", "donkey",
        "fox", "hawk",
    ],
    "landmarks": [
        "pyramid", "pyramid_minor", "ishtar_gate", "greek_stoa",
        "greek_stoa_great", "theater", "gymnasium", "stoa",
        "prytaneion", "propylaea", "lighthouse",
    ],
    "flora": ["oak", "acacia", "baobab", "bamboo", "apple"],
    # G7-M: siege engines (animated) + towers/walls/docks + production/civics.
    # Generic ram/wall/dock/catapult/bolt_shooter + civ skins already ship, so
    # only the still-missing distinct silhouettes are listed here (29 models).
    "g7m": [
        "siege_ballista", "siege_ballista_med", "siege_lithobolos",
        "siege_lithobolos_light", "siege_oxybeles", "siege_polybolos",
        "siege_scorpio", "siege_onager", "siege_mangonel", "siege_shoushe",
        "siege_tower",
        "siege_wall", "ballista_tower", "sentry_tower", "defense_tower",
        "outpost", "super_dock", "blacksmith", "workshop", "arsenal",
        "library", "amphitheater", "triumphal_arch", "mausoleum", "shrine",
        "palace", "embassy", "academy", "great_hall",
    ],
    # G7-Q: warship hulls (bireme/quinquereme/pentekonter + nordic/celtic
    # boats + han merchants) + smith/forge/field/harbor statics + hound,
    # elephant and chariot variants. Distinct silhouettes only; civ-skin
    # dupes, *_top LODs and decals are excluded (32 models).
    "g7q": [
        "bireme", "bireme_rome", "quinquereme", "quinquereme_rome",
        "pentekonter", "trireme_han", "bireme_han",
        "ship_knarr", "ship_snekkja", "ship_karvi",
        "skiff", "rowboat", "merchant_han", "fishing_han",
        "blacksmith_brit", "blacksmith_kart", "forge_germ", "forge_han",
        "field_plot", "orchard_plot", "crannog",
        "wooden_tower", "scout_tower", "tower_bolt", "tower_artillery",
        "stable_elephant", "kennel",
        "wolfhound", "mastiff_gaia", "elephant_bush", "elephant_forest",
        "chariot_han",
    ],
    "siege": [
        "siege_ballista", "siege_ballista_med", "siege_lithobolos",
        "siege_lithobolos_light", "siege_oxybeles", "siege_polybolos",
        "siege_scorpio", "siege_onager", "siege_mangonel", "siege_shoushe",
        "siege_tower",
    ],
    "walls": [
        "siege_wall", "ballista_tower", "sentry_tower", "defense_tower",
        "outpost",
    ],
    "docks": ["super_dock"],
}


FULL_ASSETS = dict(ASSETS)

TERRAIN_TILES = {  # out name -> terrain types/<biome>/<tile>
    "grass":         "types/aegean_anatolia/grass_01.png",
    "dirt":          "types/aegean_anatolia/dirt_01.png",
    "rock":          "types/aegean_anatolia/cliff_01.png",
    "sand":          "types/aegean_anatolia/sand_01.png",
    "snow":          "types/alpine_arctic/snow_01.png",
    "alpine_snow":   "types/alpine_arctic/snow_01.png",
    "desert_sand":   "types/african_savanna/dirt_01.png",
    "savanna_mud":   "types/african_savanna/mud_01.png",
    "savanna_grass": "types/african_savanna/grass_dirt_03.png",
    "medit_sand":    "types/aegean_anatolia/sand_01.png",
}

FULL_TERRAIN = dict(TERRAIN_TILES)

_ARCHIVE_ACTOR_ROOTS = ("units", "structures", "flora", "fauna", "geology")
_ARCHIVE_SKIP_SUBSTR = ("/particle/", "/decals/", "/destruction/", "destruct")
_TERRAIN_SKIP_SUFFIX = ("_norm", "_spec", "_bump", "_height", "_normal", "_ao")


def discover_terrain_tiles(zf):
    """Every albedo under art/textures/terrain/types/**, keyed for PNG dump."""
    tiles = dict(TERRAIN_TILES)
    tiles.update(FULL_TERRAIN)
    prefix = "art/textures/terrain/types/"
    for n in zf.namelist():
        if not n.startswith(prefix):
            continue
        if not n.endswith(".cached.dds"):
            continue
        rest = n[len(prefix):-len(".cached.dds")]
        stem, ext = os.path.splitext(rest)
        low = stem.lower()
        if any(low.endswith(s) for s in _TERRAIN_SKIP_SUFFIX):
            continue
        ref = "types/" + rest
        name = stem.replace("/", "_").replace("\\", "_")
        if name in tiles:
            continue
        tiles[name] = ref
    return tiles


def discover_archive_actors(zf, already_refs, already_ids):
    """Extra actors under units/structures/flora/fauna/geology for models/archive/."""
    extras = []
    used = set(already_ids)
    for n in zf.namelist():
        if not n.startswith("art/actors/") or not n.endswith(".xml"):
            continue
        parts = n.split("/")
        if len(parts) < 4 or parts[2] not in _ARCHIVE_ACTOR_ROOTS:
            continue
        low = n.lower()
        if any(s in low for s in _ARCHIVE_SKIP_SUBSTR):
            continue
        if n in already_refs:
            continue
        rel = n[len("art/actors/"):-len(".xml")].replace("/", "_").replace(" ", "_")
        aid = rel
        n_id = 2
        while aid in used:
            aid = f"{rel}_{n_id}"
            n_id += 1
        used.add(aid)
        extras.append((aid, n))
    return extras


def _archive_fit(actor_ref):
    folder = actor_ref.split("/")[2] if actor_ref.startswith("art/actors/") else ""
    if folder == "structures":
        return "width", 5.0
    if folder == "geology":
        return "width", 2.0
    if folder == "flora":
        return "height", 3.5
    if folder == "fauna":
        return "height", 1.4
    return "height", 1.8


def _glb_out_path(out_dir, asset_id, rel_dir=None):
    folder = rel_dir or asset_dir(asset_id)
    return os.path.join(out_dir, folder, f"{asset_id}.glb")

MUSIC = {"theme.ogg": "audio/music/In_the_Shadow_of_Olympus.ogg",
         "victory.ogg": "audio/music/You_are_Victorious!.ogg",
         "defeat.ogg": "audio/music/Dried_Tears.ogg"}

# Skeletal animations — PSSA (PSA) files for biped/mechanical skeletons
# Mirrors 0ad art/animation/* + art/skeletons/*; used by Godot Skeleton3D + AnimationPlayer
# We copy the cached PSSA binary and also generate JSON for debugging
ANIMATIONS = {
    # biped infantry (swordsman/spearman/archer/hero/carnyx)
    "biped_idle":       "art/animation/biped/infantry/swordsman/idle_relax_shield_01.dae.cached.psa",
    "biped_walk":       "art/animation/biped/infantry/swordsman/walk_relax_shield.dae.cached.psa",
    "biped_run":        "art/animation/biped/infantry/swordsman/run_relax_shield.dae.cached.psa",
    "biped_attack":     "art/animation/biped/infantry/swordsman/attack_melee_shield_01.dae.cached.psa",
    "biped_death":      "art/animation/biped/infantry/death_a.dae.cached.psa",
    # citizen / gatherer (male & female)
    "citizen_idle":     "art/animation/biped/citizen/idle_relax_m.dae.cached.psa",
    "citizen_walk":     "art/animation/biped/citizen/walk_relax_m.dae.cached.psa",
    "citizen_run":      "art/animation/biped/citizen/jog_m.dae.cached.psa",
    "female_idle":      "art/animation/biped/citizen/idle_relax_f.dae.cached.psa",
    "female_walk":      "art/animation/biped/citizen/walk_relax_f.dae.cached.psa",
    "female_run":       "art/animation/biped/citizen/jog_f.dae.cached.psa",
    "slaughter":        "art/animation/biped/gatherer/slaughter.dae.cached.psa",
    "gather_wood":      "art/animation/biped/gatherer/gather_wood.dae.cached.psa",
    "citizen_build":    "art/animation/biped/citizen/build.dae.cached.psa",
    "gather_mine":      "art/animation/biped/gatherer/gather_mine.dae.cached.psa",
    "gather_berries":   "art/animation/biped/gatherer/gather_berries.dae.cached.psa",
    "gather_fruit_m":   "art/animation/biped/gatherer/gather_fruit_m.dae.cached.psa",
    "gather_fruit_f":   "art/animation/biped/gatherer/gather_fruit_f.dae.cached.psa",
    "gather_grain":     "art/animation/biped/gatherer/farming.dae.cached.psa",
    "gather_meat":      "art/animation/biped/gatherer/gather_meat.dae.cached.psa",
    "carry_wood":       "art/animation/biped/gatherer/carry_wood_m.dae.cached.psa",
    "carry_ore":        "art/animation/biped/gatherer/carry_ore_m.dae.cached.psa",
    # archer specific
    "archer_attack":    "art/animation/biped/infantry/archer/attack_ranged.dae.cached.psa",
    # cavalry / horse
    "cavalry_idle":     "art/animation/biped/rider/cavalry/generic/idle_relax_01.dae.cached.psa",
    "cavalry_walk":     "art/animation/biped/rider/cavalry/generic/walk_relax_shield.dae.cached.psa",
    "cavalry_attack":   "art/animation/biped/rider/cavalry/generic/attack_melee_01.dae.cached.psa",
    "cavalry_death":    "art/animation/biped/rider/cavalry/generic/death_01.dae.cached.psa",
    "horse_idle":       "art/animation/quadraped/horse_idle_01.dae.cached.psa",
    "horse_walk":       "art/animation/quadraped/horse_walk.dae.cached.psa",
    "horse_run":        "art/animation/quadraped/horse_gallop.dae.cached.psa",
    "horse_attack":     "art/animation/quadraped/horse_attack_01.dae.cached.psa",
    "horse_death":      "art/animation/quadraped/horse_death.dae.cached.psa",
    # camel
    "camel_idle":       "art/animation/quadraped/camel_idle_01.dae.cached.psa",
    "camel_walk":       "art/animation/quadraped/camel_walk.dae.cached.psa",
    "camel_run":        "art/animation/quadraped/camel_run.dae.cached.psa",
    "camel_attack":     "art/animation/quadraped/camel_attack_melee_01.dae.cached.psa",
    "camel_death":      "art/animation/quadraped/camel_death.dae.cached.psa",
    # elephant
    "elephant_idle":    "art/animation/quadraped/elephant_asian_idle_01.dae.cached.psa",
    "elephant_walk":    "art/animation/quadraped/elephant_asian_walk.dae.cached.psa",
    "elephant_run":     "art/animation/quadraped/elephant_asian_run.dae.cached.psa",
    "elephant_attack":  "art/animation/quadraped/elephant_asian_attack_melee_01.dae.cached.psa",
    "elephant_death":   "art/animation/quadraped/elephant_asian_death.dae.cached.psa",
    # fauna
    "deer_idle":        "art/animation/quadraped/deer_idle_01.dae.cached.psa",
    "deer_walk":        "art/animation/quadraped/deer_walk_01.dae.cached.psa",
    "deer_death":       "art/animation/quadraped/deer_death_01.dae.cached.psa",
    "boar_idle":        "art/animation/quadraped/animal_boar_idle_01.dae.cached.psa",
    "boar_walk":        "art/animation/quadraped/animal_boar_walk_01.dae.cached.psa",
    "boar_run":         "art/animation/quadraped/animal_boar_run_01.dae.cached.psa",
    "boar_attack":      "art/animation/quadraped/animal_boar_attack_01.dae.cached.psa",
    "boar_death":       "art/animation/quadraped/animal_boar_death_01.dae.cached.psa",
    "wolf_idle":        "art/animation/quadraped/wolf_idle_01.dae.cached.psa",
    "wolf_walk":        "art/animation/quadraped/wolf_walk.dae.cached.psa",
    "wolf_run":         "art/animation/quadraped/wolf_run.dae.cached.psa",
    "wolf_attack":      "art/animation/quadraped/wolf_attack_01.dae.cached.psa",
    "wolf_death":       "art/animation/quadraped/wolf_death_01.dae.cached.psa",
    "sheep_idle":       "art/animation/quadraped/sheep_idle_01.dae.cached.psa",
    "sheep_walk":       "art/animation/quadraped/sheep_walk.dae.cached.psa",
    "sheep_run":        "art/animation/quadraped/sheep_run.dae.cached.psa",
    "sheep_death":      "art/animation/quadraped/sheep_death.dae.cached.psa",
    "dog_idle":         "art/animation/quadraped/mastiff_idle.dae.cached.psa",
    "dog_walk":         "art/animation/quadraped/mastiff_run.dae.cached.psa",
    "dog_run":          "art/animation/quadraped/mastiff_run.dae.cached.psa",
    "dog_attack":       "art/animation/quadraped/mastiff_attack.dae.cached.psa",
    "dog_death":        "art/animation/quadraped/mastiff_death.dae.cached.psa",
    # G7-H2 map-fauna batch (bone counts verified vs skeleton xml: psa_num_keys == len(skeleton))
    "goat_idle":        "art/animation/quadraped/goat_idle_01.dae.cached.psa",
    "goat_walk":        "art/animation/quadraped/goat_walk.dae.cached.psa",
    "goat_death":       "art/animation/quadraped/goat_death.dae.cached.psa",
    "zebra_idle":       "art/animation/quadraped/zebra_idle.dae.cached.psa",
    "zebra_walk":       "art/animation/quadraped/zebra_walk.dae.cached.psa",
    "zebra_run":        "art/animation/quadraped/zebra_run.dae.cached.psa",
    "zebra_attack":     "art/animation/quadraped/zebra_attack.dae.cached.psa",
    "zebra_death":      "art/animation/quadraped/zebra_death.dae.cached.psa",
    "chicken_idle":     "art/animation/quadraped/chicken_idle_01.dae.cached.psa",
    "chicken_walk":     "art/animation/quadraped/chicken_walk.dae.cached.psa",
    "chicken_run":      "art/animation/quadraped/chicken_run_01.dae.cached.psa",
    "chicken_attack":   "art/animation/quadraped/chicken_attack.dae.cached.psa",
    "chicken_death":    "art/animation/quadraped/chicken_death.dae.cached.psa",
    "croc_idle":        "art/animation/quadraped/Croc_idle1.dae.cached.psa",
    "croc_walk":        "art/animation/quadraped/Croc_walk.dae.cached.psa",
    "croc_run":         "art/animation/quadraped/Croc_run.dae.cached.psa",
    "croc_attack":      "art/animation/quadraped/Croc_attack.dae.cached.psa",
    "croc_death":       "art/animation/quadraped/Croc_death.dae.cached.psa",
    "giraffe_idle":     "art/animation/quadraped/giraffe_adult_idle_01.dae.cached.psa",
    "giraffe_walk":     "art/animation/quadraped/giraffe_adult_walk.dae.cached.psa",
    "giraffe_run":      "art/animation/quadraped/giraffe_adult_run.dae.cached.psa",
    "giraffe_death":    "art/animation/quadraped/giraffe_adult_death.dae.cached.psa",
    "rabbit_idle":      "art/animation/quadraped/rabbit_idle_01.dae.cached.psa",
    "rabbit_walk":      "art/animation/quadraped/rabbit_walk.dae.cached.psa",
    "rabbit_death":     "art/animation/quadraped/rabbit_death.dae.cached.psa",
    "bear_idle":        "art/animation/quadraped/bear_idle_01.dae.cached.psa",
    "bear_walk":        "art/animation/quadraped/bear_walk.dae.cached.psa",
    "bear_run":         "art/animation/quadraped/bear_run.dae.cached.psa",
    "bear_attack":      "art/animation/quadraped/bear_attack_01.dae.cached.psa",
    "bear_death":       "art/animation/quadraped/bear_death_01.dae.cached.psa",
    "cow_idle":         "art/animation/quadraped/bovidae_idle_a.dae.cached.psa",
    "cow_walk":         "art/animation/quadraped/bovidae_walk_relax.dae.cached.psa",
    "cow_run":          "art/animation/quadraped/bovidae_run.dae.cached.psa",
    "cow_attack":       "art/animation/quadraped/bovidae_attack.dae.cached.psa",
    "cow_death":        "art/animation/quadraped/bovidae_death_a.dae.cached.psa",
    "pig_idle":         "art/animation/quadraped/animal_pig_idle_01.dae.cached.psa",
    "pig_walk":         "art/animation/quadraped/animal_pig_walk_01.dae.cached.psa",
    "pig_run":          "art/animation/quadraped/animal_pig_run_01.dae.cached.psa",
    "pig_attack":       "art/animation/quadraped/animal_pig_attack_01.dae.cached.psa",
    "pig_death":        "art/animation/quadraped/animal_pig_death_01.dae.cached.psa",
    "fox_idle":         "art/animation/quadraped/fox_idle.dae.cached.psa",
    "fox_walk":         "art/animation/quadraped/fox_walk.dae.cached.psa",
    "fox_run":          "art/animation/quadraped/fox_run.dae.cached.psa",
    "fox_death":        "art/animation/quadraped/fox_death.dae.cached.psa",
    "hawk_idle":        "art/animation/other/hawk_flight.dae.cached.psa",
    "hawk_walk":        "art/animation/other/hawk_glide.dae.cached.psa",
    "lion_idle":        "art/animation/quadraped/lion_idle_01.dae.cached.psa",
    "lion_walk":        "art/animation/quadraped/lion_walk.dae.cached.psa",
    "lion_run":         "art/animation/quadraped/lion_run.dae.cached.psa",
    "lion_attack":      "art/animation/quadraped/lion_idle_01.dae.cached.psa",
    "lion_death":       "art/animation/quadraped/lion_death.dae.cached.psa",
    "donkey_idle":      "art/animation/quadraped/donkey_idle_01.dae.cached.psa",
    "donkey_walk":      "art/animation/quadraped/donkey_walk_01.dae.cached.psa",
    "donkey_death":     "art/animation/quadraped/donkey_death_01.dae.cached.psa",
    "deer_attack":      "art/animation/quadraped/deer_attack_01.dae.cached.psa",
    "deer_run":         "art/animation/quadraped/deer_run_01.dae.cached.psa",
    # healer (biped 46 — citizen healer set, not infantry relax)
    "healer_idle":      "art/animation/biped/citizen/idle_relax_h_m.dae.cached.psa",
    "healer_walk":      "art/animation/biped/citizen/walk_relax_h_m.dae.cached.psa",
    "healer_run":       "art/animation/biped/citizen/jog_h.dae.cached.psa",
    "healer_heal":      "art/animation/biped/citizen/healing_m.dae.cached.psa",
    # mechanical / siege
    "ram_idle":         "art/animation/mechanical/rome_ram_idle.dae.cached.psa",
    "ram_walk":         "art/animation/mechanical/rome_ram_move.dae.cached.psa",
    "ram_attack":       "art/animation/mechanical/rome_ram_attack.dae.cached.psa",
    # 8-bone death (Mauryan/German rams). Rome/Celt actors use particles only.
    "ram_death":        "art/animation/mechanical/maur_siege_ram_death.dae.cached.psa",
    "trader_idle":      "art/animation/mechanical/trader_cart_idle.dae.cached.psa",
    "trader_walk":      "art/animation/mechanical/trader_cart_move.dae.cached.psa",
    "trader_death":     "assets/animations/trader_death.psa",
    "catapult_idle":    "art/animation/mechanical/siege_artillery/lithobolos_med_idle_01.dae.cached.psa",
    "catapult_attack":  "art/animation/mechanical/siege_artillery/lithobolos_med_attack_ranged_01.dae.cached.psa",
    "bolt_idle":        "art/animation/mechanical/siege_artillery/oxybeles_idle_01.dae.cached.psa",
    "bolt_attack":      "art/animation/mechanical/siege_artillery/oxybeles_attack_ranged_01.dae.cached.psa",
    # mechanical chariot (15 bones)
    "chariot_idle":     "art/animation/mechanical/chariot_idle.dae.cached.psa",
    "chariot_walk":     "art/animation/mechanical/chariot_move.dae.cached.psa",
    "chariot_run":      "art/animation/mechanical/chariot_run.dae.cached.psa",
    "chariot_death":    "art/animation/mechanical/chariot_death.dae.cached.psa",
    # sit-bake clips for mount crew (46-bone biped, frame 0)
    "camel_rider_idle": "art/animation/biped/rider/camelry/archer/idle_relax_01.dae.cached.psa",
    "camel_rider_walk": "art/animation/biped/rider/camelry/archer/trot_relax.dae.cached.psa",
    "camel_rider_run":  "art/animation/biped/rider/camelry/archer/run_relax.dae.cached.psa",
    "camel_rider_attack": "art/animation/biped/rider/camelry/archer/attack_ranged_right.dae.cached.psa",
    "elephant_driver_idle": "art/animation/biped/rider/elephantry/driver/idle_relax.dae.cached.psa",
    "elephant_driver_attack": "art/animation/biped/rider/elephantry/driver/attack_melee.dae.cached.psa",
    "trader_rider_idle": "art/animation/biped/citizen/idle_relax_h_f.dae.cached.psa",
    "trader_rider_walk": "art/animation/biped/citizen/walk_relax_t.dae.cached.psa",
    "trader_rider_death": "art/animation/biped/chariot/maur_trader/death.dae.cached.psa",
    "maur_ch_idle":     "art/animation/mechanical/maur_ch_idle.dae.cached.psa",
    "maur_ch_move":     "art/animation/mechanical/maur_ch_move.dae.cached.psa",
    "maur_ch_death":    "art/animation/mechanical/maur_ch_death.dae.cached.psa",
    "maur_trader_rider_idle": "art/animation/biped/chariot/maur_trader/idle.dae.cached.psa",
    "maur_trader_rider_walk": "art/animation/biped/chariot/maur_trader/move_a.dae.cached.psa",
    "maur_trader_rider_death": "art/animation/biped/chariot/maur_trader/death.dae.cached.psa",
    "zebu_idle":        "art/animation/quadraped/zebu_idle01.dae.cached.psa",
    "elephant_archer_ready": "art/animation/biped/rider/elephantry/archer/r1_idle_ready.dae.cached.psa",
    "elephant_archer_walk": "art/animation/biped/rider/elephantry/archer/r1_walk_ready.dae.cached.psa",
    "elephant_archer_attack": "art/animation/biped/rider/elephantry/archer/r1_attack_ranged_bow.dae.cached.psa",
    "elephant_archer_death": "art/animation/biped/rider/elephantry/archer/r1_death_archer.dae.cached.psa",
    "fisherman_idle":   "art/animation/biped/new/boat_fisherman_idle.dae.cached.psa",
    "trireme_idle":     "art/animation/other/main_sail_idle.dae.cached.psa",
    "trireme_walk":     "art/animation/other/main_sail_move.dae.cached.psa",
    "merchant_sail_idle": "art/animation/others/celt_merchant_sail_idle.dae.cached.psa",
    "merchant_sail_walk": "art/animation/others/celt_merchant_sail_move.dae.cached.psa",
    "merchant_sail_run":  "art/animation/others/celt_merchant_sail_run.dae.cached.psa",
    "fisherman_gather": "art/animation/biped/new/boat_fisherman_gather_fish.dae.cached.psa",
    "rowing_idle":      "art/animation/mechanical/rowing_boat_idle.dae.cached.psa",
    "rowing_walk":      "art/animation/mechanical/rowing_boat_move.dae.cached.psa",
    "chariot_driver_idle": "art/animation/biped/chariot/idle.dae.cached.psa",
    "chariot_driver_celt": "art/animation/biped/chariot/idle_celt.dae.cached.psa",
    # G7-M siege-engine clips (mesh bones == PSA keys verified per engine).
    "rome_ballista_idle":  "art/animation/mechanical/siege_artillery/rome_ballista_idle_01.dae.cached.psa",
    "rome_ballista_attack": "art/animation/mechanical/siege_artillery/rome_ballista_attack_ranged_01.dae.cached.psa",
    "rome_ballista_med_idle": "art/animation/mechanical/siege_artillery/rome_ballista_med_idle_01.dae.cached.psa",
    "rome_ballista_med_attack": "art/animation/mechanical/siege_artillery/rome_ballista_med_attack_ranged_01.dae.cached.psa",
    "lithobolos_idle":    "art/animation/mechanical/siege_artillery/lithobolos_idle_01.dae.cached.psa",
    "lithobolos_attack":  "art/animation/mechanical/siege_artillery/lithobolos_attack_ranged_01.dae.cached.psa",
    "lithobolos_light_idle": "art/animation/mechanical/siege_artillery/lithobolos_light_idle_01.dae.cached.psa",
    "lithobolos_light_attack": "art/animation/mechanical/siege_artillery/lithobolos_light_attack_ranged_01.dae.cached.psa",
    "lithobolos_light_death": "art/animation/mechanical/siege_artillery/lithobolos_light_death_01.dae.cached.psa",
    "polybolos_idle":     "art/animation/mechanical/siege_artillery/polybolos_idle_01.dae.cached.psa",
    "polybolos_attack":   "art/animation/mechanical/siege_artillery/polybolos_attack_ranged_01.dae.cached.psa",
    "scorpio_idle":       "art/animation/mechanical/siege_artillery/scorpio_idle_01.dae.cached.psa",
    "scorpio_attack":     "art/animation/mechanical/siege_artillery/scorpio_attack_ranged_01.dae.cached.psa",
    "onager_idle":        "art/animation/mechanical/siege_onager_idle.dae.cached.psa",
    "onager_attack":      "art/animation/mechanical/siege_onager_attack.dae.cached.psa",
    "han_mangonel_idle":  "art/animation/mechanical/han_mangonel_idle.dae.cached.psa",
    "han_mangonel_attack": "art/animation/mechanical/han_mangonel_attack.dae.cached.psa",
    "han_shoushe_idle":   "art/animation/mechanical/han_shoushe_idle.dae.cached.psa",
    "han_shoushe_walk":   "art/animation/mechanical/han_shoushe_walk.dae.cached.psa",
    "han_shoushe_attack": "art/animation/mechanical/han_shoushe_attack.dae.cached.psa",
    "han_shoushe_death":  "art/animation/mechanical/han_shoushe_death.dae.cached.psa",
    "hele_siege_tower_idle": "art/animation/mechanical/hele_siege_tower_idle.dae.cached.psa",
    "hele_siege_tower_walk": "art/animation/mechanical/hele_siege_tower_move.dae.cached.psa",
    "chariot_jav_ready": "art/animation/biped/infantry/javelinist/idle_ready.dae.cached.psa",
    "chariot_archer_ready": "art/animation/biped/infantry/archer/idle_ready.dae.cached.psa",
    # G7-Q hound/elephant/chariot variants (mesh bones == skeleton == PSA keys).
    "hound_idle":       "art/animation/quadraped/wolfhound_idle1.dae.cached.psa",
    "hound_walk":       "art/animation/quadraped/wolfhound_walk.dae.cached.psa",
    "hound_attack":     "art/animation/quadraped/wolfhound_attack.dae.cached.psa",
    "hound_death":      "art/animation/quadraped/wolfhound_death.dae.cached.psa",
    "elephant_bush_idle": "art/animation/quadraped/elephant_african_bush_idle_01.dae.cached.psa",
    "elephant_bush_walk": "art/animation/quadraped/elephant_african_bush_walk.dae.cached.psa",
    "elephant_bush_run": "art/animation/quadraped/elephant_african_bush_run.dae.cached.psa",
    "elephant_bush_attack": "art/animation/quadraped/elephant_african_bush_attack_melee_01.dae.cached.psa",
    "elephant_bush_death": "art/animation/quadraped/elephant_african_bush_death.dae.cached.psa",
    "elephant_forest_idle": "art/animation/quadraped/elephant_african_forest_idle_01.dae.cached.psa",
    "elephant_forest_walk": "art/animation/quadraped/elephant_african_forest_walk.dae.cached.psa",
    "elephant_forest_run": "art/animation/quadraped/elephant_african_forest_run.dae.cached.psa",
    "elephant_forest_attack": "art/animation/quadraped/elephant_african_forest_attack_melee_01.dae.cached.psa",
    "elephant_forest_death": "art/animation/quadraped/elephant_african_forest_death.dae.cached.psa",
}

SKELETONS = {
    "biped": "art/skeletons/biped.xml",
    "horse": "art/skeletons/horse.xml",
    "deer": "art/skeletons/deer.xml",
    "camel": "art/skeletons/camelus.xml",
    "elephant": "art/skeletons/elephantidae_asian.xml",
    "chariot": "art/skeletons/chariot.xml",
    "lithobolos": "art/skeletons/lithobolos_light.xml",
    "oxybeles": "art/skeletons/oxybeles.xml",
    "maur_ch": "art/skeletons/maur_ch.xml",
    "rowing_boat": "art/skeletons/rowing_boat.xml",
    "ship_sail": "art/skeletons/ship_sail.xml",
    "celt_sail": "art/skeletons/celt_sail.xml",
    "boar": "art/skeletons/boar.xml",
    "wolf": "art/skeletons/wolf.xml",
    "sheep": "art/skeletons/sheep.xml",
    "mastiff": "art/skeletons/mastiff.xml",
    # G7-H2 map-fauna skeletons (mesh bones == skeleton bones == PSA keys, verified)
    "goat": "art/skeletons/goat.xml",
    "zebra": "art/skeletons/zebra.xml",
    "chicken": "art/skeletons/chicken.xml",
    "crocodile": "art/skeletons/crocodile.xml",
    "giraffe": "art/skeletons/giraffe.xml",
    "bear": "art/skeletons/ursidae_armature.xml",
    "cow": "art/skeletons/bovidae_skeleton.xml",
    "pig": "art/skeletons/boar.xml",
    "fox": "art/skeletons/fox_armature.xml",
    "hawk": "art/skeletons/hawk.xml",
    "lion": "art/skeletons/horse_old.xml",
    "donkey": "art/skeletons/horse_old.xml",
    # G7-M siege-engine skeletons (mesh bones == skeleton bones == PSA keys).
    "ballista_frame": "art/skeletons/ballista.xml",
    "scorpio_frame": "art/skeletons/scorpio.xml",
    "polybolos_frame": "art/skeletons/polybolos.xml",
    "onager_frame": "art/skeletons/siege_onager.xml",
    "mangonel_frame": "art/skeletons/han_mangonel.xml",
    "shoushe_frame": "art/skeletons/han_shoushe.xml",
    "hele_siege_tower": "art/skeletons/hele_lithobolos.xml",
    # G7-Q variant skeletons (mesh bones == skeleton bones == PSA keys).
    "wolfhound": "art/skeletons/wolfhound.xml",
    "elephant_bush": "art/skeletons/elephantidae_african_bush.xml",
    "elephant_forest": "art/skeletons/elephantidae_african.xml",
    "ship_one_row": "art/skeletons/ship_one_row.xml",
}

# Mount *_m XML already props crew onto attachpoints. compose_actor skips
# these actors, then attach_rider_parts sit-bakes each once. Otherwise the
# GLB gets a standing bind-pose copy plus a posed copy.
# (actor_path, attachpoint, sit_psa_key)
RIDER_ATTACHMENTS = {
    "cavalry": [
        ("art/actors/units/athenians/cavalry_swordsman_b_r.xml", "rider", "cavalry_idle"),
    ],
    "camel": [
        ("art/actors/units/ptolemies/camel_archer_a_r.xml", "rider", "camel_rider_idle"),
    ],
    "elephant": [
        ("art/actors/units/carthaginians/elephantry_c_d.xml", "rider", "elephant_driver_idle"),
    ],
    "worker_elephant": [
        ("art/actors/units/mauryas/support_elephant_d.xml", "rider", "elephant_driver_idle"),
    ],
    "chariot": [
        ("art/actors/units/britons/chariot_javelinist_c_r.xml", "rider3", "chariot_jav_ready"),
        ("art/actors/units/britons/chariot_javelinist_c_d.xml", "driver2_mid", "chariot_driver_celt"),
    ],
    "scythed_chariot": [
        ("art/actors/units/persians/chariot_archer_a_r.xml", "rider1", "chariot_archer_ready"),
        ("art/actors/units/persians/chariot_archer_a_d.xml", "driver1", "chariot_driver_idle"),
    ],
    "trader": [
        ("art/actors/units/hellenes/trader_r.xml", "trader", "trader_rider_idle"),
    ],
    "fishing_boat": [
        ("art/actors/units/hellenes/fisherman.xml", "fisherman", "fisherman_idle"),
    ],
}

# Live rider clips (second skin). Attack stays a sitting human, not the animal.
RIDER_CLIPS = {
    "cavalry": {
        "Idle": "cavalry_idle", "Walk": "cavalry_walk", "Run": "cavalry_walk",
        "Attack": "cavalry_attack", "Death": "cavalry_death",
    },
    "camel": {
        "Idle": "camel_rider_idle", "Walk": "camel_rider_walk", "Run": "camel_rider_run",
        "Attack": "camel_rider_attack", "Death": "camel_rider_idle",
    },
    "elephant": {
        "Idle": "elephant_driver_idle", "Walk": "elephant_driver_idle",
        "Run": "elephant_driver_idle", "Attack": "elephant_driver_attack",
        "Death": "elephant_driver_idle",
    },
    "worker_elephant": {
        "Idle": "elephant_driver_idle", "Walk": "elephant_driver_idle",
        "Run": "elephant_driver_idle", "Attack": "elephant_driver_attack",
        "Death": "elephant_driver_idle",
    },
    "trader": {
        "Idle": "trader_rider_idle", "Walk": "trader_rider_walk", "Run": "trader_rider_walk",
        "Death": "trader_rider_death",
    },
    "fishing_boat": {
        "Idle": "fisherman_idle", "Walk": "fisherman_idle", "Run": "fisherman_idle",
        "Gather": "fisherman_gather", "Attack": "biped_attack",
        "Carry": "fisherman_idle", "Death": "biped_death",
    },
}

UNIT_IDS = frozenset({
    "villager", "swordsman", "archer", "spearman", "cavalry", "trader", "catapult",
    "hero", "trireme", "elephant", "ram", "siege_ram", "healer", "chariot", "war_dog",
    "bolt_shooter", "fishing_boat", "merchant_ship", "female_citizen", "carnyx",
    "worker_elephant", "archer_maiden", "camel", "scythed_chariot",
    "deer", "boar", "wolf", "sheep", "sheep_gaia", "bolt_projectile",
    "arrow_projectile", "stone_projectile",
    # G7-H2 map-fauna batch (animated; statics below go to BUILDING/VEGETATION sets)
    "goat", "zebra", "chicken", "crocodile", "giraffe", "rabbit",
    "gazelle", "bear", "cow", "pig", "lion", "lioness", "donkey",
    "fox", "hawk",
    # G7-M animated siege engines (statics below go to BUILDING_IDS)
    "siege_ballista", "siege_ballista_med", "siege_lithobolos",
    "siege_lithobolos_light", "siege_oxybeles", "siege_polybolos",
    "siege_scorpio", "siege_onager", "siege_mangonel", "siege_shoushe",
    "siege_tower",
    # G7-Q animated variants (pentekonter hull + hound/elephant/chariot)
    "pentekonter",
    "wolfhound", "mastiff_gaia", "elephant_bush", "elephant_forest",
    "chariot_han",
    # C2 Han civ unit variants (animated)
    "han_cavalry_archer",
    "han_cavalry_crossbow",
    "han_cavalry_spearman",
    "han_cavalry_swordsman",
    "han_chariot_archer",
    "han_chariot_general",
    "han_citizen_female",
    "han_crossbowman",
    "han_fisherman",
    "han_halberdman",
    "han_hero_cav_han_xin",
    "han_hero_cav_liu_bang",
    "han_hero_cav_wei_qing",
    "han_hero_chariot_han",
    "han_hero_han_xin",
    "han_hero_liu_bang",
    "han_hero_wei_qing",
    "han_mangonel_attack",
    "han_mangonel_idle",
    "han_minister",
    "han_shoushe_attack",
    "han_shoushe_death",
    "han_shoushe_idle",
    "han_shoushe_walk",
    "han_siege_mangonel_packed",
    "han_siege_mangonel_pivot",
    "han_villager_female",
    "han_villager_male",
    # --- Mauryan civ batch: a/b/e infantry, elephantry, extra cavalry ---
    "maur_archer_a", "maur_archer_b", "maur_archer_e", "maur_archer_fire",
    "maur_cav_jav_a", "maur_cav_jav_b", "maur_cav_jav_e", "maur_cav_sword_a",
    "maur_cav_sword_e", "maur_chariot_archer_c", "maur_citizen_female", "maur_eleph_archer_a",
    "maur_eleph_archer_b", "maur_eleph_archer_d", "maur_eleph_archer_r1", "maur_elephantry_c",
    "maur_fisherman", "maur_hero_ashoka", "maur_hero_bindusara", "maur_hero_chanakya",
    "maur_hero_chandragupta", "maur_hero_chariot", "maur_hero_elephant", "maur_mace_inf",
    "maur_spearman_a", "maur_spearman_e", "maur_support_elephant", "maur_swordsman_a",
    "maur_swordsman_b", "maur_swordsman_e",
    # --- Kushite civ batch: infantry colour lines, mounted, heroes (animated) ---
    "kush_archer_a", "kush_archer_b", "kush_archer_e", "kush_cav_jav_a",
    "kush_cav_jav_b", "kush_cav_jav_e", "kush_cav_spear_a",
    "kush_cav_spear_e", "kush_hero_cav_harsiotef", "kush_hero_cav_nastasen",
    "kush_hero_chariot_amanirenas", "kush_hero_elephant",
    "kush_javelinist_a", "kush_javelinist_b", "kush_javelinist_e",
    "kush_maceman_a", "kush_maceman_b", "kush_maceman_e", "kush_pikeman_a",
    "kush_pikeman_b", "kush_pikeman_e", "kush_spearman_a", "kush_spearman_e",
    "kush_swordsman_a", "kush_swordsman_b", "kush_swordsman_e",
    "germ_forger", "crow_circle",
})
BUILDING_IDS = frozenset({
    "town_center", "house", "barracks", "farmstead", "storehouse", "market",
    "temple", "wall", "wall_segment", "wall_tower", "tower", "fortress",
    "gate", "wonder", "dock", "corral", "range", "archery_range", "stable", "stables", "ruins",
    "foundation_small", "foundation_large", "foundation_3x3", "foundation_3x3_tower",
    "foundation_5x5", "foundation_6x6", "foundation_8x8", "foundation_4x6",
    "foundation_wall", "foundation_dock", "scaffold_small", "scaffold_large",
    "rubble_small", "rubble_large", "town_center_rubble",
    # G7-H2 landmark/civic statics
    "pyramid", "pyramid_minor", "ishtar_gate", "greek_stoa", "greek_stoa_great",
    "theater", "gymnasium", "stoa", "prytaneion", "propylaea", "lighthouse",
    # G7-M towers/walls/docks + production/civic statics
    "siege_wall", "ballista_tower", "sentry_tower", "defense_tower",
    "outpost", "super_dock", "blacksmith", "workshop", "arsenal",
    "library", "amphitheater", "triumphal_arch", "mausoleum", "shrine",
    "palace", "embassy", "academy", "great_hall",
    # G7-Q warship hulls (0-bone statics) + smith/forge/field/harbor statics
    "bireme", "bireme_rome", "quinquereme", "quinquereme_rome",
    "trireme_han", "bireme_han",
    "ship_knarr", "ship_snekkja", "ship_karvi",
    "skiff", "rowboat", "merchant_han", "fishing_han",
    "blacksmith_brit", "blacksmith_kart", "forge_germ", "forge_han",
    "field_plot", "orchard_plot", "crannog",
    "wooden_tower", "scout_tower", "tower_bolt", "tower_artillery",
    "stable_elephant", "kennel",
    # C2 Han civ structure variants (statics)
    "han_fireship",
    "han_fishing_ship",
    "han_forge",
    "han_fortress_old",
    "han_imperial_court",
    "han_imperial_ministry",
    "han_pagoda",
    "han_temple_shrine",
    "han_tower_great",
    "han_tower_large",
    "han_tower_small",
    "han_towership",
    "han_wall_gate",
    "han_wall_long",
    "han_wall_medium",
    "han_wall_short",
    # --- Mauryan civ batch: stupa civic set, palisade towers, hulls ---
    "maur_bireme", "maur_blacksmith", "maur_outpost", "maur_palace",
    "maur_range", "maur_scout_tower", "maur_stable", "maur_stable_elephant",
    "maur_super_dock", "maur_tower_double", "maur_wooden_tower", "maur_workshop",
    # --- Kushite civ batch: Nubian civic set, pyramids, stone tower, bireme (statics) ---
    "kush_bireme", "kush_blacksmith", "kush_defense_tower_stone",
    "kush_pyramid_large", "kush_pyramid_small", "kush_range", "kush_shrine",
    "kush_stable", "kush_stable_elephant", "kush_workshop",
})
VEGETATION_IDS = frozenset({
    "tree", "pine", "palm", "gold_vein", "stone_mine", "fruit_bush",
    # G7-H2 flora variety
    "oak", "acacia", "baobab", "bamboo", "apple",
})

MAX_PROP_DEPTH = 4

# ------------------------------------------------------------- dds decode ---

def decode_dds(data):
    """Decode DXT1/3/5 or uncompressed DDS bytes -> PIL RGBA image (mip 0)."""
    magic, size, flags = struct.unpack_from("<4sII", data, 0)
    assert magic == b"DDS ", "not a DDS"
    height, width = struct.unpack_from("<II", data, 12)
    fourcc = bytes(data[84:88])
    off = 128
    if fourcc == b"DXT1":
        bpp, decode = 8, _decode_dxt1
    elif fourcc in (b"DXT3", b"DXT2"):
        bpp, decode = 16, _decode_dxt3
    elif fourcc in (b"DXT5", b"DXT4"):
        bpp, decode = 16, _decode_dxt5
    elif fourcc in (b"\x00\x00\x00\x00", b"RXGB"):
        # uncompressed 32-bit: A8R8G8B8 unless the red mask says A8B8G8R8
        rmask = struct.unpack_from("<I", data, 92)[0]
        raw = "RGBA" if rmask == 0xFF else "BGRA"
        img = Image.frombytes("RGBA", (width, height), data[off:off + width * height * 4], "raw", raw)
        return img
    else:
        raise ValueError(f"unsupported DDS fourcc {fourcc}")
    img = Image.new("RGBA", (width, height))
    blk_h, blk_w = (height + 3) // 4, (width + 3) // 4
    pix = img.load()
    buf = memoryview(data)
    for by in range(blk_h):
        for bx in range(blk_w):
            block = bytes(buf[off:off + bpp]); off += bpp
            colors = decode(block)
            for y in range(4):
                wy = by * 4 + y
                if wy >= height: break
                for x in range(4):
                    wx = bx * 4 + x
                    if wx >= width: break
                    pix[wx, wy] = colors[y * 4 + x]
    return img


def _dxt_colors(c0, c1):
    def expand565(c):
        r5 = (c >> 11) & 31; g6 = (c >> 5) & 63; b5 = c & 31
        # 5/6-bit -> 8-bit: replicate high bits into low bits (standard)
        r = (r5 << 3) | (r5 >> 2); g = (g6 << 2) | (g6 >> 4); b = (b5 << 3) | (b5 >> 2)
        return (r, g, b)
    return [expand565(c0), expand565(c1)]


def _decode_dxt1(block):
    """DXT1: c0>c1 -> 4 opaque interpolated colours; c0<=c1 -> 3 colours +
    transparent black at selector 3."""
    c0, c1 = struct.unpack_from("<HH", block, 0)
    bits = struct.unpack_from("<I", block, 4)[0]
    r0, g0, b0 = _dxt_colors(c0, c1)[0]
    r1, g1, b1 = _dxt_colors(c0, c1)[1]
    if c0 > c1:
        pal = [(r0, g0, b0, 255), (r1, g1, b1, 255),
               ((2*r0+r1)//3, (2*g0+g1)//3, (2*b0+b1)//3, 255),
               ((r0+2*r1)//3, (g0+2*g1)//3, (b0+2*b1)//3, 255)]
    else:
        pal = [(r0, g0, b0, 255), (r1, g1, b1, 255),
               ((r0+r1)//2, (g0+g1)//2, (b0+b1)//2, 255),
               (0, 0, 0, 0)]
    return [pal[(bits >> (i * 2)) & 3] for i in range(16)]


def _decode_dxt3(block):
    rgb = _decode_dxt1_rgb(block[8:16])
    amask = struct.unpack_from("<Q", block, 0)[0]
    out = []
    for i in range(16):
        a4 = (amask >> (i * 4)) & 15
        r, g, b = rgb[i]
        out.append((r, g, b, a4 * 17))
    return out


def _decode_dxt1_rgb(block):
    c0, c1 = struct.unpack_from("<HH", block, 0)
    bits = struct.unpack_from("<I", block, 4)[0]
    (r0, g0, b0), (r1, g1, b1) = _dxt_colors(c0, c1)
    if c0 > c1:
        pal = [(r0, g0, b0), (r1, g1, b1),
               ((2*r0+r1)//3, (2*g0+g1)//3, (2*b0+b1)//3),
               ((r0+2*r1)//3, (g0+2*g1)//3, (b0+2*b1)//3)]
    else:
        pal = [(r0, g0, b0), (r1, g1, b1),
               ((r0+r1)//2, (g0+g1)//2, (b0+b1)//2),
               (0, 0, 0)]
    return [pal[(bits >> (i * 2)) & 3] for i in range(16)]


def _decode_dxt5(block):
    a0, a1 = block[0], block[1]
    abits = struct.unpack_from("<Q", block[:8], 0)[0] >> 16
    apal = [a0, a1]
    if a0 > a1:
        apal += [((6 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        apal += [((4 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    rgb = _decode_dxt1_rgb(block[8:16])
    out = []
    for i in range(16):
        ai = (abits >> (i * 3)) & 7
        r, g, b = rgb[i]
        out.append((r, g, b, apal[ai]))
    return out

# -------------------------------------------------------------- pm format ---

def parse_pmd(data):
    """Parse .pmd (PSMD container) -> verts/uvs/faces/bones/prop_points + skinning.

    Follows CFileUnpacker::Read (source/ps/FileIo.cpp) + CModelDef::Load
    (source/graphics/ModelDef.cpp). Version >= 3 stores bind-pose model-space
    coordinates, which is exactly what a static export needs.
    SVertexBlend SIZE=4: m_Bone[4] u8 + m_Weight[4] f32 = 20 bytes.
    """
    magic, _, _ = struct.unpack_from("<4sII", data, 0)
    assert magic == b"PSMD", f"bad PSMD magic {magic!r}"
    version = struct.unpack_from("<I", data, 4)[0]
    payloadSize = struct.unpack_from("<I", data, 8)[0]
    assert len(data) == 12 + payloadSize
    o = 12

    def u32():
        nonlocal o
        v = struct.unpack_from("<I", data, o)[0]; o += 4; return v

    def f32():
        nonlocal o
        v = struct.unpack_from("<f", data, o)[0]; o += 4; return v

    nv = u32()
    nuv = u32() if version > 3 else 1
    verts, uvs, uv1s, blends = [], [], [], []
    for _ in range(nv):
        p = (f32(), f32(), f32()); n = (f32(), f32(), f32())
        uv_sets = []
        for _ in range(nuv):
            uv_sets.append((f32(), f32()))
        # UV set 0 = tiling/atlas diffuse (baseTex). UV set 1 = unique AO unwrap.
        uvs.append(uv_sets[0] if uv_sets else (0.0, 0.0))
        uv1s.append(uv_sets[1] if len(uv_sets) > 1 else None)
        # SVertexBlend: 4 bone indices (u8) + 4 weights (f32) — 20 bytes
        # In PMD file it's packed as 4 bytes bones + 16 bytes weights sequentially
        b0, b1, b2, b3 = struct.unpack_from("<BBBB", data, o); o += 4
        w0, w1, w2, w3 = struct.unpack_from("<4f", data, o); o += 16
        blends.append({"bones": (b0, b1, b2, b3), "weights": (w0, w1, w2, w3)})
        verts.append((p, n))
    nf = u32()
    faces = [struct.unpack_from("<3H", data, o + i * 6) for i in range(nf)]
    o += nf * 6
    nb = u32()
    bones = []
    for _ in range(nb):
        t = (f32(), f32(), f32()); q = (f32(), f32(), f32(), f32())
        bones.append((t, q))
    props = []
    if version >= 2:
        npp = u32()
        for _ in range(npp):
            slen = u32(); name = data[o:o + slen].decode("utf-8"); o += slen
            p = (f32(), f32(), f32()); q = (f32(), f32(), f32(), f32())
            bi = data[o]; o += 1
            props.append({"name": name, "pos": p, "quat": q, "bone": bi})
    # ModelDef.cpp: versions <=2 omit the default 'root' prop point
    if version <= 2 or not any(p["name"].lower() == "root" for p in props):
        props.append({"name": "root", "pos": (0.0, 0.0, 0.0), "quat": (0.0, 0.0, 0.0, 1.0), "bone": 0xFF})
    return {"version": version, "verts": verts, "uvs": uvs, "uv1s": uv1s, "faces": faces,
            "bones": bones, "blends": blends, "prop_points": props}

def parse_psa(data):
    """Parse .psa (PSSA) using 0 A.D. CSkeletonAnimDef::Load + CFileUnpacker.

    Layout: PSSA | version | payloadSize | UnpackString | f32 frameTime
            | UnpackSize numKeys | UnpackSize numFrames | CBoneState[numKeys*numFrames]
    CBoneState (SkeletonAnimDef.h): translation relative to root + rotation relative to root.
    UnpackSize is always little-endian u32 (ps/FileIo.cpp).
    """
    magic = data[0:4]
    assert magic == b"PSSA", f"bad PSSA magic {magic!r}"
    version = struct.unpack_from("<I", data, 4)[0]
    payloadSize = struct.unpack_from("<I", data, 8)[0]
    assert len(data) == 12 + payloadSize
    o = 12
    slen = struct.unpack_from("<I", data, o)[0]; o += 4
    o += slen  # unused name, same as SkeletonAnimDef.cpp
    frameTime = struct.unpack_from("<f", data, o)[0]; o += 4
    numKeys = struct.unpack_from("<I", data, o)[0]; o += 4
    numFrames = struct.unpack_from("<I", data, o)[0]; o += 4
    keys = []
    for _ in range(numFrames * numKeys):
        tx, ty, tz = struct.unpack_from("<3f", data, o); o += 12
        qx, qy, qz, qw = struct.unpack_from("<4f", data, o); o += 16
        keys.append({"t": (tx, ty, tz), "q": (qx, qy, qz, qw)})
    return {"version": version, "frameTime": frameTime, "numKeys": numKeys, "numFrames": numFrames, "keys": keys}


_PSA_KEYS_CACHE = {}


def psa_num_keys(vfs, ref):
    """Bone count a PSA drives, header only. Every clip is probed against every
    candidate body, so decoding the key array here would dominate the run."""
    if ref in _PSA_KEYS_CACHE:
        return _PSA_KEYS_CACHE[ref]
    n = None
    try:
        data = vfs.read(ref)
        if data[0:4] == b"PSSA":
            o = 12
            o += 4 + struct.unpack_from("<I", data, o)[0]  # unused name
            n = struct.unpack_from("<I", data, o + 4)[0]
    except Exception:
        n = None
    _PSA_KEYS_CACHE[ref] = n
    return n


def parse_skeleton_xml(data):
    """Parse art/skeletons/*.xml -> bone hierarchy list [{name,parent}]"""
    root = ET.fromstring(data)
    bones = []
    def walk(node, parent_idx):
        for b in node.findall("bone"):
            name = b.get("name")
            idx = len(bones)
            bones.append({"name": name, "parent": parent_idx})
            walk(b, idx)
    # skeletons may have <standard_skeleton> wrapper
    for std in root.findall("standard_skeleton"):
        walk(std, -1)
    if not bones:
        walk(root, -1)
    return bones

# ------------------------------------------------------------ math helpers --

def quat_to_mat(q):
    x, y, z, w = q
    n = x*x + y*y + z*z + w*w
    s = 2.0 / n if n else 0.0
    xx, yy, zz = s*x*x, s*y*y, s*z*z
    xy, xz, yz = s*x*y, s*x*z, s*y*z
    wx, wy, wz = s*w*x, s*w*y, s*w*z
    return [[1-yy-zz, xy-wz, xz+wy, 0],
            [xy+wz, 1-xx-zz, yz-wx, 0],
            [xz-wy, yz+wx, 1-xx-yy, 0],
            [0, 0, 0, 1]]


def trs(t, q=None):
    m = quat_to_mat(q) if q else [[1 if i == j else 0 for j in range(4)] for i in range(4)]
    m[0][3], m[1][3], m[2][3] = t
    return m


def mat_mul(a, b):
    return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def mat_apply(m, p):
    x, y, z = p
    return tuple(m[i][0]*x + m[i][1]*y + m[i][2]*z + m[i][3] for i in range(3))


def rot_apply(m, v):
    x, y, z = v
    return tuple(m[i][0]*x + m[i][1]*y + m[i][2]*z for i in range(3))


def _is_watercraft(asset_id):
    a = (asset_id or "").lower()
    return any(k in a for k in ("trireme", "fishing_boat", "merchant_ship", "fishship"))


def _yaw_quat_y(yaw):
    h = 0.5 * yaw
    return (0.0, math.sin(h), 0.0, math.cos(h))


def _rot_y(p, yaw):
    """Rotate (x,y,z) around Y so a +Z-facing vector turns by ``yaw``."""
    c, s = math.cos(yaw), math.sin(yaw)
    x, y, z = p
    return (x * c + z * s, y, -x * s + z * c)


def _snap_yaw_quarter(yaw):
    """Snap to 0 / ±90 / 180 so noisy prop points do not tilt a hull."""
    turns = (0.0, 0.5 * math.pi, math.pi, -0.5 * math.pi)
    return min(turns, key=lambda t: abs(math.atan2(math.sin(yaw - t), math.cos(yaw - t))))


def _hull_part_for_align(parts):
    """Largest structural mesh, skipping crew/weapon/head props."""
    skip = ("fisherman", "citizen", "/heads/", "weapon", "shield", "helmet")
    best, best_n = None, -1
    for p in parts:
        src = (getattr(p, "source", None) or "").lower()
        if any(s in src for s in skip):
            continue
        n = len((p.mesh_data or {}).get("verts") or [])
        if n > best_n:
            best, best_n = p, n
    return best or (parts[0] if parts else None)


def _ship_align_yaw(parts, asset_id):
    """Yaw that maps the authored bow onto Godot forward (-Z).

    0 A.D. warships mark the bow with ram / fore_sail / a far projectile.
    Several fishing hulls and the Celt/Iberian warship are authored the
    opposite way (bow at +Z). Without this, those boats sail stern-first.
    """
    if not _is_watercraft(asset_id) or not parts:
        return 0.0
    hull = _hull_part_for_align(parts)
    md = (hull.mesh_data if hull else None) or {}
    verts = md.get("verts") or []
    if not verts:
        return 0.0
    xs = [v[0][0] for v in verts]
    zs = [v[0][2] for v in verts]
    half = 0.5 * max(max(xs) - min(xs), max(zs) - min(zs), 1e-6)

    def from_bow(dx, dz, why):
        if abs(dx) < 1e-5 and abs(dz) < 1e-5:
            return 0.0
        yaw = _snap_yaw_quarter(math.atan2(-dx, -dz))
        if abs(yaw) > 0.01:
            print(f"  ship-align {asset_id}: {why} -> {math.degrees(yaw):.0f} deg")
        return yaw

    by_name = {pp["name"].lower(): pp["pos"] for pp in (md.get("prop_points") or [])}
    for name in ("ram", "fore_sail", "front"):
        if name in by_name:
            p = by_name[name]
            if abs(p[0]) + abs(p[2]) > 0.12 * half:
                return from_bow(p[0], p[2], name)
    if "projectile" in by_name:
        p = by_name["projectile"]
        if math.hypot(p[0], p[2]) > 0.28 * half:
            return from_bow(p[0], p[2], "projectile")

    zn, zp = [], []
    for (p, _) in verts:
        (zn if p[2] < 0 else zp).append(abs(p[0]))
    if zn and zp:
        azn, azp = sum(zn) / len(zn), sum(zp) / len(zp)
        # Narrower tip is the bow. Threshold lets the wide Celt warship flip.
        if azp < azn * 0.88:
            return from_bow(0.0, 1.0, "taper +Z")
        if azn < azp * 0.88:
            return from_bow(0.0, -1.0, "taper -Z")
    return 0.0

# ------------------------------------------------------------ zip vfs -------

class Vfs:
    def __init__(self, zf):
        self.zf = zf
        self._tex_cache = {}
        self._png_cache = {}
        # namelist() rebuilds a ~50k list on every call and exists() is the
        # hottest thing in the extractor, so keep it as a set.
        self._names = set(zf.namelist())

    def read(self, path):
        if os.path.isfile(path):
            with open(path, "rb") as f:
                return f.read()
        return self.zf.read(path)

    def exists(self, path):
        return path in self._names or os.path.isfile(path)

    def mesh_path(self, ref):
        ref = ref.strip().replace("\\", "/")
        return f"art/meshes/{ref}.cached.pmd"

    def skin_candidates(self, ref):
        ref = ref.strip().replace("\\", "/")
        base = f"art/textures/skins/{ref}"
        return [f"{base}.cached.dds", base]

    def load_texture_rgba(self, ref):
        """skin/terrain ref like 'gaia/tree_dead.dds' or
        'types/biome/grass_01.png' -> PIL RGBA."""
        if ref in self._tex_cache:
            return self._tex_cache[ref]
        img = None
        prefixes = ["art/textures/skins/", "art/textures/terrain/"]
        for pref in prefixes:
            base = f"{pref}{ref}"
            for cand in (f"{base}.cached.dds", base,
                         f"{base}.png.cached.dds", f"{base}.dds.cached.dds"):
                if self.exists(cand):
                    data = self.read(cand)
                    if data[:4] == b"DDS ":
                        img = decode_dds(data)
                    else:
                        img = Image.open(io.BytesIO(data)).convert("RGBA")
                    break
            if img is not None:
                break
        self._tex_cache[ref] = img
        return img

# ------------------------------------------------------------ actor model ---

class Part:
    """One composed mesh part with its own material/texture set."""
    __slots__ = ("mesh_data", "tex_base", "tex_ao", "material", "transform", "source",
                 "bone", "force_rigid", "aux", "rider_skin", "rider_attach_world",
                 "rider_attach_bone", "tool_tag", "attach_name")

    def __init__(self):
        self.mesh_data = None
        self.tex_base = None
        self.tex_ao = None
        self.material = ""
        self.transform = None
        self.source = ""
        self.bone = 0  # skeleton bone index this part attaches to (0 = root/hip)
        self.force_rigid = False  # ignore PMD blends; 100% weight on self.bone
        self.aux = False  # e.g. cavalry rider — excluded from fit AABB
        self.rider_skin = False
        self.rider_attach_world = None
        self.rider_attach_bone = 0
        self.tool_tag = None  # "Weapon", "GatherTree", … or None (always visible)
        self.attach_name = ""


def _variant_children(vfs, var):
    """Yield (mesh_ref, props, textures, material) contributions of a <variant>."""
    mesh = None
    props, texs = [], {}
    mat = None
    if var is None:
        return mesh, props, texs, mat
    m = var.find("mesh")
    if m is not None and m.text:
        mesh = m.text.strip()
    for pr in var.iter("prop"):
        actor = pr.get("actor")
        if actor:
            props.append((actor.strip(), (pr.get("attachpoint") or "root").strip(),
                          pr.get("selectable")))
    for tx in var.iter("texture"):
        f = tx.get("file")
        if f:
            texs[tx.get("name") or "baseTex"] = f.strip()
    mt = var.find("material")
    if mt is not None and mt.text:
        mat = mt.text.strip()
    return mesh, props, texs, mat


def _variant_freq(v):
    try:
        return int(v.get("frequency", "1"))
    except ValueError:
        return 1


def _pick_variant(group, shift=0):
    """The group's most frequent variant (ties -> first); with `shift`, the
    variant `shift` places after it among those 0 A.D. can pick (frequency > 0),
    wrapping, so an actor's other looks can be exported as `<id>_vN`."""
    vars_ = group.findall("variant")
    if not vars_:
        return None
    best, best_freq = None, -1
    for v in vars_:
        freq = _variant_freq(v)
        if freq > best_freq:
            best, best_freq = v, freq
    if shift:
        live = [v for v in vars_ if _variant_freq(v) > 0]
        if best in live:
            return live[(live.index(best) + shift) % len(live)]
    return best


def _looks_group(group):
    """Variants of this group differ in mesh or textures (not just props or
    particles, as cherry blossom's falling leaves)."""
    live = [v for v in group.findall("variant") if _variant_freq(v) > 0]
    return len(live) > 1 and any(v.find("mesh") is not None or v.find("textures") is not None for v in live)


def actor_look_count(vfs, actor_path):
    """How many looks an actor's own groups offer: its largest mesh/texture group."""
    if not vfs.exists(actor_path):
        return 1
    root = ET.fromstring(vfs.read(actor_path))
    if root.tag == "qualitylevels" and root.find("inline") is not None:
        root = root.find("inline")
    return max([1] + [len([v for v in g.findall("variant") if _variant_freq(v) > 0])
                      for g in root.findall("group") if _looks_group(g)])


def _resolve_variant_file(vfs, file_ref, cache):
    """0 A.D. ObjectBase.cpp loads variant @file from art/variants/ + value."""
    ref = file_ref.strip()
    if ref.startswith("art/"):
        prefixes = [ref]
    else:
        prefixes = [f"art/variants/{ref}", f"art/{ref}"]
    for path in prefixes:
        for cand in (path, path if path.endswith(".xml") else path + ".xml"):
            if vfs.exists(cand):
                if cand in cache:
                    return ET.fromstring(cache[cand])
                data = vfs.read(cand)
                cache[cand] = data
                return ET.fromstring(data)
    return None


def _as_variant(elem):
    if elem is None:
        return None
    if elem.tag == "variant":
        return elem
    found = elem.find("variant")
    return found if found is not None else elem


def _variant_has_anims(var):
    if var is None:
        return False
    return var.find("animations") is not None or var.find("animation") is not None


def resolve_variant_chain(vfs, var, cache, depth=0):
    """Follow nested variant @file (e.g. aspis_alpha -> aspis_base). Inline wins."""
    if var is None or depth > 8:
        return var
    file_ref = (var.get("file") or "").strip()
    if not file_ref:
        return var
    shared = _as_variant(_resolve_variant_file(vfs, file_ref, cache))
    if shared is None:
        return var
    shared = resolve_variant_chain(vfs, shared, cache, depth + 1)
    return _merge_variant_xml(shared, var)


def _actor_path(ref):
    r = (ref or "").replace("\\", "/").strip()
    if not r:
        return r
    return r if r.startswith("art/") else f"art/actors/{r}"


def _affine_inverse(m):
    """Inverse of a 4x4 affine matrix (rotation, scale and translation)."""
    a = [[m[i][j] for j in range(3)] for i in range(3)]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    inv = [[(a[(j + 1) % 3][(i + 1) % 3] * a[(j + 2) % 3][(i + 2) % 3]
             - a[(j + 1) % 3][(i + 2) % 3] * a[(j + 2) % 3][(i + 1) % 3]) / det for j in range(3)] for i in range(3)]
    t = [-sum(inv[i][k] * m[k][3] for k in range(3)) for i in range(3)]
    return [inv[i] + [t[i]] for i in range(3)] + [[0, 0, 0, 1]]


def _cut_below_ground(parts):
    """GROUND_AT_ORIGIN: drop triangles wholly under the ground plane and clamp
    the rest to it, in each part's own mesh space; parts left empty are dropped."""
    for part in parts:
        md = part.mesh_data
        if not md or md.get("bones"):
            continue
        xf = part.transform if part.transform is not None else trs((0, 0, 0), (0, 0, 0, 1))
        world = [mat_apply(xf, p) for (p, _n) in md["verts"]]
        if min(w[1] for w in world) >= GROUND_CUT_Y:
            continue
        inv = _affine_inverse(xf)
        md = dict(md)
        md["faces"] = [f for f in md["faces"] if max(world[i][1] for i in f) >= GROUND_CUT_Y]
        md["verts"] = [(mat_apply(inv, (w[0], GROUND_CUT_Y, w[2])) if w[1] < GROUND_CUT_Y else p, n)
                       for w, (p, n) in zip(world, md["verts"])]
        part.mesh_data = md
    # A part with nothing left above ground (the fields' prop carrier) goes.
    return [p for p in parts if not p.mesh_data or p.mesh_data.get("bones") or p.mesh_data["faces"]]


def compose_actor(vfs, actor_path, depth=0, tint=None, sources=None, skip_actors=None, look=0):
    """_compose_actor, with GROUND_AT_ORIGIN structures cut at the ground plane."""
    parts, material = _compose_actor(vfs, actor_path, depth, tint, sources, skip_actors, look)
    if depth == 0 and actor_path in GROUND_AT_ORIGIN:
        parts = _cut_below_ground(parts)
    return parts, material


def _compose_actor(vfs, actor_path, depth=0, tint=None, sources=None, skip_actors=None, look=0):
    """Compose an actor into a list of Parts.

    Deterministic picks: highest-frequency variant per group (ties -> first).
    Group semantics used:
      - a group that supplies a <mesh> replaces the current pending mesh
        (0ad variant groups override; e.g. armour upgrade swaps body mesh)
      - shared `file=` variants are resolved from art/variants/**
      - animation-only shared files (biped/quadraped/mechanical, no mesh) skip
        their state props (weapon swaps). Inline mesh+props on the same variant
        are kept (healer body + head/staff).
      - nested variant @file chains are resolved (shield front mesh, scutum, …)
      - destruction variants are skipped entirely
    Prop actors attach AFTER all groups resolve (deferred), matching CModel.
    """
    if depth > MAX_PROP_DEPTH:
        return [], ""
    if not vfs.exists(actor_path):
        print(f"  ! missing actor {actor_path}")
        return [], ""
    root = ET.fromstring(vfs.read(actor_path))
    if root.tag == "qualitylevels":
        inline = root.find("inline")
        if inline is not None:
            root = inline
    material_name = ""
    acc_mesh = None
    acc_tex = {}
    acc_mat = None
    pending_props = []
    final_parts = []          # Part list for kept meshes
    vcache = {}
    top_mat = root.findtext("material")

    def flush():
        nonlocal acc_mesh, acc_mat
        if acc_mesh is None:
            return None
        part = Part()
        mp = vfs.mesh_path(acc_mesh)
        if not vfs.exists(mp):
            print(f"  ! missing mesh {acc_mesh}")
            acc_mesh = acc_mat = None
            return None
        part.mesh_data = parse_pmd(vfs.read(mp))
        part.tex_base = acc_tex.get("baseTex")
        part.tex_ao = acc_tex.get("aoTex")
        part.material = acc_mat or top_mat or ""
        part.source = actor_path
        final_parts.append(part)
        if sources is not None:
            sources.add(("mesh", acc_mesh))
            for t in (part.tex_base, part.tex_ao):
                if t:
                    sources.add(("skin", t))
        acc_mesh = acc_mat = None
        return part

    for g in root.findall("group"):
        # `look` shifts every mesh/texture group of the top actor together (props follow).
        var = _pick_variant(g, look if depth == 0 and _looks_group(g) else 0)
        if var is None:
            continue
        file_ref = (var.get("file") or "").strip()
        if "destruction" in file_ref:
            continue
        resolved = resolve_variant_chain(vfs, var, vcache)
        m1, p1, t1, mtl1 = _variant_children(vfs, resolved)
        anim_only = _variant_has_anims(resolved) and not m1
        if m1:
            if acc_mesh is not None and acc_mesh != m1:
                if flush() is not None:
                    final_parts.pop()  # mesh replaced by later variant group
            if acc_mesh is None:
                acc_mesh = m1
        acc_tex.update(t1)
        # Base-mesh props (head, idle weapon). Animation-only variants are
        # mutually exclusive in 0 A.D.; _pick_variant keeps Idle, so gather
        # tools live on sibling variants and are harvested below.
        if p1 and not anim_only:
            pending_props.extend((a, ap, s, None) for a, ap, s in p1)
        if mtl1:
            acc_mat = mtl1
        for v in g.findall("variant"):
            resolved_v = resolve_variant_chain(vfs, v, vcache)
            m_v, p_v, _, _ = _variant_children(vfs, resolved_v)
            if m_v or not p_v:
                continue
            tags = _variant_tool_tags(resolved_v)
            if not tags:
                continue
            for actor, ap, sel in p_v:
                if _is_junk_tool_prop(actor, ap, tags):
                    continue
                for tag in tags:
                    pending_props.append((actor, ap, sel, tag))

    flush()
    material_name = acc_mat or top_mat or ""

    # For citizen/villager units, 0 A.D. specifies the knife (dagger) in
    # attack_slaughter / attack_melee_female variants rather than the base group.
    # Ensure the dagger is equipped on weapon_R so villagers hold their knife.
    is_citizen_actor = any(w in actor_path.lower() for w in ("citizen", "villager"))
    has_weapon_r = any(
        (item[1] or "").lower() == "weapon_r" and (len(item) < 4 or item[3] in (None, "Weapon"))
        for item in pending_props
    )
    if is_citizen_actor and not has_weapon_r:
        pending_props.append(("props/units/weapons/dagger.xml", "weapon_R", None, "Weapon"))

    if sources is not None:
        sources.add(("actor", actor_path))

    skip_actors = skip_actors or set()

    # resolve props against the composed parts (CModel::ValidatePosition rule:
    # world = boneBindMatrix * propPoint.transform, or transform alone if unboned)
    for item in pending_props:
        actor_ref, attachpoint = item[0], item[1]
        tool_tag = item[3] if len(item) > 3 else None
        if any(s in actor_ref for s in ("decals/", "platforms/", "particle", "flag")):
            continue
        # ObjectEntry.cpp: attachpoint 'projectile' is plucked for LaunchProjectile, not drawn on the actor.
        if (attachpoint or "").lower() == "projectile":
            continue
        sub_path = _actor_path(actor_ref)
        if sub_path in skip_actors or actor_ref in skip_actors:
            continue
        sub_parts, _ = compose_actor(vfs, sub_path, depth + 1, tint, sources, skip_actors)
        if not sub_parts:
            continue
        placed = False
        ap_l = (attachpoint or "").lower()
        tag = tool_tag or ("Weapon" if ap_l in ("weapon_r", "weapon_l") else None)
        for part in final_parts:
            for pp in part.mesh_data["prop_points"]:
                if pp["name"].lower() == ap_l:
                    local = trs(pp["pos"], pp["quat"])
                    bone_i = 0xFF
                    if pp["bone"] != 0xFF and pp["bone"] < len(part.mesh_data["bones"]):
                        t, q = part.mesh_data["bones"][pp["bone"]]
                        world = mat_mul(trs(t, q), local)
                        bone_i = pp["bone"]
                    else:
                        world = local
                    for sp in sub_parts:
                        child = sp.transform if sp.transform is not None else _identity()
                        sp.transform = mat_mul(world, child)
                        sp.bone = bone_i
                        sp.attach_name = attachpoint
                        sp.tool_tag = tag
                        if bone_i != 0xFF and not _shares_skeleton(sp, part, world):
                            sp.force_rigid = True
                    final_parts.extend(sub_parts)
                    placed = True
                    break
            if placed:
                break
        if not placed:
            print(f"[prop] SKIP attachpoint '{attachpoint}' not found for {sub_path}")

    return final_parts, material_name


# Godot clip names used by world_view_node / AnimationController. 0 A.D. names
# are free-form and mixed case (attack_ranged, gather_tree, Build_farm, …), so
# fold every 0 A.D. variant onto the state the renderer actually asks for.
_CLIP_CANON = {
    "idle": "Idle", "idle_transition": "Idle", "feeding": "Idle",
    "walk": "Walk", "move": "Walk", "walk_feeding": "Walk",
    "run": "Run", "flee": "Run",
    "attack": "Attack", "attack_melee": "Attack", "attack_ranged": "Attack",
    "attack_capture": "Attack",
    "attack_slaughter": "Slaughter", "slaughter": "Slaughter",
    "death": "Death",
    "build": "Build", "build_farm": "Build",
    "heal": "Heal",
    "promotion": "Promotion",
    # ResourceGatherer.js SelectAnimation("gather_" + type). Keep each supply
    # type as its own Godot clip so the renderer can swap with the resource.
    "gather": "Gather", "gather_tree": "GatherTree", "gather_ore": "GatherOre",
    "gather_rock": "GatherRock", "gather_ruins": "GatherOre",
    "gather_grain": "GatherGrain", "gather_fruit": "GatherFruit",
    "gather_fish": "GatherFish", "gather_meat": "GatherMeat",
}
# carry_* still fans out to one laden Walk. Do not prefix-map gather_* or
# gather_praise (a temple clip) would steal the generic Gather name.
_CLIP_PREFIX_CANON = (("carry_", "Carry"),)
# Anything outside this set is a 0 A.D. detail state we have no renderer hook
# for; harvesting it would multiply every GLB by a clip nobody plays.
_CLIP_WANTED = ("Idle", "Walk", "Run", "Attack", "Death",
                "Gather", "GatherTree", "GatherOre", "GatherRock",
                "GatherFruit", "GatherGrain", "GatherMeat", "GatherFish",
                "Build", "Carry", "Heal", "Slaughter")
# Several 0 A.D. names fold onto one state; lowest rank wins so a swordsman
# keeps attack_melee rather than the attack_capture shove.
_CLIP_RANK = {
    "attack_melee": 0, "attack_ranged": 0, "attack": 0, "attack_capture": 3,
    "carry_food": 0, "carry_wood": 0, "carry_stone": 0, "carry_metal": 0,
    "carry_meat": 1, "carry_fish": 1,
    "idle": 0, "feeding": 3, "idle_transition": 4,
    "walk": 0, "move": 0, "walk_feeding": 3,
    "run": 0, "flee": 3,
    "build": 0, "build_farm": 1,
}


def _canon_clip(name):
    n = (name or "").strip().lower()
    if not n:
        return ""
    hit = _CLIP_CANON.get(n)
    if hit:
        return hit
    for prefix, state in _CLIP_PREFIX_CANON:
        if n.startswith(prefix):
            return state
    return n[0].upper() + n[1:]


_TOOL_CLIPS = frozenset({
    "Gather", "GatherTree", "GatherOre", "GatherRock",
    "GatherFruit", "GatherGrain", "GatherMeat", "GatherFish", "Build",
})


def _variant_tool_tags(var):
    """Clip names on an animation-only variant that should keep their props."""
    tags = []
    if var is None:
        return tags
    for anim in var.iter("animation"):
        raw = (anim.get("name") or "").strip()
        if not raw:
            raw = os.path.splitext(os.path.basename(anim.get("file") or ""))[0]
        tag = _canon_clip(raw)
        if tag in _TOOL_CLIPS:
            tags.append(tag)
    vtag = _canon_clip(var.get("name") or "")
    if vtag in _TOOL_CLIPS:
        tags.append(vtag)
    seen = set()
    out = []
    for t in tags:
        if t not in seen:
            seen.add(t)
            out.append(t)
    return out


def _is_junk_tool_prop(actor, attach, tags):
    al = (actor or "").lower()
    ap = (attach or "").lower()
    if not ap or ap == "root":
        return True
    if tags and all(t == "Slaughter" for t in tags):
        return True
    if "slaughter" in al:
        return True
    return False


def _psa_for_dae(vfs, dae_file):
    """Map an actor <animation file='other/foo.dae'> to the cached PSA in the zip."""
    f = (dae_file or "").strip().replace("\\", "/")
    if not f:
        return None
    if f.startswith("art/animation/") and vfs.exists(f):
        return f
    if f.endswith(".cached.psa"):
        cands = [f if f.startswith("art/") else f"art/animation/{f}"]
    elif f.endswith(".dae"):
        cands = [f"art/animation/{f}.cached.psa", f"art/animation/{f}"]
    else:
        cands = [f"art/animation/{f}.dae.cached.psa", f"art/animation/{f}"]
    for c in cands:
        if vfs.exists(c):
            return c
    return None


def _is_carry_variant(var):
    """0 A.D. UnitAI SetAnimationVariant(carry_wood) swaps a sibling variant.

    The inner clips are still named Idle/Walk/Run — not carry_* — so the
    harvest has to notice the variant itself (name or @file).
    """
    if var is None:
        return False
    name = (var.get("name") or "").strip().lower()
    file = (var.get("file") or "").replace("\\", "/").lower()
    base = file.rsplit("/", 1)[-1]
    if name.startswith("carry") or "carry_" in name:
        return True
    if "carry_" in base or base.startswith("carry"):
        return True
    return False


def collect_actor_clips(vfs, actor_path, depth=0, seen=None, skip=None):
    """Read Idle/Walk/Attack/… PSA paths from the actor, its variants, and props.

    This is how 0 A.D. actually animates a unit: the mesh actor plus its vehicle
    props (sail, oars, the donkey under a Celtic trader) declare
    <animation file='…dae'>. No per-civ extract table.

    Every variant of a group is scanned, not just the one compose_actor picks
    for geometry: 0 A.D. keeps attack/death/gather as *sibling* variants that
    UnitAI swaps in by name, so picking one would throw the rest away.

    `skip` holds the crew actors attach_rider_parts sit-bakes separately, so a
    fisherman's idle is never mistaken for his boat's body clip.
    """
    seen = seen if seen is not None else set()
    skip = skip or set()
    clips = {}
    path = _actor_path(actor_path)
    if depth > 6 or not path or path in seen or not vfs.exists(path):
        return clips
    # Oar actors carry a 2-bone Idle/Walk PSA. Those must not occupy the ship's
    # Idle/Walk (sail is 41 bones) — oars are rigid nodes stroked at runtime.
    if "ship_oar" in path:
        return clips
    seen.add(path)
    root = ET.fromstring(vfs.read(path))
    if root.tag == "qualitylevels":
        inline = root.find("inline")
        if inline is not None:
            root = inline
    vcache = {}

    ranks = {}

    def add_anim(anim, as_carry=False):
        psa = _psa_for_dae(vfs, anim.get("file") or "")
        if not psa:
            return
        raw = (anim.get("name") or "").strip().lower()
        if as_carry:
            # Laden variant: keep the walk cycle as Carry so it cannot overwrite
            # the unit's unladen Idle/Walk. Prefer walk, then run, then idle.
            if raw in ("walk", "run", "move"):
                key = "Carry"
                rank = 0 if raw == "walk" else 1
            elif raw in ("idle",):
                key = "Carry"
                rank = 2
            else:
                key = _canon_clip(raw)
                if key not in _CLIP_WANTED:
                    return
                rank = _CLIP_RANK.get(raw, 1)
        else:
            key = _canon_clip(raw)
            if key not in _CLIP_WANTED:
                return
            rank = _CLIP_RANK.get(raw, 1)
        if key not in clips or rank < ranks[key]:
            clips[key] = psa
            ranks[key] = rank

    vehicle_props = []

    def walk_var(var, props=True):
        if var is None:
            return
        resolved = resolve_variant_chain(vfs, var, vcache)
        carry = _is_carry_variant(var) or _is_carry_variant(resolved)
        for anim in resolved.iter("animation"):
            add_anim(anim, as_carry=carry)
        if not props:
            return
        for pr in resolved.iter("prop"):
            act = (pr.get("actor") or "").strip()
            ap = (pr.get("attachpoint") or "").strip().lower()
            if not act or ap == "projectile":
                continue
            if any(s in act for s in ("decals/", "platforms/", "particle", "flag", "ship_oar")):
                continue
            if _actor_path(act) in skip:
                continue
            vehicle_props.append(act)

    for anim in root.findall("./animations/animation"):
        add_anim(anim)
    for g in root.findall("group"):
        picked = _pick_variant(g)
        for var in g.findall("variant"):
            if "destruction" in (var.get("file") or ""):
                continue
            # Props only come from the variant compose_actor renders; the other
            # variants contribute animations alone.
            walk_var(var, props=(var is picked))
    # Props run last so the actor's own clips always outrank a prop's.
    for act in vehicle_props:
        for k, v in collect_actor_clips(vfs, act, depth + 1, seen, skip).items():
            if k not in clips:
                clips[k] = v
    return clips


def _fallback_unit_cfg(asset_id):
    if asset_id in UNIT_CFG:
        return UNIT_CFG[asset_id]
    parent = _civ_base_unit_id(asset_id)
    if parent and parent in UNIT_CFG:
        sk = UNIT_CFG[parent][0]
        # Sail/oar clips only exist when the actor (or a prop) declared them.
        # Do not paste Greek ship_sail onto a hull that 0 A.D. left static.
        if sk in ("ship_sail", "celt_sail", "rowing_boat"):
            return None
        return UNIT_CFG[parent]
    return None


def _skeleton_for_clips(vfs, clips, preferred=""):
    if preferred and preferred in SKELETONS:
        return preferred
    counts = {}
    for name, ref in SKELETONS.items():
        if vfs.exists(ref):
            counts[name] = len(parse_skeleton_xml(vfs.read(ref)))
    for psa_path in clips.values():
        if not vfs.exists(psa_path):
            continue
        nk = psa_num_keys(vfs, psa_path)
        if nk is None:
            continue
        for name, n in counts.items():
            if n == nk:
                return name
        break
    return preferred or ""


def resolve_anim_cfg(vfs, asset_id, actor_ref):
    """Prefer clips declared on the 0 A.D. actor; UNIT_CFG is only a fallback."""
    harvested = collect_actor_clips(
        vfs, actor_ref, skip=_rider_skip_actors(asset_id, resolve_riders(vfs, asset_id, actor_ref)))
    base = _fallback_unit_cfg(asset_id)
    merged = dict(harvested)
    if base:
        _sk, fallback_clips = base
        if harvested:
            for state, key in fallback_clips.items():
                if state in merged:
                    continue
                ref = ANIMATIONS.get(key, key)
                if vfs.exists(ref):
                    merged[state] = ref
        else:
            for state, key in fallback_clips.items():
                ref = ANIMATIONS.get(key, key)
                if vfs.exists(ref):
                    merged[state] = ref
    if "Walk" in merged and "Run" not in merged:
        merged["Run"] = merged["Walk"]
    if "Idle" in merged:
        for extra in ("Attack", "Death"):
            if extra not in merged:
                merged[extra] = merged["Idle"]
    # Generic Gather is the wood chop; keep it as a fallback name.
    if "GatherTree" in merged and "Gather" not in merged:
        merged["Gather"] = merged["GatherTree"]
    elif "Gather" in merged and "GatherTree" not in merged:
        merged["GatherTree"] = merged["Gather"]
    if not merged:
        return None
    preferred = base[0] if base else ""
    if harvested:
        preferred = _skeleton_for_clips(vfs, harvested, preferred)
    sk = _skeleton_for_clips(vfs, merged, preferred)
    return sk, merged


def _identity():
    return trs((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))


def _prop_world(part, pp):
    local = trs(pp["pos"], pp["quat"])
    bones = part.mesh_data.get("bones") or []
    if pp["bone"] != 0xFF and pp["bone"] < len(bones):
        t, q = bones[pp["bone"]]
        return mat_mul(trs(t, q), local)
    return local


def _psa_frame_tq(psa, frame=0):
    nk = psa["numKeys"]
    keys = psa["keys"]
    out = []
    for i in range(nk):
        ki = frame * nk + i
        if ki < len(keys):
            k = keys[ki]
            out.append((k["t"], k["q"]))
        else:
            out.append(((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)))
    return out


def _shares_skeleton(sp, part, world):
    """A prop skinned to its parent's skeleton (same bones, same rest pose) on a
    prop point that stays at the origin: horse manes and tails on "hair".
    0 A.D. plays the parent's clip on the prop's own copy of the skeleton, so
    skinning it to the parent's bones draws the same thing."""
    md, pd = sp.mesh_data or {}, part.mesh_data or {}
    mine, theirs = md.get("bones") or [], pd.get("bones") or []
    if len(mine) < 20 or len(mine) != len(theirs) or not md.get("blends"):
        return False
    if not _blends_fit_skeleton(md["blends"], len(theirs)):
        return False
    for (t0, q0), (t1, q1) in zip(mine, theirs):
        if max(abs(a - b) for a, b in zip(t0, t1)) > 1e-3:
            return False
        if min(max(abs(a - b) for a, b in zip(q0, q1)), max(abs(a + b) for a, b in zip(q0, q1))) > 1e-3:
            return False
    ident = _identity()
    return all(abs(world[r][c] - ident[r][c]) < 1e-4 for r in range(4) for c in range(4))


def _blends_fit_skeleton(blends, n_bones):
    for bl in blends:
        for b, w in zip(bl["bones"], bl["weights"]):
            if w <= 1e-8 or b in (0xFF, 255):
                continue
            if b < 0 or b >= n_bones:
                return False
    return True


def _skin_mesh_to_pose(md, rest_tq, pose_tq):
    """CPU-skin PMD verts: pose * IBM * bind  (ModelDef.cpp inverse bind)."""
    n = min(len(rest_tq), len(pose_tq))
    if n <= 0:
        return
    pals = [mat_mul(trs(*pose_tq[i]), rigid_inverse(trs(*rest_tq[i]))) for i in range(n)]
    blends = md.get("blends") or []
    posed = []
    for i, (p, nrm) in enumerate(md["verts"]):
        if i >= len(blends):
            posed.append((p, nrm))
            continue
        acc = [0.0, 0.0, 0.0]
        accn = [0.0, 0.0, 0.0]
        wsum = 0.0
        bl = blends[i]
        for b, w in zip(bl["bones"], bl["weights"]):
            if w <= 1e-8 or b in (0xFF, 255) or b < 0 or b >= n:
                continue
            pp = mat_apply(pals[b], p)
            nn = rot_apply(pals[b], nrm)
            acc[0] += pp[0] * w
            acc[1] += pp[1] * w
            acc[2] += pp[2] * w
            accn[0] += nn[0] * w
            accn[1] += nn[1] * w
            accn[2] += nn[2] * w
            wsum += w
        if wsum <= 1e-8:
            posed.append((p, nrm))
            continue
        ln = math.sqrt(accn[0] * accn[0] + accn[1] * accn[1] + accn[2] * accn[2]) or 1.0
        posed.append((tuple(acc), (accn[0] / ln, accn[1] / ln, accn[2] / ln)))
    md["verts"] = posed
    md["blends"] = []


def _pose_rider_sit(vfs, rparts, psa_key):
    """Bake rider/cavalry sit onto biped parts (0 A.D. *_r actor + rider PSA)."""
    ref = ANIMATIONS.get(psa_key, psa_key)
    if not vfs.exists(ref):
        print(f"  ! rider sit PSA missing: {ref}")
        return
    try:
        psa = parse_psa(vfs.read(ref))
    except Exception as e:
        print(f"  ! rider sit PSA parse failed: {e}")
        return
    rbody = None
    for p in rparts:
        if p.mesh_data and len(p.mesh_data.get("bones") or []) > 0:
            rbody = p
            break
    if rbody is None:
        print("  ! rider body has no bind bones; sit bake skipped")
        return
    rest_tq = list(rbody.mesh_data["bones"])
    if psa["numKeys"] != len(rest_tq):
        # CModel::SetAnimation rejects this pairing outright; padding the missing
        # keys with rest-pose bones tears the mesh (posed parents, rest children).
        print(f"  ! rider sit skipped: {ref} has {psa['numKeys']} keys, "
              f"mesh has {len(rest_tq)} bones")
        return
    pose_tq = _psa_frame_tq(psa, 0)
    n = min(len(rest_tq), len(pose_tq))
    skinned = 0
    propped = 0
    for sp in rparts:
        md = sp.mesh_data
        if not md:
            continue
        blends = md.get("blends") or []
        own_bones = len(md.get("bones") or [])
        # Full biped body (~46 bones). Helmets/weapons are 0–few bones + attach xf.
        if own_bones >= 20 and blends and _blends_fit_skeleton(blends, n):
            _skin_mesh_to_pose(md, rest_tq, pose_tq)
            skinned += 1
        elif sp.transform is not None:
            bi = sp.bone if sp.bone != 0xFF else 0
            if 0 <= bi < n:
                delta = mat_mul(trs(*pose_tq[bi]), rigid_inverse(trs(*rest_tq[bi])))
                sp.transform = mat_mul(delta, sp.transform)
                propped += 1
    print(f"  rider sit <- {ref} (skinned {skinned} parts, props {propped})")


def _actor_prop_at(vfs, actor_path, ap_name, depth=0, seen=None):
    """Actor this hull props at `attachpoint`, following shared variant files."""
    seen = seen if seen is not None else set()
    path = _actor_path(actor_path)
    if depth > 4 or not path or path in seen or not vfs.exists(path):
        return None
    seen.add(path)
    root = ET.fromstring(vfs.read(path))
    if root.tag == "qualitylevels":
        inline = root.find("inline")
        if inline is not None:
            root = inline
    vcache = {}
    for g in root.findall("group"):
        var = resolve_variant_chain(vfs, _pick_variant(g), vcache)
        if var is None:
            continue
        for pr in var.iter("prop"):
            if (pr.get("attachpoint") or "").strip().lower() == ap_name.lower():
                act = (pr.get("actor") or "").strip()
                if act:
                    return _actor_path(act)
    return None


# Rider actors _mount_crew found on a mount's rider_R / rider_L variants.
MOUNT_CREW_SKIP = {}


def _mount_crew(vfs, asset_id, actor_ref, skeleton_name):
    """Riders for a horse or elephant actor no RIDER_ATTACHMENTS row names (Han,
    Kushite and Mauryan cavalry and elephants): the unit props its own `*_r`
    rider and `*_d` driver. Without a row they were drawn as plain props in
    bind pose, standing on the saddle, and twice (the death variant's
    rider_R as well)."""
    if asset_id in RIDER_ATTACHMENTS or skeleton_name not in ("horse", "elephant"):
        return
    path = _actor_path(actor_ref)
    if not vfs.exists(path):
        return
    root = ET.fromstring(vfs.read(path))
    if root.tag == "qualitylevels" and root.find("inline") is not None:
        root = root.find("inline")
    crew = []
    for g in root.findall("group"):
        picked = _pick_variant(g)
        for v in g.findall("variant"):
            var = resolve_variant_chain(vfs, v, {})
            if var is None:
                continue
            for pr in var.iter("prop"):
                ap = (pr.get("attachpoint") or "").strip()
                act = _actor_path(pr.get("actor"))
                if not act.startswith("art/actors/units/"):
                    continue
                if ap.lower().endswith(("_r", "_l")):
                    # A gather or death variant's swapped-in rider: drop it,
                    # the seated one stays.
                    MOUNT_CREW_SKIP.setdefault(asset_id, set()).add(act)
                    continue
                if v is not picked or any(ap == c[1] for c in crew):
                    continue
                stem = os.path.basename(act)[:-4]
                if skeleton_name == "horse":
                    sit = "cavalry_idle"
                elif stem.endswith("_d"):
                    sit = "elephant_driver_idle"
                else:
                    sit = "elephant_archer_ready"
                crew.append((act, ap, sit))
    if not crew:
        return
    if skeleton_name == "horse":
        RIDER_ATTACHMENTS[asset_id] = crew[:1]
        RIDER_CLIPS[asset_id] = RIDER_CLIPS["cavalry"]
    else:
        RIDER_ATTACHMENTS[asset_id] = crew
    print(f"  crew <- {', '.join(f'{os.path.basename(a)}@{ap}' for a, ap, _ in RIDER_ATTACHMENTS[asset_id])}")


def resolve_riders(vfs, asset_id, actor_ref):
    """RIDER_ATTACHMENTS, but with each crew actor taken from the hull itself.

    A Kushite fishing boat props a Kushite fisherman; using the table's Hellenic
    one would leave two men in one boat.
    """
    out = []
    for rpath, ap_name, sit_key in RIDER_ATTACHMENTS.get(asset_id, []):
        own = _actor_prop_at(vfs, actor_ref, ap_name)
        out.append((own or _actor_path(rpath), ap_name, sit_key))
    return out


def _rider_skip_actors(asset_id, riders=None):
    src = riders if riders is not None else RIDER_ATTACHMENTS.get(asset_id, [])
    return {_actor_path(p) for p, _ap, _sit in src} | MOUNT_CREW_SKIP.get(asset_id, set())


def attach_rider_parts(vfs, parts, body, asset_id, tint, sources, riders=None):
    """Sit-bake each mount crew actor and rigid-bind it to its attachpoint.

    Crew uses a 46-bone biped; the mount uses a different skeleton. One glTF
    skin cannot drive both, so crew meshes are rigid-bound to the mount joint.
    Bake the sit/ready PSA first — bind-pose riders stand in the saddle.
    compose_actor skips these props so this is the only instance of each.
    """
    for rpath, ap_name, sit_key in (riders if riders is not None
                                    else RIDER_ATTACHMENTS.get(asset_id, [])):
        rparts, _ = compose_actor(vfs, rpath, 0, tint, sources)
        if not rparts:
            print(f"  ! rider actor empty: {rpath}")
            continue
        attach_pp = None
        attach_part = body
        for part in parts:
            if not part.mesh_data:
                continue
            for pp in part.mesh_data.get("prop_points") or []:
                if pp["name"].lower() == ap_name.lower():
                    attach_pp = pp
                    attach_part = part
                    break
            if attach_pp:
                break
        if attach_pp is None:
            print(f"  ! rider attachpoint '{ap_name}' missing on {asset_id}")
            continue
        world = _prop_world(attach_part, attach_pp)
        rb = attach_pp["bone"] if attach_pp["bone"] != 0xFF else 0
        live_keys = set((RIDER_CLIPS.get(asset_id) or {}).values())
        live = bool(live_keys) and sit_key in live_keys
        if live and sit_key:
            # Keep biped blends so Attack/Walk can drive the sitting rider.
            pass
        elif sit_key:
            _pose_rider_sit(vfs, rparts, sit_key)
        for sp in rparts:
            child = sp.transform if sp.transform is not None else _identity()
            sp.transform = mat_mul(world, child)
            sp.aux = True
            if live:
                sp.rider_skin = True
                sp.rider_attach_world = world
                sp.rider_attach_bone = rb
                own = len((sp.mesh_data or {}).get("bones") or [])
                blends = (sp.mesh_data or {}).get("blends") or []
                if own < 20 or not blends:
                    sp.force_rigid = True
            else:
                if sit_key:
                    # sit already applied above for the whole group
                    pass
                sp.bone = rb
                sp.force_rigid = True
        if live:
            print(f"  rider live <- {rpath} @{ap_name} bone={rb} ({len(rparts)} parts)")
        else:
            print(f"  rider <- {rpath} @{ap_name} bone={rb} ({len(rparts)} parts)")
        parts = parts + rparts
    return parts


def _merge_variant_xml(shared_var, inline_var):
    """Combine a shared variant file's <variant> with the inline one.

    Mesh/material/textures: inline wins. Props and animations concatenate —
    replacing <props> used to drop trireme oars when the hull variant also
    declared sail props.
    """
    merged = ET.Element("variant")
    merged.attrib.update(shared_var.attrib)
    for k, v in inline_var.attrib.items():
        if v:
            merged.set(k, v)

    def _copy(el):
        return ET.fromstring(ET.tostring(el, encoding="unicode"))

    by_tag = {}
    append_tags = ("props", "animations")
    for src in (shared_var, inline_var):
        for child in list(src):
            tag = child.tag
            if tag in append_tags:
                if tag not in by_tag:
                    node = ET.SubElement(merged, tag)
                    by_tag[tag] = node
                for gc in child:
                    by_tag[tag].append(_copy(gc))
                continue
            if tag in by_tag:
                merged.remove(by_tag[tag])
            node = _copy(child)
            merged.append(node)
            by_tag[tag] = node
    return merged

# ------------------------------------------------------------- baking -------

def bake_texture(vfs, ref, ao_img, tint):
    src = vfs.load_texture_rgba(ref)
    if src is None:
        return None
    img = src.copy()  # don't mutate cache

    # Check if this texture is intended for player color masking (e.g. unit clothing/shields/banners)
    is_structural = any(k in ref for k in ("tile", "struct", "stone", "grass", "dirt", "rock", "tree", "plant", "nature", "foliage", "wood"))

    if tint is not None and not is_structural:
        px = img.load()
        w, h = img.size
        r_t, g_t, b_t = (int(c * 255) for c in tint)
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                # 0ad model_common.fs: diffuse *= mix(playerColor, white, tex.a)
                if a < 250:
                    r = int(r * (r_t * (1 - a / 255.0) + a) / 255)
                    g = int(g * (g_t * (1 - a / 255.0) + a) / 255)
                    b = int(b * (b_t * (1 - a / 255.0) + a) / 255)
                # Player-color lives in alpha; after baking it into RGB, keep the
                # texel opaque so glTF MASK materials don't discard team colors.
                # baked color is now in RGB; don't keep player-mask alpha for MASK shaders
                px[x, y] = (min(r, 255), min(g, 255), min(b, 255), 255)

    # AO (aoTex) is a unique UV1 lightmap in 0 A.D. — never multiply it onto
    # the tiling UV0 albedo in image space (that tiles the unwrap as static).
    return img


def _sample_rgba(img, u, v):
    """Sample PIL image. PMD UVs are OpenGL (0,0 = bottom-left)."""
    w, h = img.size
    uu = min(max(float(u), 0.0), 1.0)
    vv = min(max(1.0 - float(v), 0.0), 1.0)
    x = min(int(uu * (w - 1) + 0.5), w - 1)
    y = min(int(vv * (h - 1) + 0.5), h - 1)
    px = img.getpixel((x, y))
    if len(px) == 3:
        return px[0], px[1], px[2], 255
    return px[0], px[1], px[2], px[3]


def ao_vertex_colors(ao_img, uv1s):
    """Bake unique-unwrap AO into COLOR_0.

    Godot's glTF importer enables occlusionTexture but samples it with UV0,
    which turns a UV1 lightmap into static on tiling building atlases.
    """
    cols = []
    for uv in uv1s:
        if uv is None:
            cols.append((255, 255, 255, 255))
            continue
        r, g, b, _ = _sample_rgba(ao_img, uv[0], uv[1])
        a = int(round((r + g + b) / 3.0))
        cols.append((a, a, a, 255))
    return cols

# ------------------------------------------------------------ glb writer ----

class GlbBuilder:
    def __init__(self):
        self.bin = bytearray()
        self.images = {}       # content-hash -> index
        self.image_defs = []   # {name, png_bytes}
        self.materials = []    # json defs
        self.ao_image_indices = set()
        self.primitives = []   # (positions, normals, uvs, indices, material_idx, double_sided, alpha_mode)

    def add_image(self, png_bytes):
        h = hashlib.sha256(png_bytes).hexdigest()[:16]
        if h in self.images:
            return self.images[h]
        idx = len(self.image_defs)
        self.images[h] = idx
        self.image_defs.append({"name": f"tex{idx}", "bytes": png_bytes})
        return idx

    def add_material(self, img_idx, alpha_mode, double_sided, ao_idx=None):
        key = (img_idx, alpha_mode, double_sided, ao_idx)
        for i, m in enumerate(self.materials):
            if m["_key"] == key:
                return i
        d = {"_key": key, "name": f"mat{len(self.materials)}",
             "pbrMetallicRoughness": {"baseColorTexture": {"index": img_idx},
                                      "metallicFactor": 0.0, "roughnessFactor": 0.9},
             "doubleSided": bool(double_sided)}
        if alpha_mode == "MASK":
            d["alphaMode"] = "MASK"
            d["alphaCutoff"] = 0.35
        if ao_idx is not None:
            d["occlusionTexture"] = {"index": ao_idx, "texCoord": 1}
            self.ao_image_indices.add(ao_idx)
            # Godot's glTF importer enables AO but samples UV0 unless the
            # runtime sets BaseMaterial3D.FLAG_AO_ON_UV2. Keep texCoord=1.
        self.materials.append(d)
        return len(self.materials) - 1

    @staticmethod
    def _pad(b, align=4):
        while len(b) % align:
            b += b"\x00"
        return b

    def _accessor(self, comp_type, count, data_type, data_bytes, minmax=None, stride=None, normalized=False):
        self.bin = self._pad(bytes(self.bin))
        offset = len(self.bin)
        self.bin += data_bytes
        acc = {"bufferView": len(self.buffer_views), "componentType": comp_type,
               "count": count, "type": data_type}
        if normalized:
            acc["normalized"] = True
        bv = {"buffer": 0, "byteOffset": offset, "byteLength": len(data_bytes)}
        if stride:
            bv["byteStride"] = stride
        elif data_type in ("VEC3", "VEC2"):
            bv["byteStride"] = 12 if data_type == "VEC3" else 8
        self.buffer_views.append(bv)
        if minmax:
            acc["min"], acc["max"] = minmax
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def _add_primitive(self, positions, normals, uvs, indices, mat_idx=-1, joints=None, weights=None, uv1=None, colors=None):
        """Append one primitive (already in model space) and return its index."""
        pi = len(self.primitives)
        self.primitives.append((positions, normals, uvs, indices, mat_idx, True, "MASK", joints, weights, uv1, colors))
        return pi

    def build(self, node_name, nodes=None, animations=None, skins=None):
        """Build GLB.

        If ``nodes``/``animations`` are None, build the legacy flat layout
        (one mesh+node per primitive, like 0 A.D.'s separate CModel per prop).
        If provided, ``nodes`` is the scene graph and ``animations`` are glTF
        animation clips (used for skinned/rigid-prop skeletal unit animation).
        """
        import json as _json
        self.accessors = []
        self.buffer_views = []
        self.bin = bytearray()
        # Each primitive becomes its own mesh.
        meshes = []
        for pi, prim in enumerate(self.primitives):
            positions, normals, uvs, indices, mat_idx = prim[0], prim[1], prim[2], prim[3], prim[4]
            joints = prim[7] if len(prim) > 7 else None
            weights = prim[8] if len(prim) > 8 else None
            mn = [min(p[i] for p in positions) for i in range(3)]
            mx = [max(p[i] for p in positions) for i in range(3)]
            pos_acc = self._accessor(5126, len(positions), "VEC3",
                                     b"".join(struct.pack("<3f", *p) for p in positions), (mn, mx))
            nrm_acc = self._accessor(5126, len(normals), "VEC3",
                                     b"".join(struct.pack("<3f", *n) for n in normals))
            uv_acc = self._accessor(5126, len(uvs), "VEC2",
                                     b"".join(struct.pack("<2f", *t) for t in uvs))
            idx_acc = self._accessor(5125, len(indices), "SCALAR",
                                     b"".join(struct.pack("<I", i) for i in indices))
            # mark mesh buffer-view targets immediately
            attr_targets = [(pos_acc, 34962), (nrm_acc, 34962), (uv_acc, 34962), (idx_acc, 34963)]
            attrs = {"POSITION": pos_acc, "NORMAL": nrm_acc, "TEXCOORD_0": uv_acc}
            uv1 = prim[9] if len(prim) > 9 else None
            colors = prim[10] if len(prim) > 10 else None
            if uv1 and len(uv1) == len(positions):
                uv1_acc = self._accessor(5126, len(uv1), "VEC2",
                                         b"".join(struct.pack("<2f", *t) for t in uv1))
                attrs["TEXCOORD_1"] = uv1_acc
                attr_targets.append((uv1_acc, 34962))
            if colors and len(colors) == len(positions):
                col_acc = self._accessor(5121, len(colors), "VEC4",
                                         b"".join(struct.pack("<4B", *c) for c in colors),
                                         stride=4, normalized=True)
                attrs["COLOR_0"] = col_acc
                attr_targets.append((col_acc, 34962))
            if joints and weights and len(joints) == len(positions):
                j_acc = self._accessor(5121, len(joints), "VEC4",
                                       b"".join(struct.pack("<4B", *j) for j in joints), stride=4)
                w_acc = self._accessor(5126, len(weights), "VEC4",
                                       b"".join(struct.pack("<4f", *w) for w in weights), stride=16)
                attrs["JOINTS_0"] = j_acc
                attrs["WEIGHTS_0"] = w_acc
                attr_targets += [(j_acc, 34962), (w_acc, 34962)]
            for bvi, tgt in attr_targets:
                self.buffer_views[bvi]["target"] = tgt
            primj = {"attributes": attrs, "indices": idx_acc, "material": mat_idx}
            meshes.append({"name": f"{node_name}_part{pi}", "primitives": [primj]})
        images = [{"bufferView": None, "mimeType": "image/png", "name": im["name"]}
                  for im in self.image_defs]

        if nodes is None:
            # Legacy flat layout.
            nodes = [{"name": node_name, "children": list(range(1, len(meshes) + 1))}]
            for pi in range(len(meshes)):
                nodes.append({"name": f"{node_name}_part{pi}", "mesh": pi})

        gltf_nodes = []
        for nd in nodes:
            d = {"name": nd["name"]}
            if nd.get("mesh", -1) >= 0:
                d["mesh"] = nd["mesh"]
            if nd.get("translation"):
                d["translation"] = [float(x) for x in nd["translation"]]
            if nd.get("rotation"):
                d["rotation"] = [float(x) for x in nd["rotation"]]
            if nd.get("scale"):
                d["scale"] = [float(x) for x in nd["scale"]]
            if nd.get("children"):
                d["children"] = list(nd["children"])
            if nd.get("skin", -1) >= 0:
                d["skin"] = int(nd["skin"])
            gltf_nodes.append(d)

        gltf_animations = []
        if animations:
            for anim in animations:
                times = anim["times"]
                t_acc = self._accessor(5126, len(times), "SCALAR",
                                       b"".join(struct.pack("<f", t) for t in times))
                samplers = []
                channels = []
                for node_idx, tracks in anim["tracks"].items():
                    if "translation" in tracks:
                        out = b"".join(struct.pack("<3f", *v) for v in tracks["translation"])
                        o_acc = self._accessor(5126, len(tracks["translation"]), "VEC3", out)
                        si = len(samplers)
                        samplers.append({"input": t_acc, "output": o_acc, "interpolation": "LINEAR"})
                        channels.append({"sampler": si, "target": {"node": int(node_idx), "path": "translation"}})
                    if "rotation" in tracks:
                        out = b"".join(struct.pack("<4f", *v) for v in tracks["rotation"])
                        o_acc = self._accessor(5126, len(tracks["rotation"]), "VEC4", out)
                        si = len(samplers)
                        samplers.append({"input": t_acc, "output": o_acc, "interpolation": "LINEAR"})
                        channels.append({"sampler": si, "target": {"node": int(node_idx), "path": "rotation"}})
                gltf_animations.append({"name": anim["name"], "samplers": samplers, "channels": channels})

        gltf = {
            "asset": {"version": "2.0", "generator": "RTS0 extract_0ad_assets.py"},
            "scene": 0,
            "scenes": [{"nodes": [0]}],
            "nodes": gltf_nodes,
            "meshes": meshes,
            "materials": [{k: v for k, v in m.items() if k != "_key"} for m in self.materials],
            "textures": [{"source": i, "sampler": (1 if i in self.ao_image_indices else 0)}
                         for i in range(len(images))],
            "images": images,
            "samplers": [
                {"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497},
                {"magFilter": 9729, "minFilter": 9987, "wrapS": 33071, "wrapT": 33071},
            ],
            "buffers": [{"byteLength": len(self.bin)}],
            "bufferViews": self.buffer_views,
            "accessors": self.accessors,
        }
        if gltf_animations:
            gltf["animations"] = gltf_animations
        if skins:
            gltf_skins = []
            for sk in skins:
                ibm_bytes = b"".join(_mat4_gltf(m) for m in sk["inverseBindMatrices"])
                ibm_acc = self._accessor(5126, len(sk["inverseBindMatrices"]), "MAT4", ibm_bytes)
                entry = {"joints": list(sk["joints"]), "inverseBindMatrices": ibm_acc}
                if "skeleton" in sk:
                    entry["skeleton"] = int(sk["skeleton"])
                gltf_skins.append(entry)
            gltf["skins"] = gltf_skins
        # append image bytes to bin, fix bufferViews
        for im in self.image_defs:
            self.bin = self._pad(bytes(self.bin))
            gltf["images"][self.image_defs.index(im)]["bufferView"] = len(self.buffer_views)
            self.buffer_views.append({"buffer": 0, "byteOffset": len(self.bin),
                                      "byteLength": len(im["bytes"])})
            self.bin += im["bytes"]
        self.bin = self._pad(bytes(self.bin))
        gltf["buffers"][0]["byteLength"] = len(self.bin)

        js = _json.dumps(gltf, separators=(",", ":")).encode()
        while len(js) % 4:
            js += b" "  # spec: JSON chunk padded with spaces
        total = 12 + 8 + len(js) + 8 + len(self.bin)
        out = struct.pack("<4sII", b"glTF", 2, total)
        out += struct.pack("<I4s", len(js), b"JSON") + js
        out += struct.pack("<I4s", len(self.bin), b"BIN\0") + bytes(self.bin)
        return out

# ----------------------------------------------------------- model compile --

def compile_asset(vfs, asset_id, actor_ref, fit, target, tint, out_dir, rel_dir=None,
                  look=0, origin=None, want_origin=False):
    """`look` exports the actor's look-th variant set (see _pick_variant). With
    `want_origin` the record carries the model's centre and ground under
    "_origin"; a later look passes it as `origin` so its trunk stands where the
    first look's does."""
    print(f"[{asset_id}] {actor_ref}" + (f" look {look}" if look else ""))
    sources = set()
    riders = resolve_riders(vfs, asset_id, actor_ref)
    skip_actors = _rider_skip_actors(asset_id, riders)
    parts, material_name = compose_actor(vfs, actor_ref, 0, tint, sources, skip_actors, look)
    if not parts:
        raise RuntimeError(f"no parts composed for {asset_id}")
    body = None
    for p in parts:
        if p.mesh_data and len(p.mesh_data.get("bones", [])) > 0:
            body = p
            break
    if body is None:
        body = parts[0]
    parts = attach_rider_parts(vfs, parts, body, asset_id, tint, sources, riders)
    glb = GlbBuilder()
    all_pos = []
    baked_cache = {}  # (tex_ref, tint) -> img_idx
    ao_img_cache = {}
    for part in parts:
        md = part.mesh_data
        verts, uvs, faces = md["verts"], md["uvs"], md["faces"]
        uv1s = md.get("uv1s") or []
        has_uv1 = len(uv1s) == len(uvs) and all(u is not None for u in uv1s)
        xf = part.transform if part.transform is not None else trs((0, 0, 0), (0, 0, 0, 1))
        positions = [mat_apply(xf, p) for (p, n) in verts]
        normals = [mat_apply(rot_only(xf), n) for (p, n) in verts]
        all_pos.extend(positions)
        tex = part.tex_base
        mat_idx = 0
        if tex:
            key = (tex, tint)
            if key in baked_cache:
                img_idx = baked_cache[key]
            else:
                img = bake_texture(vfs, tex, None, tint)
                img_idx = -1
                png = _img_png(img)
                if png:
                    img_idx = glb.add_image(png)
                baked_cache[key] = img_idx
            if img_idx >= 0:
                cutout = _is_cutout_material(part)
                mat_idx = glb.add_material(img_idx, "MASK" if cutout else "OPAQUE", True)
            else:
                mat_idx = _plain_material(glb)
        else:
            mat_idx = _plain_material(glb)
        colors = None
        if has_uv1 and part.tex_ao:
            if part.tex_ao not in ao_img_cache:
                ao_img_cache[part.tex_ao] = vfs.load_texture_rgba(part.tex_ao)
            ao_img = ao_img_cache[part.tex_ao]
            if ao_img is not None:
                colors = ao_vertex_colors(ao_img.convert("RGBA"), uv1s)
        tri_indices = []
        for a, b, c in faces:
            tri_indices += [a, c, b]  # 0ad = CW front; glTF = CCW front
        # 0 A.D. / OpenGL texture coords use (0,0) at the BOTTOM-LEFT, while
        # glTF / Godot sample (0,0) at the TOP-LEFT. Flip V so baked textures
        # map to the correct location on the model instead of being mirrored.
        flip_uvs = [(u, 1.0 - v) for (u, v) in uvs]
        glb.primitives.append((positions, normals, flip_uvs, tri_indices, mat_idx, True, "MASK", None, None, None, colors))
    # normalize: ground + center + fit; boats also yaw so the bow is -Z
    yaw = _ship_align_yaw(parts, asset_id)
    xs = [p[0] for p in all_pos]; ys = [p[1] for p in all_pos]; zs = [p[2] for p in all_pos]
    minx, maxx, miny, maxy, minz, maxz = min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)
    dim = (maxx-minx, maxy-miny, maxz-minz)
    scale = _fit_scale(fit, target, dim)
    cx, cz = (minx+maxx)/2, (minz+maxz)/2
    # Props keep the actor's origin height, so a wreck floats as deep as in 0 A.D.
    base_y = 0.0 if asset_id in ZIP_PROPS else miny
    if origin is not None:
        cx, base_y, cz = origin
    # rewrite primitive positions in place
    for pi, prim in enumerate(glb.primitives):
        pos, nrm, uv, idx, mi = prim[0], prim[1], prim[2], prim[3], prim[4]
        pos2, nrm2 = [], []
        for x, y, z in pos:
            px, py, pz = (x - cx) * scale, (y - base_y) * scale, (z - cz) * scale
            pos2.append(_rot_y((px, py, pz), yaw) if yaw else (px, py, pz))
        if yaw:
            nrm2 = [_rot_y(n, yaw) for n in nrm]
        else:
            nrm2 = nrm
        glb.primitives[pi] = (pos2, nrm2, uv, idx, mi) + tuple(prim[5:])
    if abs(yaw - 0.5 * math.pi) < 0.01 or abs(yaw + 0.5 * math.pi) < 0.01:
        dim = (dim[2], dim[1], dim[0])
    out_path = _glb_out_path(out_dir, asset_id, rel_dir)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    blob = glb.build(asset_id)
    with open(out_path, "wb") as f:
        f.write(blob)
    for im in glb.image_defs:
        im_out = os.path.join(os.path.dirname(out_path), f"{asset_id}_{im['name']}.png")
        with open(im_out, "wb") as f:
            f.write(im["bytes"])
    final_dim = (dim[0]*scale, dim[1]*scale, dim[2]*scale)
    print(f"  -> {out_path} ({os.path.getsize(out_path)//1024}KB, "
          f"{len(parts)} parts, dim {final_dim[0]:.2f}x{final_dim[1]:.2f}x{final_dim[2]:.2f})")
    rec = {
        "file": os.path.relpath(out_path, out_dir).replace(os.sep, "/"),
        "parts": len(parts),
        "size_meters": [round(final_dim[0], 3), round(final_dim[1], 3), round(final_dim[2], 3)],
        "origin": "actor-center" if asset_id in ZIP_PROPS else "ground-center",
        "sources": sorted(f"{kind}:{ref}" for kind, ref in sources),
        "upstream_material": material_name,
    }
    if want_origin:
        rec["_origin"] = (cx, base_y, cz)
    return rec


def compile_unit_animated(vfs, asset_id, actor_ref, fit, target, tint, out_dir,
                          skeleton_name, anims_map, rel_dir=None):
    """Like compile_asset but bakes a REAL skeletal (rigid-prop) animation.

    Builds a bone-node hierarchy from the unit's skeleton, attaches every mesh
    part to the bone it is bound to, and emits glTF animation clips sampled
    from 0 A.D. PSA files so Godot plays them natively via AnimationPlayer.
    """
    print(f"[{asset_id}] (animated) {actor_ref}  skeleton={skeleton_name}")
    sources = set()
    _mount_crew(vfs, asset_id, actor_ref, skeleton_name)
    riders = resolve_riders(vfs, asset_id, actor_ref)
    skip_actors = _rider_skip_actors(asset_id, riders)
    parts, material_name = compose_actor(vfs, actor_ref, 0, tint, sources, skip_actors)
    if not parts:
        raise RuntimeError(f"no parts composed for {asset_id}")
    # Prefer the mesh whose bind-pose bone count matches the unit skeleton
    # (trireme hull is 0 bones; the 41-bone sail is the animated body).
    bone_names = []
    want_bones = 0
    skel_ref = SKELETONS.get(skeleton_name)
    if skel_ref and vfs.exists(skel_ref):
        sb = parse_skeleton_xml(vfs.read(skel_ref))
        bone_names = [b["name"] for b in sb]
        want_bones = len(sb)
    # CModel::SetAnimation only accepts a PSA whose key count equals the mesh's
    # bone count, so the animated body is the part the clips can actually drive
    # — for a siege engine that is the 7-bone frame, not the rock it throws.
    clip_bones = set()
    for psa_key in anims_map.values():
        ref = ANIMATIONS.get(psa_key, psa_key)
        nk = psa_num_keys(vfs, ref) if vfs.exists(ref) else None
        if nk is not None:
            clip_bones.add(nk)
    body = None
    fallback = None
    hull = None
    hull_n = 10 ** 9
    # Prefer the mesh the UNIT_CFG / harvested skeleton actually drives, then
    # the largest clip-compatible part. First-match used to bind a catapult to
    # its rock or a 46-bone crew tunic when those bone counts also appeared in
    # clip_bones.
    best_score = None
    for p in parts:
        nb = len((p.mesh_data or {}).get("bones") or [])
        nv = len((p.mesh_data or {}).get("verts") or [])
        src = (getattr(p, "source", "") or "").lower()
        if "ship_oar" in src:
            continue
        if nb > 0 and fallback is None:
            fallback = p
        if nb < hull_n:
            hull_n = nb
            hull = p
        if not nb or nb not in clip_bones:
            continue
        if any(w in src for w in ("projectile", "stone_ammo", "stone_projectile")):
            continue
        score = (1 if (want_bones and nb == want_bones) else 0, nv, nb)
        if best_score is None or score > best_score:
            best_score = score
            body = p
    if body is None:
        for p in parts:
            src = (getattr(p, "source", "") or "").lower()
            if "ship_oar" in src:
                continue
            nb = len((p.mesh_data or {}).get("bones") or [])
            if want_bones and nb == want_bones:
                body = p
                break
    if body is None:
        # Civ boats often have a static hull plus a skinned rider. Prefer the
        # hull when no part matches the UNIT_CFG skeleton (else the rider
        # becomes the body and hull clips warp the fisherman).
        body = hull or fallback or parts[0]
    parts = attach_rider_parts(vfs, parts, body, asset_id, tint, sources, riders)
    # Safety net: ensure any villager/citizen asset has the knife (dagger) on weapon_R
    is_villager = "villager" in asset_id or "female_citizen" in asset_id or "citizen" in asset_id
    has_weapon_r = any(
        (getattr(p, "attach_name", "") or "").lower() == "weapon_r"
        and getattr(p, "tool_tag", None) in (None, "Weapon")
        for p in parts
    )
    if is_villager and not has_weapon_r and body and "prop_points" in (body.mesh_data or {}):
        for pp in body.mesh_data["prop_points"]:
            if pp["name"].lower() == "weapon_r":
                dparts, _ = compose_actor(vfs, "art/actors/props/units/weapons/dagger.xml", 1, tint, sources)
                local = trs(pp["pos"], pp["quat"])
                bones = body.mesh_data.get("bones") or []
                bone_i = 0xFF
                if pp["bone"] != 0xFF and pp["bone"] < len(bones):
                    t, q = bones[pp["bone"]]
                    world = mat_mul(trs(t, q), local)
                    bone_i = pp["bone"]
                else:
                    world = local
                for dp in dparts:
                    child = dp.transform if dp.transform is not None else _identity()
                    dp.transform = mat_mul(world, child)
                    dp.bone = bone_i
                    dp.attach_name = "weapon_R"
                    dp.tool_tag = "Weapon"
                    if bone_i != 0xFF:
                        dp.force_rigid = True
                parts.extend(dparts)
                break
    bones_local = body.mesh_data.get("bones", [])
    num_bones = max(1, len(bones_local))
    if bones_local:
        # Same key check CModel::SetAnimation applies. A UNIT_CFG fallback aimed
        # at a sibling skeleton (lithobolos clips on an onager frame) has to go
        # before the states that borrow from Idle/Walk are filled back in.
        kept = {}
        for state, psa_key in anims_map.items():
            ref = ANIMATIONS.get(psa_key, psa_key)
            if not vfs.exists(ref):
                print(f"  ! anim missing: {ref}")
                continue
            nk = psa_num_keys(vfs, ref)
            if nk is None:
                print(f"  ! anim parse failed {state}: {ref}")
                continue
            if nk == num_bones:
                kept[state] = psa_key
            else:
                print(f"  ! {state} dropped: {ref} has {nk} keys, body has {num_bones} bones")
        if kept:
            kept.setdefault("Run", kept.get("Walk", kept.get("Idle")))
            for extra in ("Attack", "Death"):
                if "Idle" in kept:
                    kept.setdefault(extra, kept["Idle"])
        anims_map = {k: v for k, v in kept.items() if v}
    rest_worlds = []
    rest_tq = []
    for i in range(num_bones):
        t, q = bones_local[i] if i < len(bones_local) else ((0, 0, 0), (0, 0, 0, 1))
        rest_tq.append((t, q))
        rest_worlds.append(trs(t, q))

    prop_bones = []
    has_live_rider = False
    for part in parts:
        if getattr(part, "rider_skin", False) and len((part.mesh_data or {}).get("bones") or []) >= 20:
            has_live_rider = True
        if part is body or getattr(part, "rider_skin", False):
            continue
        if not getattr(part, "force_rigid", False):
            continue
        b = getattr(part, "bone", 0)
        if isinstance(b, int) and 0 <= b < num_bones and b != 0xFF and b not in prop_bones:
            prop_bones.append(b)
    prop_skin_i = (2 if has_live_rider else 1) if prop_bones else -1

    glb = GlbBuilder()
    all_pos = []
    fit_pos = []
    baked_cache = {}
    ao_img_cache = {}
    part_info = []  # (prim, bone_idx, xf, origin_locked, skin_i, oar_tq, tool_tag)
    for part in parts:
        md = part.mesh_data
        verts, uvs, faces = md["verts"], md["uvs"], md["faces"]
        uv1s = md.get("uv1s") or []
        xf = part.transform if part.transform is not None else trs((0, 0, 0), (0, 0, 0, 1))
        src = (getattr(part, "source", "") or "").lower()
        is_oar = "ship_oar" in src
        world_pos = [mat_apply(xf, p) for (p, n) in verts]
        if is_oar:
            positions = [p for (p, n) in verts]
            normals = [n for (p, n) in verts]
            oar_tq = mat_to_tq(xf)
        else:
            positions = world_pos
            normals = [mat_apply(rot_only(xf), n) for (p, n) in verts]
            oar_tq = None
        all_pos.extend(world_pos)
        tex = part.tex_base
        mat_idx = 0
        if tex:
            key = (tex, tint)
            if key in baked_cache:
                img_idx = baked_cache[key]
            else:
                img = bake_texture(vfs, tex, None, tint)
                if img is not None and max(img.size) > 512:
                    s = 512 / max(img.size)
                    img = img.resize((max(1, int(img.size[0] * s)), max(1, int(img.size[1] * s))), Image.LANCZOS)
                if img is None:
                    img_idx = -1
                else:
                    bio = io.BytesIO()
                    img.save(bio, "PNG", optimize=True)
                    img_idx = glb.add_image(bio.getvalue())
                baked_cache[key] = img_idx
            if img_idx >= 0:
                cutout = _is_cutout_material(part)
                mat_idx = glb.add_material(img_idx, "MASK" if cutout else "OPAQUE", True)
            else:
                mat_idx = _plain_material(glb)
        else:
            mat_idx = _plain_material(glb)
        flip_uvs = [(u, 1.0 - v) for (u, v) in uvs]
        flip_uv1 = None
        has_uv1 = len(uv1s) == len(uvs) and all(u is not None for u in uv1s)
        if has_uv1:
            flip_uv1 = [(u, 1.0 - v) for (u, v) in uv1s]
        colors = None
        if has_uv1 and part.tex_ao:
            if part.tex_ao not in ao_img_cache:
                ao_img_cache[part.tex_ao] = vfs.load_texture_rgba(part.tex_ao)
            ao_img = ao_img_cache[part.tex_ao]
            if ao_img is not None:
                colors = ao_vertex_colors(ao_img.convert("RGBA"), uv1s)
        tri = []
        for a, b, c in faces:
            tri += [a, c, b]
        bone_idx = getattr(part, "bone", 0)
        own_bones = len(md.get("bones") or [])
        rider_skin = getattr(part, "rider_skin", False)
        skin_n = 46 if rider_skin else num_bones
        # Horse-on-chariot / biped-on-mount: foreign skeleton cannot use this skin.
        mismatched = (not rider_skin) and own_bones > 0 and own_bones != num_bones
        use_rigid = getattr(part, "force_rigid", False) or mismatched
        has_skin_blends = bool(md.get("blends")) and _blends_fit_skeleton(md["blends"], skin_n)
        # Cape etc. attach to root (0xFF) but still have a full matching skin.
        origin_locked = bone_idx == 0xFF and not use_rigid and not has_skin_blends and not rider_skin
        is_prop_rigid = (
            not is_oar and not rider_skin and part is not body
            and use_rigid and isinstance(bone_idx, int)
            and 0 <= bone_idx < num_bones and bone_idx != 0xFF
        )
        if is_oar:
            # Keep oars as rigid child nodes so Walk can stroke them; they must
            # not ride the sail skeleton (2-bone oar PMD vs 41-bone sail).
            origin_locked = True
            use_rigid = False
            has_skin_blends = False
            joints, weights = None, None
        elif is_prop_rigid:
            # Bone-local verts + identity IBM on the prop skin so the mesh
            # rides the hand without a second bind-world multiply.
            ibm = rigid_inverse(rest_worlds[bone_idx])
            positions = [mat_apply(ibm, p) for p in world_pos]
            normals = [mat_apply(rot_only(ibm), n) for n in normals]
            li = prop_bones.index(bone_idx)
            joints = [(li, 0, 0, 0)] * len(positions)
            weights = [(1.0, 0.0, 0.0, 0.0)] * len(positions)
            origin_locked = False
        elif origin_locked:
            joints, weights = None, None
        elif use_rigid:
            if bone_idx < 0 or bone_idx == 0xFF or bone_idx >= skin_n:
                bone_idx = 0
            fb = bone_idx
            joints = [(fb, 0, 0, 0)] * len(positions)
            weights = [(1.0, 0.0, 0.0, 0.0)] * len(positions)
        else:
            if bone_idx < 0 or bone_idx >= skin_n:
                bone_idx = 0
            joints, weights = _skin_attrs(md, len(positions), bone_idx, skin_n)
        prim = glb._add_primitive(positions, normals, flip_uvs, tri, mat_idx, joints, weights, flip_uv1, colors)
        skin_i = prop_skin_i if is_prop_rigid else (1 if rider_skin else 0)
        part_info.append((prim, bone_idx, xf, origin_locked, skin_i, oar_tq,
                          getattr(part, "tool_tag", None)))

        # A part buried under the ground (0.28's encampment base sits 30 m
        # down, a hidden prop-point carrier) stays out of the fit, or it
        # would lift the whole model off the ground.
        buried = bool(world_pos) and max(p[1] for p in world_pos) < -1.0
        if not getattr(part, "aux", False) and not is_oar and not buried:
            fit_pos.extend(world_pos)

    # Normalization (center XZ, base Y, scale to target) applied as the ROOT
    # node transform so the in-model-space bone/part transforms stay intact.
    # Rider/aux parts are left out of the fit so cavalry stays horse-sized.
    box = fit_pos or all_pos
    xs = [p[0] for p in box]; ys = [p[1] for p in box]; zs = [p[2] for p in box]
    minx, maxx, miny, maxy, minz, maxz = min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)
    dim = (maxx - minx, maxy - miny, maxz - minz)
    scale = _fit_scale(fit, target, dim)
    cx, cz = (minx + maxx) / 2, (minz + maxz) / 2
    root_t = (-cx * scale, -miny * scale, -cz * scale)
    root_s = (scale, scale, scale)
    align_yaw = _ship_align_yaw(parts, asset_id)
    if abs(align_yaw) > 0.01:
        root_t = _rot_y(root_t, align_yaw)
    if abs(align_yaw - 0.5 * math.pi) < 0.01 or abs(align_yaw + 0.5 * math.pi) < 0.01:
        dim = (dim[2], dim[1], dim[0])

    # Live rider: second skin (46 biped bones) in mount space via attach xf.
    rider_rest_local = []
    rider_attach_world = _identity()
    rider_body = None
    for p in parts:
        if getattr(p, "rider_skin", False) and len((p.mesh_data or {}).get("bones") or []) >= 20:
            rider_body = p
            rider_rest_local = list(p.mesh_data["bones"])
            rider_attach_world = getattr(p, "rider_attach_world", None) or _identity()
            rider_attach_bone = getattr(p, "rider_attach_bone", 0)
            break
    rider_n = len(rider_rest_local)
    rider_rest_worlds = [mat_mul(rider_attach_world, trs(*rider_rest_local[i])) for i in range(rider_n)]
    rider_rest_tq = [mat_to_tq(rider_rest_worlds[i]) for i in range(rider_n)]
    rider_attach_bone = 0
    for p in parts:
        if getattr(p, "rider_skin", False) and hasattr(p, "rider_attach_bone"):
            rider_attach_bone = p.rider_attach_bone
            break
    if rider_n > 0 and 0 <= rider_attach_bone < num_bones:
        rider_prop_local = mat_mul(rigid_inverse(trs(*rest_tq[rider_attach_bone])), rider_attach_world)
    else:
        rider_prop_local = _identity()

    # Node graph matching CModel / CSkeletonAnimDef:
    # every bone is a sibling of the scaled root with root-relative TRS.
    bone_node = {i: 1 + i for i in range(num_bones)}
    rider_node = {i: 1 + num_bones + i for i in range(rider_n)}
    part_node_start = 1 + num_bones + rider_n
    mesh_nodes = list(range(part_node_start, part_node_start + len(part_info)))
    all_bones = [bone_node[i] for i in range(num_bones)]
    rider_joints = [rider_node[i] for i in range(rider_n)]
    root_node = {"name": asset_id, "translation": list(root_t), "scale": list(root_s),
                 "children": all_bones + rider_joints + mesh_nodes}
    if abs(align_yaw) > 0.01:
        root_node["rotation"] = list(_yaw_quat_y(align_yaw))
    nodes = [root_node]
    for i in range(num_bones):
        t, q = rest_tq[i]
        nodes.append({"name": bone_names[i] if i < len(bone_names) else f"bone{i}",
                      "translation": list(t), "rotation": list(q)})
    for i in range(rider_n):
        t, q = rider_rest_tq[i]
        nodes.append({"name": f"rider_{i}", "translation": list(t), "rotation": list(q)})
    tag_count = {}
    for k, rec in enumerate(part_info):
        prim, bidx, wxf, origin_locked, skin_i, oar_tq = rec[:6]
        tool_tag = rec[6] if len(rec) > 6 else None
        if oar_tq is not None:
            nd_name = f"oar_{k}"
        elif tool_tag:
            tag_count[tool_tag] = tag_count.get(tool_tag, 0) + 1
            nd_name = f"tool_{tool_tag}" if tag_count[tool_tag] == 1 else f"tool_{tool_tag}_{tag_count[tool_tag]}"
        else:
            nd_name = f"{asset_id}_part{k}"
        nd = {"name": nd_name, "mesh": prim}
        if oar_tq is not None:
            ot, oq = oar_tq
            nd["translation"] = list(ot)
            nd["rotation"] = list(oq)
        if not origin_locked:
            nd["skin"] = skin_i if skin_i >= 0 else 0
        nodes.append(nd)

    # Animation clips: PSA keys are already root-relative (BuildBoneMatrices).
    # Times are converted from milliseconds (GetFrameTime) to glTF seconds.
    animations = []
    rider_map = RIDER_CLIPS.get(asset_id) or {}
    # A static hull (0-bone fishing boat, arrow ship) still animates through its
    # crew, so a state survives on the rider clip alone.
    states = list(anims_map.keys()) + [s for s in rider_map if s not in anims_map]
    for state_name in states:
        psa_key = anims_map.get(state_name)
        ref = ANIMATIONS.get(psa_key, psa_key) if psa_key else ""
        psa = None
        if ref:
            if not vfs.exists(ref):
                print(f"  ! anim missing: {ref}")
            else:
                try:
                    psa = parse_psa(vfs.read(ref))
                except Exception as e:
                    print(f"  ! anim parse failed {state_name}: {e}")
        rclip = rider_map.get(state_name)
        rpsa = None
        if rider_n > 0 and rclip:
            rref = ANIMATIONS.get(rclip, rclip)
            if vfs.exists(rref):
                try:
                    rpsa = parse_psa(vfs.read(rref))
                except Exception as e:
                    print(f"  ! rider anim parse {state_name}: {e}")
        if psa is None and rpsa is None:
            continue
        if psa is not None:
            nf = psa["numFrames"]; nk = psa["numKeys"]
            times = [psa["frameTime"] * f / 1000.0 for f in range(nf)]
            if nk != num_bones:
                print(f"  ! bone count PMD={num_bones} PSA={nk} for {state_name} (0ad ENSURE equal; extra bones stay bind-pose)")
        else:
            nf = rpsa["numFrames"]; nk = 0
            times = [rpsa["frameTime"] * f / 1000.0 for f in range(nf)]
        tracks = {}
        for i in range(num_bones):
            trans = []
            rots = []
            rt, rq = rest_tq[i]
            for f in range(nf):
                ki = f * nk + i
                if psa is not None and i < nk and ki < len(psa["keys"]):
                    key = psa["keys"][ki]
                    trans.append(tuple(key["t"]))
                    rots.append(tuple(key["q"]))
                else:
                    trans.append(tuple(rt))
                    rots.append(tuple(rq))
            tracks[bone_node[i]] = {"translation": trans, "rotation": rots}
        if rider_n > 0:
            r_nf = rpsa["numFrames"] if rpsa else 1
            r_nk = rpsa["numKeys"] if rpsa else 0
            r_dt = (rpsa["frameTime"] / 1000.0) if rpsa else 1.0
            loop_r = state_name not in ("Attack", "Death")
            for i in range(rider_n):
                trans, rots = [], []
                local_rest = rider_rest_local[i] if i < len(rider_rest_local) else ((0, 0, 0), (0, 0, 0, 1))
                for f in range(nf):
                    tsec = times[f]
                    if rpsa and r_nf > 0:
                        rf = int(tsec / max(r_dt, 1e-6))
                        rf = rf % r_nf if loop_r else min(rf, r_nf - 1)
                        ki = rf * r_nk + i
                        if i < r_nk and ki < len(rpsa["keys"]):
                            k = rpsa["keys"][ki]
                            local = (k["t"], k["q"])
                        else:
                            local = local_rest
                    else:
                        local = local_rest
                    if 0 <= rider_attach_bone < num_bones:
                        mt, mq = rest_tq[rider_attach_bone]
                        ki_m = f * nk + rider_attach_bone
                        if psa is not None and rider_attach_bone < nk and ki_m < len(psa["keys"]):
                            mk = psa["keys"][ki_m]
                            mt, mq = mk["t"], mk["q"]
                        attach_f = mat_mul(trs(mt, mq), rider_prop_local)
                    else:
                        attach_f = rider_attach_world
                    wt, wq = mat_to_tq(mat_mul(attach_f, trs(*local)))
                    trans.append(wt)
                    rots.append(wq)
                tracks[rider_node[i]] = {"translation": trans, "rotation": rots}
        animations.append({"name": state_name, "times": times, "tracks": tracks})
        extra = f"+rider {rclip}" if rpsa else ""
        print(f"  anim '{state_name}' <- {ref or '(crew only)'} ({nf}f, {nk}b) {extra}")

    skins = [{
        "joints": all_bones,
        "inverseBindMatrices": [rigid_inverse(rest_worlds[i]) for i in range(num_bones)],
        "skeleton": bone_node[0],
    }]
    if rider_n > 0:
        skins.append({
            "joints": rider_joints,
            "inverseBindMatrices": [rigid_inverse(rider_rest_worlds[i]) for i in range(rider_n)],
            "skeleton": rider_joints[0],
        })
    if prop_bones:
        ident = [[1 if i == j else 0 for j in range(4)] for i in range(4)]
        skins.append({
            "joints": [bone_node[i] for i in prop_bones],
            "inverseBindMatrices": [ident] * len(prop_bones),
            "skeleton": bone_node[0],
        })

    out_path = _glb_out_path(out_dir, asset_id, rel_dir)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    clip_donor = None
    if asset_id in TEMPLATE_BASE and _same_rig(TEMPLATE_DONOR.get(asset_id), out_dir, nodes, skins):
        # A template model whose skeleton (rider included) rests exactly like
        # the civ's model of its kind borrows that model's clips at load time,
        # which keeps 1.5-2.5 MB of clips per template out of the repository.
        clip_donor = TEMPLATE_DONOR[asset_id]
        animations = []
    blob = glb.build(asset_id, nodes=nodes, animations=animations or None, skins=skins)
    with open(out_path, "wb") as f:
        f.write(blob)
    for im in glb.image_defs:
        im_out = os.path.join(os.path.dirname(out_path), f"{asset_id}_{im['name']}.png")
        with open(im_out, "wb") as f:
            f.write(im["bytes"])
    final_dim = (dim[0] * scale, dim[1] * scale, dim[2] * scale)
    print(f"  -> {out_path} ({os.path.getsize(out_path)//1024}KB, {len(parts)} parts, "
          f"{num_bones} bones, {len(animations)} anims, "
          f"dim {final_dim[0]:.2f}x{final_dim[1]:.2f}x{final_dim[2]:.2f})")
    entry = {
        "file": os.path.relpath(out_path, out_dir).replace(os.sep, "/"),
        "parts": len(parts),
        "bones": num_bones,
        "animated": True,
        "skeleton": skeleton_name,
        # What the GLB actually carries, which includes crew-only states a
        # static hull has no body clip for.
        "animations": [a["name"] for a in animations],
        "size_meters": [round(final_dim[0], 3), round(final_dim[1], 3), round(final_dim[2], 3)],
        "origin": "ground-center",
        "sources": sorted(f"{kind}:{ref}" for kind, ref in sources),
        "upstream_material": material_name,
    }
    if clip_donor:
        entry["clips_from"] = clip_donor
    return entry


def _img_png(img, max_edge=512):
    if img is None:
        return None
    if max(img.size) > max_edge:
        s = max_edge / max(img.size)
        img = img.resize((max(1, int(img.size[0] * s)), max(1, int(img.size[1] * s))), Image.LANCZOS)
    bio = io.BytesIO()
    img.save(bio, "PNG", optimize=True)
    return bio.getvalue()


def _plain_material(glb):
    glb.materials.append({"_key": ("none", len(glb.materials)), "name": f"plain{len(glb.materials)}",
                          "pbrMetallicRoughness": {"baseColorFactor": [0.6, 0.6, 0.6, 1.0],
                                                   "metallicFactor": 0.0, "roughnessFactor": 0.9},
                          "doubleSided": True})
    return len(glb.materials) - 1


def _is_cutout_material(part):
    blob = f"{part.material or ''} {part.tex_base or ''} {part.source or ''}".lower()
    return any(k in blob for k in ("foliage", "leaf", "grass", "hair", "alphatest", "plant", "tree", "canopy"))


def _skin_attrs(md, nverts, fallback_bone, num_bones):
    """Per-vertex joint indices + weights. Props with no blends bind 100% to attach bone."""
    blends = md.get("blends") or []
    max_b = max(0, num_bones - 1)

    def clamp_bone(b):
        if b in (0xFF, 255) or b < 0:
            return 0
        return min(int(b), max_b)

    joints, weights = [], []
    if len(blends) == nverts:
        for bl in blends:
            b = [clamp_bone(x) for x in bl["bones"]]
            w = list(bl["weights"])
            s = sum(w)
            if s <= 1e-6:
                b, w = [fallback_bone, 0, 0, 0], [1.0, 0.0, 0.0, 0.0]
            else:
                w = [x / s for x in w]
            while len(b) < 4:
                b.append(0)
                w.append(0.0)
            joints.append(tuple(b[:4]))
            weights.append(tuple(w[:4]))
    else:
        fb = clamp_bone(fallback_bone)
        joints = [(fb, 0, 0, 0)] * nverts
        weights = [(1.0, 0.0, 0.0, 0.0)] * nverts
    return joints, weights


def _mat4_gltf(m):
    """Pack a row-major 4x4 as glTF column-major floats."""
    vals = [m[r][c] for c in range(4) for r in range(4)]
    return struct.pack("<16f", *vals)


def rot_only(m):
    r = [row[:] for row in m]
    r[0][3] = r[1][3] = r[2][3] = 0
    return r


def rigid_inverse(m):
    """Inverse of a rigid (rotation + translation, no scale) 4x4 matrix."""
    R = [[m[i][j] for j in range(3)] for i in range(3)]
    t = (m[0][3], m[1][3], m[2][3])
    Rt = [[R[j][i] for j in range(3)] for i in range(3)]  # transpose
    it = tuple(-sum(Rt[i][k] * t[k] for k in range(3)) for i in range(3))
    return [[Rt[i][j] for j in range(3)] + [it[i]] for i in range(3)] + [[0, 0, 0, 1]]


def mat_to_tq(m):
    t = (m[0][3], m[1][3], m[2][3])
    R = [[m[i][j] for j in range(3)] for i in range(3)]
    return t, mat3_to_quat(R)


def mat3_to_quat(R):
    """Convert a 3x3 rotation matrix to a (x,y,z,w) quaternion."""
    m00, m01, m02 = R[0]
    m10, m11, m12 = R[1]
    m20, m21, m22 = R[2]
    tr = m00 + m11 + m22
    if tr > 0:
        s = (tr + 1.0) ** 0.5
        w = 0.5 * s
        s = 0.5 / s
        x = (m21 - m12) * s
        y = (m02 - m20) * s
        z = (m10 - m01) * s
    elif m00 > m11 and m00 > m22:
        s = (1.0 + m00 - m11 - m22) ** 0.5
        x = 0.5 * s
        s = 0.5 / s
        y = (m01 + m10) * s
        z = (m02 + m20) * s
        w = (m21 - m12) * s
    elif m11 > m22:
        s = (1.0 + m11 - m00 - m22) ** 0.5
        y = 0.5 * s
        s = 0.5 / s
        x = (m01 + m10) * s
        z = (m12 + m21) * s
        w = (m02 - m20) * s
    else:
        s = (1.0 + m22 - m00 - m11) ** 0.5
        z = 0.5 * s
        s = 0.5 / s
        x = (m02 + m20) * s
        y = (m12 + m21) * s
        w = (m10 - m01) * s
    return (x, y, z, w)


def bone_rest_worlds(bones_local, parents):
    """bones_local: list of (t,q); parents: list of parent indices (-1 root)."""
    worlds = []
    for i, (t, q) in enumerate(bones_local):
        local = trs(t, q)
        p = parents[i] if i < len(parents) else -1
        if p < 0 or p >= len(worlds):
            worlds.append(local)
        else:
            worlds.append(mat_mul(worlds[p], local))
    return worlds


def _civ_base_unit_id(asset_id):
    if asset_id in TEMPLATE_BASE:
        return TEMPLATE_BASE[asset_id]
    if "_" not in asset_id:
        return None
    code, base = asset_id.split("_", 1)
    if code in CIV_CORE_ACTORS and base in UNIT_IDS:
        return base
    return None


def _civ_base_building_id(asset_id):
    if "_" not in asset_id:
        return None
    code, base = asset_id.split("_", 1)
    if code not in CIV_ART_FOLDERS:
        return None
    if base in BUILDING_IDS:
        return base
    if base.endswith("_rubble") and base[: -len("_rubble")] in BUILDING_IDS:
        return base
    return None


def asset_dir(asset_id):
    if asset_id in UNIT_IDS or asset_id in ZIP_UNITS or _civ_base_unit_id(asset_id):
        return "models/units"
    if asset_id in BUILDING_IDS or asset_id in ZIP_STRUCTURES or _civ_base_building_id(asset_id):
        return "models/buildings"
    if asset_id in VEGETATION_IDS or asset_id in ZIP_FLORA:
        return "models/vegetation"
    if asset_id in ZIP_PROPS:
        return "models/props"
    raise ValueError(f"unknown asset id {asset_id!r} — add it to UNIT_IDS, BUILDING_IDS, or VEGETATION_IDS")


def _asset_id_from_filename(name):
    if "_tex" in name:
        return name.split("_tex")[0]
    for ext in (".glb.import", ".png.import", ".glb", ".png"):
        if name.endswith(ext):
            return name[: -len(ext)]
    return os.path.splitext(name)[0]


def clean_misplaced_assets(out_dir):
    """Remove files that landed in the wrong models/ folder from older extracts."""
    allowed = {
        "models/units": UNIT_IDS,
        "models/buildings": BUILDING_IDS,
        "models/vegetation": VEGETATION_IDS,
    }
    removed = 0
    for rel, ids in allowed.items():
        folder = os.path.join(out_dir, rel)
        if not os.path.isdir(folder):
            continue
        for name in os.listdir(folder):
            if name in (".", ".."):
                continue
            aid = _asset_id_from_filename(name)
            if aid in ids or (rel == "models/units" and _civ_base_unit_id(aid)):
                continue
            if rel == "models/buildings" and _civ_base_building_id(aid):
                continue
            path = os.path.join(folder, name)
            try:
                os.remove(path)
                removed += 1
                print(f"[clean] removed misplaced {rel}/{name}")
            except OSError as e:
                print(f"[clean] skip {rel}/{name}: {e}")
    if removed:
        print(f"[clean] removed {removed} misplaced files")

# Fallback only when the actor XML declares no <animation file>. Clip names
# MUST match world_view_node (Idle/Walk/Run/Attack/Death/Gather/Build).
# 0 A.D. requires PMD bone count == PSA numKeys (CSkeletonAnimDef).
UNIT_CFG = {
    "villager": ("biped", {
        "Idle": "citizen_idle", "Walk": "citizen_walk", "Run": "citizen_run",
        "Attack": "biped_attack", "Death": "biped_death",
        "Gather": "gather_wood", "GatherTree": "gather_wood",
        "GatherOre": "gather_mine", "GatherRock": "gather_mine",
        "GatherFruit": "gather_fruit_m", "GatherGrain": "gather_grain",
        "GatherMeat": "gather_meat", "Build": "citizen_build",
        "Carry": "carry_wood", "Slaughter": "slaughter"}),
    "swordsman": ("biped", {
        "Idle": "biped_idle", "Walk": "biped_walk", "Run": "biped_run",
        "Attack": "biped_attack", "Death": "biped_death"}),
    "spearman": ("biped", {
        "Idle": "biped_idle", "Walk": "biped_walk", "Run": "biped_run",
        "Attack": "biped_attack", "Death": "biped_death"}),
    "archer": ("biped", {
        "Idle": "biped_idle", "Walk": "biped_walk", "Run": "biped_run",
        "Attack": "archer_attack", "Death": "biped_death"}),
    "cavalry": ("horse", {
        "Idle": "horse_idle", "Walk": "horse_walk", "Run": "horse_run",
        "Attack": "horse_idle", "Death": "horse_death"}),
    "camel": ("camel", {
        "Idle": "camel_idle", "Walk": "camel_walk", "Run": "camel_run",
        "Attack": "camel_attack", "Death": "camel_death"}),
    "elephant": ("elephant", {
        "Idle": "elephant_idle", "Walk": "elephant_walk", "Run": "elephant_run",
        "Attack": "elephant_attack", "Death": "elephant_death"}),
    "worker_elephant": ("elephant", {
        "Idle": "elephant_idle", "Walk": "elephant_walk", "Run": "elephant_run",
        "Attack": "elephant_attack", "Death": "elephant_death"}),
    "chariot": ("chariot", {
        "Idle": "chariot_idle", "Walk": "chariot_walk", "Run": "chariot_walk",
        "Death": "chariot_death"}),
    "scythed_chariot": ("chariot", {
        "Idle": "chariot_idle", "Walk": "chariot_walk", "Run": "chariot_walk",
        "Death": "chariot_death"}),
    # 0 A.D. trader.xml: mechanical trader_cart (4 bones) + horse/rider props
    "trader": ("", {
        "Idle": "trader_idle", "Walk": "trader_walk", "Run": "trader_walk",
        "Death": "trader_death"}),
    "deer": ("deer", {
        "Idle": "deer_idle", "Walk": "deer_walk", "Run": "deer_walk",
        "Death": "deer_death"}),
    "healer": ("biped", {
        "Idle": "healer_idle", "Walk": "healer_walk", "Run": "healer_run",
        "Attack": "healer_heal", "Heal": "healer_heal", "Death": "biped_death"}),
    "ram": ("", {
        "Idle": "ram_idle", "Walk": "ram_walk", "Run": "ram_walk",
        "Attack": "ram_attack", "Death": "ram_death"}),
    "siege_ram": ("", {
        "Idle": "ram_idle", "Walk": "ram_walk", "Run": "ram_walk",
        "Attack": "ram_attack", "Death": "ram_death"}),
    "female_citizen": ("biped", {
        "Idle": "female_idle", "Walk": "female_walk", "Run": "female_run",
        "Attack": "biped_attack", "Death": "biped_death",
        "Gather": "gather_wood", "GatherTree": "gather_wood",
        "GatherOre": "gather_mine", "GatherRock": "gather_mine",
        "GatherFruit": "gather_fruit_f", "GatherGrain": "gather_grain",
        "GatherMeat": "gather_meat", "Build": "citizen_build",
        "Carry": "carry_wood", "Slaughter": "slaughter"}),
    "hero": ("biped", {
        "Idle": "biped_idle", "Walk": "biped_walk", "Run": "biped_run",
        "Attack": "biped_attack", "Death": "biped_death"}),
    "carnyx": ("biped", {
        "Idle": "biped_idle", "Walk": "biped_walk", "Run": "biped_run",
        "Attack": "biped_attack", "Death": "biped_death"}),
    "archer_maiden": ("biped", {
        "Idle": "female_idle", "Walk": "female_walk", "Run": "female_run",
        "Attack": "archer_attack", "Death": "biped_death"}),
    "war_dog": ("mastiff", {
        "Idle": "dog_idle", "Walk": "dog_walk", "Run": "dog_run",
        "Attack": "dog_attack", "Death": "dog_death"}),
    "boar": ("boar", {
        "Idle": "boar_idle", "Walk": "boar_walk", "Run": "boar_run",
        "Attack": "boar_attack", "Death": "boar_death"}),
    "wolf": ("wolf", {
        "Idle": "wolf_idle", "Walk": "wolf_walk", "Run": "wolf_run",
        "Attack": "wolf_attack", "Death": "wolf_death"}),
    "sheep": ("sheep", {
        "Idle": "sheep_idle", "Walk": "sheep_walk", "Run": "sheep_run",
        "Death": "sheep_death"}),
    "sheep_gaia": ("sheep", {
        "Idle": "sheep_idle", "Walk": "sheep_walk", "Run": "sheep_run",
        "Death": "sheep_death"}),
    # G7-H2 map-fauna (skeleton, clips). Actor XML is harvested first; these are
    # fallbacks when an actor variant declares no <animation>. Key counts verified:
    # mesh bones == skeleton bones == PSA keys (0 A.D. CSkeletonAnimDef ENSURE).
    "goat": ("goat", {
        "Idle": "goat_idle", "Walk": "goat_walk", "Run": "goat_walk",
        "Death": "goat_death"}),
    "zebra": ("zebra", {
        "Idle": "zebra_idle", "Walk": "zebra_walk", "Run": "zebra_run",
        "Attack": "zebra_attack", "Death": "zebra_death"}),
    "chicken": ("chicken", {
        "Idle": "chicken_idle", "Walk": "chicken_walk", "Run": "chicken_run",
        "Attack": "chicken_attack", "Death": "chicken_death"}),
    "crocodile": ("crocodile", {
        "Idle": "croc_idle", "Walk": "croc_walk", "Run": "croc_run",
        "Attack": "croc_attack", "Death": "croc_death"}),
    "giraffe": ("giraffe", {
        "Idle": "giraffe_idle", "Walk": "giraffe_walk", "Run": "giraffe_run",
        "Death": "giraffe_death"}),
    "rabbit": ("deer", {
        "Idle": "rabbit_idle", "Walk": "rabbit_walk", "Run": "rabbit_walk",
        "Death": "rabbit_death"}),
    "gazelle": ("deer", {
        "Idle": "deer_idle", "Walk": "deer_walk", "Run": "deer_walk",
        "Attack": "deer_attack", "Death": "deer_death"}),
    "bear": ("bear", {
        "Idle": "bear_idle", "Walk": "bear_walk", "Run": "bear_run",
        "Attack": "bear_attack", "Death": "bear_death"}),
    "cow": ("cow", {
        "Idle": "cow_idle", "Walk": "cow_walk", "Run": "cow_run",
        "Attack": "cow_attack", "Death": "cow_death"}),
    "pig": ("boar", {
        "Idle": "pig_idle", "Walk": "pig_walk", "Run": "pig_run",
        "Attack": "pig_attack", "Death": "pig_death"}),
    "lion": ("lion", {
        "Idle": "lion_idle", "Walk": "lion_walk", "Run": "lion_run",
        "Attack": "lion_attack", "Death": "lion_death"}),
    "lioness": ("lion", {
        "Idle": "lion_idle", "Walk": "lion_walk", "Run": "lion_run",
        "Attack": "lion_attack", "Death": "lion_death"}),
    "donkey": ("donkey", {
        "Idle": "donkey_idle", "Walk": "donkey_walk", "Run": "donkey_walk",
        "Death": "donkey_death"}),
    "fox": ("fox", {
        "Idle": "fox_idle", "Walk": "fox_walk", "Run": "fox_run",
        "Death": "fox_death"}),
    "hawk": ("hawk", {
        "Idle": "hawk_idle", "Walk": "hawk_walk", "Run": "hawk_walk",
        "Death": "hawk_idle"}),
    "catapult": ("lithobolos", {
        "Idle": "catapult_idle", "Attack": "catapult_attack", "Death": "catapult_idle"}),
    "bolt_shooter": ("oxybeles", {
        "Idle": "bolt_idle", "Attack": "bolt_attack", "Death": "bolt_idle"}),
    # G7-M siege engines. Actor <animation> tags are harvested first; these are
    # fallbacks. Key counts verified: mesh bones == skeleton bones == PSA keys.
    # ballista/med + lithobolos/light share the 33-key frame (lithobolos_light
    # skeleton, 33 bones); oxybeles already maps to its 36-bone skeleton.
    "siege_ballista": ("lithobolos", {
        "Idle": "rome_ballista_idle", "Attack": "rome_ballista_attack",
        "Death": "rome_ballista_idle"}),
    "siege_ballista_med": ("lithobolos", {
        "Idle": "rome_ballista_med_idle", "Attack": "rome_ballista_med_attack",
        "Death": "rome_ballista_med_idle"}),
    "siege_lithobolos": ("lithobolos", {
        "Idle": "lithobolos_idle", "Attack": "lithobolos_attack",
        "Death": "lithobolos_idle"}),
    "siege_lithobolos_light": ("lithobolos", {
        "Idle": "lithobolos_light_idle", "Attack": "lithobolos_light_attack",
        "Death": "lithobolos_light_death"}),
    "siege_oxybeles": ("oxybeles", {
        "Idle": "bolt_idle", "Attack": "bolt_attack", "Death": "bolt_idle"}),
    "siege_polybolos": ("polybolos_frame", {
        "Idle": "polybolos_idle", "Attack": "polybolos_attack",
        "Death": "polybolos_idle"}),
    "siege_scorpio": ("scorpio_frame", {
        "Idle": "scorpio_idle", "Attack": "scorpio_attack",
        "Death": "scorpio_idle"}),
    "siege_onager": ("onager_frame", {
        "Idle": "onager_idle", "Attack": "onager_attack",
        "Death": "onager_idle"}),
    "siege_mangonel": ("mangonel_frame", {
        "Idle": "han_mangonel_idle", "Attack": "han_mangonel_attack",
        "Death": "han_mangonel_idle"}),
    "siege_shoushe": ("shoushe_frame", {
        "Idle": "han_shoushe_idle", "Walk": "han_shoushe_walk",
        "Run": "han_shoushe_walk", "Attack": "han_shoushe_attack",
        "Death": "han_shoushe_death"}),
    "siege_tower": ("hele_siege_tower", {
        "Idle": "hele_siege_tower_idle", "Walk": "hele_siege_tower_walk",
        "Run": "hele_siege_tower_walk", "Death": "hele_siege_tower_idle"}),
    # G7-Q hound/elephant/chariot variants + animated pentekonter hull.
    # Triples verified: wolfhound 40==40==40, mastiff 42==42==42,
    # elephant bush/forest 58==58==58, han chariot 15==15==15 (chariot rig),
    # pentekonter 3==3==3 actor-harvested (ship_one_row skeleton).
    "wolfhound": ("wolfhound", {
        "Idle": "hound_idle", "Walk": "hound_walk", "Run": "hound_walk",
        "Attack": "hound_attack", "Death": "hound_death"}),
    "mastiff_gaia": ("mastiff", {
        "Idle": "dog_idle", "Walk": "dog_walk", "Run": "dog_run",
        "Attack": "dog_attack", "Death": "dog_death"}),
    "elephant_bush": ("elephant_bush", {
        "Idle": "elephant_bush_idle", "Walk": "elephant_bush_walk",
        "Run": "elephant_bush_run", "Attack": "elephant_bush_attack",
        "Death": "elephant_bush_death"}),
    "elephant_forest": ("elephant_forest", {
        "Idle": "elephant_forest_idle", "Walk": "elephant_forest_walk",
        "Run": "elephant_forest_run", "Attack": "elephant_forest_attack",
        "Death": "elephant_forest_death"}),
    "chariot_han": ("chariot", {
        "Idle": "chariot_idle", "Walk": "chariot_walk", "Run": "chariot_walk",
        "Attack": "chariot_idle", "Death": "chariot_idle"}),
    "pentekonter": ("ship_one_row", {
        "Idle": "rowing_idle", "Walk": "rowing_walk", "Run": "rowing_walk",
        "Attack": "rowing_idle", "Death": "rowing_idle"}),
    "fishing_boat": ("rowing_boat", {
        "Idle": "rowing_idle", "Walk": "rowing_walk", "Run": "rowing_walk",
        "Gather": "rowing_idle", "Attack": "rowing_idle",
        "Carry": "rowing_walk", "Death": "rowing_idle"}),
    "trireme": ("ship_sail", {
        "Idle": "trireme_idle", "Walk": "trireme_walk", "Run": "trireme_walk",
        # Attack.js SelectAnimation("attack_ranged") → GetAnimations falls back to idle.
        # The visible attack is LaunchProjectile (bolt_tower), not a hull DAE.
        "Attack": "trireme_idle", "Death": "trireme_idle"}),
    "merchant_ship": ("celt_sail", {
        "Idle": "merchant_sail_idle", "Walk": "merchant_sail_walk",
        "Run": "merchant_sail_run", "Death": "merchant_sail_idle"}),
}

# Runtime ClipId states the renderer actually requests, per model kind.
# Ships only need Idle (hulls are often unskinned; sail/oar are harvested if
# the actor declared them). Buildings have no skeletal clips.
_KIND_WANTED_CLIPS = {
    "villager": ("Idle", "Walk", "Run", "Attack", "Death", "Gather",
                 "GatherTree", "GatherOre", "GatherRock", "GatherFruit",
                 "GatherGrain", "GatherMeat", "Build", "Carry", "Slaughter"),
    "female_citizen": ("Idle", "Walk", "Run", "Attack", "Death", "Gather",
                       "GatherTree", "GatherOre", "GatherRock", "GatherFruit",
                       "GatherGrain", "GatherMeat", "Build", "Carry", "Slaughter"),
    "swordsman": ("Idle", "Walk", "Run", "Attack", "Death"),
    "spearman": ("Idle", "Walk", "Run", "Attack", "Death"),
    "archer": ("Idle", "Walk", "Run", "Attack", "Death"),
    "cavalry": ("Idle", "Walk", "Run", "Attack", "Death"),
    "camel": ("Idle", "Walk", "Run", "Attack", "Death"),
    "elephant": ("Idle", "Walk", "Run", "Attack", "Death"),
    "worker_elephant": ("Idle", "Walk", "Run", "Attack", "Death"),
    "chariot": ("Idle", "Walk", "Death"),
    "scythed_chariot": ("Idle", "Walk", "Death"),
    "trader": ("Idle", "Walk", "Death"),
    "healer": ("Idle", "Walk", "Run", "Heal", "Death"),
    "ram": ("Idle", "Walk", "Attack", "Death"),
    "siege_ram": ("Idle", "Walk", "Attack", "Death"),
    "hero": ("Idle", "Walk", "Run", "Attack", "Death"),
    "carnyx": ("Idle", "Walk", "Run", "Attack", "Death"),
    "archer_maiden": ("Idle", "Walk", "Run", "Attack", "Death"),
    "war_dog": ("Idle", "Walk", "Run", "Attack", "Death"),
    # G7-Q animated variants (audit verifies these clips per GLB)
    "wolfhound": ("Idle", "Walk", "Run", "Attack", "Death"),
    "mastiff_gaia": ("Idle", "Walk", "Run", "Attack", "Death"),
    "elephant_bush": ("Idle", "Walk", "Run", "Attack", "Death"),
    "elephant_forest": ("Idle", "Walk", "Run", "Attack", "Death"),
    "chariot_han": ("Idle", "Walk", "Death"),
    "pentekonter": ("Idle", "Walk", "Run"),
    "catapult": ("Idle", "Attack"),
    "bolt_shooter": ("Idle", "Attack"),
    "siege_ballista": ("Idle", "Attack"),
    "siege_ballista_med": ("Idle", "Attack"),
    "siege_lithobolos": ("Idle", "Attack"),
    "siege_lithobolos_light": ("Idle", "Attack"),
    "siege_oxybeles": ("Idle", "Attack"),
    "siege_polybolos": ("Idle", "Attack"),
    "siege_scorpio": ("Idle", "Attack"),
    "siege_onager": ("Idle", "Attack"),
    "siege_mangonel": ("Idle", "Attack"),
    "siege_shoushe": ("Idle", "Walk", "Run", "Attack", "Death"),
    "siege_tower": ("Idle", "Walk", "Run"),
    "fishing_boat": ("Idle",),
    "trireme": ("Idle",),
    "merchant_ship": ("Idle",),
    "deer": ("Idle", "Walk", "Death"),
    "boar": ("Idle", "Walk", "Run", "Attack", "Death"),
    # G7-H2 map-fauna: every GLB below ships all five (actor-harvested, with
    # Walk->Run and Idle->Attack/Death fallbacks in resolve_anim_cfg).
    "goat": ("Idle", "Walk", "Run", "Attack", "Death"),
    "zebra": ("Idle", "Walk", "Run", "Attack", "Death"),
    "chicken": ("Idle", "Walk", "Run", "Attack", "Death"),
    "crocodile": ("Idle", "Walk", "Run", "Attack", "Death"),
    "giraffe": ("Idle", "Walk", "Run", "Attack", "Death"),
    "rabbit": ("Idle", "Walk", "Run", "Attack", "Death"),
    "gazelle": ("Idle", "Walk", "Run", "Attack", "Death"),
    "bear": ("Idle", "Walk", "Run", "Attack", "Death"),
    "cow": ("Idle", "Walk", "Run", "Attack", "Death"),
    "pig": ("Idle", "Walk", "Run", "Attack", "Death"),
    "lion": ("Idle", "Walk", "Run", "Attack", "Death"),
    "lioness": ("Idle", "Walk", "Run", "Attack", "Death"),
    "donkey": ("Idle", "Walk", "Run", "Attack", "Death"),
    "fox": ("Idle", "Walk", "Run", "Attack", "Death"),
    "hawk": ("Idle", "Walk", "Run", "Attack", "Death"),
    "wolf": ("Idle", "Walk", "Run", "Attack", "Death"),
    "sheep": ("Idle", "Walk", "Run", "Death"),
    "sheep_gaia": ("Idle", "Walk", "Run", "Death"),
}


def _kind_for_audit(asset_id):
    if asset_id in _KIND_WANTED_CLIPS:
        return asset_id
    parent = _civ_base_unit_id(asset_id)
    if parent and parent in _KIND_WANTED_CLIPS:
        return parent
    return None


def audit_extracted_clips(manifest):
    """Print GLB clip lists vs the states each unit type can enter in-game."""
    models = manifest.get("models") or {}
    print("[audit] GLB clips vs wanted runtime states")
    missing_assets = 0
    static_hulls = 0
    rows = 0
    for aid in sorted(models):
        kind = _kind_for_audit(aid)
        if not kind:
            continue
        rec = models[aid]
        have = list(rec.get("animations") or [])
        if rec.get("clips_from") in models:
            # A template model plays its donor's clips.
            have = list(models[rec["clips_from"]].get("animations") or [])
        wanted = _KIND_WANTED_CLIPS[kind]
        # Unskinned civ hulls list no clips — rest pose is correct. Do not
        # invent sail Idle or flag them as MISSING.
        if _is_watercraft(aid) and not have:
            rows += 1
            static_hulls += 1
            print(f"  static {aid:28s} []")
            continue
        miss = [c for c in wanted if c not in have]
        rows += 1
        tag = "ok" if not miss else "!"
        if miss:
            missing_assets += 1
            print(f"  {tag} {aid:28s} have={have}  MISSING {miss}")
        else:
            print(f"  {tag} {aid:28s} {have}")
    print(f"[audit] {rows} unit/fauna assets, {static_hulls} static hulls, "
          f"{missing_assets} missing wanted clips")
    return missing_assets


# --------------------------------------------------------------- main -------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=os.path.normpath(DEFAULT_ZIP))
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "assets"))
    ap.add_argument("--all", action="store_true", help="discover extra actors + all terrain types")
    ap.add_argument("--full", action="store_true", help=argparse.SUPPRESS)  # alias for --all, keeps docs ` --full` working
    ap.add_argument("--all-animations", action="store_true", help="extract all 650+ PSA animations (preserves folder structure)")
    ap.add_argument("--all-skeletons", action="store_true", help="extract all skeletons")
    ap.add_argument("--skip-archive-actors", action="store_true",
                    help="with --all, skip composing leftover actors into models/archive/")
    ap.add_argument("--only", nargs="+", help="extract only these asset ids")
    ap.add_argument("--templates", action="store_true",
                    help="one model per civ roster template whose actor has none yet (with --only: id prefixes),"
                         " and src/graphics/assets/unit_template_models.h")
    ap.add_argument("--class", dest="klass", default=None,
                    help="extract a curated HIGH-VISIBILITY class: g7h2 (map fauna + "
                         "landmarks/civics + flora variety, 31 models), g7m (siege "
                         "engines + towers/walls/docks + civics, 29 models), g7q "
                         "(warships + smith/forge/field/harbor + hound/elephant/"
                         "chariot variants, 32 models). Expands to --only ids.")
    ap.add_argument("--pattern", default=None,
                    help="extract asset ids matching a substring (e.g. fauna, stoa, tree)")
    ap.add_argument("--list-actors", action="store_true", help="list all art/actors/*.xml in zip and exit")
    ap.add_argument("--audit", action="store_true",
                    help="print clip coverage vs wanted runtime states and exit (uses existing manifest)")
    args = ap.parse_args()
    if getattr(args, "full", False):
        args.all = True
    out_dir = os.path.abspath(args.out)
    print(f"archive: {args.zip}")
    print(f"output : {out_dir}")
    if args.audit:
        man_path = os.path.join(out_dir, "models", "manifest.json")
        with open(man_path) as f:
            audit_extracted_clips(json.load(f))
        return
    # Other tools write fauna and packed-siege GLBs here too; a partial run
    # (--only, --templates) must not delete them.
    if not args.only and not args.templates:
        clean_misplaced_assets(out_dir)
    manifest = {"generator": "tools/extract_0ad_assets.py",
                "origin": "0 A.D. by Wildfire Games (public mod archive)",
                "license": "CC-BY-SA 3.0 / GPL-2.0 (see docs/0AD_ATTRIBUTION.md)",
                "models": {}}
    # A 0 A.D. git checkout stands in for public.zip: its Collada sources
    # convert on read (tools/oad_collada_archive.py).
    if os.path.isdir(args.zip):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from oad_collada_archive import ColladaArchive
        archive = ColladaArchive(args.zip)
    else:
        archive = zipfile.ZipFile(args.zip)
    with archive as zf:
        vfs = Vfs(zf)
        if args.list_actors:
            for n in sorted(zf.namelist()):
                if n.startswith("art/actors/") and n.endswith(".xml"):
                    print(n)
            return
        assets = dict(ASSETS)
        civ_ids = register_civ_unit_assets(assets, zf)
        bld_ids = register_civ_building_assets(assets, zf)
        assets.update({k: v for k, v in ZIP_PROPS.items() if v[0] in vfs._names})
        assets.update({k: v for k, v in ZIP_FLORA.items() if v[0] in vfs._names})
        print(f"[civ] registered {len(civ_ids)} civilization unit meshes + {len(bld_ids)} buildings")
        terrain = dict(TERRAIN_TILES)
        archive_actors = []
        template_plan = None
        if args.templates:
            template_plan = register_template_assets(assets, zf)
            tids = sorted(TEMPLATE_BASE)
            if args.only:
                tids = [t for t in tids if any(t == o or t.startswith(o) for o in args.only)]
            print(f"[templates] {len(TEMPLATE_BASE)} template models planned, extracting {len(tids)}")
            args.only = tids or ["--templates matched nothing"]
        if args.all:
            assets.update(FULL_ASSETS)
            register_civ_unit_assets(assets, zf)
            register_civ_building_assets(assets, zf)
            terrain = discover_terrain_tiles(zf)
            print(f"[all] roster {len(assets)} models + {len(terrain)} terrain tiles")
            if args.skip_archive_actors:
                print("[all] skipping leftover actors (--skip-archive-actors)")
            else:
                already_refs = {v[0] for v in assets.values()}
                archive_actors = discover_archive_actors(zf, already_refs, set(assets))
                print(f"[all] {len(archive_actors)} extra actors -> models/archive/")
        if getattr(args, "klass", None):
            cls = args.klass
            if cls not in G7_CLASSSETS:
                sys.exit(f"--class {cls!r} unknown. choices: {', '.join(sorted(G7_CLASSSETS))}")
            extra = [i for i in G7_CLASSSETS[cls] if i in ASSETS]
            args.only = list(args.only or []) + [i for i in extra if i not in (args.only or [])]
            print(f"[class] {cls}: {len(extra)} ids")
        if getattr(args, "pattern", None):
            pat = args.pattern.lower()
            hits = [k for k in ASSETS if pat in k.lower()]
            if not hits:
                sys.exit(f"--pattern {args.pattern!r} matched nothing")
            args.only = list(args.only or []) + [i for i in hits if i not in (args.only or [])]
            print(f"[pattern] {args.pattern!r}: {len(hits)} ids")
        if args.only:
            known = dict(ASSETS)
            known.update(FULL_ASSETS)
            register_civ_unit_assets(known, zf)
            register_civ_building_assets(known, zf)
            known.update({k: v for k, v in ZIP_PROPS.items() if v[0] in vfs._names})
            known.update({k: v for k, v in ZIP_FLORA.items() if v[0] in vfs._names})
            known.update({k: assets[k] for k in TEMPLATE_BASE if k in assets})
            only = set(args.only)
            def wanted(aid):
                if aid in only:
                    return True
                for o in only:
                    if aid.endswith("_" + o):
                        return True
                return False
            assets = {k: v for k, v in known.items() if wanted(k)}
            if not assets:
                sys.exit(f"--only matched nothing. known ids: {', '.join(sorted(known))}")
        for asset_id, (actor_ref, fit, target, tint) in assets.items():
            try:
                cfg = resolve_anim_cfg(vfs, asset_id, actor_ref)
                if cfg:
                    sk, anims = cfg
                    manifest["models"][asset_id] = compile_unit_animated(
                        vfs, asset_id, actor_ref, fit, target, tint, out_dir, sk, anims)
                elif asset_id in RIDER_ATTACHMENTS:
                    # Static hull + sit-baked crew (0 A.D. left the hull unskinned).
                    manifest["models"][asset_id] = compile_unit_animated(
                        vfs, asset_id, actor_ref, fit, target, tint, out_dir, "", {})
                elif asset_id in ZIP_FLORA:
                    # 0.28 picks one look per tree; the actor's others go out as <id>_v2.._v4,
                    # which the 3D view mixes per tree.
                    rec = compile_asset(vfs, asset_id, actor_ref, fit, target, tint, out_dir,
                                        want_origin=True)
                    origin = rec.pop("_origin")
                    manifest["models"][asset_id] = rec
                    for look in range(1, min(FLORA_LOOKS, actor_look_count(vfs, actor_ref))):
                        vid = f"{asset_id}_v{look + 1}"
                        vrec = compile_asset(vfs, vid, actor_ref, fit, target, tint, out_dir,
                                             rel_dir="models/vegetation", look=look,
                                             origin=origin)
                        manifest["models"][vid] = vrec
                else:
                    manifest["models"][asset_id] = compile_asset(
                        vfs, asset_id, actor_ref, fit, target, tint, out_dir)
            except Exception as e:
                print(f"[{asset_id}] FAILED: {e}")
                if os.environ.get("RTSO_EXTRACT_TRACE"):
                    import traceback
                    traceback.print_exc()
                continue
        archive_ok = 0
        archive_skip = 0
        for asset_id, actor_ref in archive_actors:
            fit, target = _archive_fit(actor_ref)
            try:
                cfg = resolve_anim_cfg(vfs, asset_id, actor_ref)
                if cfg:
                    sk, anims = cfg
                    rec = compile_unit_animated(
                        vfs, asset_id, actor_ref, fit, target, None, out_dir, sk, anims,
                        rel_dir="models/archive")
                else:
                    rec = compile_asset(
                        vfs, asset_id, actor_ref, fit, target, None, out_dir,
                        rel_dir="models/archive")
                rec["archive"] = True
                rec["source"] = actor_ref
                manifest.setdefault("archive", {})[asset_id] = rec
                archive_ok += 1
            except Exception as e:
                archive_skip += 1
                print(f"[archive] SKIP {asset_id}: {e}")
                continue
        if archive_actors:
            print(f"[archive] composed {archive_ok}, skipped {archive_skip}")
        if not args.only:
            terr_dir = os.path.join(out_dir, "textures", "terrain")
            os.makedirs(terr_dir, exist_ok=True)
            for name, ref in terrain.items():
                img = vfs.load_texture_rgba(ref)
                if img is None:
                    print(f"! terrain tile missing: {ref}")
                    continue
                img = img.resize((512, 512), Image.LANCZOS)
                p = os.path.join(terr_dir, f"{name}.png")
                img.save(p, optimize=True)
                print(f"[terrain] {name} <- {ref}")
                manifest.setdefault("terrain", {})[name] = ref
        # music
        mus_dir = os.path.join(out_dir, "music")
        os.makedirs(mus_dir, exist_ok=True)
        for out_name, ref in MUSIC.items():
            dst = os.path.join(mus_dir, out_name)
            if os.path.exists(dst) or not vfs.exists(ref):
                continue
            with open(dst, "wb") as f:
                f.write(zf.read(ref))
            print(f"[music] {out_name} ({zf.getinfo(ref).file_size//1024}KB)")
            manifest.setdefault("music", {})[out_name] = ref
        # skeletons
        skel_dir = os.path.join(out_dir, "skeletons")
        os.makedirs(skel_dir, exist_ok=True)
        # --templates and --only redo models only; the shared skeletons and clips stay as they are.
        skels_to_extract = {} if args.templates or args.only else dict(SKELETONS)
        if args.all_skeletons:
            # discover all skeletons in zip
            for n in zf.namelist():
                if n.startswith("art/skeletons/") and n.endswith(".xml"):
                    name = os.path.splitext(os.path.basename(n))[0]
                    if name not in skels_to_extract:
                        skels_to_extract[name] = n
        for skel_name, ref in skels_to_extract.items():
            if ref not in zf.namelist():
                print(f"! skeleton missing: {ref}")
                continue
            data = zf.read(ref)
            dst = os.path.join(skel_dir, f"{skel_name}.xml")
            with open(dst, "wb") as f:
                f.write(data)
            bones = parse_skeleton_xml(data)
            print(f"[skeleton] {skel_name}.xml ({len(bones)} bones) <- {ref}")
            manifest.setdefault("skeletons", {})[skel_name] = {"file": f"skeletons/{skel_name}.xml", "bones": len(bones), "source": ref}
            # also verify PMD bone count matches skeleton
        # animations — copy PSSA binary + decode to JSON for runtime + manifest
        anim_dir = os.path.join(out_dir, "animations")
        os.makedirs(anim_dir, exist_ok=True)
        anims_to_extract = {} if args.templates or args.only else dict(ANIMATIONS)
        if args.all_animations:
            # discover all PSA files
            for n in zf.namelist():
                if n.startswith("art/animation/") and n.endswith(".psa"):
                    # keep relative path as key, sanitize
                    rel = n[len("art/animation/"):-len(".dae.cached.psa")]
                    # replace slashes with _ for flat file, but preserve hierarchy in manifest
                    flat = rel.replace("/", "_").replace("\\", "_")
                    if flat not in anims_to_extract and n not in anims_to_extract.values():
                        anims_to_extract[flat] = n
            print(f"[all-animations] discovered {len(anims_to_extract)} total PSA (curated {len(ANIMATIONS)} + discovered)")
        for anim_name, ref in anims_to_extract.items():
            if ref.startswith("assets/"):
                continue  # hand-authored clip already living in the output tree
            if ref not in zf.namelist():
                print(f"! anim missing: {ref}")
                continue
            data = zf.read(ref)
            # verify it parses
            try:
                psa = parse_psa(data)
                # write binary preserving hierarchy if all-animations
                if "/" in ref or "\\" in ref:
                    # use flat name for file but keep source
                    dst = os.path.join(anim_dir, f"{anim_name}.psa")
                else:
                    dst = os.path.join(anim_dir, f"{anim_name}.psa")
                # ensure parent dir if we preserve hierarchy
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "wb") as f:
                    f.write(data)
                # write human-readable JSON summary (first frame only) for debugging
                jdst = os.path.join(anim_dir, f"{anim_name}.json")
                with open(jdst, "w") as jf:
                    json.dump({
                        "source": ref,
                        "frameTime": psa["frameTime"],
                        "numFrames": psa["numFrames"],
                        "numKeys": psa["numKeys"],
                        "duration": psa["frameTime"] * psa["numFrames"],
                        "bones": psa["numKeys"],
                    }, jf, indent=1)
                # only print curated or first 50 if all
                if anim_name in ANIMATIONS or len(manifest.get("animations", {})) < 50:
                    print(f"[anim] {anim_name}.psa frames={psa['numFrames']} bones={psa['numKeys']} dur={psa['frameTime']*psa['numFrames']:.0f}ms <- {ref}")
                elif len(manifest.get("animations", {})) == 50:
                    print(f"[anim] ... and {len(anims_to_extract)-50} more (suppressed)")
                manifest.setdefault("animations", {})[anim_name] = {
                    "file": f"animations/{anim_name}.psa",
                    "json": f"animations/{anim_name}.json",
                    "source": ref,
                    "frames": psa["numFrames"],
                    "bones": psa["numKeys"],
                    "frameTime": psa["frameTime"],
                }
            except Exception as e:
                print(f"! anim parse failed {anim_name}: {e}")
                continue
    man_path = os.path.join(out_dir, "models", "manifest.json")
    if args.only:
        prev_path = os.path.join(out_dir, "models", "manifest.json")
        if os.path.exists(prev_path):
            with open(prev_path) as f:
                prev = json.load(f)
            prev.setdefault("models", {}).update(manifest.get("models", {}))
            prev.setdefault("animations", {}).update(manifest.get("animations", {}))
            prev.setdefault("skeletons", {}).update(manifest.get("skeletons", {}))
            manifest = prev
    manifest["mapping"] = {
        "uv0": "baseTex atlas (TEXCOORD_0). PMD OpenGL V flipped for glTF/Godot.",
        "uv1_ao": "aoTex unique unwrap sampled into COLOR_0. Never multiplied onto UV0 pixels.",
        "player_color": "baseTex.a masks mix(playerColor, white) into RGB (model_common.fs).",
        "skin": "PMD SVertexBlend 4 bones+weights -> JOINTS_0/WEIGHTS_0. Prop attach bone if no blends.",
        "skeleton": {uid: cfg[0] for uid, cfg in UNIT_CFG.items()},
        "clips": {uid: cfg[1] for uid, cfg in UNIT_CFG.items()},
    }
    with open(man_path, "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    print(f"manifest -> {man_path}")
    if template_plan:
        import unit_template_models as utm
        units_dir = os.path.join(out_dir, "models", "units")
        per_row = [m if m and (m in manifest.get("models", {}) or os.path.exists(os.path.join(units_dir, m + ".glb")))
                   else None for m in template_plan[0]]
        repo_assets = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets"))
        hdr = utm.HEADER if os.path.normpath(out_dir) == repo_assets else os.path.join(out_dir, "unit_template_models.h")
        have = lambda m: m and (m in manifest.get("models", {}) or os.path.exists(os.path.join(units_dir, m + ".glb")))
        ranks = {t: tuple(m if have(m) else None for m in pair) for t, pair in template_plan[2].items()}
        ranks = {t: pair for t, pair in ranks.items() if any(pair)}
        models = {}
        for m in per_row + [m for pair in ranks.values() for m in pair]:
            if m and m in TEMPLATE_BASE:
                rec = manifest.get("models", {}).get(m, {})
                code = m.split("_", 1)[0]
                models[m] = (f"{code}_{TEMPLATE_BASE[m]}", rec.get("clips_from"))
        utm.write_header(per_row, models, hdr, ranks)
        print(f"[templates] {sum(1 for m in per_row if m)} of {len(per_row)} templates have a model -> {hdr}")
    # also write skeleton/animation verification summary
    print(f"extracted {len(manifest.get('animations',{}))} animations + {len(manifest.get('skeletons',{}))} skeletons")
    audit_extracted_clips(manifest)


if __name__ == "__main__":
    main()
