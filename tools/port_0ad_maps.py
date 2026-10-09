"""Converts 0 A.D. scenario and skirmish maps (.xml + .pmp) to RTS0 JSON in data/maps/.

  python3 tools/port_0ad_maps.py                  # the built-in list, from a local public.zip
  python3 tools/port_0ad_maps.py --github NAME..  # skirmish maps by 0 A.D. file name (e.g.
                                                  # libyan_oases_4p), fetched from 0 A.D.'s GitHub
  python3 tools/port_0ad_maps.py --github-builtin # the built-in list, fetched from GitHub
  python3 tools/port_0ad_maps.py --github-scenarios [NAME..]  # scenario maps, same way

Maps fetched with --github get their world size from the .pmp (one RTS0 world unit per
0 A.D. tile, clamped to 128..512) and a heightmap of one sample per 4 tiles.
0 A.D. maps are CC BY-SA 3.0; see docs/0AD_ATTRIBUTION.md.
"""
import zipfile
import xml.etree.ElementTree as ET
import json
import struct
import math
import os
import sys
import urllib.parse
import urllib.request
import urllib.error
import time

zip_path = "/home/abhi/project/0ad/binaries/data/mods/public/public.zip"
GITHUB_RAW = "https://raw.githubusercontent.com/0ad/0ad/master/binaries/data/mods/public/"
NAV_WATER_MARGIN = 0.3


class GithubSource:
    """Reads 0 A.D. files from its GitHub repository, like ZipFile.read."""
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def read(self, rel):
        url = GITHUB_RAW + urllib.parse.quote(rel)
        with urllib.request.urlopen(url, timeout=60) as r:
            return r.read()


def pmp_tiles(raw_pmp):
    """Map width in tiles (16 tiles per patch)."""
    return struct.unpack('<I', raw_pmp[12:16])[0] * 16

def extract_pmp_heights(raw_pmp, target_res=64, height_scale=0.20):
    magic = raw_pmp[:4].decode('ascii', errors='ignore')
    if magic != "PSMP":
        return [0.0] * (target_res * target_res), 0.0
    patches = struct.unpack('<I', raw_pmp[12:16])[0]
    vert_side = patches * 16 + 1
    num_heights = vert_side * vert_side
    offset = 16
    raw_h = struct.unpack(f'<{num_heights}H', raw_pmp[offset:offset + num_heights * 2])

    heights = []
    for r in range(target_res):
        for c in range(target_res):
            src_r = int(r * (vert_side - 1) / (target_res - 1))
            src_c = int(c * (vert_side - 1) / (target_res - 1))
            idx = src_r * vert_side + src_c
            raw_val = raw_h[idx]
            # 0 A.D. source/graphics/Terrain.h: HEIGHT_UNITS_PER_METRE = 92. Water
            # heights in the map XML are in metres too, so both scale the same way.
            val_m = (raw_val / 92.0) * height_scale
            heights.append(round(val_m, 2))

    return heights, vert_side

def gaia_entity(ltpl, wx, wz):
    """0 A.D. gaia/fauna_*, gaia/treasure/* and gaia/ruins/* templates as RTS0 map
    entries, named by template stem (see src/simulation/gaia_data.h)."""
    if "gaia/fish/" in ltpl:
        return None
    if "merc_camp" in ltpl:  # structures/merc_camp_egyptian: a capturable Gaia mercenary camp
        return "gaia_structures", {"type": "mercenary_camp", "x": wx, "z": wz}
    for prefix, key in (("gaia/treasure/", "treasures"), ("gaia/ruins/", "ruins")):
        if prefix in ltpl:
            return key, {"type": ltpl.split(prefix, 1)[1], "x": wx, "z": wz}
    if "gaia/fauna_" in ltpl:
        stem = ltpl.split("gaia/fauna_", 1)[1]
        if stem.endswith("_trainable"):
            stem = stem[: -len("_trainable")]
        return "fauna", {"type": stem, "x": wx, "z": wz}
    return None

_TEMPLATE_CACHE = {}

def _read_template(source, rel):
    """Parsed simulation/templates/<rel>.xml, or None if 0 A.D. has no such file."""
    for attempt in range(5):
        try:
            return ET.fromstring(source.read(f"simulation/templates/{rel}.xml").decode("utf-8"))
        except urllib.error.HTTPError as e:
            if e.code == 404:
                break
            time.sleep(2 ** attempt)
        except KeyError:  # not in the zip
            break
        except Exception:
            time.sleep(2 ** attempt)
    print(f"  ?? template {rel} not found")
    return None


def _supply(root):
    rs = root.find("ResourceSupply") if root is not None else None
    if rs is None:
        return None, None
    try:
        mx = float(rs.findtext("Max"))
    except (TypeError, ValueError):
        mx = None
    return (rs.findtext("Type") or "").strip() or None, mx


def template_info(source, name):
    """0 A.D. template `name` with its parents resolved: {"chain": [name, parent, ...],
    "supply_type": "stone.rock" or None, "supply_max": float or None}. A parent like
    "bush|template_gaia_tree" is the mixin simulation/templates/mixins/bush.xml applied over
    template_gaia_tree; the leftmost part wins."""
    if name in _TEMPLATE_CACHE:
        return _TEMPLATE_CACHE[name]
    if "|" in name:
        mixin, rest = name.split("|", 1)
        base = template_info(source, rest)
        root = _read_template(source, f"mixins/{mixin}")
        parent = None
    else:
        base = None
        root = _read_template(source, name)
        parent = root.get("parent") if root is not None else None
        if parent:
            base = template_info(source, parent)
        mixin = name
    st, mx = _supply(root)
    info = {"chain": [mixin] + (base["chain"] if base else []),
            "supply_type": st or (base["supply_type"] if base else None),
            "supply_max": mx if mx is not None else (base["supply_max"] if base else None)}
    _TEMPLATE_CACHE[name] = info
    return info


# 0 A.D. structure template class -> RTS0 building name (main_node.cpp map_building_type).
# template_structure_special is not ported; plain template_structure is scenery (gaia_owned).
GAIA_STRUCTURE_CLASSES = [
    ("template_structure_civic_civil_centre", "town_center"),
    ("template_structure_civic_house", "house"),
    ("template_structure_civic_temple", "temple"),
    ("template_structure_economic_market", "market"),
    ("template_structure_economic_storehouse", "storehouse"),
    ("template_structure_economic_farmstead", "farmstead"),
    ("template_structure_military_barracks", "barracks"),
    ("template_structure_military_stable", "stable"),
    ("template_structure_military_range", "range"),
    ("template_structure_military_dock", "dock"),
    ("template_structure_military_fortress", "fortress"),
    ("template_structure_defensive_tower_sentry", "sentry_tower"),
    ("template_structure_defensive_tower_outpost", "outpost"),
    ("template_structure_defensive_tower", "tower"),
    ("template_structure_defensive_palisade", "palisade"),
    ("template_structure_defensive_wall_tower", "wall_tower"),
    ("template_structure_defensive_wall_gate", "gate"),
    ("template_structure_defensive_wall", "wall_segment"),
    ("template_structure_resource_corral", "corral"),
    ("template_structure_resource_field", "field_plot"),  # anyone may farm a Gaia field
]


def gaia_owned(source, tpl, wx, wz, angle=0.0, scale=1.0):
    """A Gaia-owned 0 A.D. unit or structure (Player 0 in the map) as a gaia_units,
    gaia_structures or scenery entry; None for what RTS0 doesn't port."""
    info = template_info(source, tpl)
    chain = info["chain"]
    if tpl.startswith("structures/"):
        if len(chain) > 1 and chain[1] == "template_structure" and tpl.count("/") == 1:
            # Fences, columns, obelisks, benches (gaia_data.h SceneryTypeInfo). scale: RTS0
            # units per 0 A.D. metre, for the obstruction size.
            return "scenery", {"type": tpl.split("/", 1)[1], "x": wx, "z": wz,
                               "angle": round(angle, 4), "scale": round(scale, 5)}
        for cls, name in GAIA_STRUCTURE_CLASSES:
            if any(c.startswith(cls) for c in chain):
                return "gaia_structures", {"type": name, "template": tpl, "x": wx, "z": wz}
        return None
    if tpl.startswith("units/"):
        utype = unit_class(" ".join([tpl.lower()] + chain))
        if utype is None:
            return None
        stem = tpl[len("units/"):]
        rank = 2 if stem.endswith("_a") else 3 if stem.endswith("_e") else 1
        return "gaia_units", {"type": utype, "template": stem, "rank": rank, "x": wx, "z": wz}
    return None


def unit_class(ltpl):
    """RTS0 unit class for a 0 A.D. unit template name (lower case), or None."""
    # Most specific first: a hero or champion cavalry template's chain also names its weapon.
    for keys, utype in (
            (["hero"], "hero"),
            (["ship_fishing", "fishing_boat"], "fishing_boat"),
            (["ship_merchant"], "merchant_ship"),
            (["ship"], "trireme"),
            (["elephant"], "elephant"),
            (["chariot"], "chariot"),
            (["siege_ram"], "siege_ram"),
            (["bolt_shooter"], "bolt_shooter"),
            (["catapult", "onager", "lithobolos"], "catapult"),
            (["war_dog", "_dog"], "war_dog"),
            (["healer"], "healer"),
            (["trader"], "trader"),
            (["female_citizen", "support_female", "citizen"], "villager"),
            (["cavalry"], "cavalry"),
            (["spearman", "pikeman", "hoplite"], "spearman"),
            (["infantry_ranged", "archer", "slinger", "javelineer"], "archer"),
            (["swordsman", "infantry_melee", "champion"], "swordsman")):
        if any(k in ltpl for k in keys):
            return utype
    return None

def gaia_resource(source, ltpl, wx, wz):
    """A Gaia resource supply (tree, fruit, rock, mine) as a resources entry, typed and sized
    by its 0 A.D. template's ResourceSupply. Templates that can't be read fall back to a
    guess from the name."""
    info = template_info(source, ltpl)
    st, amount = info["supply_type"], info["supply_max"]
    if st is None:
        if len(info["chain"]) > 1 or not ltpl.startswith("gaia/"):
            return None  # read and not a resource, or a prop/particle with no template
        for keys, rtype, amt in ((["fruit", "berry", "apple", "date", "bush_berry", "grain", "food"], "food", 300),
                                 (["stone", "rock"], "stone", 1000),
                                 (["metal", "gold", "silver", "iron", "ore"], "gold", 1000),
                                 (["tree", "palm", "pine", "cedar", "cypress", "oak", "carob", "poplar", "wood", "forest"], "wood", 300)):
            if any(k in ltpl for k in keys):
                st, amount = rtype, amt
                break
        else:
            return None
    kind = st.split(".")[0]
    if amount is None or amount != amount or amount == float("inf"):
        return None
    if kind == "wood":
        rtype = "palm" if ("palm" in ltpl or "date" in ltpl) else ("pine" if ("pine" in ltpl or "cedar" in ltpl or "cypress" in ltpl) else "tree")
    elif kind == "stone":
        rtype = "stone"
    elif kind == "metal" or kind == "gold":
        rtype = "gold"
    elif st == "food.fish":
        rtype = "fish"
    elif kind == "food":
        if st == "food.grain":
            return None  # a Gaia field: nobody may farm it
        rtype = "food"
    else:
        return None
    return {"type": rtype, "x": wx, "z": wz, "amount": int(amount)}

def port_map(map_id, xml_rel, pmp_rel, world_size_m=256.0, biome="mediterranean", height_scale=0.20, source=None, gaia_only=False):
    print(f"Porting map: {map_id} ({xml_rel})...")
    with (source if source is not None else zipfile.ZipFile(zip_path, 'r')) as z:
        files = z  # `z` is reused below for entity positions
        xml_data = z.read(xml_rel).decode('utf-8')
        root = ET.fromstring(xml_data)

        # Script settings
        s_elem = root.find("ScriptSettings")
        settings = json.loads(s_elem.text) if s_elem is not None and s_elem.text else {}
        name = settings.get("Name", map_id.replace("_", " ").title())
        desc = settings.get("Description", "")

        # Terrain heights from PMP
        raw_pmp = z.read(pmp_rel)
        target_res = 64
        if world_size_m is None:
            # One RTS0 world unit per 0 A.D. tile, as for random maps.
            world_size_m = float(min(512, max(128, pmp_tiles(raw_pmp))))
            target_res = max(64, int(world_size_m) // 4 + 1)
        heightmap, vert_side = extract_pmp_heights(raw_pmp, target_res=target_res, height_scale=height_scale)

        # Water settings
        water_height = -1.0
        w_elem = root.find(".//WaterBody/Height")
        if w_elem is not None:
            try:
                raw_w = float(w_elem.text)
                # RTS0's nav grid counts a cell as water below water_height + 0.3
                # (NavGrid::set_water_from_heights). Take that margin off here so
                # only ground under 0 A.D.'s water line becomes water.
                water_height = round(raw_w * height_scale - NAV_WATER_MARGIN, 2)
            except Exception:
                pass

        # Coordinate transformation from 0 A.D. to RTS0 centered space
        map_span_0ad = (vert_side - 1) * 4.0
        center_x = map_span_0ad / 2.0
        center_z = map_span_0ad / 2.0
        scale = world_size_m / map_span_0ad

        entities_elem = root.find("Entities")
        raw_entities = []
        angle_by_uid = {}  # uid -> Orientation y (radians)
        garrisoned_in = {}  # passenger uid -> holder uid

        if entities_elem is not None:
            for e in entities_elem.findall("Entity"):
                tpl = e.find("Template").text if e.find("Template") is not None and e.find("Template").text else ""
                ply = int(e.find("Player").text) if e.find("Player") is not None and e.find("Player").text else 0
                pos = e.find("Position")
                if pos is None:
                    continue
                x = float(pos.get("x", 0))
                z = float(pos.get("z", 0))
                raw_entities.append((tpl, ply, x, z, e.get("uid")))
                orient = e.find("Orientation")
                if orient is not None:
                    angle_by_uid[e.get("uid")] = float(orient.get("y", 0) or 0)
                # <Garrison><GarrisonedEntity uid=../></Garrison>: those entities start
                # inside this one (sicilia_nomad_2p's ships); their own Position is 0,0.
                g = e.find("Garrison")
                if g is not None:
                    for ge in g.findall("GarrisonedEntity"):
                        garrisoned_in[ge.get("uid")] = e.get("uid")

        # Convert entities into RTS0 format
        resources = []
        gaia = {"fauna": [], "treasures": [], "ruins": [], "gaia_structures": [], "gaia_units": [], "scenery": []}
        gaia_unit_index = {}       # uid -> index in gaia["gaia_units"]
        gaia_structure_index = {}  # uid -> index in gaia["gaia_structures"]
        fauna = gaia["fauna"]
        players = {}
        structures = []
        units = []

        pos_by_uid = {uid: (rx, rz) for _, _, rx, rz, uid in raw_entities}
        unit_index = {}  # uid -> index in units
        for tpl, ply, rx, rz, uid in raw_entities:
            if uid in garrisoned_in:
                rx, rz = pos_by_uid.get(garrisoned_in[uid], (rx, rz))
            wx = round((rx - center_x) * scale, 2)
            wz = round((rz - center_z) * scale, 2)

            # Clamp within map boundary
            half = world_size_m * 0.48
            wx = max(-half, min(half, wx))
            wz = max(-half, min(half, wz))

            ltpl = tpl.lower()
            if not ltpl or ltpl.startswith("actor|") or "decal" in ltpl or "particle" in ltpl:
                continue

            if ply == 0:
                # Gaia: resources, fauna, treasures, ruins, and Gaia-owned units and structures
                if "gaia/fish/" in ltpl:
                    # template_gaia_fish.xml: 1000 food.fish, gathered only by fishing boats
                    resources.append({"type": "fish", "x": wx, "z": wz, "amount": 1000})
                elif gaia_entity(ltpl, wx, wz):
                    key, ent = gaia_entity(ltpl, wx, wz)
                    gaia[key].append(ent)
                elif ltpl.startswith(("units/", "structures/")):
                    owned = gaia_owned(files, ltpl, wx, wz, angle_by_uid.get(uid, 0.0), scale)
                    if owned:
                        key, ent = owned
                        if key == "gaia_units":
                            gaia_unit_index[uid] = len(gaia[key])
                        elif key == "gaia_structures":
                            gaia_structure_index[uid] = len(gaia[key])
                        gaia[key].append(ent)
                else:
                    res = gaia_resource(files, ltpl, wx, wz)
                    if res:
                        resources.append(res)
            else:
                # Player base / units
                if ply not in players:
                    players[ply] = {"id": ply, "team": ply - 1, "start_x": wx, "start_z": wz}
                n_units = len(units)
                # Unit templates (units/, skirmish/units/) never match the structure
                # names below; "default_infantry_ranged_b" used to match "range".
                is_unit = "units/" in ltpl

                # Structures
                if is_unit:
                    pass
                elif any(k in ltpl for k in ["civil_centre", "town_center", "civic"]):
                    players[ply]["start_x"] = wx
                    players[ply]["start_z"] = wz
                    structures.append({"type": "town_center", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["barracks", "range", "stable", "hall", "arsenal", "elephant_stable", "military"]):
                    structures.append({"type": "barracks", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["house", "apartment", "hut", "longhouse"]):
                    structures.append({"type": "house", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["storehouse", "forge"]):
                    structures.append({"type": "storehouse", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["farmstead", "field", "mill", "rotarymill"]):
                    structures.append({"type": "farmstead", "player": ply, "x": wx, "z": wz})
                elif "market" in ltpl:
                    structures.append({"type": "market", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["fortress", "palisades_fort", "keep"]):
                    structures.append({"type": "fortress", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["gate", "wall_gate"]):
                    structures.append({"type": "gate", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["temple", "tophet", "shrine", "pyramid"]):
                    structures.append({"type": "temple", "player": ply, "x": wx, "z": wz})
                elif "wonder" in ltpl:
                    structures.append({"type": "wonder", "player": ply, "x": wx, "z": wz})
                elif "dock" in ltpl:
                    structures.append({"type": "dock", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["wall_tower", "defense_tower", "sentry_tower", "outpost", "tower"]):
                    structures.append({"type": "tower", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["wall", "palisade"]):
                    structures.append({"type": "wall_segment", "player": ply, "x": wx, "z": wz})
                elif "corral" in ltpl:
                    structures.append({"type": "corral", "player": ply, "x": wx, "z": wz})

                # Units
                if not is_unit:
                    pass
                elif any(k in ltpl for k in ["villager", "support_civilian", "citizen", "female"]):
                    units.append({"type": "villager", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["infantry_melee", "swordsman", "champion"]):
                    units.append({"type": "swordsman", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["spearman", "hoplite", "phalanx", "pikeman"]):
                    units.append({"type": "spearman", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["infantry_ranged", "archer", "slinger", "skirmisher", "ranged"]):
                    units.append({"type": "archer", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["cavalry", "horse"]):
                    units.append({"type": "cavalry", "player": ply, "x": wx, "z": wz})
                elif "trader" in ltpl:
                    units.append({"type": "trader", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["catapult", "onager", "ballista"]):
                    units.append({"type": "catapult", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["hero", "special_starting_unit"]):
                    units.append({"type": "hero", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["trireme", "ship_arrow", "ship_scout", "warship", "ship"]):
                    units.append({"type": "trireme", "player": ply, "x": wx, "z": wz})
                elif "elephant" in ltpl:
                    units.append({"type": "elephant", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["ram", "siege_ram"]):
                    units.append({"type": "siege_ram", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["healer", "priest"]):
                    units.append({"type": "healer", "player": ply, "x": wx, "z": wz})
                elif "chariot" in ltpl:
                    units.append({"type": "chariot", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["dog", "war_dog"]):
                    units.append({"type": "war_dog", "player": ply, "x": wx, "z": wz})
                elif "bolt_shooter" in ltpl:
                    units.append({"type": "bolt_shooter", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["fishing_boat", "boat"]):
                    units.append({"type": "fishing_boat", "player": ply, "x": wx, "z": wz})
                if len(units) > n_units:
                    unit_index[uid] = n_units

        for uid, holder in garrisoned_in.items():
            if uid in unit_index and holder in unit_index:
                units[unit_index[uid]]["garrison_in"] = unit_index[holder]
            elif uid in gaia_unit_index and holder in gaia_unit_index:
                gaia["gaia_units"][gaia_unit_index[uid]]["garrison_in"] = gaia_unit_index[holder]
            elif uid in gaia_unit_index and holder in gaia_structure_index:
                gaia["gaia_units"][gaia_unit_index[uid]]["garrison_in_structure"] = gaia_structure_index[holder]

        # Ensure every detected player has a town center / start position. Nomad maps
        # (no civil centre at all, e.g. sicilia_nomad_2p) start with units only.
        nomad = not any(s["type"] == "town_center" for s in structures)
        for p in ([] if nomad else players.values()):
            has_tc = any(s["type"] == "town_center" and s["player"] == p["id"] for s in structures)
            if not has_tc:
                structures.append({"type": "town_center", "player": p["id"], "x": p["start_x"], "z": p["start_z"]})

        map_data = {
            "id": map_id,
            "name": name,
            "desc": desc,
            "biome": biome,
            "nomad": nomad,
            "world_size": world_size_m,
            "water_height": water_height,
            "heightmap_res": target_res,
            "heightmap": heightmap,
            "players": list(players.values()),
            "resources": resources,
            "fauna": fauna,
            "treasures": gaia["treasures"],
            "ruins": gaia["ruins"],
            "gaia_structures": gaia["gaia_structures"],
            "gaia_units": gaia["gaia_units"],
            "scenery": gaia["scenery"],
            "structures": structures,
            "units": units
        }
        # TriggerScripts, by base name; RTS0 runs its C++ port of each (Simulation::register_trigger_script).
        scripts = [os.path.splitext(os.path.basename(t))[0] for t in settings.get("TriggerScripts", [])
                   if not t.startswith("scripts/")]
        if scripts:
            map_data["trigger_scripts"] = scripts

        os.makedirs("data/maps", exist_ok=True)
        out_path = f"data/maps/{map_id}.json"
        if gaia_only:
            # Keep the existing map; only refresh its Gaia arrays.
            if not os.path.exists(out_path):
                return
            with open(out_path) as f:
                old = json.load(f)
            ws = old.get("world_size", world_size_m)
            k = ws / world_size_m
            gaia["resources"] = resources
            for key in ("resources", "fauna", "treasures", "ruins", "gaia_structures", "gaia_units", "scenery"):
                old[key] = [dict(e, x=round(e["x"] * k, 2), z=round(e["z"] * k, 2)) for e in gaia[key]]
            for e in old["scenery"]:
                e["scale"] = round(e["scale"] * k, 5)
            map_data = old
        with open(out_path, "w") as f:
            json.dump(map_data, f, indent=2)
        print(f"  -> Generated {out_path}: {len(resources)} resources, {len(fauna)} fauna, {len(structures)} structures, {len(units)} units, {len(gaia['gaia_structures'])} Gaia structures, {len(gaia['gaia_units'])} Gaia units, {len(players)} players (Water={water_height}m).")

maps_list = [
    ("acropolis_bay", "maps/skirmishes/acropolis_bay_2p.xml", "maps/skirmishes/acropolis_bay_2p.pmp", 256.0, "mediterranean", 0.20),
    ("corinthian_isthmus", "maps/skirmishes/corinthian_isthmus_4p.xml", "maps/skirmishes/corinthian_isthmus_4p.pmp", 384.0, "mediterranean", 0.20),
    ("atlas_valleys", "maps/skirmishes/atlas_valleys_8p.xml", "maps/skirmishes/atlas_valleys_8p.pmp", 384.0, "savanna", 0.20),
    ("bactria", "maps/skirmishes/bactria_2p.xml", "maps/skirmishes/bactria_2p.pmp", 256.0, "desert", 0.15),
    ("battle_for_the_tiber", "maps/scenarios/battle_for_the_tiber.xml", "maps/scenarios/battle_for_the_tiber.pmp", 320.0, "temperate", 0.20),
    ("belgian_bog", "maps/skirmishes/belgian_bog_2p.xml", "maps/skirmishes/belgian_bog_2p.pmp", 256.0, "temperate", 0.15),
    ("death_canyon", "maps/skirmishes/death_canyon_2p.xml", "maps/skirmishes/death_canyon_2p.pmp", 256.0, "desert", 0.25),
    ("deccan_plateau", "maps/skirmishes/deccan_plateau_2p.xml", "maps/skirmishes/deccan_plateau_2p.pmp", 256.0, "tropical", 0.20),
    ("cycladic_archipelago", "maps/skirmishes/cycladic_archipelago_2p.xml", "maps/skirmishes/cycladic_archipelago_2p.pmp", 256.0, "mediterranean", 0.20),
    ("golden_island", "maps/skirmishes/golden_island_2p.xml", "maps/skirmishes/golden_island_2p.pmp", 256.0, "mediterranean", 0.20),
    ("persian_highlands", "maps/skirmishes/persian_highlands_4p.xml", "maps/skirmishes/persian_highlands_4p.pmp", 384.0, "desert", 0.20),
    ("saharan_oases", "maps/skirmishes/saharan_oases_4p.xml", "maps/skirmishes/saharan_oases_4p.pmp", 384.0, "desert", 0.15),
    ("sicilia", "maps/skirmishes/sicilia_2p.xml", "maps/skirmishes/sicilia_2p.pmp", 256.0, "mediterranean", 0.20),
    ("alpine_valleys", "maps/skirmishes/alpine_valleys_2p.xml", "maps/skirmishes/alpine_valleys_2p.pmp", 256.0, "alpine", 0.25),
    ("tuscan_acropolis", "maps/skirmishes/tuscan_acropolis_4p.xml", "maps/skirmishes/tuscan_acropolis_4p.pmp", 384.0, "mediterranean", 0.20),
    ("arcadia", "maps/scenarios/arcadia.xml", "maps/scenarios/arcadia.pmp", 256.0, "mediterranean", 0.20),
    ("vesuvius", "maps/skirmishes/vesuvius_6p.xml", "maps/skirmishes/vesuvius_6p.pmp", 384.0, "mediterranean", 0.25),
    ("mediterranean_cove", "maps/skirmishes/mediterranean_cove_2p.xml", "maps/skirmishes/mediterranean_cove_2p.pmp", 256.0, "mediterranean", 0.20),
    ("nile_river", "maps/skirmishes/nile_river_4p.xml", "maps/skirmishes/nile_river_4p.pmp", 384.0, "desert", 0.15),
    ("gallic_fields", "maps/skirmishes/gallic_fields_3p.xml", "maps/skirmishes/gallic_fields_3p.pmp", 320.0, "temperate", 0.20),
]

# 0 A.D. skirmish maps fetched with --github: file name -> (RTS0 id, biome).
github_skirmishes = {
    "alpine_mountains_3p": ("alpine_mountains", "alpine"),
    "gambia_river_3p": ("gambia_river", "savanna"),
    "greek_acropolis_4p": ("greek_acropolis", "aegean"),
    "libyan_oases_4p": ("libyan_oases", "desert"),
    "lorraine_plain_2p": ("lorraine_plain", "temperate"),
    "median_oasis_2p": ("median_oasis", "desert"),
    "miletus_peninsula_2p": ("miletus_peninsula", "aegean"),
    "neareastern_badlands_4p": ("neareastern_badlands", "desert"),
    "punjab_2p": ("punjab", "india"),
    "sahel_4p": ("sahel", "savanna"),
    "savanna_river_2p": ("savanna_river", "savanna"),
    "sporades_islands_2p": ("sporades_islands", "aegean"),
    "syria_2p": ("syria", "desert"),
    "two_seas_6p": ("two_seas", "mediterranean"),
    "zagros_mountains_2p": ("zagros_mountains", "persia"),
    "arabian_oases_2p": ("arabian_oases", "desert"),
    "farmland_2p": ("farmland", "temperate"),
    "forest_battle_4p": ("forest_battle", "temperate"),
    "hindu_kush_2p": ("hindu_kush", "alpine"),
    "hydaspes_river_4p": ("hydaspes_river", "india"),
    "isthmus_of_corinth_2p": ("isthmus_of_corinth", "aegean"),
    "northern_island_2p": ("northern_island", "polar"),
    "oceanside_2p": ("oceanside", "mediterranean"),
    "watering_holes_4p": ("watering_holes", "savanna"),
    "sicilia_nomad_2p": ("sicilia_nomad", "mediterranean"),
}

# 0 A.D. scenario maps (maps/scenarios/) fetched with --github-scenarios: file -> (id, biome).
# Left out: the combat/pathfinding demos.
github_scenarios = {
    "treasure_islands": ("treasure_islands", "mediterranean"),
    "gold_rush": ("gold_rush", "savanna"),
    "laconia": ("laconia", "aegean"),
    "migration": ("migration", "mediterranean"),
    "miletus": ("miletus", "aegean"),
    "necropolis": ("necropolis", "desert"),
    "siwa_oasis": ("siwa_oasis", "desert"),
    "the_massacre_of_delphi": ("the_massacre_of_delphi", "aegean"),
    "the_persian_gates": ("the_persian_gates", "persia"),
}
for _civ in ["athenians", "britons", "carthaginians", "gauls", "han", "iberians", "kushites",
             "macedonians", "mauryas", "persians", "ptolemies", "romans", "seleucids", "spartans"]:
    github_scenarios[f"sandbox_{_civ}"] = (f"sandbox_{_civ}", "mediterranean")

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--gaia-only":
        # Refresh fauna, treasures and ruins of every ported map from 0 A.D.'s GitHub.
        for mid, xrel, prel, size, bome, hscale in maps_list:
            try:
                port_map(mid, xrel, prel, size, bome, hscale, source=GithubSource(), gaia_only=True)
            except Exception as e:
                print(f"  !! {mid}: {e}")
        for name, (mid, bome) in sorted(github_skirmishes.items()):
            try:
                port_map(mid, f"maps/skirmishes/{name}.xml", f"maps/skirmishes/{name}.pmp", None, bome, 0.20,
                         source=GithubSource(), gaia_only=True)
            except Exception as e:
                print(f"  !! {mid}: {e}")
        for name, (mid, bome) in sorted(github_scenarios.items()):
            try:
                port_map(mid, f"maps/scenarios/{name}.xml", f"maps/scenarios/{name}.pmp", None, bome, 0.20,
                         source=GithubSource(), gaia_only=True)
            except Exception as e:
                print(f"  !! {mid}: {e}")
    elif len(sys.argv) > 1 and sys.argv[1] == "--github-scenarios":
        for name in sys.argv[2:] or sorted(github_scenarios):
            mid, bome = github_scenarios.get(name, (name, "mediterranean"))
            port_map(mid, f"maps/scenarios/{name}.xml", f"maps/scenarios/{name}.pmp", None, bome, 0.20,
                     source=GithubSource())
    elif len(sys.argv) > 1 and sys.argv[1] == "--github":
        names = sys.argv[2:] or sorted(github_skirmishes)
        for name in names:
            mid, bome = github_skirmishes.get(name, (name, "mediterranean"))
            port_map(mid, f"maps/skirmishes/{name}.xml", f"maps/skirmishes/{name}.pmp", None, bome, 0.20,
                     source=GithubSource())
    else:
        # --github-builtin: the built-in list, fetched from 0 A.D.'s GitHub.
        src = GithubSource if len(sys.argv) > 1 and sys.argv[1] == "--github-builtin" else None
        for mid, xrel, prel, size, bome, hscale in maps_list:
            port_map(mid, xrel, prel, size, bome, hscale, source=src() if src else None)
