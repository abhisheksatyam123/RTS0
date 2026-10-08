"""Converts 0 A.D. scenario and skirmish maps (.xml + .pmp) to RTS0 JSON in data/maps/.

  python3 tools/port_0ad_maps.py                  # the built-in list, from a local public.zip
  python3 tools/port_0ad_maps.py --github NAME..  # skirmish maps by 0 A.D. file name (e.g.
                                                  # libyan_oases_4p), fetched from 0 A.D.'s GitHub
  python3 tools/port_0ad_maps.py --github-builtin # the built-in list, fetched from GitHub

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
    for prefix, key in (("gaia/treasure/", "treasures"), ("gaia/ruins/", "ruins")):
        if prefix in ltpl:
            return key, {"type": ltpl.split(prefix, 1)[1], "x": wx, "z": wz}
    if "gaia/fauna_" in ltpl:
        stem = ltpl.split("gaia/fauna_", 1)[1]
        if stem.endswith("_trainable"):
            stem = stem[: -len("_trainable")]
        return "fauna", {"type": stem, "x": wx, "z": wz}
    return None

def port_map(map_id, xml_rel, pmp_rel, world_size_m=256.0, biome="mediterranean", height_scale=0.20, source=None, gaia_only=False):
    print(f"Porting map: {map_id} ({xml_rel})...")
    with (source if source is not None else zipfile.ZipFile(zip_path, 'r')) as z:
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

        if entities_elem is not None:
            for e in entities_elem.findall("Entity"):
                tpl = e.find("Template").text if e.find("Template") is not None and e.find("Template").text else ""
                ply = int(e.find("Player").text) if e.find("Player") is not None and e.find("Player").text else 0
                pos = e.find("Position")
                if pos is None:
                    continue
                x = float(pos.get("x", 0))
                z = float(pos.get("z", 0))
                raw_entities.append((tpl, ply, x, z))

        # Convert entities into RTS0 format
        resources = []
        gaia = {"fauna": [], "treasures": [], "ruins": []}
        fauna = gaia["fauna"]
        players = {}
        structures = []
        units = []

        for tpl, ply, rx, rz in raw_entities:
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
                # Gaia resources & wildlife fauna
                if "gaia/fish/" in ltpl:
                    # template_gaia_fish.xml: 1000 food.fish, gathered only by fishing boats
                    resources.append({"type": "fish", "x": wx, "z": wz, "amount": 1000})
                elif gaia_entity(ltpl, wx, wz):
                    key, ent = gaia_entity(ltpl, wx, wz)
                    gaia[key].append(ent)
                elif any(k in ltpl for k in ["fruit", "berry", "apple", "date", "bush_berry", "grain", "food"]):
                    resources.append({"type": "food", "x": wx, "z": wz, "amount": 300})
                elif any(k in ltpl for k in ["stone", "rock"]):
                    resources.append({"type": "stone", "x": wx, "z": wz, "amount": 1000})
                elif any(k in ltpl for k in ["metal", "gold", "silver", "iron", "ore"]):
                    resources.append({"type": "gold", "x": wx, "z": wz, "amount": 1000})
                elif any(k in ltpl for k in ["tree", "palm", "pine", "cedar", "cypress", "oak", "carob", "poplar", "wood", "forest"]):
                    res_type = "palm" if ("palm" in ltpl or "date" in ltpl) else ("pine" if ("pine" in ltpl or "cedar" in ltpl or "cypress" in ltpl) else "tree")
                    resources.append({"type": res_type, "x": wx, "z": wz, "amount": 300})
            else:
                # Player base / units
                if ply not in players:
                    players[ply] = {"id": ply, "team": ply - 1, "start_x": wx, "start_z": wz}

                # Structures
                if any(k in ltpl for k in ["civil_centre", "town_center", "civic"]):
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

        # Ensure every detected player has a town center / start position
        for p in players.values():
            has_tc = any(s["type"] == "town_center" and s["player"] == p["id"] for s in structures)
            if not has_tc:
                structures.append({"type": "town_center", "player": p["id"], "x": p["start_x"], "z": p["start_z"]})

        map_data = {
            "id": map_id,
            "name": name,
            "desc": desc,
            "biome": biome,
            "world_size": world_size_m,
            "water_height": water_height,
            "heightmap_res": target_res,
            "heightmap": heightmap,
            "players": list(players.values()),
            "resources": resources,
            "fauna": fauna,
            "treasures": gaia["treasures"],
            "ruins": gaia["ruins"],
            "structures": structures,
            "units": units
        }

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
            for key in ("fauna", "treasures", "ruins"):
                old[key] = [dict(e, x=round(e["x"] * k, 2), z=round(e["z"] * k, 2)) for e in gaia[key]]
            map_data = old
        with open(out_path, "w") as f:
            json.dump(map_data, f, indent=2)
        print(f"  -> Generated {out_path}: {len(resources)} resources, {len(fauna)} fauna, {len(structures)} structures, {len(units)} units, {len(players)} players (Water={water_height}m).")

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
}

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
