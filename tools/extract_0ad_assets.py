#!/usr/bin/env python3
"""Extract game-ready assets from 0 A.D.'s public mod archive into RTS0's asset folders.

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
    # core MVP — keep deterministic ids for GameSim
    "villager":    ("art/actors/units/athenians/female_citizen.xml",      "height", 1.70, (0.36, 0.62, 0.94)),
    "swordsman":   ("art/actors/units/athenians/infantry_swordsman_c.xml","height", 1.80, (0.92, 0.32, 0.28)),
    "town_center": ("art/actors/structures/hellenes/civic_centre_new.xml", "width",  6.20, (0.36, 0.62, 0.94)),
    "house":       ("art/actors/structures/romans/house.xml",             "width",  2.40, (0.36, 0.62, 0.94)),
    "tree":        ("art/actors/flora/trees/oak_new.xml",                 "height", 4.50, None),
    "gold_vein":   ("art/actors/geology/metalmine_granite_greek.xml",     "width",  1.80, None),
}

# Additional curated 0ad catalogue for full game (pulled with --all)
# Covers economy, combat, flora, geology, defenses — enough for 0ad-like loop
FULL_ASSETS = {
    # --- Hellenes/Generic units (enough for tech tree demo) — use verified existing actor paths ---
    "archer":      ("art/actors/units/athenians/infantry_archer_a.xml",   "height", 1.80, (0.36, 0.62, 0.94)),
    "spearman":    ("art/actors/units/athenians/infantry_spearman_b.xml", "height", 1.80, (0.36, 0.62, 0.94)),
    "cavalry":     ("art/actors/units/athenians/cavalry_swordsman_b_m.xml","height", 1.90, (0.36, 0.62, 0.94)),
    "healer":      ("art/actors/units/athenians/healer.xml",              "height", 1.70, (0.92, 0.90, 0.82)),
    "ram":         ("art/actors/units/romans/siege_ram.xml",                "width",  3.00, None),
    # --- Structures — use civ-specific existing ---
    "barracks":    ("art/actors/structures/athenians/barracks.xml",         "width",  4.00, (0.36, 0.62, 0.94)),
    "farmstead":   ("art/actors/structures/hellenes/farmstead.xml",        "width",  3.20, (0.36, 0.62, 0.94)),
    "storehouse":  ("art/actors/structures/hellenes/storehouse.xml",       "width",  3.40, (0.36, 0.62, 0.94)),
    "market":      ("art/actors/structures/hellenes/market.xml",           "width",  4.20, (0.36, 0.62, 0.94)),
    "temple":      ("art/actors/structures/hellenes/temple_epic.xml",      "width",  5.00, (0.36, 0.62, 0.94)),
    "wall":        ("art/actors/structures/athenians/wall_long.xml",       "width",  2.00, (0.36, 0.62, 0.94)),
    "wall_tower":  ("art/actors/structures/hellenes/wall_tower.xml",       "width",  2.20, (0.36, 0.62, 0.94)),
    "fortress":    ("art/actors/structures/athenians/fortress.xml",        "width",  6.00, (0.36, 0.62, 0.94)),
    # --- Flora ---
    "pine":        ("art/actors/flora/trees/pine.xml",                     "height", 4.80, None),
    "palm":        ("art/actors/flora/trees/palm_cretan_date.xml",         "height", 4.00, None),
    # --- Geology / resources ---
    "stone_mine":  ("art/actors/geology/stonemine_granite.xml",            "width",  2.00, None),
    "ruins":       ("art/actors/structures/hellenes/temple_epic.xml",      "width",  2.50, None), # placeholder reuse temple for ruins
    # --- Fauna (for hunting) ---
    "deer":        ("art/actors/fauna/deer.xml",                           "height", 1.40, None),
}

TERRAIN_TILES = {  # out name -> terrain types/<biome>/<tile>
    "grass": "types/aegean_anatolia/grass_01.png",
    "dirt":  "types/aegean_anatolia/dirt_01.png",
    "rock":  "types/aegean_anatolia/cliff_01.png",
}

FULL_TERRAIN = {
    "alpine_snow":   "types/alpine_arctic/snow_01.png",
    "desert_sand":   "types/african_savanna/dirt_01.png",
    "savanna_mud":   "types/african_savanna/mud_01.png",
    "savanna_grass": "types/african_savanna/grass_02.png",
    "medit_sand":    "types/aegean_anatolia/sand_01.png",
}

MUSIC = {"theme.ogg": "audio/music/In_the_Shadow_of_Olympus.ogg",
         "victory.ogg": "audio/music/You_are_Victorious!.ogg",
         "defeat.ogg": "audio/music/Dried_Tears.ogg"}

# Skeletal animations — PSSA (PSA) files for biped/mechanical skeletons
# Mirrors 0ad art/animation/* + art/skeletons/*; used by Godot Skeleton3D + AnimationPlayer
# We copy the cached PSSA binary and also generate JSON for debugging
ANIMATIONS = {
    # biped infantry (swordsman/spearman/archer)
    "biped_idle":       "art/animation/biped/infantry/swordsman/idle_relax_shield_01.dae.cached.psa",
    "biped_walk":       "art/animation/biped/infantry/swordsman/walk_relax_shield.dae.cached.psa",
    "biped_run":        "art/animation/biped/infantry/swordsman/run_relax_shield.dae.cached.psa",
    "biped_attack":     "art/animation/biped/infantry/swordsman/attack_melee_shield_01.dae.cached.psa",
    "biped_death":      "art/animation/biped/infantry/death_a.dae.cached.psa",
    # citizen / gatherer
    "citizen_idle":     "art/animation/biped/citizen/idle_relax_m.dae.cached.psa",
    "citizen_walk":     "art/animation/biped/citizen/walk_relax_m.dae.cached.psa",
    "gather_wood":      "art/animation/biped/gatherer/gather_wood.dae.cached.psa",
    "gather_mine":      "art/animation/biped/gatherer/gather_mine.dae.cached.psa",
    "gather_berries":   "art/animation/biped/gatherer/gather_berries.dae.cached.psa",
    "carry_wood":       "art/animation/biped/gatherer/carry_wood_m.dae.cached.psa",
    "carry_ore":        "art/animation/biped/gatherer/carry_ore_m.dae.cached.psa",
    # archer specific
    "archer_attack":    "art/animation/biped/infantry/archer/attack_ranged.dae.cached.psa",
    # horse (cavalry mesh is the mount — PMD bone count matches horse.xml, not biped)
    "horse_idle":       "art/animation/quadraped/horse_idle_01.dae.cached.psa",
    "horse_walk":       "art/animation/quadraped/horse_walk.dae.cached.psa",
    "horse_run":        "art/animation/quadraped/horse_gallop.dae.cached.psa",
    "horse_attack":     "art/animation/quadraped/horse_attack_01.dae.cached.psa",
    "horse_death":      "art/animation/quadraped/horse_death.dae.cached.psa",
    "deer_idle":        "art/animation/quadraped/deer_idle_01.dae.cached.psa",
    "deer_walk":        "art/animation/quadraped/deer_walk_01.dae.cached.psa",
    # healer (biped 46 — citizen healer set, not infantry relax)
    "healer_idle":      "art/animation/biped/citizen/idle_relax_h_m.dae.cached.psa",
    "healer_walk":      "art/animation/biped/citizen/walk_relax_h_m.dae.cached.psa",
    "healer_run":       "art/animation/biped/citizen/jog_h.dae.cached.psa",
    "healer_heal":      "art/animation/biped/citizen/healing_m.dae.cached.psa",
    # roman ram (mechanical, 8 bones matching rome_ram.dae)
    "ram_idle":         "art/animation/mechanical/rome_ram_idle.dae.cached.psa",
    "ram_walk":         "art/animation/mechanical/rome_ram_move.dae.cached.psa",
    "ram_attack":       "art/animation/mechanical/rome_ram_attack.dae.cached.psa",
}

SKELETONS = {
    "biped": "art/skeletons/biped.xml",
    "horse": "art/skeletons/horse.xml",
    "deer": "art/skeletons/deer.xml",
}

# 0 A.D. cavalry is two actors: *_m mount + *_r rider parented to horse "rider".
RIDER_ACTORS = {
    "cavalry": ("art/actors/units/athenians/cavalry_swordsman_b_r.xml", "rider"),
}

UNIT_IDS = frozenset({
    "villager", "swordsman", "archer", "spearman", "cavalry", "healer", "ram", "deer",
})
BUILDING_IDS = frozenset({
    "town_center", "house", "barracks", "farmstead", "storehouse", "market",
    "temple", "wall", "wall_tower", "fortress", "ruins",
})
VEGETATION_IDS = frozenset({
    "tree", "pine", "palm", "gold_vein", "stone_mine",
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
        # uncompressed: assume A8R8G8B8
        img = Image.frombytes("RGBA", (width, height), data[off:off + width * height * 4], "raw", "BGRA")
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

# ------------------------------------------------------------ zip vfs -------

class Vfs:
    def __init__(self, zf):
        self.zf = zf
        self._tex_cache = {}
        self._png_cache = {}

    def read(self, path):
        return self.zf.read(path)

    def exists(self, path):
        return path in self.zf.namelist()

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
                 "bone", "force_rigid", "aux")

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


def _pick_variant(group):
    vars_ = group.findall("variant")
    if not vars_:
        return None
    best, best_freq = None, -1
    for v in vars_:
        try:
            freq = int(v.get("frequency", "1"))
        except ValueError:
            freq = 1
        if freq > best_freq:
            best, best_freq = v, freq
    return best


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


def compose_actor(vfs, actor_path, depth=0, tint=None, sources=None):
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
        var = _pick_variant(g)
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
        # Skip animation-library prop swaps (empty attach / slaughter dagger).
        if p1 and not anim_only:
            pending_props.extend(p1)
        if mtl1:
            acc_mat = mtl1

    flush()
    material_name = acc_mat or top_mat or ""

    if sources is not None:
        sources.add(("actor", actor_path))

    # resolve props against the composed parts (CModel::ValidatePosition rule:
    # world = boneBindMatrix * propPoint.transform, or transform alone if unboned)
    for actor_ref, attachpoint, _sel in pending_props:
        if any(s in actor_ref for s in ("decals/", "platforms/", "particle", "flag")):
            continue
        sub_path = actor_ref if actor_ref.startswith("art/") else f"art/actors/{actor_ref}"
        sub_parts, _ = compose_actor(vfs, sub_path, depth + 1, tint, sources)
        if not sub_parts:
            continue
        placed = False
        for part in final_parts:
            for pp in part.mesh_data["prop_points"]:
                if pp["name"].lower() == attachpoint.lower():
                    local = trs(pp["pos"], pp["quat"])
                    if pp["bone"] != 0xFF and pp["bone"] < len(part.mesh_data["bones"]):
                        t, q = part.mesh_data["bones"][pp["bone"]]
                        world = mat_mul(trs(t, q), local)
                    else:
                        world = local
                    for sp in sub_parts:
                        sp.transform = world
                        sp.bone = pp["bone"] if pp["bone"] != 0xFF else 0xFF
                    final_parts.extend(sub_parts)
                    placed = True
                    break
            if placed:
                break
        if not placed:
            # CModel: boneIndex 0xFF => relative to object origin, not a bone
            print(f"  ! attachpoint '{attachpoint}' not found for {sub_path}; origin-attach")
            for sp in sub_parts:
                if sp.transform is None:
                    sp.transform = trs((0, 0, 0), (0, 0, 0, 1))
                sp.bone = 0xFF
            final_parts.extend(sub_parts)

    return final_parts, material_name


def _identity():
    return trs((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))


def _prop_world(part, pp):
    local = trs(pp["pos"], pp["quat"])
    bones = part.mesh_data.get("bones") or []
    if pp["bone"] != 0xFF and pp["bone"] < len(bones):
        t, q = bones[pp["bone"]]
        return mat_mul(trs(t, q), local)
    return local


def attach_rider_parts(vfs, parts, body, asset_id, tint, sources):
    """Parent the *_r cavalry actor onto the horse `rider` prop (bone 2).

    Rider uses a 46-bone biped; the mount uses 49 horse bones. One glTF skin
    cannot drive both, so rider meshes are rigid-bound to the horse rider joint.
    """
    cfg = RIDER_ACTORS.get(asset_id)
    if not cfg:
        return parts
    rpath, ap_name = cfg
    rparts, _ = compose_actor(vfs, rpath, 0, tint, sources)
    if not rparts:
        print(f"  ! rider actor empty: {rpath}")
        return parts
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
        return parts
    world = _prop_world(attach_part, attach_pp)
    rb = attach_pp["bone"] if attach_pp["bone"] != 0xFF else 0
    for sp in rparts:
        child = sp.transform if sp.transform is not None else _identity()
        sp.transform = mat_mul(world, child)
        sp.bone = rb
        sp.force_rigid = True
        sp.aux = True
    print(f"  rider <- {rpath} @{ap_name} bone={rb} ({len(rparts)} parts)")
    return parts + rparts


def _merge_variant_xml(shared_var, inline_var):
    """Combine a shared variant file's <variant> with the inline one
    (inline wins for meshes/materials; children merge)."""
    merged = ET.Element("variant")
    for child in shared_var:
        merged.append(child)
    for child in inline_var:
        tag = child.tag
        existing = merged.findall(tag)
        if existing:
            for e in existing:
                merged.remove(e)
        merged.append(child)
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

def compile_asset(vfs, asset_id, actor_ref, fit, target, tint, out_dir):
    print(f"[{asset_id}] {actor_ref}")
    sources = set()
    parts, material_name = compose_actor(vfs, actor_ref, 0, tint, sources)
    if not parts:
        raise RuntimeError(f"no parts composed for {asset_id}")
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
    # normalize: ground + center + fit
    xs = [p[0] for p in all_pos]; ys = [p[1] for p in all_pos]; zs = [p[2] for p in all_pos]
    minx, maxx, miny, maxy, minz, maxz = min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)
    dim = (maxx-minx, maxy-miny, maxz-minz)
    scale = (target / max(dim[0], dim[2])) if fit == "width" else (target / max(dim[1], 1e-6))
    cx, cz = (minx+maxx)/2, (minz+maxz)/2
    # rewrite primitive positions in place
    for pi, prim in enumerate(glb.primitives):
        pos, nrm, uv, idx, mi = prim[0], prim[1], prim[2], prim[3], prim[4]
        pos2 = [((x-cx)*scale, (y-miny)*scale, (z-cz)*scale) for x, y, z in pos]
        glb.primitives[pi] = (pos2, nrm, uv, idx, mi) + tuple(prim[5:])
    out_path = os.path.join(out_dir, asset_dir(asset_id), f"{asset_id}.glb")
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
    return {
        "file": os.path.relpath(out_path, out_dir).replace(os.sep, "/"),
        "parts": len(parts),
        "size_meters": [round(final_dim[0], 3), round(final_dim[1], 3), round(final_dim[2], 3)],
        "origin": "ground-center",
        "sources": sorted(f"{kind}:{ref}" for kind, ref in sources),
        "upstream_material": material_name,
    }


def compile_unit_animated(vfs, asset_id, actor_ref, fit, target, tint, out_dir,
                          skeleton_name, anims_map):
    """Like compile_asset but bakes a REAL skeletal (rigid-prop) animation.

    Builds a bone-node hierarchy from the unit's skeleton, attaches every mesh
    part to the bone it is bound to, and emits glTF animation clips sampled
    from 0 A.D. PSA files so Godot plays them natively via AnimationPlayer.
    """
    print(f"[{asset_id}] (animated) {actor_ref}  skeleton={skeleton_name}")
    sources = set()
    parts, material_name = compose_actor(vfs, actor_ref, 0, tint, sources)
    if not parts:
        raise RuntimeError(f"no parts composed for {asset_id}")

    # Body part carries the skeleton (bones + prop attachment points).
    body = None
    for p in parts:
        if p.mesh_data and len(p.mesh_data.get("bones", [])) > 0:
            body = p
            break
    if body is None:
        body = parts[0]
    parts = attach_rider_parts(vfs, parts, body, asset_id, tint, sources)
    bones_local = body.mesh_data.get("bones", [])
    num_bones = max(1, len(bones_local))

    # Skeleton names from art/skeletons/*.xml (display only). 0 A.D. does NOT
    # parent-multiply bone matrices — CBoneState is "relative to root"
    # (SkeletonAnimDef.h) and BuildBoneMatrices writes each bone independently.
    bone_names = []
    skel_ref = SKELETONS.get(skeleton_name)
    if skel_ref and vfs.exists(skel_ref):
        sb = parse_skeleton_xml(vfs.read(skel_ref))
        bone_names = [b["name"] for b in sb]
    rest_worlds = []
    rest_tq = []
    for i in range(num_bones):
        t, q = bones_local[i] if i < len(bones_local) else ((0, 0, 0), (0, 0, 0, 1))
        rest_tq.append((t, q))
        rest_worlds.append(trs(t, q))

    glb = GlbBuilder()
    all_pos = []
    fit_pos = []
    baked_cache = {}
    ao_img_cache = {}
    part_info = []  # (prim_idx, bone_idx, world_xf)
    for part in parts:
        md = part.mesh_data
        verts, uvs, faces = md["verts"], md["uvs"], md["faces"]
        uv1s = md.get("uv1s") or []
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
        origin_locked = bone_idx == 0xFF and not getattr(part, "force_rigid", False)
        if origin_locked:
            joints, weights = None, None
        elif getattr(part, "force_rigid", False):
            if bone_idx < 0 or bone_idx == 0xFF or bone_idx >= num_bones:
                bone_idx = 0
            fb = bone_idx
            joints = [(fb, 0, 0, 0)] * len(positions)
            weights = [(1.0, 0.0, 0.0, 0.0)] * len(positions)
        else:
            if bone_idx < 0 or bone_idx >= num_bones:
                bone_idx = 0
            joints, weights = _skin_attrs(md, len(positions), bone_idx, num_bones)
        prim = glb._add_primitive(positions, normals, flip_uvs, tri, mat_idx, joints, weights, flip_uv1, colors)
        part_info.append((prim, bone_idx, xf, origin_locked))

        if not getattr(part, "aux", False):
            fit_pos.extend(positions)

    # Normalization (center XZ, base Y, scale to target) applied as the ROOT
    # node transform so the in-model-space bone/part transforms stay intact.
    # Rider/aux parts are left out of the fit so cavalry stays horse-sized.
    box = fit_pos or all_pos
    xs = [p[0] for p in box]; ys = [p[1] for p in box]; zs = [p[2] for p in box]
    minx, maxx, miny, maxy, minz, maxz = min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)
    dim = (maxx - minx, maxy - miny, maxz - minz)
    scale = (target / max(dim[0], dim[2])) if fit == "width" else (target / max(dim[1], 1e-6))
    cx, cz = (minx + maxx) / 2, (minz + maxz) / 2
    root_t = (-cx * scale, -miny * scale, -cz * scale)
    root_s = (scale, scale, scale)

    # Node graph matching CModel / CSkeletonAnimDef:
    # every bone is a sibling of the scaled root with root-relative TRS.
    # Skinning uses IBM = inverse(bind) like ModelDef.cpp (Translate(-t) then
    # Rotate(q^-1)), which is the same as rigid_inverse(bind).
    bone_node = {i: 1 + i for i in range(num_bones)}
    part_node_start = 1 + num_bones
    mesh_nodes = list(range(part_node_start, part_node_start + len(part_info)))
    all_bones = [bone_node[i] for i in range(num_bones)]
    nodes = [{"name": asset_id, "translation": list(root_t), "scale": list(root_s),
              "children": all_bones + mesh_nodes}]
    for i in range(num_bones):
        t, q = rest_tq[i]
        nodes.append({"name": bone_names[i] if i < len(bone_names) else f"bone{i}",
                      "translation": list(t), "rotation": list(q)})
    for k, (prim, bidx, wxf, origin_locked) in enumerate(part_info):
        nd = {"name": f"{asset_id}_part{k}", "mesh": prim}
        if not origin_locked:
            nd["skin"] = 0
        nodes.append(nd)

    # Animation clips: PSA keys are already root-relative (BuildBoneMatrices).
    # Times are converted from milliseconds (GetFrameTime) to glTF seconds.
    animations = []
    for state_name, psa_key in anims_map.items():
        ref = ANIMATIONS.get(psa_key, psa_key)
        if not vfs.exists(ref):
            print(f"  ! anim missing: {ref}")
            continue
        try:
            psa = parse_psa(vfs.read(ref))
        except Exception as e:
            print(f"  ! anim parse failed {state_name}: {e}")
            continue
        nf = psa["numFrames"]; nk = psa["numKeys"]
        if nk != num_bones:
            print(f"  ! bone count PMD={num_bones} PSA={nk} for {state_name} (0ad ENSURE equal; extra bones stay bind-pose)")
        times = [psa["frameTime"] * f / 1000.0 for f in range(nf)]
        tracks = {}
        for i in range(num_bones):
            trans = []
            rots = []
            rt, rq = rest_tq[i]
            for f in range(nf):
                ki = f * nk + i
                if i < nk and ki < len(psa["keys"]):
                    key = psa["keys"][ki]
                    trans.append(tuple(key["t"]))
                    rots.append(tuple(key["q"]))
                else:
                    trans.append(tuple(rt))
                    rots.append(tuple(rq))
            tracks[bone_node[i]] = {"translation": trans, "rotation": rots}
        animations.append({"name": state_name, "times": times, "tracks": tracks})
        print(f"  anim '{state_name}' <- {ref} ({nf}f, {nk}b)")

    skins = [{
        "joints": all_bones,
        "inverseBindMatrices": [rigid_inverse(rest_worlds[i]) for i in range(num_bones)],
        "skeleton": bone_node[0],
    }]

    out_path = os.path.join(out_dir, asset_dir(asset_id), f"{asset_id}.glb")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    blob = glb.build(asset_id, nodes=nodes, animations=animations, skins=skins)
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
        "animations": list(anims_map.keys()),
        "size_meters": [round(final_dim[0], 3), round(final_dim[1], 3), round(final_dim[2], 3)],
        "origin": "ground-center",
        "sources": sorted(f"{kind}:{ref}" for kind, ref in sources),
        "upstream_material": material_name,
    }
    return entry


def _img_png(img, max_edge=1024):
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


def mat_to_tq(m):
    t = (m[0][3], m[1][3], m[2][3])
    q = mat3_to_quat([[m[i][j] for j in range(3)] for i in range(3)])
    return t, q


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


def asset_dir(asset_id):
    if asset_id in UNIT_IDS:
        return "models/units"
    if asset_id in BUILDING_IDS:
        return "models/buildings"
    if asset_id in VEGETATION_IDS:
        return "models/vegetation"
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
            if aid in ids:
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

# Per-unit skeleton + animation-state -> PSA mapping.
# 0 A.D. requires PMD bone count == PSA numKeys (CSkeletonAnimDef).
# Clip names MUST match world_view_node (Idle/Walk/Run/Attack/Death/Gather/Build).
#
# Texture mapping (same as source/graphics + shaders/glsl/model_common.fs):
#   UV0 / TEXCOORD_0  -> baseTex atlas (tiling). OpenGL V is flipped for glTF.
#   UV1 / COLOR_0     -> aoTex unique unwrap (never multiplied onto UV0 in image space).
#   vertex blend      -> JOINTS_0 / WEIGHTS_0 (4 influences, 0xFF = unbound)
#   actor prop @bone  -> rigid bind 100% to that joint
UNIT_CFG = {
    "villager": ("biped", {
        "Idle": "citizen_idle", "Walk": "citizen_walk", "Run": "citizen_walk",
        "Attack": "biped_attack", "Death": "biped_death",
        "Gather": "gather_wood", "Build": "gather_wood"}),
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
        "Attack": "horse_attack", "Death": "horse_death"}),
    "deer": ("deer", {
        "Idle": "deer_idle", "Walk": "deer_walk", "Run": "deer_walk"}),
    "healer": ("biped", {
        "Idle": "healer_idle", "Walk": "healer_walk", "Run": "healer_run",
        "Attack": "healer_heal", "Death": "biped_death"}),
    "ram": ("", {
        "Idle": "ram_idle", "Walk": "ram_walk", "Run": "ram_walk",
        "Attack": "ram_attack"}),
}

# --------------------------------------------------------------- main -------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=os.path.normpath(DEFAULT_ZIP))
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "assets"))
    ap.add_argument("--all", action="store_true", help="also extract FULL_ASSETS + FULL_TERRAIN catalogue")
    ap.add_argument("--all-animations", action="store_true", help="extract all 650+ PSA animations (preserves folder structure)")
    ap.add_argument("--all-skeletons", action="store_true", help="extract all skeletons")
    ap.add_argument("--only", nargs="+", help="extract only these asset ids")
    ap.add_argument("--list-actors", action="store_true", help="list all art/actors/*.xml in zip and exit")
    args = ap.parse_args()
    out_dir = os.path.abspath(args.out)
    print(f"archive: {args.zip}")
    print(f"output : {out_dir}")
    clean_misplaced_assets(out_dir)
    manifest = {"generator": "tools/extract_0ad_assets.py",
                "origin": "0 A.D. by Wildfire Games (public mod archive)",
                "license": "CC-BY-SA 3.0 / GPL-2.0 (see docs/0AD_ATTRIBUTION.md)",
                "models": {}}
    with zipfile.ZipFile(args.zip) as zf:
        vfs = Vfs(zf)
        if args.list_actors:
            for n in sorted(zf.namelist()):
                if n.startswith("art/actors/") and n.endswith(".xml"):
                    print(n)
            return
        assets = dict(ASSETS)
        terrain = dict(TERRAIN_TILES)
        if args.all:
            assets.update(FULL_ASSETS)
            terrain.update(FULL_TERRAIN)
            print(f"[all] expanding to {len(assets)} models + {len(terrain)} terrain tiles")
        if args.only:
            known = dict(ASSETS)
            known.update(FULL_ASSETS)
            assets = {k: v for k, v in known.items() if k in args.only}
            if not assets:
                sys.exit(f"--only matched nothing. known ids: {', '.join(sorted(known))}")
        for asset_id, (actor_ref, fit, target, tint) in assets.items():
            try:
                if asset_id in UNIT_CFG:
                    sk, anims = UNIT_CFG[asset_id]
                    manifest["models"][asset_id] = compile_unit_animated(
                        vfs, asset_id, actor_ref, fit, target, tint, out_dir, sk, anims)
                else:
                    manifest["models"][asset_id] = compile_asset(
                        vfs, asset_id, actor_ref, fit, target, tint, out_dir)
            except Exception as e:
                print(f"[{asset_id}] FAILED: {e}")
                continue
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
            if os.path.exists(dst):
                continue
            with open(dst, "wb") as f:
                f.write(zf.read(ref))
            print(f"[music] {out_name} ({zf.getinfo(ref).file_size//1024}KB)")
            manifest.setdefault("music", {})[out_name] = ref
        # skeletons
        skel_dir = os.path.join(out_dir, "skeletons")
        os.makedirs(skel_dir, exist_ok=True)
        skels_to_extract = dict(SKELETONS)
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
        anims_to_extract = dict(ANIMATIONS)
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
    # also write skeleton/animation verification summary
    print(f"extracted {len(manifest.get('animations',{}))} animations + {len(manifest.get('skeletons',{}))} skeletons")


if __name__ == "__main__":
    main()
