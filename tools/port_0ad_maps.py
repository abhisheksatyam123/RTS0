"""Converts 0 A.D. scenario and skirmish maps (.xml + .pmp) to RTS0 JSON in data/maps/.

  python3 tools/port_0ad_maps.py                  # the built-in list, from a local public.zip
  python3 tools/port_0ad_maps.py --github NAME..  # skirmish maps by 0 A.D. file name (e.g.
                                                  # libyan_oases_4p), fetched from 0 A.D.'s GitHub
  python3 tools/port_0ad_maps.py --github-builtin # the built-in list, fetched from GitHub
  python3 tools/port_0ad_maps.py --github-scenarios [NAME..]  # scenario maps, same way
  python3 tools/port_0ad_maps.py --zip-scenarios ZIP [NAME..]  # scenario maps from a 0 A.D. public.zip
  python3 tools/port_0ad_maps.py --zip-skirmishes ZIP|DIR [NAME..]  # skirmish maps from a 0 A.D. public.zip or unpacked tree
  python3 tools/port_0ad_maps.py --github-tutorials [NAME..]  # the tutorials (maps/tutorials/)
  python3 tools/port_0ad_maps.py --terrain-only   # add each ported map's per-tile terrain from GitHub
  python3 tools/port_0ad_maps.py --environment-only   # add each ported map's Environment (sun, fog, water) from GitHub
  python3 tools/port_0ad_maps.py --walls-only   # turn each ported map's walls, palisades and gates into WallSet pieces
  python3 tools/port_0ad_maps.py --livestock-only DIR  # refresh each ported map's player-owned sheep, goats and
                                                       # other livestock from an unpacked public.zip
  python3 tools/port_0ad_maps.py --units-only DIR  # refresh each ported map's player units with their 0 A.D.
                                                 # templates from an unpacked public.zip
  python3 tools/port_0ad_maps.py --supplies-only DIR  # refresh each ported map's resource amounts and
                                                  # gatherer limits from the templates of an unpacked
                                                  # 0 A.D. public.zip at DIR (maps/ and simulation/ under it)
  python3 tools/port_0ad_maps.py --player-data-only DIR  # refresh each ported map's PlayerData (civs, starting
                                                  # resources, locked teams, PopulationLimit) from an unpacked
                                                  # 0 A.D. public.zip at DIR (maps/ under it)
  python3 tools/port_0ad_maps.py --circular-only DIR  # refresh each ported map's "circular" (ScriptSettings
                                                  # CircularMap) from an unpacked 0 A.D. public.zip at DIR

Maps fetched with --github get their world size from the .pmp (one RTS0 world unit per
0 A.D. tile, clamped to 128..512) and a heightmap of one sample per 4 tiles.
0 A.D. maps are CC BY-SA 3.0; see docs/0AD_ATTRIBUTION.md.
"""
import re
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
import base64

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_flora import ORDER as _FLORA_ORDER, family_of_fruit, family_of_tree  # noqa: E402

FLORA_TEMPLATES = frozenset(_FLORA_ORDER)

zip_path = "/home/abhi/project/0ad/binaries/data/mods/public/public.zip"
GITHUB_RAW = "https://raw.githubusercontent.com/0ad/0ad/master/binaries/data/mods/public/"
NAV_WATER_MARGIN = 0.3
# RTS0 height units per 0 A.D. metre (grid_nav.h HEIGHT_PER_METRE): a 4 m tile is one unit.
HEIGHT_PER_METRE = 0.25


# RTS0 Civilization ids (sim_types.h), by 0 A.D. civ code.
CIV_CODES = ["athen", "spart", "pers", "rome", "cart", "mace", "brit", "gaul", "iber", "maur", "kush", "ptol", "sele", "han"]


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

def flat_pmp(patches, texture, height):
    """The .pmp a map without one stands for: CMapReader's <Terrain patches= texture=
    height=> fills a patches x patches map with one texture at one height."""
    side = patches * 16 + 1
    name = (texture + ".xml").encode("utf-8")
    out = bytearray(b"PSMP") + struct.pack("<III", 6, 0, patches)
    out += struct.pack("<H", height) * (side * side)
    out += struct.pack("<I", 1) + struct.pack("<I", len(name)) + name
    out += struct.pack("<HHI", 0, 0xFFFF, 0) * (patches * patches * 256)
    return bytes(out)

def extract_pmp_heights(raw_pmp, target_res=64, height_scale=HEIGHT_PER_METRE):
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

def extract_cinema_paths(root, center_x, center_z, scale, height_scale):
    """The map's <Paths> (0 A.D. MapReader ReadPaths) in RTS0 units: each node is
    [x, y, z, seconds since the spline's previous node]. A <Node> without a <Position>
    (or <Target>) adds its deltatime to that spline's next node, as MapReader does."""
    paths = []
    for p in root.findall("Paths/Path"):
        nodes, targets = [], []
        pos_t = tgt_t = 0.0
        for n in p.findall("Node"):
            dt = float(n.get("deltatime", 0) or 0)
            pos_t += dt
            tgt_t += dt
            for child in n:
                x, y, z = (float(child.get(k, 0) or 0) for k in ("x", "y", "z"))
                v = [round((x - center_x) * scale, 3), round(y * height_scale, 3), round((z - center_z) * scale, 3)]
                if child.tag == "Position":
                    nodes.append(v + [round(pos_t, 3)])
                    pos_t = 0.0
                elif child.tag == "Target":
                    targets.append(v + [round(tgt_t, 3)])
                    tgt_t = 0.0
        if not nodes:
            continue
        paths.append({"name": p.get("name", ""), "timescale": float(p.get("timescale", 1) or 1),
                      "orientation": p.get("orientation", ""), "mode": p.get("mode", "ease_inout"),
                      "style": p.get("style", "default"), "nodes": nodes, "targets": targets})
    return paths

def extract_pmp_terrain(raw_pmp):
    """The per-tile terrain textures (CTerrain STileDesc tex1) after the heights:
    {"res": tiles per side, "names": [0 A.D. terrain ids], "rle": base64 of
    (run - 1, name index) byte pairs over the tiles, row-major from the -x/-z
    corner like the heightmap}. The .pmp stores tiles patch by patch (16 x 16
    tiles each). None when the file has no terrain section."""
    if raw_pmp[:4] != b"PSMP":
        return None
    patches = struct.unpack('<I', raw_pmp[12:16])[0]
    vert_side = patches * 16 + 1
    o = 16 + vert_side * vert_side * 2
    if o + 4 > len(raw_pmp):
        return None
    count = struct.unpack('<I', raw_pmp[o:o + 4])[0]
    o += 4
    names = []
    for _ in range(count):
        n = struct.unpack('<I', raw_pmp[o:o + 4])[0]
        o += 4
        names.append(os.path.splitext(raw_pmp[o:o + n].decode('utf-8'))[0])
        o += n
    side = patches * 16
    if not names or len(names) > 255 or side > 1024 or o + side * side * 8 > len(raw_pmp):
        return None
    tiles = bytearray(side * side)
    prio_sum = [0] * len(names)
    prio_n = [0] * len(names)
    for pz in range(patches):
        for px in range(patches):
            for j in range(16):
                for i in range(16):
                    tex1, _tex2, prio = struct.unpack_from('<HHI', raw_pmp, o)
                    o += 8
                    t = tex1 if tex1 < len(names) else 0
                    tiles[(pz * 16 + j) * side + px * 16 + i] = t
                    prio_sum[t] += prio
                    prio_n[t] += 1
    # Only the names the tiles use, by priority: a tile blends over neighbours of
    # lower priority, which RTS0 takes as the name's index (src/presentation/
    # terrain_blend.h). 0 A.D. keeps a priority per tile; each terrain takes the
    # mean of its tiles', ties in first-use order.
    first = list(dict.fromkeys(tiles))
    first.sort(key=lambda t: prio_sum[t] / prio_n[t])
    remap = {t: k for k, t in enumerate(first)}
    used = [names[t] for t in first]
    rle = bytearray()
    k = 0
    while k < len(tiles):
        v = tiles[k]
        run = 1
        while k + run < len(tiles) and tiles[k + run] == v and run < 256:
            run += 1
        rle += bytes((run - 1, remap[v]))
        k += run
    return {"res": side, "names": used, "rle": base64.b64encode(bytes(rle)).decode('ascii')}

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
# Structures only maps place, which RTS0 finds by template (sim_types.h map_structure_template).
MAP_ONLY_STRUCTURE = re.compile(r"(^|/)(\w+/tower_bolt|\w+/tower_artillery|\w+/royal_stoa|hellenic_stoa|pers/inn|rome/tent|rome/arch"
                                r"|pers/hall|pers/warehouse|gaul/tavern|maur/tower_double|kush/shrine|han/fortress_old|han/laozigate)$")

def unit_parents(source, tpl):
    """The units/ templates a unit template inherits from, nearest first
    (rome/champion_infantry_swordsman_03 -> _02, then champion_infantry_swordsman), so RTS0
    can stand in the nearest roster row for a map-only variant."""
    out = []
    rel = tpl
    while len(out) < 8:
        root = _read_template(source, rel)
        parent = root.get("parent", "") if root is not None else ""
        if not parent.startswith("units/") or "|" in parent:
            break
        out.append(parent.split("/", 1)[1])
        rel = parent
    return out


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


def _wall_length(root):
    try:
        return float(root.find("WallPiece").findtext("Length"))
    except (AttributeError, TypeError, ValueError):
        return None


def _obstruction(root):
    """The Obstruction Static (width, depth) of a template, or None."""
    try:
        st = root.find("Obstruction").find("Static")
        return float(st.get("width")), float(st.get("depth"))
    except (AttributeError, TypeError, ValueError):
        return None


def _disabled(root, component):
    """True when the template disables `component` (<TerritoryDecay disable=""/>), False when it
    declares it, None when it leaves it to its parent."""
    if root is None or root.find(component) is None:
        return None
    return root.find(component).get("disable") is not None


def _max_gatherers(root):
    try:
        return int(root.find("ResourceSupply").findtext("MaxGatherers"))
    except (AttributeError, TypeError, ValueError):
        return None


def template_info(source, name):
    """0 A.D. template `name` with its parents resolved: {"chain": [name, parent, ...],
    "supply_type": "stone.rock" or None, "supply_max": float or None, "max_gatherers": int or None}. A parent like
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
    mg = _max_gatherers(root)
    wl = _wall_length(root)
    ob = _obstruction(root)
    dd, cd = _disabled(root, "TerritoryDecay"), _disabled(root, "Capturable")
    info = {"chain": [mixin] + (base["chain"] if base else []),
            "wall_length": wl if wl is not None else (base["wall_length"] if base else None),
            "obstruction": ob if ob is not None else (base["obstruction"] if base else None),
            "decay_disabled": dd if dd is not None else (base["decay_disabled"] if base else False),
            "capture_disabled": cd if cd is not None else (base["capture_disabled"] if base else False),
            "supply_type": st or (base["supply_type"] if base else None),
            "supply_max": mx if mx is not None else (base["supply_max"] if base else None),
            "max_gatherers": mg if mg is not None else (base["max_gatherers"] if base else None)}
    _TEMPLATE_CACHE[name] = info
    return info


def wall_piece(source, tpl, wx, wz, angle, scale):
    """A 0 A.D. WallSet piece (a template with a WallPiece: stone walls, palisades, Roman siege
    walls) as a structures entry: its piece kind, WallSet, Position angle and WallPiece Length
    in RTS0 units, laid turned by main_node.cpp like Walls.js laid it; None for anything else."""
    info = template_info(source, tpl)
    length = info["wall_length"]
    if length is None and info["obstruction"] and any(c.startswith("template_structure_defensive_palisade") for c in info["chain"]):
        # palisades_straight has no WallPiece: a palisade as long as its obstruction, which
        # runs along its depth (a WallPiece's Length runs along its width).
        w, d = info["obstruction"]
        length = max(w, d)
        if d > w:
            angle += math.pi / 2
    # Fences have a WallPiece too, but they are scenery (gaia_owned), not defensive structures.
    if length is None or not any(c.startswith("template_structure_defensive") for c in info["chain"]):
        return None
    names = " ".join(info["chain"]).lower()
    wallset = "palisade" if "palisade" in names else "siege" if "siege_wall" in names else "stone"
    piece = "tower" if "tower" in names else "gate" if "gate" in names else "wall"
    btype = {("stone", "tower"): "wall_tower", ("stone", "gate"): "gate", ("stone", "wall"): "wall_segment",
             ("palisade", "gate"): "palisade_gate", ("siege", "gate"): "siege_wall_gate",
             ("siege", "tower"): "siege_wall", ("siege", "wall"): "siege_wall"}.get((wallset, piece), "palisade")
    entry = {"type": btype, "x": wx, "z": wz, "wallset": wallset, "piece": piece, "angle": round(angle, 4),
             "length": round(length * scale, 3)}
    # palisades_spike_*: <TerritoryDecay disable=""/>; palisades_fort: <Capturable disable=""/>.
    if info["decay_disabled"]:
        entry["no_decay"] = True
    if info["capture_disabled"]:
        entry["no_capture"] = True
    return entry


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
        wall = wall_piece(source, tpl, wx, wz, angle, scale)
        if wall:
            return "gaia_structures", dict(wall, template=tpl)
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

# 0 A.D. gaia/tree/* and gaia/fruit/* templates by the plant RTS0 draws for them
# (FloraKind in sim_types.h and flora_data.h, models in assets/models/vegetation): a
# template with its own kind by its "<tree|fruit>/<stem>" name, else its family.
def flora_kind(ltpl):
    name = ltpl.rsplit("/", 1)[-1]
    if "tree/" + name in FLORA_TEMPLATES and ltpl.startswith("gaia/tree/"):
        return "tree/" + name
    return family_of_tree(name)


def fruit_kind(ltpl):
    name = ltpl.rsplit("/", 1)[-1]
    if "fruit/" + name in FLORA_TEMPLATES and ltpl.startswith("gaia/fruit/"):
        return "fruit/" + name
    kind = family_of_fruit(name)
    return "food" if kind == "tree" else kind


def supply_family(rtype):
    """A resource type with a template plant ("tree/elm", "fruit/fig") as its family's name."""
    if rtype.startswith("tree/"):
        return family_of_tree(rtype[len("tree/"):])
    if rtype.startswith("fruit/"):
        kind = family_of_fruit(rtype[len("fruit/"):])
        return "food" if kind == "tree" else kind
    return rtype


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
        rtype = flora_kind(ltpl)
    elif kind == "stone":
        rtype = "stone"
    elif kind == "metal" or kind == "gold":
        rtype = "gold"
    elif st == "food.fish":
        rtype = "fish"
    elif kind == "food":
        if st == "food.grain":
            return None  # a Gaia field: nobody may farm it
        rtype = fruit_kind(ltpl)
    else:
        return None
    entry = {"type": rtype, "x": wx, "z": wz, "amount": int(amount)}
    # RTS0's trees and fruit default to template_gaia_tree/fruit's 8 gatherers (banyan 12, baobab 9).
    mg = info.get("max_gatherers")
    if mg is not None and mg != 8 and rtype not in ("stone", "gold", "fish"):
        entry["max_gatherers"] = mg
    return entry

def extract_environment(root):
    """The map XML's <Environment> (sky, sun, ambient light, fog, water, post-processing)
    as plain numbers for the renderer, or None when the map has none. Colours are
    [r, g, b] in 0 A.D.'s 0-1(+) range, angles in radians."""
    env = root.find("Environment")
    if env is None:
        return None

    def colour(path):
        e = env.find(path)
        return None if e is None else [round(float(e.get(c, 0.0)), 4) for c in "rgb"]

    def number(path, attr=None):
        e = env.find(path)
        if e is None:
            return None
        try:
            return round(float(e.get(attr) if attr else e.text), 6)
        except (TypeError, ValueError):
            return None

    def text(path):
        e = env.find(path)
        return e.text.strip() if e is not None and e.text else None

    out = {
        "sky_set": text("SkySet"),
        "sun_color": colour("SunColor"),
        "sun_elevation": number("SunElevation", "angle"),
        "sun_rotation": number("SunRotation", "angle"),
        "ambient_color": colour("AmbientColor"),
        "fog_color": colour("Fog/FogColor"),
        "fog_factor": number("Fog/FogFactor"),
        "fog_thickness": number("Fog/FogThickness"),
        "water_type": text("Water/WaterBody/Type"),
        "water_color": colour("Water/WaterBody/Color"),
        "water_tint": colour("Water/WaterBody/Tint"),
        "water_waviness": number("Water/WaterBody/Waviness"),
        "water_murkiness": number("Water/WaterBody/Murkiness"),
        "water_wind_angle": number("Water/WaterBody/WindAngle"),
        "brightness": number("Postproc/Brightness"),
        "contrast": number("Postproc/Contrast"),
        "saturation": number("Postproc/Saturation"),
        "bloom": number("Postproc/Bloom"),
        "post_effect": text("Postproc/PostEffect"),
    }
    return {k: v for k, v in out.items() if v is not None}


class DirSource:
    """Reads 0 A.D. files from an unpacked public.zip, like ZipFile.read."""
    def __init__(self, root):
        self.root = root

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def read(self, rel):
        with open(os.path.join(self.root, rel), "rb") as f:
            return f.read()


def units_demo_entities(z):
    """maps/scenarios/units_demo.js: every simulation template (FindAllTemplates(false):
    not the template_* bases) that has a Footprint and a Position, in name order,
    placed in rows from (20, 20) to x 1870 with 14 m gaps, owned by player 1.
    Returns (template, player, x, z, uid) rows like the map's own entities."""
    names = z.namelist() if hasattr(z, "namelist") else [
        os.path.relpath(os.path.join(d, f), z.root).replace(os.sep, "/")
        for d, _, fs in os.walk(os.path.join(z.root, "simulation/templates"), followlinks=True) for f in fs]
    pre = "simulation/templates/"
    names = sorted(n[len(pre):-4] for n in names if n.startswith(pre) and n.endswith(".xml"))
    cache = {}

    def node(tpl):
        if tpl not in cache:
            try:
                cache[tpl] = ET.fromstring(z.read(f"{pre}{tpl}.xml"))
            except (KeyError, FileNotFoundError, ET.ParseError):
                cache[tpl] = None
        return cache[tpl]

    def component(tpl, name, depth=0):
        """The template's component element after inheritance (None if absent or disabled)."""
        root = node(tpl)
        if root is None or depth > 32:
            return None
        own = root.find(name)
        if own is not None:
            return None if "disable" in own.attrib else own
        for parent in (root.get("parent") or "").split("|"):
            if parent:
                found = component(parent, name, depth + 1)
                if found is not None:
                    return found
        return None

    def size(tpl):
        """(width, depth) from the Footprint after inheritance, merging the shape attributes."""
        w = h = None
        circle = None
        chain, t = [], tpl
        for _ in range(32):
            root = node(t)
            if root is None:
                break
            chain.append(root)
            t = (root.get("parent") or "").split("|")[0]
            if not t:
                break
        for root in reversed(chain):
            fp = root.find("Footprint")
            if fp is None:
                continue
            sq, ci = fp.find("Square"), fp.find("Circle")
            if sq is not None:
                circle = None
                w, h = float(sq.get("width", w or 0)), float(sq.get("depth", h or 0))
            if ci is not None:
                circle = float(ci.get("radius", circle or 0))
        if circle is not None:
            return circle * 2, circle * 2
        return w, h

    out = []
    x, zz, maxh, gap, stop = 20.0, 20.0, 0.0, 14.0, 1870.0
    for tpl in names:
        if os.path.basename(tpl).startswith("template_"):
            continue
        if component(tpl, "Footprint") is None or component(tpl, "Position") is None:
            continue
        w, h = size(tpl)
        if w is None:
            continue
        if x + w >= stop:
            x, zz, maxh = 20.0, zz + maxh + gap, 0.0
        if zz >= stop:
            continue
        # SetOwner(1) on everything with an Ownership; RTS0 keeps trees, mines and
        # animals as Gaia's, which only Gaia entries become (an owned tree gathers the same).
        out.append((tpl, 0 if tpl.startswith("gaia/") else 1, x + w / 2, zz, f"u{len(out) + 1}"))
        x += w + gap
        maxh = max(maxh, h)
    return out


def apply_player_data(players, settings):
    """ScriptSettings PlayerData: a scenario's fixed civs, starting resources, locked teams and
    population limits, onto the map's player entries (by id). Its first entry is player 1, or
    an empty Gaia placeholder in older maps. Returns the PopulationLimit every player shares, if any."""
    pdata = settings.get("PlayerData", [])
    offset = 0 if pdata and not isinstance(pdata[0], dict) else 1
    limits = []
    for p in players:
        i = p["id"] - offset
        pd = pdata[i] if 0 <= i < len(pdata) and isinstance(pdata[i], dict) else {}
        if pd.get("Civ") in CIV_CODES:
            p["civ"] = CIV_CODES.index(pd["Civ"])
        if isinstance(pd.get("Resources"), dict):
            p["resources"] = {k: int(v) for k, v in pd["Resources"].items()}
        # Locked teams (LockTeams) are the map's own; otherwise Game Setup picks them.
        if settings.get("LockTeams") and isinstance(pd.get("Team"), int):
            p["team"] = pd["Team"]
        if isinstance(pd.get("PopulationLimit"), int):
            p["pop_limit"] = pd["PopulationLimit"]
        limits.append(pd.get("PopulationLimit"))
    return limits[0] if limits and isinstance(limits[0], int) and all(l == limits[0] for l in limits) else None


def port_map(map_id, xml_rel, pmp_rel, world_size_m=256.0, biome="mediterranean", height_scale=None, source=None, gaia_only=False,
             terrain_only=False, environment_only=False, triggers_only=False, player_data_only=False,
             supplies_only=False, walls_only=False, livestock_only=False, structures_only=False,
             units_only=False, circular_only=False):
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
        environment = extract_environment(root)
        # ScriptSettings CircularMap: the map's playable area is the inscribed circle.
        circular = bool(settings.get("CircularMap", False))
        if circular_only:
            out_path = f"data/maps/{map_id}.json"
            if not os.path.exists(out_path):
                return
            with open(out_path) as f:
                old = json.load(f)
            old["circular"] = circular
            with open(out_path, "w") as f:
                json.dump(old, f, indent=2)
            print(f"  -> {out_path}: circular {circular}")
            return
        if player_data_only:
            # Keep the existing map; only refresh what its PlayerData sets.
            out_path = f"data/maps/{map_id}.json"
            if not os.path.exists(out_path):
                return
            with open(out_path) as f:
                old = json.load(f)
            limit = apply_player_data(old["players"], settings)
            old.pop("pop_limit", None)
            if limit is not None:
                old["pop_limit"] = limit
            with open(out_path, "w") as f:
                json.dump(old, f, indent=2)
            print(f"  -> {out_path}: player data")
            return
        if environment_only:
            # Keep the existing map; only add or refresh its Environment.
            out_path = f"data/maps/{map_id}.json"
            if not os.path.exists(out_path) or environment is None:
                return
            with open(out_path) as f:
                old = json.load(f)
            old["environment"] = environment
            with open(out_path, "w") as f:
                json.dump(old, f, indent=2)
            print(f"  -> {out_path}: environment ({len(environment)} values)")
            return

        # Terrain heights from PMP
        try:
            raw_pmp = z.read(pmp_rel)
        except (KeyError, FileNotFoundError):
            # No .pmp: the map's <Terrain patches= texture= height=> (units_demo,
            # the unit motion tests, Combat Demo (Huge)).
            t = root.find("Terrain")
            if t is None or not t.get("patches"):
                raise
            raw_pmp = flat_pmp(int(t.get("patches")), t.get("texture", "grass1_spring"), int(t.get("height", 0)))
        target_res = 64
        if world_size_m is None:
            # One RTS0 world unit per 0 A.D. tile, as for random maps.
            world_size_m = float(min(512, max(128, pmp_tiles(raw_pmp))))
            target_res = max(64, int(world_size_m) // 4 + 1)
        if height_scale is None:
            # Heights shrink with the map: HEIGHT_PER_METRE where a world unit is one
            # 0 A.D. tile, scaled by world units per tile on maps squeezed or stretched
            # to another size, so slopes stay 0 A.D.'s.
            height_scale = HEIGHT_PER_METRE * world_size_m / pmp_tiles(raw_pmp)
        heightmap, vert_side = extract_pmp_heights(raw_pmp, target_res=target_res, height_scale=height_scale)
        terrain = extract_pmp_terrain(raw_pmp)
        if terrain_only:
            # Keep the existing map; only add or refresh its per-tile terrain.
            out_path = f"data/maps/{map_id}.json"
            if not os.path.exists(out_path) or terrain is None:
                return
            with open(out_path) as f:
                old = json.load(f)
            old["terrain"] = terrain
            with open(out_path, "w") as f:
                json.dump(old, f, indent=2)
            print(f"  -> {out_path}: {terrain['res']}x{terrain['res']} tiles, {len(terrain['names'])} terrains")
            return

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

        if "scenarios/units_demo.js" in settings.get("TriggerScripts", []):
            raw_entities = units_demo_entities(z)
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

        # trigger/trigger_point_<ref> markers (TriggerPoint.js) and their owners, for the
        # map's TriggerScripts (Simulation::add_trigger_point).
        trigger_points = []
        for tpl, ply, rx, rz, uid in raw_entities:
            if tpl.lower().startswith("trigger/trigger_point_"):
                trigger_points.append({"ref": tpl[len("trigger/trigger_point_"):], "owner": ply,
                                       "x": round((rx - center_x) * scale, 2), "z": round((rz - center_z) * scale, 2)})
        cinema_paths = extract_cinema_paths(root, center_x, center_z, scale, height_scale)
        # units_demo.js only places entities, which units_demo_entities already did.
        scripts = [os.path.splitext(os.path.basename(t))[0] for t in settings.get("TriggerScripts", [])
                   if not t.startswith("scripts/") and t != "scenarios/units_demo.js"]
        if triggers_only:
            # Keep the existing map; only add or refresh its trigger points and scripts.
            out_path = f"data/maps/{map_id}.json"
            if not os.path.exists(out_path):
                return
            with open(out_path) as f:
                old = json.load(f)
            k = old.get("world_size", world_size_m) / world_size_m
            old["trigger_points"] = [dict(p, x=round(p["x"] * k, 2), z=round(p["z"] * k, 2)) for p in trigger_points]
            if cinema_paths:
                old["cinema_paths"] = [dict(c, nodes=[[n[0] * k, n[1], n[2] * k, n[3]] for n in c["nodes"]],
                                            targets=[[n[0] * k, n[1], n[2] * k, n[3]] for n in c["targets"]])
                                       for c in cinema_paths]
            if scripts:
                old["trigger_scripts"] = scripts
            with open(out_path, "w") as f:
                json.dump(old, f, indent=2)
            print(f"  -> {out_path}: {len(trigger_points)} trigger points, scripts {scripts}")
            return

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
                n_structures = len(structures)
                # Unit templates (units/, skirmish/units/) never match the structure
                # names below; "default_infantry_ranged_b" used to match "range".
                is_unit = "units/" in ltpl
                if "gaia/fauna_" in ltpl:
                    # A player's livestock (Isthmus of Corinth's starting sheep).
                    key, ent = gaia_entity(ltpl, wx, wz)
                    gaia[key].append(dict(ent, player=ply))
                    continue
                if "gaia/ruins/" in ltpl:
                    # A player's statue (Island of Meroe's, Crocodilopolis'): RTS0 ruins have no owner.
                    key, ent = gaia_entity(ltpl, wx, wz)
                    gaia[key].append(ent)
                    continue
                if ltpl.startswith("vision_revealers/"):
                    # A VisionRevealer marker: RTS0 has no such entity.
                    continue

                # Structures
                wall = None if is_unit else wall_piece(files, tpl, wx, wz, angle_by_uid.get(uid, 0.0), scale)
                if is_unit:
                    pass
                elif wall:
                    # The template: SkirmishReplacer keeps skirmish/structures/iber_wall_* for
                    # Iberians only, and default_wall_* turns into the owner's own walls.
                    structures.append(dict(wall, player=ply, template=tpl))
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
                elif ltpl.count("/") == 1 and (gaia_owned(files, ltpl, wx, wz) or ("",))[0] == "scenery":
                    # A player's obelisk or fence (Saharan Oases' scenario): RTS0 scenery has no owner.
                    gaia["scenery"].append(gaia_owned(files, ltpl, wx, wz, angle_by_uid.get(uid, 0.0), scale)[1])
                else:
                    # A civ structure the names above miss (spart/gerousia): RTS0 finds it by template.
                    structures.append({"type": "", "player": ply, "x": wx, "z": wz})

                # Units
                if not is_unit:
                    pass
                elif any(k in ltpl for k in ["villager", "support_civilian", "citizen", "female"]):
                    units.append({"type": "villager", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["cavalry", "horse"]):
                    # Before the infantry names: cavalry_spearman, cavalry_javelineer are cavalry.
                    units.append({"type": "cavalry", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["infantry_melee", "swordsman", "champion"]):
                    units.append({"type": "swordsman", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["spearman", "hoplite", "phalanx", "pikeman"]):
                    units.append({"type": "spearman", "player": ply, "x": wx, "z": wz})
                elif any(k in ltpl for k in ["infantry_ranged", "archer", "slinger", "skirmisher", "javelin", "ranged"]):
                    units.append({"type": "archer", "player": ply, "x": wx, "z": wz})
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
                # The 0 A.D. template ("athen/infantry_javelineer_b"): RTS0 spawns that civ roster
                # template or building when it has one, else falls back to "type".
                short = tpl.split("/", 1)[1] if "/" in tpl else tpl
                if len(units) > n_units:
                    unit_index[uid] = n_units
                    units[-1]["template"] = short
                    parents = unit_parents(files, tpl) if tpl.startswith("units/") else []
                    if parents:
                        units[-1]["parents"] = parents
                if len(structures) > n_structures:
                    structures[-1]["template"] = short

        for uid, holder in garrisoned_in.items():
            if uid in unit_index and holder in unit_index:
                units[unit_index[uid]]["garrison_in"] = unit_index[holder]
            elif uid in gaia_unit_index and holder in gaia_unit_index:
                gaia["gaia_units"][gaia_unit_index[uid]]["garrison_in"] = gaia_unit_index[holder]
            elif uid in gaia_unit_index and holder in gaia_structure_index:
                gaia["gaia_units"][gaia_unit_index[uid]]["garrison_in_structure"] = gaia_structure_index[holder]

        # Ensure every detected player has a town center / start position. Nomad maps
        # (no civil centre at all, e.g. sicilia_nomad_2p) start with units only, and so
        # does a player that starts with an army and nothing else (Death Canyon's
        # Invasion Force).
        nomad = not any(s["type"] == "town_center" for s in structures)
        for p in ([] if nomad else players.values()):
            has_tc = any(s["type"] == "town_center" and s["player"] == p["id"] for s in structures)
            army_only = not any(s["player"] == p["id"] for s in structures) and any(u["player"] == p["id"] for u in units)
            if not has_tc and not army_only:
                structures.append({"type": "town_center", "player": p["id"], "x": p["start_x"], "z": p["start_z"]})

        pop_limit = apply_player_data(list(players.values()), settings)

        map_data = {
            "id": map_id,
            "name": name,
            "desc": desc,
            "biome": biome,
            "nomad": nomad,
            "circular": circular,
            "world_size": world_size_m,
            "water_height": water_height,
            "heightmap_res": target_res,
            "heightmap": heightmap,
            "terrain": terrain,
            "environment": environment,
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
        if pop_limit is not None:
            map_data["pop_limit"] = pop_limit
        if scripts:
            map_data["trigger_scripts"] = scripts
        if trigger_points:
            map_data["trigger_points"] = trigger_points
        if cinema_paths:
            map_data["cinema_paths"] = cinema_paths

        os.makedirs("data/maps", exist_ok=True)
        out_path = f"data/maps/{map_id}.json"
        if structures_only:
            # Keep the existing map; only give it the structures only maps place (bolt and artillery
            # towers, stoas, the Persian inn, the Roman tent and arch: sim_types.h
            # map_structure_template): an entry at the same spot gets the template, a missing one
            # is appended (indices of the entries already there don't move).
            if not os.path.exists(out_path):
                return
            with open(out_path) as f:
                old = json.load(f)
            k = old.get("world_size", world_size_m) / world_size_m
            tagged = added = walls = 0
            for key, new in (("structures", structures), ("gaia_structures", gaia["gaia_structures"])):
                have = old.setdefault(key, [])
                used = set()
                for e in new:
                    if "wallset" in e:
                        # A WallSet piece: the palisades the map had as stone wall segments
                        # (palisades_straight), and the spikes' and forts' no_decay / no_capture.
                        x, z = round(e["x"] * k, 2), round(e["z"] * k, 2)
                        near = [o for o in have if abs(o["x"] - x) < 0.5 and abs(o["z"] - z) < 0.5
                                and o.get("player") == e.get("player") and id(o) not in used]
                        o = min(near, key=lambda o: math.hypot(o["x"] - x, o["z"] - z), default=None)
                        if o is not None:
                            used.add(id(o))
                        want = dict(e, x=x, z=z, length=round(e["length"] * k, 3))
                        if o is not None and o.get("template") and "template" not in want:
                            want["template"] = o["template"]
                        if o is not None and o != want:
                            o.clear()
                            o.update(want)
                            walls += 1
                        continue
                    if not MAP_ONLY_STRUCTURE.search(e.get("template", "")):
                        continue
                    x, z = round(e["x"] * k, 2), round(e["z"] * k, 2)
                    o = next((o for o in have if abs(o["x"] - x) < 0.5 and abs(o["z"] - z) < 0.5 and "wallset" not in o
                              and o.get("player") == e.get("player")), None)
                    if o is None:
                        have.append(dict(e, x=x, z=z))
                        added += 1
                    elif o.get("template") != e["template"]:
                        o["template"] = e["template"]
                        tagged += 1
            if tagged or added or walls:
                with open(out_path, "w") as f:
                    json.dump(old, f, indent=2)
            print(f"  -> {out_path}: {tagged} structures given their template, {added} added, {walls} wall pieces refreshed")
            return
        if supplies_only:
            # Keep the existing map; only refresh each resource's amount and gatherer limit,
            # entry by entry, when the entries still line up with the template's.
            if not os.path.exists(out_path):
                return
            with open(out_path) as f:
                old = json.load(f)
            have = old.get("resources", [])
            k = old.get("world_size", world_size_m) / world_size_m
            if len(have) != len(resources) or any(supply_family(a["type"]) != supply_family(b["type"]) or
                                                  abs(a["x"] - b["x"] * k) > 0.5 or
                                                  abs(a["z"] - b["z"] * k) > 0.5 for a, b in zip(have, resources)):
                print(f"  !! {out_path}: {len(have)} resources, 0 A.D. has {len(resources)}; not refreshed")
                return
            changed = 0
            for a, b in zip(have, resources):
                before = (a.get("type"), a.get("amount"), a.get("max_gatherers"))
                a["type"] = b["type"]  # a template with its own FloraKind names itself (flora_kind)
                a["amount"] = b["amount"]
                a.pop("max_gatherers", None)
                if "max_gatherers" in b:
                    a["max_gatherers"] = b["max_gatherers"]
                changed += before != (a.get("type"), a.get("amount"), a.get("max_gatherers"))
            if changed:
                with open(out_path, "w") as f:
                    json.dump(old, f, indent=2)
            print(f"  -> {out_path}: {changed} of {len(have)} supplies changed")
            return
        if units_only:
            # Keep the existing map; only refresh the players' units, each with its 0 A.D. template
            # (skirmish maps' skirmish/units/default_* become the owner's civ units at load, as
            # SkirmishReplacer does).
            if not os.path.exists(out_path):
                return
            with open(out_path) as f:
                old = json.load(f)
            k = old.get("world_size", world_size_m) / world_size_m
            mine = [dict(e, x=round(e["x"] * k, 2), z=round(e["z"] * k, 2)) for e in units]
            if mine != old.get("units", []):
                old["units"] = mine
                with open(out_path, "w") as f:
                    json.dump(old, f, indent=2)
            print(f"  -> {out_path}: {len(mine)} units")
            return
        if livestock_only:
            # Keep the existing map; only refresh the players' own livestock (fauna with a player).
            if not os.path.exists(out_path):
                return
            with open(out_path) as f:
                old = json.load(f)
            k = old.get("world_size", world_size_m) / world_size_m
            mine = [dict(e, x=round(e["x"] * k, 2), z=round(e["z"] * k, 2)) for e in gaia["fauna"] if "player" in e]
            kept = [e for e in old.get("fauna", []) if "player" not in e]
            if kept + mine != old.get("fauna", []):
                old["fauna"] = kept + mine
                with open(out_path, "w") as f:
                    json.dump(old, f, indent=2)
            print(f"  -> {out_path}: {len(mine)} livestock")
            return
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
            for e in old["gaia_structures"]:
                if "length" in e:
                    e["length"] = round(e["length"] * k, 3)
            map_data = old
        elif walls_only:
            # Keep the existing map; only give its walls, palisades and gates their WallSet piece,
            # angle and length (wall_piece), matched by position.
            if not os.path.exists(out_path):
                return
            with open(out_path) as f:
                old = json.load(f)
            k = old.get("world_size", world_size_m) / world_size_m
            matched = missed = 0
            for key, new in (("structures", structures), ("gaia_structures", gaia["gaia_structures"])):
                walls = [w for w in new if "wallset" in w]
                for w in walls:
                    e = next((e for e in old.get(key, []) if abs(e["x"] - w["x"] * k) < 0.05 and abs(e["z"] - w["z"] * k) < 0.05
                              and "wallset" not in e), None)
                    if e is None:
                        missed += 1
                        continue
                    e.update(type=w["type"], wallset=w["wallset"], piece=w["piece"], angle=w["angle"],
                             length=round(w["length"] * k, 3))
                    matched += 1
            print(f"  -> {out_path}: {matched} wall pieces turned, {missed} not found")
            with open(out_path, "w") as f:
                json.dump(old, f, indent=2)
            return
        with open(out_path, "w") as f:
            json.dump(map_data, f, indent=2)
        print(f"  -> Generated {out_path}: {len(resources)} resources, {len(fauna)} fauna, {len(structures)} structures, {len(units)} units, {len(gaia['gaia_structures'])} Gaia structures, {len(gaia['gaia_units'])} Gaia units, {len(players)} players (Water={water_height}m).")

# The map's width comes from its .pmp (None): 16 tiles a patch, 1 RTS0 unit a 4 m tile.
maps_list = [
    ("acropolis_bay", "maps/skirmishes/acropolis_bay_2p.xml", "maps/skirmishes/acropolis_bay_2p.pmp", None, "mediterranean", None),
    ("corinthian_isthmus", "maps/skirmishes/corinthian_isthmus_4p.xml", "maps/skirmishes/corinthian_isthmus_4p.pmp", None, "mediterranean", None),
    ("atlas_valleys", "maps/skirmishes/atlas_valleys_8p.xml", "maps/skirmishes/atlas_valleys_8p.pmp", None, "savanna", None),
    ("bactria", "maps/skirmishes/bactria_2p.xml", "maps/skirmishes/bactria_2p.pmp", None, "desert", None),
    ("battle_for_the_tiber", "maps/scenarios/battle_for_the_tiber.xml", "maps/scenarios/battle_for_the_tiber.pmp", None, "temperate", None),
    ("belgian_bog", "maps/skirmishes/belgian_bog_2p.xml", "maps/skirmishes/belgian_bog_2p.pmp", None, "temperate", None),
    ("death_canyon", "maps/skirmishes/death_canyon_2p.xml", "maps/skirmishes/death_canyon_2p.pmp", None, "desert", None),
    ("deccan_plateau", "maps/skirmishes/deccan_plateau_2p.xml", "maps/skirmishes/deccan_plateau_2p.pmp", None, "tropical", None),
    ("cycladic_archipelago", "maps/skirmishes/cycladic_archipelago_2p.xml", "maps/skirmishes/cycladic_archipelago_2p.pmp", None, "mediterranean", None),
    ("golden_island", "maps/skirmishes/golden_island_2p.xml", "maps/skirmishes/golden_island_2p.pmp", None, "mediterranean", None),
    ("persian_highlands", "maps/skirmishes/persian_highlands_4p.xml", "maps/skirmishes/persian_highlands_4p.pmp", None, "desert", None),
    ("saharan_oases", "maps/skirmishes/saharan_oases_4p.xml", "maps/skirmishes/saharan_oases_4p.pmp", None, "desert", None),
    ("sicilia", "maps/skirmishes/sicilia_2p.xml", "maps/skirmishes/sicilia_2p.pmp", None, "mediterranean", None),
    ("alpine_valleys", "maps/skirmishes/alpine_valleys_2p.xml", "maps/skirmishes/alpine_valleys_2p.pmp", None, "alpine", None),
    ("tuscan_acropolis", "maps/skirmishes/tuscan_acropolis_4p.xml", "maps/skirmishes/tuscan_acropolis_4p.pmp", None, "mediterranean", None),
    ("arcadia", "maps/scenarios/arcadia.xml", "maps/scenarios/arcadia.pmp", None, "mediterranean", None),
    ("vesuvius", "maps/skirmishes/vesuvius_6p.xml", "maps/skirmishes/vesuvius_6p.pmp", None, "mediterranean", None),
    ("mediterranean_cove", "maps/skirmishes/mediterranean_cove_2p.xml", "maps/skirmishes/mediterranean_cove_2p.pmp", None, "mediterranean", None),
    ("nile_river", "maps/skirmishes/nile_river_4p.xml", "maps/skirmishes/nile_river_4p.pmp", None, "desert", None),
    ("gallic_fields", "maps/skirmishes/gallic_fields_3p.xml", "maps/skirmishes/gallic_fields_3p.pmp", None, "temperate", None),
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
    # 0.28 skirmishes ported from an unpacked 0.28 maps/ tree (F).
    "barcania_3p": ("barcania", "mediterranean"),
    "butana_steppe_2p": ("butana_steppe", "nubia"),
    "caspian_sea_2pv2p": ("caspian_sea", "persia"),
    "cisalpine_winter_2p": ("cisalpine_winter", "alpine"),
    "corsica_and_sardinia_4p": ("corsica_and_sardinia", "mediterranean"),
    "crocodilopolis_4p": ("crocodilopolis", "desert"),
    "dueling_cliffs_3pv3p": ("dueling_cliffs", "alpine"),
    "egypt_3pv3p": ("egypt", "desert"),
    "gallic_highlands_2p": ("gallic_highlands", "temperate"),
    "golden_oasis_2p": ("golden_oasis", "desert"),
    "greek_acropolis_night_2p": ("greek_acropolis_night", "mediterranean"),
    "libyan_oasis_2p": ("libyan_oasis", "desert"),
    "magadha_2p": ("magadha", "india"),
    "north_african_savanna_2p": ("north_african_savanna", "savanna"),
    "nubian_frontier_2p": ("nubian_frontier", "nubia"),
    # 0.28 skirmishes ported from its public.zip (--zip-skirmishes).
    "obedska_bog_4p": ("obedska_bog", "temperate"),
    "obedska_bog_night_4p": ("obedska_bog_night", "temperate"),
    "sahyadri_buttes_5p": ("sahyadri_buttes", "india"),
    "scythia_4p": ("scythia", "temperate"),
    "tarim_basin_2p": ("tarim_basin_2p", "desert"),
    "tarim_basin_4p": ("tarim_basin_4p", "desert"),
    "team_oasis_2pv2p": ("team_oasis", "desert"),
    "temperate_roadway_2p": ("temperate_roadway", "temperate"),
    "thessalian_plains_4p": ("thessalian_plains", "aegean"),
    "via_augusta_4p": ("via_augusta", "mediterranean"),
    "white_cliffs_of_dover_5p": ("white_cliffs_of_dover", "temperate"),
    "yangtze_2p": ("yangtze", "temperate"),
    # 0.28's demo skirmishes (Keywords "demo": Game Setup's Demo Maps filter).
    "north_african_savanna_2p": ("north_african_savanna", "savanna"),
    "skirmish_demo_2p": ("skirmish_demo", "mediterranean"),
}

# 0 A.D. scenario maps (maps/scenarios/) fetched with --github-scenarios: file -> (id, biome).
# Left out: the combat/pathfinding demos.
github_scenarios = {
    "treasure_islands": ("treasure_islands", "mediterranean"),
    "death_canyon_invasion_force": ("death_canyon_invasion_force", "desert"),
    "gold_rush": ("gold_rush", "savanna"),
    "laconia": ("laconia", "aegean"),
    "migration": ("migration", "mediterranean"),
    "miletus": ("miletus", "aegean"),
    "necropolis": ("necropolis", "desert"),
    "siwa_oasis": ("siwa_oasis", "desert"),
    "the_massacre_of_delphi": ("the_massacre_of_delphi", "aegean"),
    "the_persian_gates": ("the_persian_gates", "persia"),
    # Scenario versions of skirmish maps (fixed civs and per-player resources); the
    # skirmish keeps the plain id.
    "sahel": ("sahel_scenario", "savanna"),
    "saharan_oases": ("saharan_oases_scenario", "desert"),
    # 0.28 scenarios ported from its public.zip (--zip-scenarios).
    "azure_coast_2p": ("azure_coast_2p", "mediterranean"),
    "azure_coast_4p": ("azure_coast_4p", "mediterranean"),
    "azure_coast_8p": ("azure_coast_8p", "mediterranean"),
    "belgian_bog_night": ("belgian_bog_night", "temperate"),
    "fast_oasis": ("fast_oasis", "desert"),
    "peloponnese": ("peloponnese", "aegean"),
    "savanna_ravine": ("savanna_ravine", "savanna"),
    "serengeti": ("serengeti", "savanna"),
    "ship_combat_demo": ("ship_combat_demo", "mediterranean"),
    "third_macedonian_war": ("third_macedonian_war", "mediterranean"),
    # 0.28's test stubs, listed in its Scenarios dropdown like any other.
    "gorge": ("gorge", "temperate"),
    "temperate_map": ("temperate_map", "temperate"),
    "walltest": ("walltest", "temperate"),
    # 0.28's demo scenarios (Keywords "demo"). Left out: the ones run by trigger scripts RTS0
    # has no port of, or with no terrain file (see REMAINING_GAPS).
    "bridge_demo": ("bridge_demo", "temperate"),
    "campaign_test_map": ("campaign_test_map", "mediterranean"),
    "campaign_test_map_2_heightmap": ("campaign_test_map_2_heightmap", "mediterranean"),
    "combat_demo": ("combat_demo", "mediterranean"),
    "demo_trading": ("demo_trading", "mediterranean"),
    "eire_and_albion": ("eire_and_albion", "temperate"),
    "fishing_demo": ("fishing_demo", "mediterranean"),
    "flight_demo": ("flight_demo", "mediterranean"),
    "multiplayer_demo": ("multiplayer_demo", "mediterranean"),
    "napata_reconstruction": ("napata_reconstruction", "nubia"),
    "pathfinding_demo": ("pathfinding_demo", "mediterranean"),
    "pathfinding_terrain_demo": ("pathfinding_terrain_demo", "mediterranean"),
    "polynesia": ("polynesia", "tropical"),
    "reservoir": ("reservoir", "mediterranean"),
    "resource_demo": ("resource_demo", "mediterranean"),
    "road_demo": ("road_demo", "mediterranean"),
    "sandbox_germans": ("sandbox_germans", "temperate"),
    "sandbox_ptolemies_02": ("sandbox_ptolemies_02", "desert"),
    "ship_formations": ("ship_formations", "mediterranean"),
    "shipattacks": ("shipattacks", "mediterranean"),
    "territory_demo": ("territory_demo", "mediterranean"),
    "test_foundation": ("test_foundation", "mediterranean"),
    "tropical_island": ("tropical_island", "tropical"),
    "walls": ("walls", "mediterranean"),
    "we_are_legion": ("we_are_legion", "mediterranean"),
    "units_demo": ("units_demo", "mediterranean"),
    "combat_demo_huge": ("combat_demo_huge", "mediterranean"),
    "vertex_pathfinder_test": ("vertex_pathfinder_test", "temperate"),
    "triggers_demo": ("triggers_demo", "alpine"),
    "pickup_test_map": ("pickup_test_map", "mediterranean"),
    "unit_chasing_test": ("unit_chasing_test", "mediterranean"),
    "unit_dancing_test": ("unit_dancing_test", "mediterranean"),
    "unit_pushing_test": ("unit_pushing_test", "mediterranean"),
    "unit_motion_integration_test": ("unit_motion_integration_test", "mediterranean"),
    "cinema_demo": ("cinema_demo", "desert"),
}
# 0 A.D. tutorials (maps/tutorials/), fetched with --github-tutorials: file -> (id, biome).
github_tutorials = {
    "introductory_tutorial": ("introductory_tutorial", "mediterranean"),
    "starting_economy_walkthrough": ("starting_economy_walkthrough", "persia"),
}

for _civ in ["athenians", "britons", "carthaginians", "gauls", "han", "iberians", "kushites",
             "macedonians", "mauryas", "persians", "ptolemies", "romans", "seleucids", "spartans"]:
    github_scenarios[f"sandbox_{_civ}"] = (f"sandbox_{_civ}", "mediterranean")

if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] in ("--player-data-only", "--circular-only"):
        src = DirSource(sys.argv[2])
        tables = [(mid, xrel) for mid, xrel, *_ in maps_list]
        tables += [(mid, f"maps/skirmishes/{name}.xml") for name, (mid, _) in github_skirmishes.items()]
        tables += [(mid, f"maps/{folder}/{name}.xml") for folder, table in (("scenarios", github_scenarios),
                   ("tutorials", github_tutorials)) for name, (mid, _) in table.items()]
        for mid, xrel in sorted(set(tables)):
            try:
                port_map(mid, xrel, None, source=src, **({"player_data_only": True} if sys.argv[1] == "--player-data-only"
                                                          else {"circular_only": True}))
            except FileNotFoundError:
                print(f"  !! {mid}: no {xrel} in 0 A.D.")
    elif len(sys.argv) > 2 and sys.argv[1] in ("--supplies-only", "--livestock-only", "--structures-only", "--units-only"):
        # --structures-only <public.zip> [map ids]: from a 0 A.D. public.zip (or an unpacked one).
        def source():  # port_map closes a ZipFile when done with it
            return zipfile.ZipFile(sys.argv[2]) if sys.argv[2].endswith(".zip") else DirSource(sys.argv[2])
        only = {"--supplies-only": {"supplies_only": True}, "--livestock-only": {"livestock_only": True},
                "--structures-only": {"structures_only": True}, "--units-only": {"units_only": True}}[sys.argv[1]]
        tables = [(mid, xrel, prel, size, bome, hscale) for mid, xrel, prel, size, bome, hscale in maps_list]
        tables += [(mid, f"maps/skirmishes/{name}.xml", f"maps/skirmishes/{name}.pmp", None, bome, None)
                   for name, (mid, bome) in github_skirmishes.items()]
        tables += [(mid, f"maps/{folder}/{name}.xml", f"maps/{folder}/{name}.pmp", None, bome, None)
                   for folder, table in (("scenarios", github_scenarios), ("tutorials", github_tutorials))
                   for name, (mid, bome) in table.items()]
        for row in sorted(set(tables)):
            if sys.argv[3:] and row[0] not in sys.argv[3:]:
                continue
            try:
                port_map(*row, source=source(), **only)
            except (FileNotFoundError, KeyError):
                print(f"  !! {row[0]}: no {row[1]} in 0 A.D.")
    elif len(sys.argv) > 1 and sys.argv[1] in ("--gaia-only", "--terrain-only", "--environment-only", "--walls-only"):
        # Refresh fauna, treasures and ruins (or only the per-tile terrain, or only the
        # Environment) of every ported map from 0 A.D.'s GitHub.
        only = {"--gaia-only": {"gaia_only": True}, "--terrain-only": {"terrain_only": True},
                "--environment-only": {"environment_only": True}, "--walls-only": {"walls_only": True}}[sys.argv[1]]
        for mid, xrel, prel, size, bome, hscale in maps_list:
            try:
                port_map(mid, xrel, prel, size, bome, hscale, source=GithubSource(), **only)
            except Exception as e:
                print(f"  !! {mid}: {e}")
        for name, (mid, bome) in sorted(github_skirmishes.items()):
            try:
                port_map(mid, f"maps/skirmishes/{name}.xml", f"maps/skirmishes/{name}.pmp", None, bome, None,
                         source=GithubSource(), **only)
            except Exception as e:
                print(f"  !! {mid}: {e}")
        for folder, table in (("scenarios", github_scenarios), ("tutorials", github_tutorials)):
            for name, (mid, bome) in sorted(table.items()):
                try:
                    port_map(mid, f"maps/{folder}/{name}.xml", f"maps/{folder}/{name}.pmp", None, bome, None,
                             source=GithubSource(), **only)
                except Exception as e:
                    print(f"  !! {mid}: {e}")
    elif len(sys.argv) > 2 and sys.argv[1] == "--triggers-only":
        # --triggers-only <public.zip> [map ids]: refresh the trigger points and scripts of
        # the listed maps_list maps (all by default) from a 0 A.D. public.zip.
        for mid, xrel, prel, size, bome, hscale in maps_list:
            if sys.argv[3:] and mid not in sys.argv[3:]:
                continue
            port_map(mid, xrel, prel, size, bome, hscale, source=zipfile.ZipFile(sys.argv[2]), triggers_only=True)
    elif len(sys.argv) > 1 and sys.argv[1] == "--github-scenarios":
        for name in sys.argv[2:] or sorted(github_scenarios):
            mid, bome = github_scenarios.get(name, (name, "mediterranean"))
            port_map(mid, f"maps/scenarios/{name}.xml", f"maps/scenarios/{name}.pmp", None, bome, None,
                     source=GithubSource())
    elif len(sys.argv) > 2 and sys.argv[1] == "--zip-scenarios":
        # --zip-scenarios <public.zip> [names]: like --github-scenarios, from a 0 A.D. public.zip.
        for name in sys.argv[3:] or sorted(github_scenarios):
            mid, bome = github_scenarios.get(name, (name, "mediterranean"))
            port_map(mid, f"maps/scenarios/{name}.xml", f"maps/scenarios/{name}.pmp", None, bome, None,
                     source=DirSource(sys.argv[2]) if os.path.isdir(sys.argv[2]) else zipfile.ZipFile(sys.argv[2]))
    elif len(sys.argv) > 2 and sys.argv[1] == "--zip-skirmishes":
        # --zip-skirmishes <public.zip or unpacked dir> [names]: like --github, from a 0 A.D. public.zip
        # or a directory holding its maps/ and simulation/.
        for name in sys.argv[3:] or sorted(github_skirmishes):
            mid, bome = github_skirmishes.get(name, (name, "mediterranean"))
            src = DirSource(sys.argv[2]) if os.path.isdir(sys.argv[2]) else zipfile.ZipFile(sys.argv[2])
            port_map(mid, f"maps/skirmishes/{name}.xml", f"maps/skirmishes/{name}.pmp", None, bome, None,
                     source=src)
    elif len(sys.argv) > 1 and sys.argv[1] == "--github-tutorials":
        for name in sys.argv[2:] or sorted(github_tutorials):
            mid, bome = github_tutorials.get(name, (name, "mediterranean"))
            port_map(mid, f"maps/tutorials/{name}.xml", f"maps/tutorials/{name}.pmp", None, bome, None,
                     source=GithubSource())
    elif len(sys.argv) > 1 and sys.argv[1] == "--github":
        names = sys.argv[2:] or sorted(github_skirmishes)
        for name in names:
            mid, bome = github_skirmishes.get(name, (name, "mediterranean"))
            port_map(mid, f"maps/skirmishes/{name}.xml", f"maps/skirmishes/{name}.pmp", None, bome, None,
                     source=GithubSource())
    else:
        # --github-builtin: the built-in list, fetched from 0 A.D.'s GitHub.
        src = GithubSource if len(sys.argv) > 1 and sys.argv[1] == "--github-builtin" else None
        for mid, xrel, prel, size, bome, hscale in maps_list:
            port_map(mid, xrel, prel, size, bome, hscale, source=src() if src else None)
