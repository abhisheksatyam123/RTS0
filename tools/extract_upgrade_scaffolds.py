#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the art
it places is CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

0 A.D.'s Upgrade.js switches a structure's actor to its "upgrading" variant
while the upgrade runs (template <Upgrade><*><Variant>upgrading</Variant>), and
those variants prop a construction scaffold (props/structures/construction/
scaf_*.xml) on the structure's root. This tool writes data/upgrade_scaffolds.json,
which tells the 3D view which scaffold GLB (extract_0ad_assets.py ZIP_STRUCTURES
"scaffold_up_*") to show on each building GLB and where:

  {"<building GLB id>": {"scaffold": "scaffold_up_3x3_tower",
                         "scale": k, "offset": [x, y, z]}}

`scale` and `offset` are in the building GLB's own units. extract_0ad_assets.py
and extract_0ad_structures.py fit each building to a size of their own and move
its origin to the centre of its ground footprint, so the tool composes the
structure's and the scaffold's actors from the release archive as
extract_0ad_assets.compose_actor does, and maps the scaffold's actor-space
footprint into the building GLB through the GLB's size in the manifest.

Usage:
  python3 -I tools/extract_upgrade_scaffolds.py --zip <0.28 public.zip> [--check]
"""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_assets import DEFAULT_ZIP, ZIP_STRUCTURES, Vfs, compose_actor, mat_apply, trs  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
OUT_JSON = os.path.join(REPO, "data", "upgrade_scaffolds.json")
SCAF_PREFIX = "props/structures/construction/scaf_"


def scaffold_glb_id(actor):
    """props/structures/construction/scaf_3x3_tower.xml -> scaffold_up_3x3_tower"""
    return "scaffold_up_" + os.path.basename(actor)[len("scaf_"):-len(".xml")]


def upgrading_scaffolds(vfs, actor_path):
    """The scaffold actors the actor's "upgrading" variant props, in order."""
    try:
        root = ET.fromstring(vfs.read(actor_path))
    except (KeyError, ET.ParseError):
        return []
    found = []
    for var in root.iter("variant"):
        if (var.get("name") or "").lower() != "upgrading":
            continue
        for prop in var.iter("prop"):
            a = (prop.get("actor") or "").replace("\\", "/")
            if a.startswith(SCAF_PREFIX) and a not in found:
                found.append(a)
    return found


def actor_bounds(vfs, actor_path):
    """(minx, maxx, miny, maxy, minz, maxz) of the actor as compose_actor builds it."""
    parts, _ = compose_actor(vfs, actor_path, 0, None, set(), None)
    xs, ys, zs = [], [], []
    for part in parts:
        xf = part.transform if part.transform is not None else trs((0, 0, 0), (0, 0, 0, 1))
        for p, _n in part.mesh_data["verts"]:
            x, y, z = mat_apply(xf, p)
            xs.append(x); ys.append(y); zs.append(z)
    if not xs:
        return None
    return (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))


def build(vfs, models):
    scaf_scale = {aid: row[2] for aid, row in ZIP_STRUCTURES.items() if aid.startswith("scaffold_up_")}
    bounds = {}

    def bounds_of(path):
        if path not in bounds:
            bounds[path] = actor_bounds(vfs, path)
        return bounds[path]

    out = {}
    for gid in sorted(models):
        m = models[gid]
        if not m.get("file", "").startswith("models/buildings/") or "size_meters" not in m:
            continue
        actors = [s[len("actor:"):] for s in m.get("sources", [])
                  if s.startswith("actor:art/actors/structures/")]
        for actor in actors:
            scafs = upgrading_scaffolds(vfs, actor)
            if not scafs:
                continue
            b = bounds_of(actor)
            s = bounds_of("art/actors/" + scafs[0])
            if not b or not s:
                continue
            sid = scaffold_glb_id(scafs[0])
            if sid not in scaf_scale:
                raise SystemExit(f"{gid}: {scafs[0]} has no ZIP_STRUCTURES row {sid}")
            dim = (b[1] - b[0], b[3] - b[2], b[5] - b[4])
            size = m["size_meters"]
            # The GLB's scale against the actor: the extractors fit uniformly, so
            # take the median of the three axes (a dropped decal moves one of them).
            r = sorted(size[i] / dim[i] for i in range(3) if dim[i] > 1e-6)
            r = r[len(r) // 2]
            cx, cz, base = (b[0] + b[1]) / 2, (b[4] + b[5]) / 2, b[2]
            sx, sz, sbase = (s[0] + s[1]) / 2, (s[4] + s[5]) / 2, s[2]
            out[gid] = {
                "scaffold": sid,
                "scale": round(r / scaf_scale[sid], 4),
                "offset": [round((sx - cx) * r, 4), round((sbase - base) * r, 4), round((sz - cz) * r, 4)],
            }
            break
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=os.path.normpath(DEFAULT_ZIP))
    ap.add_argument("--check", action="store_true", help="fail if data/upgrade_scaffolds.json is stale")
    args = ap.parse_args()
    with open(MANIFEST) as f:
        models = json.load(f)["models"]
    with zipfile.ZipFile(args.zip) as zf:
        table = build(Vfs(zf), models)
    text = json.dumps(table, indent=1, sort_keys=True) + "\n"
    if args.check:
        with open(OUT_JSON) as f:
            if f.read() != text:
                raise SystemExit("data/upgrade_scaffolds.json is stale: rerun tools/extract_upgrade_scaffolds.py")
        print(f"{len(table)} building GLBs: up to date")
        return
    with open(OUT_JSON, "w") as f:
        f.write(text)
    print(f"{len(table)} building GLBs -> {os.path.relpath(OUT_JSON, REPO)}")


if __name__ == "__main__":
    main()
