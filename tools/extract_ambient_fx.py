#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the art
it copies is CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

Many 0 A.D. structure actors prop particle emitters in the variants they always
show: chimney smoke on houses, temple and camp fires, the lighthouse's beacon,
and the flames and smoke of the rubble a destroyed civic centre leaves. The
model extractors drop those props. This tool finds the art/particles/*.xml
props of each building GLB's structure actor (and of the actors it props),
reads those particle files, and writes data/ambient_fx.json, which the 3D view
turns into GPUParticles3D on the structure or its rubble:

  {"particles": {"<name>": {"tex": "<texture>", "add": bool, "rate": n, "life": [min, max],
                            "pos": [[min, max] x3], "vel": [[min, max] x3], "size": [min, max],
                            "growth": n, "force": [x, y, z], "color": [min, max],
                            "angle": [min, max], "spin": [min, max], "start_full": bool,
                            "relative": bool}},
   "structures": {"<building GLB id>": {"metre": m, "fx": [{"p": "<name>", "pos": [x, y, z]}]}}}

`pos` is in the GLB's units (the GLB is centred on its actor's bounds and
grounded, as tools/extract_foundations.py frames say) and `metre` is GLB units
per 0 A.D. metre; the particle values are the files' metres and seconds. The
construction dust foundations prop is left out: the view raises that per
builder (data/foundations.json). The particle textures are copied into
assets/textures/particles/.

Usage:
  python3 -I tools/extract_ambient_fx.py --zip <0.28 public.zip> [--check]
"""

import argparse
import io
import json
import os
import sys
import xml.etree.ElementTree as ET
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_assets import DEFAULT_ZIP, Vfs, _actor_path, compose_actor, decode_dds, mat_apply, trs  # noqa: E402
from extract_building_decals import picked_props  # noqa: E402
from extract_foundations import Bounds  # noqa: E402
from extract_production_fx import prop_point  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
OUT_JSON = os.path.join(REPO, "data", "ambient_fx.json")
TEX_DIR = os.path.join(REPO, "assets", "textures", "particles")
SKIP = {"construction_dust"}
MAX_DEPTH = 3
BURIED = -5.0  # metres: a part lying wholly this far below ground is not in the GLB


def ground(vfs, actor_path):
    """The lowest vertex the GLB keeps, in actor metres. The Germans' encampment base
    composes flat at y = -29.6 (a skinned mesh the GLB poses), which would lift every
    emitter by that much; parts lying wholly below BURIED are left out."""
    parts, _ = compose_actor(vfs, actor_path, 0, None, set(), None)
    lows = []
    for part in parts:
        xf = part.transform if part.transform is not None else trs((0, 0, 0), (0, 0, 0, 1))
        ys = [mat_apply(xf, p)[1] for p, _n in part.mesh_data["verts"]]
        if ys and max(ys) >= BURIED:
            lows.append(min(ys))
    return min(lows) if lows else 0.0


def particle_def(vfs, name):
    """The particle file's emitter, in metres and seconds."""
    root = ET.fromstring(vfs.read(f"art/particles/{name}.xml"))
    vals = {}
    for e in root:
        if e.tag == "constant":
            v = float(e.get("value"))
            vals[e.get("name")] = (v, v)
        elif e.tag == "uniform":
            vals[e.get("name")] = (float(e.get("min")), float(e.get("max")))
    for e in root.findall("copy"):
        if e.get("from") in vals:
            vals[e.get("name")] = vals[e.get("from")]
    force = root.find("force")
    rng = lambda k: [vals.get(k, (0.0, 0.0))[0], vals.get(k, (0.0, 0.0))[1]]  # noqa: E731
    blend = root.find("blend")
    return {
        "tex": os.path.basename(root.findtext("texture").strip()),
        "add": blend is not None and blend.get("mode") == "add",
        "rate": vals.get("emissionrate", (0.0, 0.0))[1],
        "life": rng("lifetime"),
        "pos": [rng("position.x"), rng("position.y"), rng("position.z")],
        "vel": [rng("velocity.x"), rng("velocity.y"), rng("velocity.z")],
        "size": rng("size"),
        "growth": vals.get("size.growthRate", (0.0, 0.0))[1],
        "force": [float(force.get(k) or 0.0) for k in ("x", "y", "z")] if force is not None else [0.0, 0.0, 0.0],
        "color": rng("color.r"),
        "angle": rng("angle"),
        "spin": rng("velocity.angle"),
        "start_full": root.find("start_full") is not None,
        "relative": root.find("use_relative_velocity") is not None,
    }


def actor_particles(vfs, actor_path, cache, depth=0):
    """Particle props under the actor, in its space: (name, x, y, z)."""
    if depth > MAX_DEPTH:
        return []
    out, parts = [], None
    for ref, ap in picked_props(vfs, actor_path, cache):
        if "/decals/" in ref:
            continue
        if ap == "root":
            at = (0.0, 0.0, 0.0)
        else:
            if parts is None:
                parts, _ = compose_actor(vfs, actor_path, 0, None, set(), None)
            at = prop_point(parts, ap)
            if at is None:
                continue
        if ref.startswith("particle/"):
            name = os.path.splitext(os.path.basename(ref))[0]
            if name not in SKIP:
                out.append((name,) + tuple(at))
        else:
            out.extend((n, at[0] + x, at[1] + y, at[2] + z)
                       for n, x, y, z in actor_particles(vfs, _actor_path(ref), cache, depth + 1))
    return out


def build(vfs, models):
    bounds, cache, table, used = Bounds(vfs), {}, {}, set()
    for gid in sorted(models):
        m = models[gid]
        if not m.get("file", "").startswith("models/buildings/") or "size_meters" not in m:
            continue
        actors = [s[len("actor:"):] for s in m.get("sources", []) if s.startswith("actor:art/actors/structures/")]
        if not actors:
            continue
        actor = next((a for a in actors if os.path.splitext(os.path.basename(a))[0] in gid), actors[0])
        fx = actor_particles(vfs, actor, cache)
        if not fx:
            continue
        b = bounds(actor)
        if not b:
            continue
        dim = (b[1] - b[0], b[3] - b[2], b[5] - b[4])
        size = m["size_meters"]
        r = sorted(size[i] / dim[i] for i in range(3) if dim[i] > 1e-6)
        r = r[len(r) // 2]
        cx, cz, base = (b[0] + b[1]) / 2, (b[4] + b[5]) / 2, ground(vfs, actor)
        table[gid] = {"metre": round(r, 5),
                      "fx": [{"p": n, "pos": [round((x - cx) * r, 4), round((y - base) * r, 4), round((z - cz) * r, 4)]}
                             for n, x, y, z in fx]}
        used.update(n for n, *_ in fx)
    return table, {n: particle_def(vfs, n) for n in sorted(used)}


def copy_textures(zf, particles, check):
    stale = []
    for tex in sorted({p["tex"] for p in particles.values()}):
        path = os.path.join(TEX_DIR, tex)
        if check:
            if not os.path.isfile(path):
                stale.append(path)
            continue
        if os.path.isfile(path):
            continue
        buf = io.BytesIO()
        decode_dds(zf.read(f"art/textures/particles/{tex}.cached.dds")).save(buf, "PNG")
        with open(path, "wb") as f:
            f.write(buf.getvalue())
    return stale


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=os.path.normpath(DEFAULT_ZIP))
    ap.add_argument("--check", action="store_true", help="fail if data/ambient_fx.json is stale")
    args = ap.parse_args()
    with open(MANIFEST) as f:
        models = json.load(f)["models"]
    with zipfile.ZipFile(args.zip) as zf:
        table, particles = build(Vfs(zf), models)
        stale = copy_textures(zf, particles, args.check)
    text = json.dumps({"particles": particles, "structures": table}, indent=1, sort_keys=True) + "\n"
    if args.check:
        with open(OUT_JSON) as f:
            if f.read() != text or stale:
                raise SystemExit("data/ambient_fx.json or its textures are stale: rerun tools/extract_ambient_fx.py")
        print(f"{len(table)} building GLBs, {len(particles)} particle files: up to date")
        return
    with open(OUT_JSON, "w") as f:
        f.write(text)
    print(f"{len(table)} building GLBs, {len(particles)} particle files -> {os.path.relpath(OUT_JSON, REPO)}")


if __name__ == "__main__":
    main()
