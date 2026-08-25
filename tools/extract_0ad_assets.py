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
    from PIL import Image
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
    "villager":    ("art/actors/units/athenians/female_citizen.xml",      "height", 1.70, (0.36, 0.62, 0.94)),
    "swordsman":   ("art/actors/units/athenians/infantry_swordsman_c.xml","height", 1.80, (0.92, 0.32, 0.28)),
    "town_center": ("art/actors/structures/athenians/civil_centre.xml",   "width",  5.20, (0.36, 0.62, 0.94)),
    "house":       ("art/actors/structures/hellenes/house.xml",           "width",  2.10, (0.36, 0.62, 0.94)),
    "tree":        ("art/actors/flora/trees/oak_new.xml",                 "height", 4.50, None),
    "gold_vein":   ("art/actors/geology/metalmine_granite_greek.xml",     "width",  1.80, None),
}

TERRAIN_TILES = {  # out name -> terrain types/<biome>/<tile>
    "grass": "types/aegean_anatolia/grass_01.png",
    "dirt":  "types/aegean_anatolia/dirt_01.png",
    "rock":  "types/aegean_anatolia/cliff_01.png",
}

MUSIC = {"theme.ogg": "audio/music/In_the_Shadow_of_Olympus.ogg",
         "victory.ogg": "audio/music/You_are_Victorious!.ogg",
         "defeat.ogg": "audio/music/Dried_Tears.ogg"}

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
    """Parse .pmd (PSMD container) -> verts/uvs/faces/bones/prop_points.

    Follows CFileUnpacker::Read (source/ps/FileIo.cpp) + CModelDef::Load
    (source/graphics/ModelDef.cpp). Version >= 3 stores bind-pose model-space
    coordinates, which is exactly what a static export needs.
    """
    magic, _, _ = struct.unpack_from("<4sII", data, 0)
    assert magic == b"PSMD", f"bad PSMD magic {magic!r}"
    version = struct.unpack_from("<I", data, 4)[0]
    payloadSize = struct.unpack_from("<I", data, 8)[0]
    assert len(data) == 12 + payloadSize
    o = 12
    rd = {}

    def u32():
        nonlocal o
        v = struct.unpack_from("<I", data, o)[0]; o += 4; return v

    def f32():
        nonlocal o
        v = struct.unpack_from("<f", data, o)[0]; o += 4; return v

    nv = u32()
    nuv = u32() if version > 3 else 1
    verts, uvs = [], []
    for _ in range(nv):
        p = (f32(), f32(), f32()); n = (f32(), f32(), f32())
        uvlast = (0.0, 0.0)
        for _ in range(nuv):
            uvlast = (f32(), f32())
        uvs.append(uvlast)
        o += 20
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
    return {"version": version, "verts": verts, "uvs": uvs, "faces": faces,
            "bones": bones, "prop_points": props}

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
    __slots__ = ("mesh_data", "tex_base", "tex_ao", "material", "transform", "source")

    def __init__(self):
        self.mesh_data = None
        self.tex_base = None
        self.tex_ao = None
        self.material = ""
        self.transform = None
        self.source = ""


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
    path = f"art/{file_ref.strip()}" if not file_ref.startswith("art/") else file_ref
    for cand in (path, path + ".xml"):
        if vfs.exists(cand):
            key = cand
            if key in cache:
                return ET.fromstring(cache[key])
            data = vfs.read(cand)
            cache[key] = data
            return ET.fromstring(data)
    return None


def compose_actor(vfs, actor_path, depth=0, tint=None, sources=None):
    """Compose an actor into a list of Parts.

    Deterministic picks: highest-frequency variant per group (ties -> first).
    Group semantics used:
      - a group that supplies a <mesh> replaces the current pending mesh
        (0ad variant groups override; e.g. armour upgrade swaps body mesh)
      - shared `file=` variants are resolved from art/variants/**
      - groups whose shared file is an animation-state ('biped/...') contribute
        textures only — their props/meshes belong to runtime states we don't bake
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
        resolved = var
        if file_ref:
            if "destruction" in file_ref:
                continue
            shared_root = _resolve_variant_file(vfs, file_ref, vcache)
            shared_var = shared_root.find("variant") if shared_root is not None else None
            if shared_var is not None:
                resolved = _merge_variant_xml(shared_var, var)
        anim_state = file_ref.startswith("biped/") or \
            resolved.find("animation") is not None
        m1, p1, t1, mtl1 = _variant_children(vfs, resolved)
        if m1:
            if not anim_state:
                if acc_mesh is not None and acc_mesh != m1:
                    if flush() is not None:
                        final_parts.pop()  # mesh replaced by later variant group
                if acc_mesh is None:
                    acc_mesh = m1
        acc_tex.update(t1)
        if not anim_state:
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
        if any(s in actor_ref for s in ("decals/", "particle", "flag")):
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
                    final_parts.extend(sub_parts)
                    placed = True
                    break
            if placed:
                break
        if not placed:
            print(f"  ! attachpoint '{attachpoint}' not found for {sub_path}")

    return final_parts, material_name


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
    if max(img.size) > 1024:
        img = img.resize((img.size[0] // 2, img.size[1] // 2), Image.LANCZOS)
    px = img.load()
    w, h = img.size
    r_t, g_t, b_t = (int(c * 255) for c in tint) if tint else (255, 255, 255)
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if tint is not None:
                # 0ad model_common.fs: diffuse *= mix(playerColor, white, tex.a)
                r = int(r * (r_t * (1 - a / 255.0) + a) / 255)
                g = int(g * (g_t * (1 - a / 255.0) + a) / 255)
                b = int(b * (b_t * (1 - a / 255.0) + a) / 255)
                a = 255
            if ao_img is not None:
                ar, ag, ab = ao_img.getpixel((min(x, ao_img.size[0]-1), min(y, ao_img.size[1]-1)))[:3]
                lum = (ar * 299 + ag * 587 + ab * 114) // 1000
                lum = max(lum, 40)
                r = r * lum // 255; g = g * lum // 255; b = b * lum // 255
            px[x, y] = (min(r, 255), min(g, 255), min(b, 255), a)
    return img

# ------------------------------------------------------------ glb writer ----

class GlbBuilder:
    def __init__(self):
        self.bin = bytearray()
        self.images = {}       # content-hash -> index
        self.image_defs = []   # {name, png_bytes}
        self.materials = []    # json defs
        self.primitives = []   # (positions, normals, uvs, indices, material_idx, double_sided, alpha_mode)

    def add_image(self, png_bytes):
        h = hashlib.sha256(png_bytes).hexdigest()[:16]
        if h in self.images:
            return self.images[h]
        idx = len(self.image_defs)
        self.images[h] = idx
        self.image_defs.append({"name": f"tex{idx}", "bytes": png_bytes})
        return idx

    def add_material(self, img_idx, alpha_mode, double_sided):
        key = (img_idx, alpha_mode, double_sided)
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
        self.materials.append(d)
        return len(self.materials) - 1

    @staticmethod
    def _pad(b, align=4):
        while len(b) % align:
            b += b"\x00"
        return b

    def _accessor(self, comp_type, count, data_type, data_bytes, minmax=None):
        self.bin = self._pad(bytes(self.bin))
        offset = len(self.bin)
        self.bin += data_bytes
        acc = {"bufferView": len(self.buffer_views), "componentType": comp_type,
               "count": count, "type": data_type}
        bv = {"buffer": 0, "byteOffset": offset, "byteLength": len(data_bytes)}
        if data_type != "SCALAR":
            bv["byteStride"] = {"VEC3": 12, "VEC2": 8}[data_type]
        self.buffer_views.append(bv)
        if minmax:
            acc["min"], acc["max"] = minmax
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def build(self, node_name):
        import json as _json
        self.accessors = []
        self.buffer_views = []
        self.bin = bytearray()
        prim_json = []
        for positions, normals, uvs, indices, mat_idx, ds, am in self.primitives:
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
            prim_json.append({"attributes": {"POSITION": pos_acc, "NORMAL": nrm_acc,
                                             "TEXCOORD_0": uv_acc},
                              "indices": idx_acc, "material": mat_idx})
        # mark buffer views targets
        for bv, tgt in zip(self.buffer_views, [34962, 34962, 34962, 34963] * len(self.primitives)):
            bv["target"] = tgt
        images = [{"bufferView": None, "mimeType": "image/png", "name": im["name"]}
                  for im in self.image_defs]
        gltf = {
            "asset": {"version": "2.0", "generator": "RTS0 extract_0ad_assets.py"},
            "scene": 0,
            "scenes": [{"nodes": [0]}],
            "nodes": [{"name": node_name, "mesh": 0}],
            "meshes": [{"name": node_name, "primitives": prim_json}],
            "materials": [{k: v for k, v in m.items() if k != "_key"} for m in self.materials],
            "textures": [{"source": i, "sampler": 0} for i in range(len(images))],
            "images": images,
            "samplers": [{"magFilter": 9729, "minFilter": 9987,
                          "wrapS": 10497, "wrapT": 10497}],
            "buffers": [{"byteLength": len(self.bin)}],
            "bufferViews": self.buffer_views,
            "accessors": self.accessors,
        }
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
    baked_cache = {}  # (tex_ref, ao_ref) -> (png_bytes, img_idx)
    for part in parts:
        md = part.mesh_data
        verts, uvs, faces = md["verts"], md["uvs"], md["faces"]
        xf = part.transform if part.transform is not None else trs((0, 0, 0), (0, 0, 0, 1))
        positions = [mat_apply(xf, p) for (p, n) in verts]
        normals = [mat_apply(rot_only(xf), n) for (p, n) in verts]
        all_pos.extend(positions)
        tex = part.tex_base
        mat_idx = 0
        if tex:
            key = (tex, part.tex_ao, tint)
            if key in baked_cache:
                png, img_idx = baked_cache[key]
            else:
                ao = vfs.load_texture_rgba(part.tex_ao) if part.tex_ao else None
                img = bake_texture(vfs, tex, ao, tint)
                if img is not None and max(img.size) > 512:
                    s = 512 / max(img.size)
                    img = img.resize((max(1, int(img.size[0]*s)), max(1, int(img.size[1]*s))), Image.LANCZOS)
                if img is None:
                    png, img_idx = b"", -1
                else:
                    bio = io.BytesIO(); img.save(bio, "PNG", optimize=True)
                    png = bio.getvalue()
                    img_idx = glb.add_image(png)
                baked_cache[key] = (png, img_idx)
            if img_idx >= 0:
                alpha_masked = "player_" not in (part.material or "")
                mat_idx = glb.add_material(img_idx, "MASK" if alpha_masked else "OPAQUE", True)
            else:
                mat_idx = _plain_material(glb)
        else:
            mat_idx = _plain_material(glb)
        tri_indices = []
        for a, b, c in faces:
            tri_indices += [a, c, b]  # 0ad = CW front; glTF = CCW front
        glb.primitives.append((positions, normals, uvs, tri_indices, mat_idx, True, "MASK"))
    # normalize: ground + center + fit
    xs = [p[0] for p in all_pos]; ys = [p[1] for p in all_pos]; zs = [p[2] for p in all_pos]
    minx, maxx, miny, maxy, minz, maxz = min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)
    dim = (maxx-minx, maxy-miny, maxz-minz)
    scale = (target / max(dim[0], dim[2])) if fit == "width" else (target / max(dim[1], 1e-6))
    cx, cz = (minx+maxx)/2, (minz+maxz)/2
    # rewrite primitive positions in place
    for pi, prim in enumerate(glb.primitives):
        pos, nrm, uv, idx, mi, ds, am = prim
        pos2 = [((x-cx)*scale, (y-miny)*scale, (z-cz)*scale) for x, y, z in pos]
        glb.primitives[pi] = (pos2, nrm, uv, idx, mi, ds, am)
    out_path = os.path.join(out_dir, asset_dir(asset_id), f"{asset_id}.glb")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    blob = glb.build(asset_id)
    with open(out_path, "wb") as f:
        f.write(blob)
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


def _plain_material(glb):
    glb.materials.append({"_key": ("none", len(glb.materials)), "name": f"plain{len(glb.materials)}",
                          "pbrMetallicRoughness": {"baseColorFactor": [0.6, 0.6, 0.6, 1.0],
                                                   "metallicFactor": 0.0, "roughnessFactor": 0.9},
                          "doubleSided": True})
    return len(glb.materials) - 1


def rot_only(m):
    r = [row[:] for row in m]
    r[0][3] = r[1][3] = r[2][3] = 0
    return r


def asset_dir(asset_id):
    if asset_id in ("villager", "swordsman"): return "models/units"
    if asset_id in ("town_center", "house"): return "models/buildings"
    return "models/vegetation"

# --------------------------------------------------------------- main -------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=os.path.normpath(DEFAULT_ZIP))
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "assets"))
    args = ap.parse_args()
    out_dir = os.path.abspath(args.out)
    print(f"archive: {args.zip}")
    print(f"output : {out_dir}")
    manifest = {"generator": "tools/extract_0ad_assets.py",
                "origin": "0 A.D. by Wildfire Games (public mod archive)",
                "license": "CC-BY-SA 3.0 / GPL-2.0 (see docs/0AD_ATTRIBUTION.md)",
                "models": {}}
    with zipfile.ZipFile(args.zip) as zf:
        vfs = Vfs(zf)
        for asset_id, (actor_ref, fit, target, tint) in ASSETS.items():
            manifest["models"][asset_id] = compile_asset(
                vfs, asset_id, actor_ref, fit, target, tint, out_dir)
        # terrain tiles
        terr_dir = os.path.join(out_dir, "textures", "terrain")
        os.makedirs(terr_dir, exist_ok=True)
        for name, ref in TERRAIN_TILES.items():
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
    man_path = os.path.join(out_dir, "models", "manifest.json")
    with open(man_path, "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    print(f"manifest -> {man_path}")


if __name__ == "__main__":
    main()
