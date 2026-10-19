#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the art
it converts is CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

Convert 0 A.D. animal actors from their source files (Collada meshes and
clips, PNG/DDS skins in a 0 A.D. git checkout) into animated GLBs under
assets/models/units/fauna_<actor>.glb.

extract_0ad_assets.py reads the cached .pmd/.psa files of 0 A.D.'s release
archive, which this repository does not ship; the source tree on GitHub has
the .dae files those caches are built from, so this tool reads those instead.
It does what 0 A.D.'s Collada converter (source/collada) does for a skinned
mesh: bind-shape and inverse-bind matrices skin the mesh into its rest pose,
and each clip's joint matrices are sampled into root-relative bone poses.

The GLB layout matches extract_0ad_assets.compile_unit_animated: a root node
that grounds, centres and scales the model, every bone a flat child of it with
a root-relative pose, one skin, and clips named Idle, Walk, Run, Attack and
Death for RTS0's crowd renderer.

Usage:
  git clone --depth 1 --filter=blob:none --sparse https://github.com/0ad/0ad.git 0ad
  git -C 0ad sparse-checkout set binaries/data/mods/public/art/{actors,variants,meshes/skeletal,animation/quadraped,textures/skins/skeletal}
  python3 -I tools/extract_0ad_fauna_models.py --0ad 0ad [--only fauna_hippopotamus ...]
"""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_assets import GlbBuilder, decode_dds, _img_png, mat3_to_quat  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO, "assets", "models", "units")
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")

# model id -> (0 A.D. actor under art/actors, height in RTS0 model units[, fit]).
# Heights follow the animals extract_0ad_assets.py already sized (boar 1.1,
# cow 1.4, lion 1.2, donkey 1.2, bush elephant 3.2, giraffe 4.2, chicken 0.5);
# sea animals are fitted by length instead (a trireme is 7 long).
MODELS = {
    "fauna_bear_black": ("fauna/bear_black.xml", 1.5),
    "fauna_bear_white": ("fauna/bear_white.xml", 1.7),
    "fauna_bull": ("fauna/bull.xml", 1.5),
    "fauna_sanga_cattle": ("fauna/sanga_cattle.xml", 1.5),
    "fauna_zebu_wild": ("fauna/zebu_wild.xml", 1.5),
    "fauna_camel": ("fauna/camel.xml", 2.1),
    "fauna_elephant_african_baby": ("fauna/elephant_african_baby.xml", 1.5),
    "fauna_elephant_asian": ("fauna/elephant_asian.xml", 2.9),
    "fauna_fox_arctic": ("fauna/fox_arctic.xml", 0.55),
    "fauna_giraffe_baby": ("fauna/giraffe_baby.xml", 2.4),
    "fauna_hippopotamus": ("fauna/hippopotamus.xml", 1.5),
    "fauna_horse": ("fauna/horse.xml", 1.7),
    "fauna_horse_breed": ("fauna/horse_breed.xml", 1.7),
    "fauna_horse_lusitano": ("fauna/horse_lusitano.xml", 1.7),
    "fauna_horse_marwari": ("fauna/horse_marwari.xml", 1.7),
    "fauna_horse_persian": ("fauna/horse_persian.xml", 1.7),
    "fauna_pony": ("fauna/pony.xml", 1.3),
    "fauna_muskox": ("fauna/muskox.xml", 1.4),
    "fauna_peacock": ("fauna/peacock.xml", 0.9),
    "fauna_pig_flaming": ("fauna/pig_flaming.xml", 0.8),
    "fauna_piglet": ("fauna/piglet.xml", 0.45),
    "fauna_rhino": ("fauna/rhino.xml", 1.7),
    "fauna_shark": ("fauna/shark.xml", 2.6, "length"),
    "fauna_tiger": ("fauna/tiger.xml", 1.1),
    "fauna_walrus": ("fauna/walrus.xml", 1.1),
    "fauna_whale_fin": ("fauna/whale_fin.xml", 11.0, "length"),
    "fauna_whale": ("fauna/whale.xml", 10.0, "length"),
    "fauna_wildebeest": ("fauna/wildebeest.xml", 1.4),
    "fauna_arctic_wolf": ("fauna/arctic_wolf.xml", 1.0),
}

# RTS0 clip -> 0 A.D. animation names in the actor (lower case), best first.
# "feeding" is 0.28 UnitAI animal IDLE.LINGERING's SelectAnimation("feeding"): the
# grazing loop actors without one fall back from (horses, ponies and hippos alias
# one of their idles, so they linger on those as in 0.28).
CLIPS = {
    "Idle": ("idle",),
    "Walk": ("walk",),
    "Run": ("run", "trot", "gallop"),
    "Attack": ("attack_melee", "attack", "attack_melee1"),
    "Death": ("death",),
    "Feeding": ("feeding",),
}

COLLADA = "{http://www.collada.org/2005/11/COLLADASchema}"
# Collada Z_UP -> RTS0 model space: (x, y, z) -> (x, z, y), the axis swap 0 A.D.'s
# converter bakes into its .pmd files (a mirror, so triangle winding flips too).
# Models then face -Z like the ones extract_0ad_assets.py writes.
ZUP = np.array([[1, 0, 0, 0], [0, 0, 1, 0], [0, 1, 0, 0], [0, 0, 0, 1]], dtype=np.float64)


def q(tag):
    return "/".join(COLLADA + t for t in tag.split("/"))


class Art:
    def __init__(self, root):
        self.base = os.path.join(root, "binaries", "data", "mods", "public", "art")

    def path(self, *parts):
        return os.path.join(self.base, *parts)

    def xml(self, *parts):
        return ET.parse(self.path(*parts)).getroot()


def _merge_variant(art, dst, v):
    if v.get("file"):
        merged = art.xml("variants", v.get("file"))
        _merge_variant(art, dst, merged)
    m = v.find("mesh")
    if m is not None and m.text:
        dst["mesh"] = m.text.strip()
    for t in v.findall("textures/texture"):
        dst["tex"][t.get("name")] = t.get("file")
    for a in v.findall("animations/animation"):
        dst["anims"].setdefault((a.get("name") or "").lower(), []).append(
            (a.get("file"), float(a.get("frequency") or 1)))
    for p in v.findall("props/prop"):
        if p.get("actor"):
            dst["props"].append((p.get("actor"), p.get("attachpoint")))


def resolve_actor(art, actor):
    """The mesh, textures, clips and props of an actor's first variant in each group."""
    root = art.xml("actors", actor)
    d = {"actor": actor, "mesh": None, "tex": {}, "anims": {}, "props": []}
    for g in root.findall("group"):
        vs = g.findall("variant")
        if vs:
            # The variant 0 A.D. picks most often; ties go to the first.
            best = max(vs, key=lambda v: float(v.get("frequency") or 1))
            _merge_variant(art, d, best)
    m = root.find("material")
    d["material"] = (m.text or "").strip() if m is not None else ""
    return d


def pick_clip(anims, names):
    for n in names:
        cands = anims.get(n)
        if cands:
            return max(cands, key=lambda c: c[1])[0]
    return None


# ------------------------------------------------------------------ collada --

def _floats(el):
    return np.array([float(x) for x in el.text.split()], dtype=np.float64) if el is not None and el.text else np.zeros(0)


def _source(doc, ref):
    sid = ref.lstrip("#")
    for s in doc.iter(COLLADA + "source"):
        if s.get("id") == sid:
            return s
    raise KeyError(ref)


def _mat(text):
    return np.array([float(x) for x in text.split()], dtype=np.float64).reshape(4, 4)


def _node_local(node):
    """A node's rest transform: its matrix, or its translate/rotate/scale stack."""
    m = np.eye(4)
    for c in node:
        tag = c.tag[len(COLLADA):]
        if tag == "matrix":
            m = m @ _mat(c.text)
        elif tag == "translate":
            t = np.eye(4)
            t[:3, 3] = [float(x) for x in c.text.split()]
            m = m @ t
        elif tag == "rotate":
            x, y, z, ang = [float(v) for v in c.text.split()]
            ax = np.array([x, y, z])
            n = np.linalg.norm(ax)
            if n > 0:
                ax /= n
                a = np.radians(ang)
                K = np.array([[0, -ax[2], ax[1]], [ax[2], 0, -ax[0]], [-ax[1], ax[0], 0]])
                r = np.eye(4)
                r[:3, :3] = np.eye(3) + np.sin(a) * K + (1 - np.cos(a)) * K @ K
                m = m @ r
        elif tag == "scale":
            s = np.eye(4)
            s[0, 0], s[1, 1], s[2, 2] = [float(v) for v in c.text.split()]
            m = m @ s
    return m


def scene_nodes(doc):
    """name -> (node id, parent name or None, rest local) for every scene node."""
    out = {}

    def walk(n, parent):
        # Joints are looked up by name; other nodes (armature objects, which
        # may share a joint's name) by id.
        name = n.get("sid") or n.get("name") or n.get("id")
        if n.get("type") != "JOINT":
            name = "~" + (n.get("id") or name)
        while name in out:
            name += "'"
        out[name] = (n.get("id"), parent, _node_local(n))
        for c in n.findall(COLLADA + "node"):
            walk(c, name)

    for vs in doc.iter(COLLADA + "visual_scene"):
        for n in vs.findall(COLLADA + "node"):
            walk(n, None)
        break
    return out


def node_animations(doc):
    """node id -> (times, [4x4 local matrices]) for every baked transform channel."""
    out = {}
    for ch in doc.iter(COLLADA + "channel"):
        target = ch.get("target") or ""
        if not target.endswith("/transform"):
            continue
        node_id = target[: -len("/transform")]
        samp_id = ch.get("source").lstrip("#")
        samp = next(s for s in doc.iter(COLLADA + "sampler") if s.get("id") == samp_id)
        inputs = {i.get("semantic"): i.get("source") for i in samp.findall(COLLADA + "input")}
        times = _floats(_source(doc, inputs["INPUT"]).find(COLLADA + "float_array"))
        mats = _floats(_source(doc, inputs["OUTPUT"]).find(COLLADA + "float_array")).reshape(-1, 4, 4)
        out[node_id] = (times, mats)
    return out


def sample(track, t):
    times, mats = track
    if t <= times[0]:
        return mats[0]
    if t >= times[-1]:
        return mats[-1]
    i = int(np.searchsorted(times, t)) - 1
    f = (t - times[i]) / max(times[i + 1] - times[i], 1e-9)
    return mats[i] * (1 - f) + mats[i + 1] * f


def world_matrices(nodes, locals_):
    cache = {}

    def w(name):
        if name in cache:
            return cache[name]
        _, parent, _ = nodes[name]
        m = locals_[name] if parent is None else w(parent) @ locals_[name]
        cache[name] = m
        return m

    return {n: w(n) for n in nodes}


def _geometry_corners(doc, mesh):
    """Triangles of a <mesh> as (position index, normal, uv) corners."""
    verts_el = mesh.find(COLLADA + "vertices")
    pos_src = verts_el.find(COLLADA + "input").get("source")
    positions = _floats(_source(doc, pos_src).find(COLLADA + "float_array")).reshape(-1, 3)
    corners = []
    for prim in list(mesh.findall(COLLADA + "triangles")) + list(mesh.findall(COLLADA + "polylist")):
        ins = prim.findall(COLLADA + "input")
        stride_p = max(int(i.get("offset")) for i in ins) + 1
        data = {}
        for i in ins:
            sem = i.get("semantic")
            if sem in data:
                continue
            arr = None
            if sem != "VERTEX":
                s = _source(doc, i.get("source"))
                acc = s.find(q("technique_common/accessor"))
                arr = _floats(s.find(COLLADA + "float_array")).reshape(-1, int(acc.get("stride")))
            data[sem] = (int(i.get("offset")), arr)
        p = [int(x) for x in prim.find(COLLADA + "p").text.split()]
        counts = [3] * (len(p) // (3 * stride_p))
        if prim.tag == COLLADA + "polylist":
            counts = [int(x) for x in prim.find(COLLADA + "vcount").text.split()]
        k = 0
        for c in counts:
            poly = []
            for _ in range(c):
                rec = p[k: k + stride_p]
                k += stride_p
                vi = rec[data["VERTEX"][0]]
                nrm = data["NORMAL"][1][rec[data["NORMAL"][0]]][:3] if "NORMAL" in data else np.array([0, 0, 1.0])
                uv = data["TEXCOORD"][1][rec[data["TEXCOORD"][0]]][:2] if "TEXCOORD" in data else np.zeros(2)
                poly.append((vi, tuple(np.round(nrm, 5)), tuple(np.round(uv, 6))))
            # The axis swap below mirrors the mesh, so the winding is reversed here.
            for j in range(1, c - 1):
                corners += [poly[0], poly[j + 1], poly[j]]
    return positions, corners


def read_mesh(path):
    """A .dae's scene, rest skeleton and triangles: skinned through its first
    skin controller, or static (every vertex weighted to no bone)."""
    doc = ET.parse(path).getroot()
    nodes = scene_nodes(doc)
    rest_world = world_matrices(nodes, {n: v[2] for n, v in nodes.items()})
    out = {"nodes": nodes, "rest_world": rest_world, "joint_names": [], "skin_m": [], "infl": None}
    ctrl = next(doc.iter(COLLADA + "controller"), None)
    if ctrl is None:
        # Static: the first geometry, placed by the node that instances it.
        geom = next(doc.iter(COLLADA + "geometry"))
        place = np.eye(4)
        for n in doc.iter(COLLADA + "node"):
            ig = n.find(COLLADA + "instance_geometry")
            if ig is not None and ig.get("url", "").lstrip("#") == geom.get("id"):
                key = next((k for k, v in nodes.items() if v[0] == n.get("id")), None)
                place = rest_world.get(key, np.eye(4))
                break
        out["static_xf"] = place
        out["positions"], out["corners"] = _geometry_corners(doc, geom.find(COLLADA + "mesh"))
        return out
    skin = ctrl.find(COLLADA + "skin")
    bsm_el = skin.find(COLLADA + "bind_shape_matrix")
    bsm = _mat(bsm_el.text) if bsm_el is not None else np.eye(4)
    jinputs = {i.get("semantic"): i.get("source") for i in skin.findall(q("joints/input"))}
    joint_names = _source(doc, jinputs["JOINT"]).find(COLLADA + "Name_array").text.split()
    ibms = _floats(_source(doc, jinputs["INV_BIND_MATRIX"]).find(COLLADA + "float_array")).reshape(-1, 4, 4)
    vw = skin.find(COLLADA + "vertex_weights")
    winputs = {i.get("semantic"): (i.get("source"), int(i.get("offset"))) for i in vw.findall(COLLADA + "input")}
    weights_src = _floats(_source(doc, winputs["WEIGHT"][0]).find(COLLADA + "float_array"))
    vcount = [int(x) for x in vw.find(COLLADA + "vcount").text.split()]
    v = [int(x) for x in vw.find(COLLADA + "v").text.split()]
    stride = len(winputs)
    infl = []
    k = 0
    for n in vcount:
        pairs = []
        for _ in range(n):
            pairs.append((v[k + winputs["JOINT"][1]], weights_src[v[k + winputs["WEIGHT"][1]]]))
            k += stride
        infl.append(pairs)
    for jn in joint_names:
        if jn not in nodes:
            raise SystemExit(f"{path}: joint {jn} not in scene")
    geom_id = skin.get("source").lstrip("#")
    geom = next(g for g in doc.iter(COLLADA + "geometry") if g.get("id") == geom_id)
    out["positions"], out["corners"] = _geometry_corners(doc, geom.find(COLLADA + "mesh"))
    out["joint_names"] = joint_names
    out["infl"] = infl
    # Rest skinning per joint: W_j * IBM_j * BSM, as 0 A.D.'s converter does.
    out["skin_m"] = [rest_world[jn] @ ibms[i] @ bsm for i, jn in enumerate(joint_names)]
    return out


class Part:
    """One primitive in RTS0 model space (after the axis swap)."""

    def __init__(self):
        self.pos, self.nrm, self.uv, self.joints, self.weights, self.idx = [], [], [], [], [], []
        self.tex_ref = None
        self.cutout = False


def skin_part(m, bone_index, place=None, bone=None):
    """Rest-pose a mesh into a Part. Its joints map onto the body's bones by
    name; place/bone attach a rigid (or foreign-rigged) prop to one bone."""
    part = Part()
    keys = {}
    names = m["joint_names"]
    for vi, nrm, uv in m["corners"]:
        key = (vi, nrm, uv)
        idx = keys.get(key)
        if idx is None:
            p4 = np.append(m["positions"][vi], 1.0)
            n3 = np.array(nrm)
            if m["infl"] is not None and bone is None:
                pairs = sorted(m["infl"][vi], key=lambda p: -p[1])[:4]
                tot = sum(w for _, w in pairs) or 1.0
                pairs = [(j, w / tot) for j, w in pairs]
                pos = sum(w * (m["skin_m"][j] @ p4) for j, w in pairs)
                n3 = sum(w * (m["skin_m"][j][:3, :3] @ n3) for j, w in pairs)
                jj = [bone_index[names[j]] for j, _ in pairs]
                ww = [w for _, w in pairs]
            else:
                xf = m["static_xf"] if m["infl"] is None else np.eye(4)
                if m["infl"] is not None:
                    pairs = sorted(m["infl"][vi], key=lambda p: -p[1])[:4]
                    tot = sum(w for _, w in pairs) or 1.0
                    xf = sum((w / tot) * m["skin_m"][j] for j, w in pairs)
                if place is not None:
                    xf = place @ xf
                pos = xf @ p4
                n3 = xf[:3, :3] @ n3
                jj = [bone if bone is not None else 0]
                ww = [1.0]
            pos = ZUP @ pos
            n3 = ZUP[:3, :3] @ n3
            n3 = n3 / (np.linalg.norm(n3) or 1.0)
            while len(jj) < 4:
                jj.append(0)
                ww.append(0.0)
            idx = len(part.pos)
            keys[key] = idx
            part.pos.append(pos[:3])
            part.nrm.append(tuple(float(x) for x in n3))
            part.uv.append((float(uv[0]), 1.0 - float(uv[1])))
            part.joints.append(tuple(jj))
            part.weights.append(tuple(ww))
        part.idx.append(idx)
    return part


def prop_point(nodes, attach):
    for cand in (f"prop_{attach}", f"prop-{attach}", attach):
        if cand in nodes:
            return cand
    return None


def convert(art, model_id, actor, size, out_dir, fit="height"):
    a = resolve_actor(art, actor)
    if not a["mesh"]:
        raise SystemExit(f"{actor}: no mesh")
    m = read_mesh(art.path("meshes", a["mesh"]))
    nodes, rest_world = m["nodes"], m["rest_world"]
    # Bones: every scene node that is a joint of the skin or below one (prop
    # points included), in scene order so parents come first.
    joint_set = set(m["joint_names"])
    order = [n for n in nodes if n in joint_set or any(p in joint_set for p in _ancestors(nodes, n))]
    bone_index = {n: i for i, n in enumerate(order)}
    if len(order) > 255:
        raise SystemExit(f"{actor}: {len(order)} bones")

    body = skin_part(m, bone_index)
    body.tex_ref = a["tex"].get("baseTex")
    body.cutout = "basic_trans" in a["material"] or "alphatest" in a["material"]
    parts = [body]
    sources = [f"actor:art/actors/{actor}", f"mesh:{a['mesh']}", f"skin:{body.tex_ref}"]
    # Props: a prop rigged to the same skeleton (horse manes and tails) is
    # skinned to the body's bones by name; any other mesh rides its prop point.
    for prop_actor, attach in a["props"]:
        if prop_actor.startswith("particle/") or not os.path.exists(art.path("actors", prop_actor)):
            continue
        pa = resolve_actor(art, prop_actor)
        mesh_path = art.path("meshes", pa["mesh"] or "")
        if not pa["mesh"] or not os.path.exists(mesh_path):
            print(f"  ! prop {prop_actor}: mesh {pa['mesh']} missing")
            continue
        pm = read_mesh(mesh_path)
        if pm["joint_names"] and all(j in bone_index for j in pm["joint_names"]):
            part = skin_part(pm, bone_index)
        else:
            pp = prop_point(nodes, attach)
            if pp is None or pp not in bone_index:
                print(f"  ! prop {prop_actor}: no prop point {attach}")
                continue
            part = skin_part(pm, bone_index, rest_world[pp], bone_index[pp])
        part.tex_ref = pa["tex"].get("baseTex")
        part.cutout = "trans" in pa["material"] and "player" not in pa["material"]
        parts.append(part)
        sources += [f"actor:art/actors/{prop_actor}", f"mesh:{pa['mesh']}", f"skin:{part.tex_ref}"]

    # Face -Z like every other RTS0 model: some rigs (the fin whale) hang off
    # an object node that turns them sideways, so snap the head bone to the front.
    yaw = np.eye(4)
    head = next((n for n in order if "head" in n.lower()), None)
    if head is not None:
        allp = np.array([p for part in parts for p in part.pos])
        mid = (allp.min(axis=0) + allp.max(axis=0)) / 2
        hp = (ZUP @ rest_world[head][:, 3])[:3] - mid
        ang = np.arctan2(hp[0], -hp[2])  # angle of the head from -Z, about +Y
        quarter = round(ang / (np.pi / 2)) * (np.pi / 2)
        if abs(quarter) > 1e-6:
            c, s_ = np.cos(quarter), np.sin(quarter)
            yaw = np.array([[c, 0, s_, 0], [0, 1, 0, 0], [-s_, 0, c, 0], [0, 0, 0, 1]])
            print(f"  turned {np.degrees(quarter):.0f} degrees to face -Z")
            for part in parts:
                part.pos = [yaw[:3, :3] @ p for p in part.pos]
                part.nrm = [tuple(float(x) for x in yaw[:3, :3] @ np.array(n)) for n in part.nrm]
    axis = yaw @ ZUP
    axis_inv = np.linalg.inv(axis)

    pos = np.array([p for part in parts for p in part.pos])
    # Ground the model at y = 0, centre it in x/z, scale it to its height.
    lo, hi = pos.min(axis=0), pos.max(axis=0)
    extent = hi[2] - lo[2] if fit == "length" else hi[1] - lo[1]
    scale = size / max(extent, 1e-6)
    centre = np.array([(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2])

    def bone_pose(world):
        w = axis @ world @ axis_inv
        lin = w[:3, :3]
        s = float(np.cbrt(abs(np.linalg.det(lin)))) or 1.0
        u, _, vt = np.linalg.svd(lin)
        r = u @ vt
        if np.linalg.det(r) < 0:
            u[:, -1] *= -1
            r = u @ vt
        return w[:3, 3], r, s

    rest = [bone_pose(rest_world[n]) for n in order]

    def trs_mat(t, r, s):
        out = np.eye(4)
        out[:3, :3] = r * s
        out[:3, 3] = t
        return out

    glb = GlbBuilder()
    for part in parts:
        img = load_texture(art, part.tex_ref)
        if not part.cutout:
            img = img.convert("RGB")
        img_idx = glb.add_image(_img_png(img))
        mat_idx = glb.add_material(img_idx, "MASK" if part.cutout else "OPAQUE", part.cutout)
        glb._add_primitive([tuple(map(float, p)) for p in part.pos], part.nrm, part.uv, part.idx, mat_idx,
                           part.joints if order else None, part.weights if order else None)

    n_b = len(order)
    n_p = len(parts)
    root = {"name": model_id, "translation": list(map(float, -centre * scale)), "scale": [float(scale)] * 3,
            "children": list(range(1, n_b + n_p + 1))}
    nodes_out = [root]
    for i, n in enumerate(order):
        t, r, s = rest[i]
        nd = {"name": n, "translation": list(map(float, t)), "rotation": list(mat3_to_quat(r.tolist()))}
        if abs(s - 1.0) > 1e-4:
            nd["scale"] = [s] * 3
        nodes_out.append(nd)
    for k in range(n_p):
        nd = {"name": f"{model_id}_part{k}", "mesh": k}
        if order:
            nd["skin"] = 0
        nodes_out.append(nd)

    animations = []
    used = {}
    for clip, names in CLIPS.items():
        f = pick_clip(a["anims"], names) if order else None
        if not f:
            continue
        anim = read_clip(art, f, nodes, order, bone_pose)
        if anim is None:
            print(f"  ! {clip}: {f} has no channels for this rig")
            continue
        times, tracks = anim
        animations.append({"name": clip, "times": times,
                           "tracks": {1 + i: tr for i, tr in enumerate(tracks)}})
        used[clip] = f
    # 0 A.D. plays one of an actor's same-named clips at random; world_view_node
    # gives some animals the next most frequent idle and death (Idle2, Death2).
    for clip, name in (("Idle2", "idle"), ("Death2", "death")):
        first = used.get(clip[:-1])
        rest_c = [c for c in sorted(a["anims"].get(name) or [], key=lambda c: -c[1]) if c[0] != first]
        if not order or not first or not rest_c:
            continue
        anim = read_clip(art, rest_c[0][0], nodes, order, bone_pose)
        if anim is None:
            continue
        times, tracks = anim
        animations.append({"name": clip, "times": times,
                           "tracks": {1 + i: tr for i, tr in enumerate(tracks)}})
        used[clip] = rest_c[0][0]
    skins = None
    if order:
        ibm = [np.linalg.inv(trs_mat(*p)).tolist() for p in rest]
        skins = [{"joints": list(range(1, n_b + 1)), "inverseBindMatrices": ibm, "skeleton": 1}]
    blob = glb.build(model_id, nodes=nodes_out, animations=animations or None, skins=skins)
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, model_id + ".glb")
    with open(out, "wb") as fh:
        fh.write(blob)
    dim = (hi - lo) * scale
    print(f"[{model_id}] {actor}: {len(pos)} verts, {n_p} parts, {n_b} bones, clips {list(used)}, "
          f"{dim[0]:.2f} x {dim[1]:.2f} x {dim[2]:.2f}, {len(blob) // 1024} KB")
    sources += [f"anim:{f}" for f in used.values()]
    return {
        "file": f"models/units/{model_id}.glb", "parts": n_p, "bones": n_b, "animated": bool(animations),
        "skeleton": model_id if order else None, "animations": [x["name"] for x in animations],
        "size_meters": [round(float(x), 3) for x in dim], "origin": "ground-center",
        "sources": sorted(set(sources)), "upstream_material": a["material"],
        "generator": "tools/extract_0ad_fauna_models.py",
    }


def _ancestors(nodes, n):
    out = []
    p = nodes[n][1]
    while p is not None:
        out.append(p)
        p = nodes[p][1]
    return out


def read_clip(art, rel, mesh_nodes, order, bone_pose):
    path = art.path("animation", rel)
    if not os.path.exists(path):
        print(f"  ! missing clip {rel}")
        return None
    doc = ET.parse(path).getroot()
    anodes = scene_nodes(doc)
    tracks_by_id = node_animations(doc)
    by_name = {}
    for name, (nid, _, _) in anodes.items():
        if nid in tracks_by_id:
            by_name[name] = tracks_by_id[nid]
    if not any(n in by_name for n in order):
        return None
    t_end = max(tr[0][-1] for tr in by_name.values())
    nkeys = max(len(tr[0]) for tr in by_name.values())
    times = [t_end * i / max(nkeys - 1, 1) for i in range(nkeys)]
    out = [{"translation": [], "rotation": []} for _ in order]
    for t in times:
        locals_ = {}
        for name, (_, _, rest_local) in mesh_nodes.items():
            if name in by_name:
                locals_[name] = sample(by_name[name], t)
            elif name in anodes and name in order:
                locals_[name] = anodes[name][2]
            else:
                locals_[name] = rest_local
        world = world_matrices(mesh_nodes, locals_)
        for i, n in enumerate(order):
            tr, r, _ = bone_pose(world[n])
            out[i]["translation"].append(tuple(map(float, tr)))
            out[i]["rotation"].append(tuple(mat3_to_quat(r.tolist())))
    return [float(x) for x in times], out


def load_texture(art, ref):
    base = art.path("textures", "skins", ref)
    stem = os.path.splitext(base)[0]
    for cand in (base, stem + ".png", stem + ".dds"):
        if os.path.exists(cand):
            if cand.endswith(".dds"):
                with open(cand, "rb") as fh:
                    return decode_dds(fh.read())
            return Image.open(cand).convert("RGBA")
    raise SystemExit(f"missing texture {ref}")


def append_feeding(art, model_id, actor):
    """Append 0.28's "feeding" clip to an existing fauna GLB as "Feeding".

    For the PSA-built animals tools/add_0ad_feeding_clips.py drops the release
    .psa onto the GLB; horse_breed (Blender-named rig, no standard skeleton fit)
    and the peacock (re-exported rest pose) need their source .dae sampled onto
    their own rig instead, which is what this does (read_clip matches by node
    name, so the rigs must come from the same source tree as the GLB).
    """
    import json as _json
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from add_0ad_clips import add_accessor, glb_bones, read_glb, write_glb  # noqa: E402
    with open(MANIFEST) as fh:
        man = _json.load(fh)
    rec = man["models"][model_id]
    path = os.path.join(REPO, "assets", rec["file"])
    gltf, bin_ = read_glb(path)
    if any(a.get("name") == "Feeding" for a in gltf.get("animations", [])):
        print(f"[{model_id}]: has Feeding")
        return False
    a = resolve_actor(art, actor)
    if not a["mesh"]:
        raise SystemExit(f"{actor}: no mesh")
    feed = pick_clip(a["anims"], CLIPS["Feeding"])
    if not feed:
        raise SystemExit(f"{actor}: no feeding animation")
    m = read_mesh(art.path("meshes", a["mesh"]))
    nodes = m["nodes"]
    joint_set = set(m["joint_names"])
    order = [n for n in nodes if n in joint_set or any(p in joint_set for p in _ancestors(nodes, n))]
    joints, names, _ = glb_bones(gltf)
    if list(names) != order:
        raise SystemExit(f"{model_id}: GLB bones differ from the mesh rig ({len(names)} vs {len(order)})")
    yaw = np.eye(4)
    head = next((n for n in order if "head" in n.lower()), None)
    if head is not None:
        rest_world = m["rest_world"]
        allp = np.array([p for part in [skin_part(m, {n: i for i, n in enumerate(order)})] for p in part.pos])
        mid = (allp.min(axis=0) + allp.max(axis=0)) / 2
        hp = (ZUP @ rest_world[head][:, 3])[:3] - mid
        ang = np.arctan2(hp[0], -hp[2])
        quarter = round(ang / (np.pi / 2)) * (np.pi / 2)
        if abs(quarter) > 1e-6:
            c, s_ = np.cos(quarter), np.sin(quarter)
            yaw = np.array([[c, 0, s_, 0], [0, 1, 0, 0], [-s_, 0, c, 0], [0, 0, 0, 1]])
    axis = yaw @ ZUP
    axis_inv = np.linalg.inv(axis)

    def bone_pose(world):
        w = axis @ world @ axis_inv
        lin = w[:3, :3]
        u, _, vt = np.linalg.svd(lin)
        r = u @ vt
        if np.linalg.det(r) < 0:
            u[:, -1] *= -1
            r = u @ vt
        return w[:3, 3], r, 1.0

    anim = read_clip(art, feed, nodes, order, bone_pose)
    if anim is None:
        raise SystemExit(f"{feed}: no channels for this rig")
    times, tracks = anim
    anims = gltf.setdefault("animations", [])
    t_acc = add_accessor(gltf, bin_, np.array(times, dtype=np.float32).reshape(-1, 1), "SCALAR")
    samplers, channels = [], []
    for i, node in enumerate(joints):
        for path_name, arr, typ in (("translation", np.array(tracks[i]["translation"]), "VEC3"),
                                    ("rotation", np.array(tracks[i]["rotation"]), "VEC4")):
            samplers.append({"input": t_acc, "output": add_accessor(gltf, bin_, arr, typ), "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": path_name}})
    anims.append({"name": "Feeding", "samplers": samplers, "channels": channels})
    write_glb(path, gltf, bin_)
    if "Feeding" not in rec.setdefault("animations", []):
        rec["animations"].append("Feeding")
    src = f"anim:{feed}"
    if src not in rec.setdefault("sources", []):
        rec["sources"] = sorted(rec["sources"] + [src])
    with open(MANIFEST, "w") as fh:
        _json.dump(man, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print(f"[{model_id}] {actor}: +Feeding from {feed} ({len(times)} frames)")
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zeroad", required=True, help="0 A.D. git checkout")
    ap.add_argument("--only", nargs="+")
    ap.add_argument("--out", default=OUT_DIR)
    ap.add_argument("--no-manifest", action="store_true")
    ap.add_argument("--feeding-only", action="store_true",
                    help="append 0.28's feeding clip to existing GLBs instead of converting")
    args = ap.parse_args()
    art = Art(args.zeroad)
    if args.feeding_only:
        for model_id, spec in MODELS.items():
            if args.only and model_id not in args.only:
                continue
            append_feeding(art, model_id, spec[0])
        return
    entries = {}
    for model_id, spec in MODELS.items():
        if args.only and model_id not in args.only:
            continue
        entries[model_id] = convert(art, model_id, *spec[:2], args.out, *spec[2:])
    if not args.no_manifest and entries:
        with open(MANIFEST) as fh:
            man = json.load(fh)
        man["models"].update(entries)
        man["models"] = dict(sorted(man["models"].items()))
        with open(MANIFEST, "w") as fh:
            json.dump(man, fh, indent=1, sort_keys=True)


if __name__ == "__main__":
    main()
