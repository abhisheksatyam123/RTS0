#!/usr/bin/env python3
"""The 0 A.D. meshes this reads are CC BY-SA 3.0, Wildfire Games (see
docs/0AD_ATTRIBUTION.md).

Skins the horse manes and tails of existing unit GLBs to the horse.

0 A.D. hangs props/horse/hair_*.dae on the horse's "hair" prop point. Those
meshes are skinned to the horse skeleton, and the prop actors play the horse's
clips, so manes and tails sway, rear and fall with the horse. Older
extract_0ad_assets.py runs made them rigid on the "hair" bone, so they hang in
the air while the horse dies or rears. (extract_0ad_assets.py now skins such
props itself, see _shares_skeleton.)

For every unit GLB listing a prop mesh in its manifest sources, this finds the
rigid primitives whose vertices are that mesh's, at a bone resting on the
origin, and rebinds them to the mount's skin with the mesh's own weights.

Usage: python3 tools/skin_0ad_mount_props.py --0ad <0 A.D. checkout> [--only id ...]
"""

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from add_0ad_clips import MANIFEST, REPO, read_accessor, read_glb, write_glb  # noqa: E402
from extract_0ad_assets import parse_pmd  # noqa: E402
from oad_collada_archive import ColladaArchive  # noqa: E402

PROP_MESH = "mesh:props/horse/hair_"


def read_mat4(gltf, bin_, idx):
    acc = gltf["accessors"][idx]
    bv = gltf["bufferViews"][acc["bufferView"]]
    off = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    m = np.frombuffer(bytes(bin_[off: off + 64 * acc["count"]]), dtype="<f4").reshape(acc["count"], 4, 4)
    return np.transpose(m, (0, 2, 1))  # glTF is column-major


def read_joints(gltf, bin_, idx):
    acc = gltf["accessors"][idx]
    bv = gltf["bufferViews"][acc["bufferView"]]
    off = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    dt = {5121: "<u1", 5123: "<u2"}[acc["componentType"]]
    size = np.dtype(dt).itemsize * 4
    stride = bv.get("byteStride", size)
    raw = np.frombuffer(bytes(bin_[off: off + stride * acc["count"]]), dtype=np.uint8).reshape(acc["count"], stride)
    return np.ascontiguousarray(raw[:, :size]).view(dt).reshape(acc["count"], 4)


def add_buffer(gltf, bin_, data):
    while len(bin_) % 4:
        bin_ += b"\x00"
    gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(bin_), "byteLength": len(data)})
    bin_ += data
    return len(gltf["bufferViews"]) - 1


def skin_prim(gltf, bin_, prim, blends):
    joints = np.array([[b if w > 0 and b != 0xFF else 0 for b, w in zip(bl["bones"], bl["weights"])]
                       for bl in blends], dtype="<u2")
    weights = np.array([[w if b != 0xFF else 0.0 for b, w in zip(bl["bones"], bl["weights"])]
                        for bl in blends], dtype="<f4")
    weights /= np.maximum(weights.sum(axis=1, keepdims=True), 1e-6)
    n = len(blends)
    gltf["accessors"].append({"bufferView": add_buffer(gltf, bin_, joints.tobytes()),
                              "componentType": 5123, "count": n, "type": "VEC4"})
    prim["attributes"]["JOINTS_0"] = len(gltf["accessors"]) - 1
    gltf["accessors"].append({"bufferView": add_buffer(gltf, bin_, weights.tobytes()),
                              "componentType": 5126, "count": n, "type": "VEC4"})
    prim["attributes"]["WEIGHTS_0"] = len(gltf["accessors"]) - 1


def fix(path, meshes, archive):
    gltf, bin_ = read_glb(path)
    skins = gltf.get("skins") or []
    if not skins:
        return 0
    mount = skins[0]
    rest = np.linalg.inv(read_mat4(gltf, bin_, mount["inverseBindMatrices"]))
    pmds = {}
    for rel in meshes:
        md = parse_pmd(archive.read(f"art/meshes/{rel}.cached.pmd"))
        bones = md.get("bones") or []
        if len(bones) != len(mount["joints"]) or len(md.get("blends") or []) != len(md["verts"]):
            continue
        # Same skeleton in the same rest pose as the mount's skin.
        if any(np.abs(rest[i][:3, 3] - np.array(t)).max() > 1e-3 for i, (t, _q) in enumerate(bones)):
            continue
        pmds[rel] = md
    done = 0
    for node in gltf["nodes"]:
        if "mesh" not in node or node.get("skin") in (None, 0):
            continue
        skin = skins[node["skin"]]
        prims = gltf["meshes"][node["mesh"]]["primitives"]
        if len(prims) != 1:
            continue
        # A rigid prop: every vertex on one joint, the prop point's bone,
        # which must rest on the origin.
        used = {int(j) for j in read_joints(gltf, bin_, prims[0]["attributes"]["JOINTS_0"])[:, 0]}
        if len(used) != 1:
            continue
        joint = skin["joints"][used.pop()]
        bone = mount["joints"].index(joint) if joint in mount["joints"] else -1
        if bone < 0 or np.abs(rest[bone] - np.eye(4)).max() > 1e-4:
            continue
        for prim in prims:
            pos = read_accessor(gltf, bin_, prim["attributes"]["POSITION"])
            for md in pmds.values():
                v = np.array([p for p, _n in md["verts"]])
                if len(v) == len(pos) and np.abs(v - pos).max() < 1e-4:
                    skin_prim(gltf, bin_, prim, md["blends"])
                    node["skin"] = 0
                    done += 1
                    break
    if done:
        write_glb(path, gltf, bin_)
    return done


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zeroad", required=True)
    ap.add_argument("--only", nargs="+")
    args = ap.parse_args()
    archive = ColladaArchive(args.zeroad)
    with open(MANIFEST) as fh:
        models = json.load(fh)["models"]
    total = 0
    for mid, e in sorted(models.items()):
        if args.only and mid not in args.only:
            continue
        meshes = sorted(s[len("mesh:"):] for s in e.get("sources", []) if s.startswith(PROP_MESH))
        if not meshes or not e["file"].startswith("models/units/"):
            continue
        path = os.path.join(REPO, "assets", e["file"])
        if not os.path.exists(path):
            continue
        n = fix(path, meshes, archive)
        total += bool(n)
        print(f"{mid}: {n} parts skinned")
    print(f"{total} GLBs changed")


if __name__ == "__main__":
    main()
