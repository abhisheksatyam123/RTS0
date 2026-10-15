#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the art
it copies is CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

0 A.D.'s ProductionQueue.js selects a structure's "researching" or "training"
actor variant while the first item of its queue is a technology or a batch,
and back to "idle" when the queue empties. In 0.28 the forges use it: their
"researching" variant props blacksmith sparks, forge smoke and flames on the
forge's prop points (the Han arsenal smokes while it trains too). The German
forge also props its forger, hammering at the anvil, and the German temple a
murder of crows circling 27 m above it. This tool writes
data/production_fx.json, which tells the 3D view where to draw them on each
building GLB:

  {"<building GLB id>": {"metre": m, "researching": [{"fx": "smoke", "pos": [x, y, z]},
                                                    {"glb": "germ_forger", "pos": [x, y, z], "yaw": a}, ...],
                         "training": [...]}}

`pos` is in the building GLB's units and `metre` is GLB units per 0 A.D. metre
(for the particles' sizes and speeds). A "glb" entry is an animated unit GLB
extracted at 0 A.D. metres (tools/extract_0ad_assets.py, fit "scale" 1), drawn
playing its Idle with its origin at `pos`, turned by `yaw` (radians about +Y)
and scaled by `metre`. Like tools/extract_upgrade_scaffolds.py
it composes each structure actor as extract_0ad_assets.compose_actor does and
maps the prop points into the GLB through the GLB's size in the manifest.
It also copies the particle textures the effects use into
assets/textures/particles/.

Usage:
  python3 -I tools/extract_production_fx.py --zip <0.28 public.zip> [--check]
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
from extract_0ad_assets import DEFAULT_ZIP, Vfs, compose_actor, decode_dds, mat_apply, mat_mul, trs  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
OUT_JSON = os.path.join(REPO, "data", "production_fx.json")
TEX_DIR = os.path.join(REPO, "assets", "textures", "particles")
STATES = ("researching", "training")
# 0 A.D. particle file -> the view's effect. position.y is the file's emitter
# height above the prop point, in metres.
PARTICLES = {
    "particle/sparks_blacksmith.xml": ("sparks", 2.0),
    "particle/smoke_blacksmith.xml": ("smoke", 2.0),
    "particle/flame.xml": ("flame", 0.0),
    "particle/flame_firepit_2.xml": ("flame", 0.0),
    "particle/flame_catapult.xml": ("flame", 0.0),
}
# Animated actor props -> their unit GLB id (extracted at 0 A.D. metres).
ACTORS = {
    "units/germans/forger.xml": "germ_forger",
    "fauna/crow_circle.xml": "crow_circle",
}
# The textures those effects draw (flame.png and smoke_64a.png are already copied).
TEXTURES = ("sparks.png", "smoke_64a_blacksmith.png")


def state_props(vfs, actor_path, state):
    """(particle actor, attachpoint) pairs the actor's `state` variant props."""
    try:
        root = ET.fromstring(vfs.read(actor_path))
    except (KeyError, ET.ParseError):
        return []
    found = []
    for var in root.iter("variant"):
        if (var.get("name") or "").lower() != state:
            continue
        for prop in var.iter("prop"):
            a = (prop.get("actor") or "").replace("\\", "/")
            if a in PARTICLES or a in ACTORS:
                found.append((a, (prop.get("attachpoint") or "root").lower()))
    return found


def prop_point(parts, name):
    """World position of prop point `name` on the composed parts (CModel rule)."""
    xf = prop_matrix(parts, name)
    return None if xf is None else mat_apply(xf, (0.0, 0.0, 0.0))


def prop_matrix(parts, name):
    """World transform of prop point `name` on the composed parts."""
    if name == "root":
        return trs((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))
    for part in parts:
        md = part.mesh_data
        for pp in md.get("prop_points") or []:
            if pp["name"].lower() != name:
                continue
            local = trs(pp["pos"], pp["quat"])
            if pp["bone"] != 0xFF and pp["bone"] < len(md["bones"]):
                t, q = md["bones"][pp["bone"]]
                local = mat_mul(trs(t, q), local)
            xf = part.transform if part.transform is not None else trs((0, 0, 0), (0, 0, 0, 1))
            return mat_mul(xf, local)
    return None


def glb_pivot(vfs, actor_path):
    """The actor point the unit GLB's origin is at: the centre of its rest-pose
    bounds in x and z, their bottom in y (compile_unit_animated's grounding)."""
    parts, _ = compose_actor(vfs, actor_path, 0, None, set(), None)
    pts = []
    for part in parts:
        xf = part.transform if part.transform is not None else trs((0, 0, 0), (0, 0, 0, 1))
        pts.extend(mat_apply(xf, p) for p, _n in part.mesh_data["verts"])
    xs, ys, zs = [p[0] for p in pts], [p[1] for p in pts], [p[2] for p in pts]
    return ((min(xs) + max(xs)) / 2, min(ys), (min(zs) + max(zs)) / 2)


def build(vfs, models):
    out, pivots = {}, {}
    for gid in sorted(models):
        m = models[gid]
        if not m.get("file", "").startswith("models/buildings/") or "size_meters" not in m:
            continue
        for src in m.get("sources", []):
            if not src.startswith("actor:art/actors/structures/"):
                continue
            actor = src[len("actor:"):]
            props = {s: state_props(vfs, actor, s) for s in STATES}
            if not any(props.values()):
                continue
            parts, _ = compose_actor(vfs, actor, 0, None, set(), None)
            xs, ys, zs = [], [], []
            for part in parts:
                xf = part.transform if part.transform is not None else trs((0, 0, 0), (0, 0, 0, 1))
                for p, _n in part.mesh_data["verts"]:
                    x, y, z = mat_apply(xf, p)
                    xs.append(x); ys.append(y); zs.append(z)
            if not xs:
                continue
            dim = (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
            size = m["size_meters"]
            r = sorted(size[i] / dim[i] for i in range(3) if dim[i] > 1e-6)
            r = r[len(r) // 2]
            cx, cz, base = (min(xs) + max(xs)) / 2, (min(zs) + max(zs)) / 2, min(ys)
            entry = {"metre": round(r, 5)}
            for state, plist in props.items():
                fx = []
                for actor_ref, ap in plist:
                    xf = prop_matrix(parts, ap)
                    if xf is None:
                        print(f"  ! {gid}: no prop point '{ap}' for {actor_ref}")
                        continue
                    if actor_ref in ACTORS:
                        if actor_ref not in pivots:
                            pivots[actor_ref] = glb_pivot(vfs, "art/actors/" + actor_ref)
                        p = mat_apply(xf, pivots[actor_ref])
                        o, fwd = mat_apply(xf, (0.0, 0.0, 0.0)), mat_apply(xf, (0.0, 0.0, 1.0))
                        fx.append({"glb": ACTORS[actor_ref],
                                   "pos": [round((p[0] - cx) * r, 4), round((p[1] - base) * r, 4), round((p[2] - cz) * r, 4)],
                                   "yaw": round(math.atan2(fwd[0] - o[0], fwd[2] - o[2]), 4)})
                        continue
                    p = mat_apply(xf, (0.0, 0.0, 0.0))
                    kind, height = PARTICLES[actor_ref]
                    fx.append({"fx": kind, "pos": [round((p[0] - cx) * r, 4), round((p[1] + height - base) * r, 4),
                                                    round((p[2] - cz) * r, 4)]})
                if fx:
                    entry[state] = fx
            if len(entry) > 1:
                out[gid] = entry
            break
    return out


def copy_textures(zf, check):
    stale = []
    for name in TEXTURES:
        data = zf.read(f"art/textures/particles/{name}.cached.dds")
        buf = io.BytesIO()
        decode_dds(data).save(buf, "PNG")
        path = os.path.join(TEX_DIR, name)
        if check:
            if not os.path.isfile(path):
                stale.append(path)
            continue
        with open(path, "wb") as f:
            f.write(buf.getvalue())
    return stale


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=os.path.normpath(DEFAULT_ZIP))
    ap.add_argument("--check", action="store_true", help="fail if data/production_fx.json is stale")
    args = ap.parse_args()
    with open(MANIFEST) as f:
        models = json.load(f)["models"]
    with zipfile.ZipFile(args.zip) as zf:
        table = build(Vfs(zf), models)
        stale = copy_textures(zf, args.check)
    text = json.dumps(table, indent=1, sort_keys=True) + "\n"
    if args.check:
        with open(OUT_JSON) as f:
            if f.read() != text or stale:
                raise SystemExit("data/production_fx.json or its textures are stale: rerun tools/extract_production_fx.py")
        print(f"{len(table)} building GLBs: up to date")
        return
    with open(OUT_JSON, "w") as f:
        f.write(text)
    print(f"{len(table)} building GLBs -> {os.path.relpath(OUT_JSON, REPO)}")


if __name__ == "__main__":
    main()
