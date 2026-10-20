#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the meshes
it converts are CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

Write the other bodies of animals whose 0.28 actor picks its mesh by variant
(the cow's horned and unhorned cattle, the female and male goat, the normal and
fat rabbit) as <id>_v2.glb next to <id>.glb: the same skeleton, clips, skin
image and scale, with the other .pmd's vertices, skin weights and triangles.

extract_0ad_assets.py writes a skinned animal's vertices, UVs (V flipped) and
skin weights straight from its .pmd, with the .pmd's bone indices as joints, so
another mesh on the same skeleton drops in. Each GLB is checked first: its mesh
must come back exactly from the .pmd it was made from, or it is left alone.

Usage: python3 -I tools/add_0ad_mesh_looks.py [--zip public.zip] [--check]
"""

import argparse
import copy
import json
import os
import sys
import zipfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from add_0ad_clips import read_glb, write_glb  # noqa: E402
from extract_0ad_assets import DEFAULT_ZIP, parse_pmd  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
TOL = 1e-5

# GLB -> the actor's other mesh variants (equal frequencies in 0.28: cow.xml,
# goat.xml, rabbit1.xml). The bull's and sanga cattle's GLBs come from Collada
# (extract_0ad_fauna_models.py), whose vertices don't match the .pmd's.
LOOKS = {
    "cow": ["skeletal/cattle_female_b.dae"],
    "goat": ["skeletal/goat_male.dae"],
    "rabbit": ["skeletal/rabbit_02.dae"],
}

DTYPES = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read(gltf, bin_, idx):
    a = gltf["accessors"][idx]
    bv = gltf["bufferViews"][a["bufferView"]]
    n, dt = WIDTH[a["type"]], DTYPES[a["componentType"]]
    size = n * np.dtype(dt).itemsize
    stride = bv.get("byteStride", size)
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    raw = np.frombuffer(bytes(bin_[off: off + stride * a["count"]]), dtype=np.uint8).reshape(a["count"], stride)
    return np.frombuffer(raw[:, :size].tobytes(), dtype=dt).reshape(a["count"], n)


def pmd_arrays(d):
    pos = np.array([v[0] for v in d["verts"]], dtype=np.float32)
    nrm = np.array([v[1] for v in d["verts"]], dtype=np.float32)
    uv0 = np.array([(u, 1.0 - v) for u, v in d["uvs"]], dtype=np.float32)
    uv1 = None
    if all(u is not None for u in d["uv1s"]):
        uv1 = np.array([(u, 1.0 - v) for u, v in d["uv1s"]], dtype=np.float32)
    joints = np.array([[b if w > 0 and b != 255 else 0 for b, w in zip(bl["bones"], bl["weights"])]
                       for bl in d["blends"]], dtype=np.uint8)
    weights = np.array([[w if b != 255 else 0.0 for b, w in zip(bl["bones"], bl["weights"])]
                        for bl in d["blends"]], dtype=np.float32)
    idx = np.array([(a, c, b) for a, b, c in d["faces"]], dtype=np.uint16).reshape(-1, 1)
    return {"POSITION": pos, "NORMAL": nrm, "TEXCOORD_0": uv0, "TEXCOORD_1": uv1,
            "JOINTS_0": joints, "WEIGHTS_0": weights}, idx


def matches(gltf, bin_, prim, arrays, idx):
    for key, want in arrays.items():
        if key not in prim["attributes"]:
            if want is None:
                continue
            return f"no {key}"
        have = read(gltf, bin_, prim["attributes"][key])
        if want is None or have.shape != want.shape:
            return f"{key} differs in shape"
        live = arrays["WEIGHTS_0"] > 0 if key == "JOINTS_0" else np.ones(have.shape, bool)
        if np.abs(have.astype(np.float64) - want)[live].max(initial=0.0) > TOL:
            return f"{key} differs"
    if not np.array_equal(read(gltf, bin_, prim["indices"]).reshape(-1), idx.reshape(-1).astype(np.int64)):
        return "triangles differ"
    return None


def rebuild(gltf, bin_, prim, arrays, idx):
    """A copy of the GLB with prim's accessors replaced, unused buffer views dropped."""
    out = copy.deepcopy(gltf)
    p = out["meshes"][0]["primitives"][0]
    for key, arr in arrays.items():
        if arr is None:
            p["attributes"].pop(key, None)  # the AO unwrap, when only the base has one
    news = [(k, v) for k, v in arrays.items() if v is not None] + [("indices", idx)]
    for key, arr in news:
        old = out["accessors"][p["attributes"][key] if key != "indices" else p["indices"]]
        acc = {"bufferView": None, "componentType": old["componentType"], "count": int(arr.shape[0]),
               "type": old["type"], "_data": np.ascontiguousarray(arr, dtype=DTYPES[old["componentType"]]).tobytes(),
               "_target": out["bufferViews"][old["bufferView"]].get("target")}
        if key == "POSITION":
            acc["min"] = [float(x) for x in arr.min(0)]
            acc["max"] = [float(x) for x in arr.max(0)]
        out["accessors"].append(acc)
        if key == "indices":
            p["indices"] = len(out["accessors"]) - 1
        else:
            p["attributes"][key] = len(out["accessors"]) - 1
    # Every accessor still referenced, by meshes, skins and animations.
    used = set()
    for m in out["meshes"]:
        for q in m["primitives"]:
            used.update(q["attributes"].values())
            if "indices" in q:
                used.add(q["indices"])
    for s in out.get("skins", []):
        if "inverseBindMatrices" in s:
            used.add(s["inverseBindMatrices"])
    for a in out.get("animations", []):
        for s in a["samplers"]:
            used.update((s["input"], s["output"]))
    views_used = {out["accessors"][i]["bufferView"] for i in used if out["accessors"][i]["bufferView"] is not None}
    views_used |= {im["bufferView"] for im in out.get("images", []) if "bufferView" in im}
    new_bin, new_views, remap = bytearray(), [], {}
    for vi, bv in enumerate(out["bufferViews"]):
        if vi not in views_used:
            continue
        while len(new_bin) % 4:
            new_bin += b"\x00"
        start = bv.get("byteOffset", 0)
        nbv = dict(bv, byteOffset=len(new_bin))
        new_bin += bin_[start: start + bv["byteLength"]]
        remap[vi] = len(new_views)
        new_views.append(nbv)
    acc_remap, new_accs = {}, []
    for ai, a in enumerate(out["accessors"]):
        if ai not in used:
            continue
        a = dict(a)
        if "_data" in a:
            while len(new_bin) % 4:
                new_bin += b"\x00"
            view = {"buffer": 0, "byteOffset": len(new_bin), "byteLength": len(a["_data"])}
            if a["_target"]:
                view["target"] = a["_target"]
            new_bin += a.pop("_data")
            a.pop("_target")
            new_views.append(view)
            a["bufferView"] = len(new_views) - 1
        else:
            a["bufferView"] = remap[a["bufferView"]]
        acc_remap[ai] = len(new_accs)
        new_accs.append(a)
    out["accessors"], out["bufferViews"] = new_accs, new_views
    for m in out["meshes"]:
        for q in m["primitives"]:
            q["attributes"] = {k: acc_remap[v] for k, v in q["attributes"].items()}
            if "indices" in q:
                q["indices"] = acc_remap[q["indices"]]
    for s in out.get("skins", []):
        if "inverseBindMatrices" in s:
            s["inverseBindMatrices"] = acc_remap[s["inverseBindMatrices"]]
    for a in out.get("animations", []):
        for s in a["samplers"]:
            s["input"], s["output"] = acc_remap[s["input"]], acc_remap[s["output"]]
    for im in out.get("images", []):
        if "bufferView" in im:
            im["bufferView"] = remap[im["bufferView"]]
    return out, new_bin


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=DEFAULT_ZIP)
    ap.add_argument("--check", action="store_true", help="fail if a listed look is missing")
    args = ap.parse_args()
    with open(MANIFEST) as fh:
        manifest = json.load(fh)
    models = manifest["models"]
    if args.check:
        missing = [f"{g}_v{i + 2}" for g, alts in LOOKS.items() for i in range(len(alts)) if f"{g}_v{i + 2}" not in models]
        if missing:
            sys.exit(f"missing looks: {', '.join(missing)}")
        return
    zf = zipfile.ZipFile(args.zip)
    for gid, alts in LOOKS.items():
        rec = models[gid]
        gltf, bin_ = read_glb(os.path.join(REPO, "assets", rec["file"]))
        if len(gltf["meshes"]) != 1 or len(gltf["meshes"][0]["primitives"]) != 1:
            print(f"{gid}: not one skinned primitive, skipped")
            continue
        prim = gltf["meshes"][0]["primitives"][0]
        base_mesh = next(s[5:] for s in rec["sources"] if s.startswith("mesh:"))
        arrays, idx = pmd_arrays(parse_pmd(zf.read(f"art/meshes/{base_mesh}.cached.pmd")))
        why = matches(gltf, bin_, prim, arrays, idx)
        if why:
            print(f"{gid}: GLB does not come back from {base_mesh} ({why}), skipped")
            continue
        nbones = len(gltf["skins"][0]["joints"])
        for i, alt in enumerate(alts):
            d = parse_pmd(zf.read(f"art/meshes/{alt}.cached.pmd"))
            a_arrays, a_idx = pmd_arrays(d)
            if len(d["bones"]) != nbones or int(a_arrays["JOINTS_0"].max()) >= nbones:
                print(f"{gid}: {alt} is on another skeleton, skipped")
                continue
            if "TEXCOORD_1" not in prim["attributes"]:
                a_arrays["TEXCOORD_1"] = None
            look_id = f"{gid}_v{i + 2}"
            out, out_bin = rebuild(gltf, bin_, prim, a_arrays, a_idx)
            for n in out["nodes"]:
                if n.get("name") == gid:
                    n["name"] = look_id
            path = os.path.join(os.path.dirname(os.path.join(REPO, "assets", rec["file"])), look_id + ".glb")
            write_glb(path, out, out_bin)
            look = copy.deepcopy(rec)
            look["file"] = os.path.relpath(path, os.path.join(REPO, "assets")).replace(os.sep, "/")
            look["sources"] = sorted([s for s in rec["sources"] if s != f"mesh:{base_mesh}"] + [f"mesh:{alt}"])
            models[look_id] = look
            print(f"{look_id}: {alt}, {len(d['verts'])} vertices")
    with open(MANIFEST, "w") as fh:
        json.dump(manifest, fh, indent=1)
        fh.write("\n")


if __name__ == "__main__":
    main()
