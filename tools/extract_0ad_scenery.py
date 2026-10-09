#!/usr/bin/env python3
"""0 A.D. actors for the Gaia scenery types in src/simulation/gaia_data.h.

Each scenery template (simulation/templates/structures/<name>.xml) names a
VisualActor; this reads that actor from the 0 A.D. source tree, takes every
mesh variant of its first group (fallen columns have several), the base
texture of the actor, and any props on the "root" attach point (the Doric
column's capital), and converts each variant into one static GLB with
tools/dae_to_glb.gd in prop mode: 0 A.D.'s axes (x, up, z) and metres, origin
on the entity's ground point.

Writes assets/models/scenery/<name>[_<n>].glb and data/scenery_actors.json:
  {"types": {"<template name>": ["scenery/<name>_0.glb", ...]}}

player_trans materials (obelisk, colonnade) mask the player colour in the
texture's alpha; Gaia's colour is white (player_defaults.json), baked in here.

Usage: python3 tools/extract_0ad_scenery.py --godot <godot binary> [--ref master]
"""
import argparse
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAIA_WHITE = (255, 255, 255)


def fetch(ref, path):
    url = f"https://raw.githubusercontent.com/0ad/0ad/{ref}/binaries/data/mods/public/{path}"
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read()


def scenery_templates():
    src = open(os.path.join(ROOT, "src", "simulation", "gaia_data.h")).read()
    body = src[src.index("kTable[SCENERY_TYPE_COUNT]"):]
    body = body[:body.index("};")]
    return re.findall(r'\{"([a-z_]+)",', body)


def parse_actor(ref, actor):
    """(variants: [[mesh, ...]], base texture, material, root props: [(mesh, texture)])."""
    root = ET.fromstring(fetch(ref, "art/actors/" + actor))
    material = (root.findtext("material") or "").strip()
    texture = None
    props = []
    first = None
    for group in root.findall("group"):
        meshes = [(v.findtext("mesh") or "").strip() for v in group.findall("variant")]
        if first is None and any(meshes):
            first = [m for m in meshes if m]
        for v in group.findall("variant"):
            for t in v.iter("texture"):
                if t.get("name") == "baseTex" and texture is None:
                    texture = t.get("file")
            for p in v.iter("prop"):
                if p.get("attachpoint") == "root" and p.get("actor"):
                    pv, pt, _, _ = parse_actor(ref, p.get("actor"))
                    if pv and pt:
                        props.append((pv[0][0], pt))
    return [[m] for m in (first or [])], texture, material, props


def texture_png(ref, path, player_trans):
    from PIL import Image
    img = Image.open(io.BytesIO(fetch(ref, "art/textures/skins/" + path))).convert("RGBA")
    # Shared structure atlases are 2048 px; scenery reads fine at 1024.
    if max(img.size) > 1024:
        img.thumbnail((1024, 1024), Image.LANCZOS)
    if player_trans:
        bg = Image.new("RGBA", img.size, GAIA_WHITE + (255,))
        bg.alpha_composite(img)
        img = bg
    else:
        img.putalpha(255)
    out = io.BytesIO()
    img.convert("RGB").save(out, "PNG")
    return out.getvalue()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--godot", required=True)
    ap.add_argument("--ref", default="master")
    args = ap.parse_args()
    work = tempfile.mkdtemp(prefix="scenery_")
    open(os.path.join(work, "project.godot"), "w").write("config_version=5\n")
    shutil.copy(os.path.join(ROOT, "tools", "dae_to_glb.gd"), work)
    jobs = []  # (out name, [(dae, png), ...])
    types = {}
    for name in scenery_templates():
        tmpl = ET.fromstring(fetch(args.ref, f"simulation/templates/structures/{name}.xml"))
        actor = (tmpl.findtext("VisualActor/Actor") or "").strip()
        variants, tex, material, props = parse_actor(args.ref, actor)
        if not variants or not tex:
            print(f"{name}: no mesh or texture in {actor}", file=sys.stderr)
            continue
        pt = material.startswith("player_trans")
        parts_extra = [(pmesh, ptex, False) for pmesh, ptex in props]
        files = []
        for vi, (mesh,) in enumerate(variants):
            parts = [(mesh, tex, pt)] + parts_extra
            local = []
            for mesh_path, tex_path, trans in parts:
                dae = "m_" + mesh_path.replace("/", "_")
                png = "t_" + ("pt_" if trans else "") + os.path.splitext(tex_path.replace("/", "_"))[0] + ".png"
                if not os.path.exists(os.path.join(work, dae)):
                    open(os.path.join(work, dae), "wb").write(fetch(args.ref, "art/meshes/" + mesh_path))
                if not os.path.exists(os.path.join(work, png)):
                    open(os.path.join(work, png), "wb").write(texture_png(args.ref, tex_path, trans))
                local.append((dae, png))
            out = f"{name}_{vi}.glb"
            jobs.append((out, local))
            files.append("scenery/" + out)
        print(f"{name}: {actor} -> {len(files)} variant(s), texture {tex}", file=sys.stderr)
        types[name] = files
    subprocess.run([args.godot, "--headless", "--path", work, "--import"], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dest = os.path.join(ROOT, "assets", "models", "scenery")
    os.makedirs(dest, exist_ok=True)
    for out, local in jobs:
        cmd = [args.godot, "--headless", "--path", work, "-s", "dae_to_glb.gd", "--"]
        for dae, png in local:
            cmd += [dae, png]
        cmd += [os.path.join(work, out), "prop"]
        subprocess.run(cmd, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if not os.path.exists(os.path.join(work, out)):
            sys.exit(f"conversion failed: {out}")
        shutil.copy(os.path.join(work, out), dest)
    path = os.path.join(ROOT, "data", "scenery_actors.json")
    with open(path, "w") as f:
        json.dump({"source": f"0ad/0ad@{args.ref} simulation/templates/structures, art/actors/props/special/eyecandy",
                   "types": types}, f, indent=1)
        f.write("\n")
    shutil.rmtree(work)
    print(f"{len(jobs)} GLBs for {len(types)} scenery types -> {dest}", file=sys.stderr)


if __name__ == "__main__":
    main()
