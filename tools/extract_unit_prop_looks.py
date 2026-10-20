#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the meshes
it converts are CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

0 A.D. picks each unit's prop variants at random by frequency, and many head,
helmet, shield and cape actors have a group that swaps the mesh (beards and hair
cuts, helmet crests, shield shapes). The unit GLBs bake the most frequent one.
This tool finds, for each unit GLB, the baked part of every such prop (the part
whose vertices are its default .pmd's under one rigid transform and scale) and
writes:

  assets/models/props/looks/<mesh>.glb   each other mesh of the group, as its
      .pmd stores it (positions, normals, UVs with V flipped), no material
  data/unit_prop_looks.json  {"<glb id>": [{"node": part, "joint": skin joint,
      "xf": [12 floats, basis columns then origin: .pmd space to the part's],
      "meshes": ["" (the baked part), "res://...glb", ...], "freq": [n, ...]}]}

ModelLibrary::set_prop_looks draws one per unit by its id, on the part's joint
with the part's material. Only variants that keep the baked part's skin and add
no props of their own are taken; the others count towards the baked mesh. Weapons
are left: the extractor merges their parts.

Usage: python3 -I tools/extract_unit_prop_looks.py --zip <0.28 public.zip> [--check]
"""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET
import zipfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from add_0ad_clips import read_glb, write_glb  # noqa: E402
from add_0ad_mesh_looks import read  # noqa: E402
from extract_0ad_assets import DEFAULT_ZIP, Vfs, _pick_variant, _variant_freq, parse_pmd, resolve_variant_chain  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
OUT_JSON = os.path.join(REPO, "data", "unit_prop_looks.json")
LOOK_DIR = os.path.join(REPO, "assets", "models", "props", "looks")
KINDS = ("heads", "helmets", "shields", "capes")
MAX_LOOKS = 8
FIT_TOL = 2e-4  # of the part's size


def mesh_group(vfs, actor, cache):
    """(default mesh, [(mesh, frequency)]) of the actor's first group that swaps
    meshes, the variants that change the skin or add props folded into the default."""
    try:
        root = ET.fromstring(vfs.read(actor))
    except (KeyError, ET.ParseError):
        return None
    if root.tag == "qualitylevels" and root.find("inline") is not None:
        root = root.find("inline")

    def base_tex(r):
        return next((t.get("file") for t in r.iter("texture") if (t.get("name") or "baseTex") == "baseTex"), None)

    for g in root.findall("group"):
        live = []
        for v in g.findall("variant"):
            if _variant_freq(v) <= 0:
                continue
            r = resolve_variant_chain(vfs, v, cache)
            if r is None or not (r.findtext("mesh") or "").strip():
                live = []
                break
            live.append((r.findtext("mesh").strip(), _variant_freq(v), base_tex(r), r.find("props") is not None))
        if len({x[0] for x in live}) < 2:
            continue
        best = resolve_variant_chain(vfs, _pick_variant(g), cache)
        dmesh, dtex = best.findtext("mesh").strip(), base_tex(best)
        freq = {dmesh: 0}
        for mesh, f, tex, has_props in live:
            key = mesh if (tex == dtex and not has_props) else dmesh
            freq[key] = freq.get(key, 0) + f
        return dmesh, freq
    return None


def pmd_mesh(d):
    pos = np.array([v[0] for v in d["verts"]], dtype=np.float64)
    nrm = np.array([v[1] for v in d["verts"]], dtype=np.float32)
    uv = np.array([(u, 1.0 - v) for u, v in d["uvs"]], dtype=np.float32)
    idx = np.array([(a, c, b) for a, b, c in d["faces"]], dtype=np.uint16).reshape(-1)
    return pos, nrm, uv, idx


def fit(src, dst):
    """s, R, t with s * R @ src + t ~ dst, and the largest miss; None without one."""
    cs, cd = src.mean(0), dst.mean(0)
    a, b = src - cs, dst - cd
    na = (a ** 2).sum()
    if na < 1e-12:
        return None
    u, _s, vt = np.linalg.svd(a.T @ b)
    r = vt.T @ u.T
    if np.linalg.det(r) < 0:
        return None
    s = np.sqrt((b ** 2).sum() / na)
    t = cd - s * r @ cs
    miss = np.abs(a @ r.T * s + cd - dst).max()
    return s, r, t, miss


def write_look(path, pos, nrm, uv, idx):
    blobs = [pos.astype(np.float32).tobytes(), nrm.tobytes(), uv.tobytes(), idx.tobytes()]
    views, accs, bin_ = [], [], bytearray()
    kinds = [("VEC3", 5126, len(pos), 34962), ("VEC3", 5126, len(nrm), 34962), ("VEC2", 5126, len(uv), 34962),
             ("SCALAR", 5123, len(idx), 34963)]
    for blob, (typ, comp, count, target) in zip(blobs, kinds):
        while len(bin_) % 4:
            bin_ += b"\x00"
        views.append({"buffer": 0, "byteOffset": len(bin_), "byteLength": len(blob), "target": target})
        bin_ += blob
        accs.append({"bufferView": len(views) - 1, "componentType": comp, "count": count, "type": typ})
    accs[0]["min"] = [float(x) for x in pos.min(0)]
    accs[0]["max"] = [float(x) for x in pos.max(0)]
    name = os.path.splitext(os.path.basename(path))[0]
    gltf = {"asset": {"version": "2.0", "generator": "RTS0 extract_unit_prop_looks.py"},
            "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": name, "mesh": 0}],
            "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                       "indices": 3}]}],
            "accessors": accs, "bufferViews": views, "buffers": [{"byteLength": len(bin_)}]}
    os.makedirs(os.path.dirname(path), exist_ok=True)
    write_glb(path, gltf, bin_)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=os.path.normpath(DEFAULT_ZIP))
    ap.add_argument("--check", action="store_true", help="fail if data/unit_prop_looks.json or a look is stale")
    args = ap.parse_args()
    with open(MANIFEST) as f:
        models = json.load(f)["models"]
    zf = zipfile.ZipFile(args.zip)
    vfs, cache, pmds, groups = Vfs(zf), {}, {}, {}
    table, looks = {}, {}

    def pmd(mesh):
        if mesh not in pmds:
            try:
                pmds[mesh] = pmd_mesh(parse_pmd(zf.read(f"art/meshes/{mesh}.cached.pmd")))
            except KeyError:
                pmds[mesh] = None
        return pmds[mesh]

    for gid in sorted(models):
        m = models[gid]
        src = m.get("sources", [])
        if not m.get("file", "").startswith("models/units/") or not any(s.startswith("actor:art/actors/units/") for s in src):
            continue
        props = [s[len("actor:"):] for s in src
                 if s.startswith("actor:art/actors/props/units/") and s.split("/")[4] in KINDS]
        found = []
        for a in props:
            if a not in groups:
                groups[a] = mesh_group(vfs, a, cache)
            if groups[a] and len([k for k, f in groups[a][1].items() if f > 0]) > 1:
                found.append(groups[a])
        if not found:
            continue
        gltf, bin_ = read_glb(os.path.join(REPO, "assets", m["file"]))
        parts = []
        for nd in gltf["nodes"]:
            if "mesh" not in nd or nd.get("name", "").startswith("tool_") or nd.get("skin") is None:
                continue
            prims = gltf["meshes"][nd["mesh"]]["primitives"]
            if len(prims) != 1:
                continue
            p = prims[0]
            joints = read(gltf, bin_, p["attributes"]["JOINTS_0"])
            weights = read(gltf, bin_, p["attributes"]["WEIGHTS_0"])
            live = np.unique(joints[weights > 0])
            if len(live) != 1:
                continue  # rigid props sit on one joint
            parts.append((nd["name"], int(live[0]), read(gltf, bin_, p["attributes"]["POSITION"]).astype(np.float64)))
        slots, used = [], set()
        for dmesh, freq in found:
            base = pmd(dmesh)
            if base is None:
                continue
            for name, joint, pos in parts:
                if name in used or len(pos) != len(base[0]):
                    continue
                res = fit(base[0], pos)
                if res is None or res[3] > FIT_TOL * max(np.ptp(pos, 0).max(), 1e-6):
                    continue
                s, r, t, _miss = res
                meshes, freqs = [""], [freq[dmesh]]
                for mesh, f in sorted(freq.items(), key=lambda kv: (-kv[1], kv[0])):
                    if mesh == dmesh or f <= 0 or len(meshes) >= MAX_LOOKS or pmd(mesh) is None:
                        continue
                    rel = "models/props/looks/" + os.path.splitext(mesh)[0].replace("/", "__") + ".glb"
                    looks[rel] = mesh
                    meshes.append("res://assets/" + rel)
                    freqs.append(f)
                if len(meshes) < 2:
                    break
                basis = s * r
                xf = [float(x) for x in list(basis[:, 0]) + list(basis[:, 1]) + list(basis[:, 2]) + list(t)]
                slots.append({"node": name, "joint": joint, "xf": [round(x, 6) for x in xf],
                              "meshes": meshes, "freq": freqs})
                used.add(name)
                break
        if slots:
            table[gid] = slots
    stale = []
    for rel, mesh in sorted(looks.items()):
        path = os.path.join(REPO, "assets", rel)
        if args.check:
            if not os.path.isfile(path):
                stale.append(rel)
            continue
        write_look(path, *pmd(mesh))
    text = json.dumps(table, indent=1, sort_keys=True) + "\n"
    n_slots = sum(len(v) for v in table.values())
    if args.check:
        with open(OUT_JSON) as f:
            if f.read() != text or stale:
                raise SystemExit("data/unit_prop_looks.json or its looks are stale: rerun tools/extract_unit_prop_looks.py")
        print(f"{len(table)} unit GLBs, {n_slots} props, {len(looks)} looks: up to date")
        return
    with open(OUT_JSON, "w") as f:
        f.write(text)
    print(f"{len(table)} unit GLBs, {n_slots} props, {len(looks)} looks -> {os.path.relpath(OUT_JSON, REPO)}")


if __name__ == "__main__":
    main()
