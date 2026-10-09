#!/usr/bin/env python3
"""Garrison flag points for the building GLBs.

0 A.D. structures carry a "garrisoned" actor variant that props
props/special/common/garrison_flag_<style>.xml onto the "garrisoned" prop
point of the structure mesh (art/actors/structures/*/*.xml). The release
archive's .pmd meshes are not in this repository, so this reads each
building's actor and Collada mesh from the 0 A.D. source tree, finds the
prop-garrisoned joint, and maps it into the GLB's model space the way
extract_0ad_assets.py lays the mesh out (PMD axes x, z_up, y, grounded,
centred and fitted to the model's size).

Writes data/garrison_flags.json:
  {"models": {"<model id>": {"pos": [x, y, z], "scale": s, "flag": "<style>"}}}
where scale is the fit factor, which the flag prop takes too.

Usage: python3 tools/extract_garrison_flags.py [--ref master] [--jobs 8]
"""
import argparse
import json
import math
import os
import re
import struct
import sys
import urllib.request
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NS = "{http://www.collada.org/2005/11/COLLADASchema}"


def fetch(ref, path):
    url = f"https://raw.githubusercontent.com/0ad/0ad/{ref}/binaries/data/mods/public/{path}"
    try:
        with urllib.request.urlopen(url, timeout=60) as r:
            return r.read()
    except Exception:
        return None


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def joint_world(dae_bytes, names):
    """World matrix of the first node named in `names` (Collada node hierarchy)."""
    root = ET.fromstring(dae_bytes)
    found = {}
    geom_world = {}

    def walk(n, parent):
        # Collada applies a node's transform elements in document order.
        local = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
        for el in n:
            tag = el.tag[len(NS):] if el.tag.startswith(NS) else el.tag
            v = [float(x) for x in (el.text or "").split()] if tag in ("matrix", "translate", "rotate", "scale") else None
            if tag == "matrix":
                m = [v[0:4], v[4:8], v[8:12], v[12:16]]
            elif tag == "translate":
                m = [[1, 0, 0, v[0]], [0, 1, 0, v[1]], [0, 0, 1, v[2]], [0, 0, 0, 1]]
            elif tag == "scale":
                m = [[v[0], 0, 0, 0], [0, v[1], 0, 0], [0, 0, v[2], 0], [0, 0, 0, 1]]
            elif tag == "rotate":
                x, y, z, a = v[0], v[1], v[2], math.radians(v[3])
                ln = math.sqrt(x * x + y * y + z * z) or 1.0
                x, y, z = x / ln, y / ln, z / ln
                c, s_, t = math.cos(a), math.sin(a), 1 - math.cos(a)
                m = [[t * x * x + c, t * x * y - s_ * z, t * x * z + s_ * y, 0],
                     [t * x * y + s_ * z, t * y * y + c, t * y * z - s_ * x, 0],
                     [t * x * z - s_ * y, t * y * z + s_ * x, t * z * z + c, 0],
                     [0, 0, 0, 1]]
            else:
                continue
            local = mat_mul(local, m)
        w = mat_mul(parent, local)
        nm = n.get("name") or n.get("id") or ""
        if nm not in found:
            found[nm] = w
        for ig in n.findall(NS + "instance_geometry"):
            geom_world[(ig.get("url") or "")[1:]] = w
        for c in n.findall(NS + "node"):
            walk(c, w)

    ident = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    for vs in root.iter(NS + "visual_scene"):
        for n in vs.findall(NS + "node"):
            walk(n, ident)
    hit = None
    for nm in names:
        if nm in found:
            hit = found[nm]
            break
    return hit, root, geom_world


def dae_bounds(root, geom_world):
    """PMD-axes (x, z_up, y) bounds of every mesh vertex in the Collada scene."""
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for g in root.iter(NS + "geometry"):
        w = geom_world.get(g.get("id"))
        if w is None:
            continue
        mesh = g.find(NS + "mesh")
        if mesh is None:
            continue
        verts = mesh.find(NS + "vertices")
        src_id = None
        if verts is not None:
            for inp in verts.findall(NS + "input"):
                if inp.get("semantic") == "POSITION":
                    src_id = (inp.get("source") or "")[1:]
        for src in mesh.findall(NS + "source"):
            if src.get("id") != src_id:
                continue
            fa = [float(x) for x in src.find(NS + "float_array").text.split()]
            for i in range(0, len(fa) - 2, 3):
                x, y, z = fa[i], fa[i + 1], fa[i + 2]
                wx = w[0][0] * x + w[0][1] * y + w[0][2] * z + w[0][3]
                wy = w[1][0] * x + w[1][1] * y + w[1][2] * z + w[1][3]
                wz = w[2][0] * x + w[2][1] * y + w[2][2] * z + w[2][3]
                for k, v in enumerate((wx, wz, wy)):
                    lo[k] = min(lo[k], v)
                    hi[k] = max(hi[k], v)
    return (lo, hi) if lo[0] < 1e29 else None


def actor_garrison(ref, actor_path):
    """(mesh path, flag style) of a structure actor with a garrisoned variant."""
    data = fetch(ref, actor_path)
    if not data:
        return None, None
    try:
        root = ET.fromstring(data)
    except ET.ParseError:
        return None, None
    mesh = None
    for m in root.iter("mesh"):
        mesh = (m.text or "").strip()
        break
    style = None
    for v in root.iter("variant"):
        if (v.get("name") or "").lower() != "garrisoned":
            continue
        for p in v.iter("prop"):
            if (p.get("attachpoint") or "") == "garrisoned":
                m = re.search(r"garrison_flag_([a-z_]+)\.xml", p.get("actor") or "")
                style = m.group(1) if m else "hele"
    return mesh, style


def glb_bounds(path):
    """Model-space bounds of every primitive (building GLBs carry no node transforms)."""
    with open(path, "rb") as f:
        b = f.read()
    n = struct.unpack("<I", b[12:16])[0]
    j = json.loads(b[20:20 + n])
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for m in j.get("meshes", []):
        for prim in m["primitives"]:
            a = j["accessors"][prim["attributes"]["POSITION"]]
            for k in range(3):
                lo[k] = min(lo[k], a["min"][k])
                hi[k] = max(hi[k], a["max"][k])
    return (lo, hi) if lo[0] < 1e29 else None


def process(ref, mid, entry):
    actors = [s[6:] for s in entry["sources"] if s.startswith("actor:art/actors/structures/")]
    if len(actors) != 1:
        return mid, None
    mesh, style = actor_garrison(ref, actors[0])
    if not mesh or not style:
        return mid, None
    dae = fetch(ref, "art/meshes/" + mesh)
    if not dae:
        return mid, None
    w, root, geom_world = joint_world(dae, ("prop-garrisoned", "prop_garrisoned", "garrisoned"))
    if w is None:
        return mid, None
    # Collada Z-up -> PMD (x, z, y); 0 A.D. ignores <unit>.
    p = (w[0][3], w[2][3], w[1][3])
    bounds = dae_bounds(root, geom_world)
    gb = glb_bounds(os.path.join(ROOT, "assets", entry["file"]))
    if not bounds or not gb:
        return mid, None
    # extract_0ad_assets.py grounds, centres and fits the whole actor:
    # (p - (cx, miny, cz)) * scale. The structure mesh spans the actor's
    # footprint, so its bounds stand in for the composed actor's.
    lo, hi = bounds
    dw = max(hi[0] - lo[0], hi[2] - lo[2])
    gw = max(gb[1][0] - gb[0][0], gb[1][2] - gb[0][2])
    if dw <= 1e-6:
        return mid, None
    scale = gw / dw
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    x, y, z = (p[0] - cx) * scale, (p[1] - lo[1]) * scale, (p[2] - cz) * scale
    return mid, {"pos": [round(x, 4), round(y, 4), round(z, 4)], "scale": round(scale, 5), "flag": style}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default="master")
    ap.add_argument("--jobs", type=int, default=8)
    args = ap.parse_args()
    manifest = json.load(open(os.path.join(ROOT, "assets", "models", "manifest.json")))["models"]
    todo = [(k, v) for k, v in manifest.items() if v["file"].startswith("models/buildings/")]
    out = {}
    with ThreadPoolExecutor(args.jobs) as ex:
        for mid, res in ex.map(lambda kv: process(args.ref, *kv), todo):
            if res:
                out[mid] = res
    path = os.path.join(ROOT, "data", "garrison_flags.json")
    with open(path, "w") as f:
        json.dump({"source": f"0ad/0ad@{args.ref} art/actors/structures (garrisoned variant)",
                   "models": dict(sorted(out.items()))}, f, indent=1)
        f.write("\n")
    print(f"{len(out)} of {len(todo)} building models have a garrison flag -> {path}", file=sys.stderr)


if __name__ == "__main__":
    main()
