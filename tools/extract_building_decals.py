#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the art
it copies is CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

0 A.D. structures and their foundations prop a ground decal (props/structures/
decals/*.xml: dirt, gravel, sand, paving) that CModelDecal lays on the terrain
under them: a <decal width depth angle offsetx offsetz> rectangle, in metres
and degrees around the prop point, textured with the decal actor's baseTex.
The model extractors drop those props. This tool finds the decals each building
GLB's structure actor (and the actors it props) carries, and writes
data/building_decals.json, which the 3D view turns into Godot Decal nodes on
the terrain:

  {"decals": {"<building GLB id>": [{"tex": "<texture>", "pos": [x, z],
                                     "size": [w, d], "angle": degrees}, ...]}}

`pos` (the decal's centre) and `size` are in the building GLB's horizontal
units (the view lays the decal on the ground the GLB stands on): the actor is mapped into the GLB through the GLB's size in the manifest,
as tools/extract_foundations.py frames do. It also copies the decal textures
into assets/textures/decals/ (at most 512 px on a side).

Usage:
  python3 -I tools/extract_building_decals.py --zip <0.28 public.zip> [--check]
"""

import argparse
import io
import json
import math
import os
import sys
import xml.etree.ElementTree as ET
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_assets import (DEFAULT_ZIP, Vfs, _actor_path, _pick_variant, _variant_children,  # noqa: E402
                                compose_actor, resolve_variant_chain)
from extract_foundations import Bounds  # noqa: E402
from extract_production_fx import prop_point  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
OUT_JSON = os.path.join(REPO, "data", "building_decals.json")
TEX_DIR = os.path.join(REPO, "assets", "textures", "decals")
MAX_TEX = 512
MAX_DEPTH = 3


def picked_props(vfs, actor_path, cache):
    """(prop actor, attachpoint) pairs of the variants compose_actor picks."""
    try:
        root = ET.fromstring(vfs.read(actor_path))
    except (KeyError, ET.ParseError):
        return []
    if root.tag == "qualitylevels" and root.find("inline") is not None:
        root = root.find("inline")
    props = []
    for g in root.findall("group"):
        var = _pick_variant(g)
        if var is None or "destruction" in (var.get("file") or ""):
            continue
        _m, p, _t, _mt = _variant_children(vfs, resolve_variant_chain(vfs, var, cache))
        props.extend((a.replace("\\", "/"), ap.lower()) for a, ap, _s in p)
    return props


def decal_of(vfs, actor_path, cache):
    """(baseTex, width, depth, angle, offsetx, offsetz) of a decal actor, or None."""
    try:
        root = ET.fromstring(vfs.read(actor_path))
    except (KeyError, ET.ParseError):
        return None
    decal, tex = None, None
    for g in root.findall("group"):
        var = resolve_variant_chain(vfs, _pick_variant(g), cache)
        if var is None:
            continue
        d = var.find("decal")
        if d is not None:
            decal = d
        for t in var.iter("texture"):
            if (t.get("name") or "baseTex") == "baseTex" and t.get("file"):
                tex = t.get("file").strip()
    if decal is None or not tex:
        return None
    f = lambda k: float(decal.get(k) or 0.0)  # noqa: E731
    return tex, f("width"), f("depth"), f("angle"), f("offsetx"), f("offsetz")


def actor_decals(vfs, actor_path, cache, depth=0):
    """Decals under the actor, in its space: (tex, x, z, w, d, angle)."""
    if depth > MAX_DEPTH:
        return []
    out = []
    parts = None
    for ref, ap in picked_props(vfs, actor_path, cache):
        if "particle/" in ref:
            continue
        if ap == "root":
            at = (0.0, 0.0, 0.0)
        else:
            if parts is None:
                parts, _ = compose_actor(vfs, actor_path, 0, None, set(), None)
            at = prop_point(parts, ap)
            if at is None:
                continue
        sub = _actor_path(ref)
        if "/decals/" in ref:
            d = decal_of(vfs, sub, cache)
            if d:
                tex, w, dd, ang, ox, oz = d
                out.append((tex, at[0] + ox, at[2] + oz, w, dd, ang))
        else:
            out.extend((t, at[0] + x, at[2] + z, w, d, a)
                       for t, x, z, w, d, a in actor_decals(vfs, sub, cache, depth + 1))
    return out


def build(vfs, models):
    bounds, cache, table = Bounds(vfs), {}, {}
    for gid in sorted(models):
        m = models[gid]
        if not m.get("file", "").startswith("models/buildings/") or "size_meters" not in m:
            continue
        actors = [s[len("actor:"):] for s in m.get("sources", []) if s.startswith("actor:art/actors/structures/")]
        if not actors:
            continue
        actor = next((a for a in actors if os.path.splitext(os.path.basename(a))[0] in gid), actors[0])
        decals = actor_decals(vfs, actor, cache)
        if not decals:
            continue
        b = bounds(actor)
        if not b:
            continue
        dim = (b[1] - b[0], b[3] - b[2], b[5] - b[4])
        size = m["size_meters"]
        r = sorted(size[i] / dim[i] for i in range(3) if dim[i] > 1e-6)
        r = r[len(r) // 2]
        cx, cz = (b[0] + b[1]) / 2, (b[4] + b[5]) / 2
        table[gid] = [{"tex": os.path.splitext(os.path.basename(t))[0],
                       "src": t,
                       "pos": [round((x - cx) * r, 4), round((z - cz) * r, 4)],
                       "size": [round(w * r, 4), round(d * r, 4)],
                       "angle": a} for t, x, z, w, d, a in decals]
    return table


def copy_textures(vfs, table, check):
    stale, done = [], set()
    for entries in table.values():
        for e in entries:
            src = e.pop("src")
            name = e["tex"]
            if name in done:
                continue
            done.add(name)
            path = os.path.join(TEX_DIR, name + ".png")
            if check:
                if not os.path.isfile(path):
                    stale.append(path)
                continue
            img = vfs.load_texture_rgba(src)
            if img is None:
                raise SystemExit(f"missing decal texture {src}")
            if max(img.size) > MAX_TEX:
                k = MAX_TEX / max(img.size)
                img = img.resize((max(1, round(img.size[0] * k)), max(1, round(img.size[1] * k))))
            os.makedirs(TEX_DIR, exist_ok=True)
            buf = io.BytesIO()
            img.save(buf, "PNG", optimize=True)
            with open(path, "wb") as f:
                f.write(buf.getvalue())
    return stale, len(done)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=os.path.normpath(DEFAULT_ZIP))
    ap.add_argument("--check", action="store_true", help="fail if data/building_decals.json is stale")
    args = ap.parse_args()
    with open(MANIFEST) as f:
        models = json.load(f)["models"]
    with zipfile.ZipFile(args.zip) as zf:
        vfs = Vfs(zf)
        table = build(vfs, models)
        stale, ntex = copy_textures(vfs, table, args.check)
    text = json.dumps({"decals": table}, indent=1, sort_keys=True) + "\n"
    if args.check:
        with open(OUT_JSON) as f:
            if f.read() != text or stale:
                raise SystemExit("data/building_decals.json or its textures are stale: rerun tools/extract_building_decals.py")
        print(f"{len(table)} building GLBs, {ntex} textures: up to date")
        return
    with open(OUT_JSON, "w") as f:
        f.write(text)
    print(f"{len(table)} building GLBs, {ntex} decal textures -> {os.path.relpath(OUT_JSON, REPO)}")


if __name__ == "__main__":
    main()
