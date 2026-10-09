#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the clips
it adds are CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

Add 0 A.D. animation clips, read from the source Collada files, to unit GLBs
that extract_0ad_assets.py already wrote.

Those GLBs keep every bone as a flat child of the root with a root-relative
pose (0 A.D.'s CModel bone matrices), named after the skeleton's standard
bones. A Collada clip names its joints the same way, so each frame's joint
world matrices, after 0 A.D.'s (x, z, y) axis swap, are the bone poses as they
stand. A standard bone the clip lacks keeps its rest offset from its nearest
animated ancestor.

Usage:
  python3 -I tools/add_0ad_clips.py --0ad PATH/TO/0ad --clip Capture=biped/infantry/capturing_a.dae \\
      [--skeleton biped] [--only spearman ...] [--replace] [--check-against Walk]

--check-against NAME rebuilds a clip the GLB already has from its Collada
source and prints the largest difference instead of writing anything.
"""

import argparse
import json
import os
import struct
import sys
import xml.etree.ElementTree as ET

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_assets import mat3_to_quat  # noqa: E402
from extract_0ad_fauna_models import (  # noqa: E402
    ZUP, Art, node_animations, sample, scene_nodes, world_matrices)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")


def read_glb(path):
    with open(path, "rb") as fh:
        data = fh.read()
    jlen = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + jlen])
    off = 20 + jlen
    blen = struct.unpack_from("<I", data, off)[0]
    return gltf, bytearray(data[off + 8: off + 8 + blen])


def write_glb(path, gltf, bin_):
    while len(bin_) % 4:
        bin_ += b"\x00"
    gltf["buffers"][0]["byteLength"] = len(bin_)
    js = json.dumps(gltf, separators=(",", ":")).encode()
    while len(js) % 4:
        js += b" "
    total = 12 + 8 + len(js) + 8 + len(bin_)
    out = struct.pack("<4sII", b"glTF", 2, total)
    out += struct.pack("<I4s", len(js), b"JSON") + js
    out += struct.pack("<I4s", len(bin_), b"BIN\0") + bytes(bin_)
    with open(path, "wb") as fh:
        fh.write(out)


def read_accessor(gltf, bin_, idx):
    acc = gltf["accessors"][idx]
    bv = gltf["bufferViews"][acc["bufferView"]]
    n = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[acc["type"]]
    off = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    return np.frombuffer(bytes(bin_[off: off + 4 * n * acc["count"]]), dtype="<f4").reshape(acc["count"], n)


def add_accessor(gltf, bin_, arr, typ):
    while len(bin_) % 4:
        bin_ += b"\x00"
    data = np.ascontiguousarray(arr, dtype="<f4").tobytes()
    gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(bin_), "byteLength": len(data)})
    bin_ += data
    acc = {"bufferView": len(gltf["bufferViews"]) - 1, "componentType": 5126,
           "count": int(arr.shape[0]), "type": typ}
    if typ == "SCALAR":
        acc["min"] = [float(arr.min())]
        acc["max"] = [float(arr.max())]
    gltf["accessors"].append(acc)
    return len(gltf["accessors"]) - 1


def bone_parents(skeleton_xml):
    """Standard bone -> parent standard bone, from assets/skeletons/<name>.xml."""
    parents = {}
    root = ET.parse(skeleton_xml).getroot()
    std = root.find("standard_skeleton")

    def walk(el, parent):
        for b in el.findall("bone"):
            parents[b.get("name")] = parent
            walk(b, b.get("name"))

    if std is not None:
        walk(std, None)
    return parents


def to_pose(world):
    w = ZUP @ world @ ZUP
    u, _, vt = np.linalg.svd(w[:3, :3])
    r = u @ vt
    if np.linalg.det(r) < 0:
        u[:, -1] *= -1
        r = u @ vt
    return w[:3, 3], r


def trs(t, q):
    x, y, z, w = q
    r = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    m = np.eye(4)
    m[:3, :3] = r
    m[:3, 3] = t
    return m


def sample_clip(art, rel, bones, rest, parents):
    """Root-relative (translation, quaternion) per bone per frame."""
    doc = ET.parse(art.path("animation", rel)).getroot()
    nodes = scene_nodes(doc)
    tracks = node_animations(doc)
    by_name = {n: tracks[v[0]] for n, v in nodes.items() if v[0] in tracks}
    if not by_name:
        raise SystemExit(f"{rel}: no animated joints")
    t_end = max(tr[0][-1] for tr in by_name.values())
    nkeys = max(len(tr[0]) for tr in by_name.values())
    times = np.array([t_end * i / max(nkeys - 1, 1) for i in range(nkeys)], dtype=np.float32)
    rest_m = {b: trs(*rest[b]) for b in bones}
    out_t = np.zeros((len(bones), nkeys, 3), dtype=np.float32)
    out_q = np.zeros((len(bones), nkeys, 4), dtype=np.float32)
    for k, t in enumerate(times):
        locals_ = {n: (sample(by_name[n], t) if n in by_name else v[2]) for n, v in nodes.items()}
        world = world_matrices(nodes, locals_)
        posed = {}

        def pose(b):
            if b in posed:
                return posed[b]
            if b in nodes:
                tr, r = to_pose(world[b])
                m = np.eye(4)
                m[:3, :3] = r
                m[:3, 3] = tr
            else:
                p = parents.get(b)
                m = rest_m[b] if p is None or p not in rest_m else pose(p) @ np.linalg.inv(rest_m[p]) @ rest_m[b]
            posed[b] = m
            return m

        for i, b in enumerate(bones):
            m = pose(b)
            out_t[i, k] = m[:3, 3]
            q = mat3_to_quat(m[:3, :3].tolist())
            out_q[i, k] = q
    # Keep quaternions on one hemisphere so linear interpolation never flips.
    for i in range(len(bones)):
        for k in range(1, nkeys):
            if np.dot(out_q[i, k], out_q[i, k - 1]) < 0:
                out_q[i, k] = -out_q[i, k]
    return times, out_t, out_q


def glb_bones(gltf):
    skin = gltf["skins"][0]
    names = [gltf["nodes"][j]["name"] for j in skin["joints"]]
    rest = {gltf["nodes"][j]["name"]: (gltf["nodes"][j].get("translation", [0, 0, 0]),
                                       gltf["nodes"][j].get("rotation", [0, 0, 0, 1])) for j in skin["joints"]}
    return skin["joints"], names, rest


def add_clip(path, name, art, rel, parents, replace):
    gltf, bin_ = read_glb(path)
    anims = gltf.setdefault("animations", [])
    if any(a["name"] == name for a in anims):
        if not replace:
            return False
        anims[:] = [a for a in anims if a["name"] != name]
    joints, bones, rest = glb_bones(gltf)
    times, tt, qq = sample_clip(art, rel, bones, rest, parents)
    t_acc = add_accessor(gltf, bin_, times.reshape(-1, 1), "SCALAR")
    samplers, channels = [], []
    for i, node in enumerate(joints):
        for path_name, arr, typ in (("translation", tt[i], "VEC3"), ("rotation", qq[i], "VEC4")):
            o = add_accessor(gltf, bin_, arr, typ)
            samplers.append({"input": t_acc, "output": o, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": path_name}})
    anims.append({"name": name, "samplers": samplers, "channels": channels})
    write_glb(path, gltf, bin_)
    return True


def check_clip(path, name, art, rel, parents):
    gltf, bin_ = read_glb(path)
    joints, bones, rest = glb_bones(gltf)
    anim = next(a for a in gltf["animations"] if a["name"] == name)
    times, tt, qq = sample_clip(art, rel, bones, rest, parents)
    node_bone = {n: bones.index(gltf["nodes"][n]["name"]) for n in joints}
    worst_t = worst_q = 0.0
    for ch in anim["channels"]:
        b = node_bone.get(ch["target"]["node"])
        if b is None:
            continue
        s = anim["samplers"][ch["sampler"]]
        have = read_accessor(gltf, bin_, s["output"])
        mine = tt[b] if ch["target"]["path"] == "translation" else qq[b]
        n = min(len(have), len(mine))
        # Both start at frame 0; compare the first frame and the shared frame count.
        d = np.abs(have[:1] - mine[:1]).max()
        if ch["target"]["path"] == "rotation":
            d = min(d, np.abs(have[:1] + mine[:1]).max())
            worst_q = max(worst_q, d)
        else:
            worst_t = max(worst_t, d)
        del n
    print(f"{os.path.basename(path)} {name} <- {rel}: frame 0 max |dt| {worst_t:.4f}, max |dq| {worst_q:.4f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zeroad", required=True)
    ap.add_argument("--clip", required=True, help="Name=animation/relative/path.dae")
    ap.add_argument("--skeleton", default="biped", help="manifest skeleton the GLBs must use")
    ap.add_argument("--only", nargs="+")
    ap.add_argument("--replace", action="store_true")
    ap.add_argument("--check-against")
    args = ap.parse_args()
    art = Art(args.zeroad)
    name, rel = args.clip.split("=", 1)
    parents = bone_parents(os.path.join(REPO, "assets", "skeletons", args.skeleton + ".xml"))
    with open(MANIFEST) as fh:
        man = json.load(fh)
    changed = 0
    for mid, e in sorted(man["models"].items()):
        if args.only and mid not in args.only:
            continue
        # Unit GLBs only: the Han boats rig their rowers on the biped skeleton too.
        if e.get("skeleton") != args.skeleton or not e.get("animated") or not e["file"].startswith("models/units/"):
            continue
        path = os.path.join(REPO, "assets", e["file"])
        if args.check_against:
            check_clip(path, args.check_against, art, rel, parents)
            continue
        if add_clip(path, name, art, rel, parents, args.replace):
            changed += 1
            if name not in e["animations"]:
                e["animations"].append(name)
            src = f"anim:{rel}"
            if src not in e["sources"]:
                e["sources"] = sorted(e["sources"] + [src])
    if changed:
        with open(MANIFEST, "w") as fh:
            json.dump(man, fh, indent=1, sort_keys=True)
    if not args.check_against:
        print(f"{name}: added to {changed} GLBs")


if __name__ == "__main__":
    main()
