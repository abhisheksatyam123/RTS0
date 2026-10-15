#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the art
it converts is CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

A destroyed 0 A.D. structure leaves the entity its template names in
Health/SpawnEntityOnDeath ("decay|rubble/rubble_stone_4x4"): a rubble template
whose VisualActor is a structures/destruct_*.xml actor (stone rubble sized to
the footprint, the civic centres' and docks' own ruins, a fallow field). RTS0
drew one of three generic rubble models. This tool converts each rubble actor
the RTS0 civs' structures name (0.28 release public.zip) and writes
data/rubble.json:

  {"structures": {"<civ code>": {"<BuildingType>": "<rubble GLB id>"}},
   "rubble": {"<rubble GLB id>": {"scale": s, "origin": [x, y, z]}}}

`scale` is GLB units per 0 A.D. metre and `origin` is where the actor's origin
(the entity's position) lies in GLB units, so the 3D view lays the rubble where
the structure stood through the building GLB's frame in data/foundations.json.
Structures come from data/reference/templates.json.

Usage:
  python3 -I tools/extract_rubble.py --zip <0.28 public.zip> [--check]
"""

import argparse
import json
import os
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_assets import DEFAULT_ZIP, Vfs, compile_asset  # noqa: E402
from extract_foundations import Bounds, origin_in_glb, template_value  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS_DIR = os.path.join(REPO, "assets")
MANIFEST = os.path.join(ASSETS_DIR, "models", "manifest.json")
REFERENCE = os.path.join(REPO, "data", "reference", "templates.json")
OUT_JSON = os.path.join(REPO, "data", "rubble.json")
SCALE = 0.21  # GLB units per metre, as the foundations


def rubble_glb_id(actor):
    """structures/destruct_stone_4x4.xml -> rubble_stone_4x4, destruct_hele_dock_rubble.xml -> rubble_hele_dock,
    plot_field_fallow.xml -> rubble_plot_field_fallow"""
    base = os.path.splitext(os.path.basename(actor))[0]
    base = base[len("destruct_"):] if base.startswith("destruct_") else base
    return "rubble_" + (base[:-len("_rubble")] if base.endswith("_rubble") else base)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=os.path.normpath(DEFAULT_ZIP))
    ap.add_argument("--check", action="store_true", help="fail if data/rubble.json is stale")
    args = ap.parse_args()
    with open(REFERENCE) as f:
        reference = json.load(f)["structures"]
    with open(MANIFEST) as f:
        manifest = json.load(f)
    models = manifest["models"]
    with zipfile.ZipFile(args.zip) as zf:
        names = set(zf.namelist())
        vfs = Vfs(zf)
        bounds = Bounds(vfs)
        structures, wanted = {}, {}
        for civ in sorted(reference):
            for btype, entry in sorted(reference[civ].items(), key=lambda kv: int(kv[0])):
                tmpl = entry.get("template")
                spawn = tmpl and template_value(zf, names, tmpl, "Health/SpawnEntityOnDeath")
                if not spawn:
                    continue
                rubble = next((p for p in spawn.split("|") if p.startswith("rubble/")), None)
                actor = rubble and template_value(zf, names, rubble, "VisualActor/Actor")
                if not actor:
                    continue
                rid = rubble_glb_id(actor)
                wanted[rid] = "art/actors/" + actor
                structures.setdefault(civ, {})[btype] = rid
        table, new_entries = {}, {}
        for rid, actor in sorted(wanted.items()):
            b = bounds(actor)
            if not b:
                continue
            if rid not in models and not args.check:
                new_entries[rid] = compile_asset(vfs, rid, actor, "scale", SCALE, None, ASSETS_DIR, "models/buildings")
            table[rid] = {"scale": SCALE, "origin": origin_in_glb(b, SCALE)}
        if new_entries:
            models.update(new_entries)
            raw = open(MANIFEST).read()
            text = json.dumps(manifest, indent=1, sort_keys=True)
            with open(MANIFEST, "w") as f:
                f.write(text + ("\n" if raw.endswith("\n") else ""))
    structures = {civ: {bt: rid for bt, rid in rows.items() if rid in table} for civ, rows in structures.items()}
    text = json.dumps({"structures": structures, "rubble": table}, indent=1, sort_keys=True) + "\n"
    if args.check:
        missing = [k for k in table if k not in models]
        with open(OUT_JSON) as f:
            if f.read() != text or missing:
                raise SystemExit("data/rubble.json is stale: rerun tools/extract_rubble.py")
        print(f"{len(table)} rubble models: up to date")
        return
    with open(OUT_JSON, "w") as f:
        f.write(text)
    print(f"{len(table)} rubble models ({len(new_entries)} GLBs converted) -> {os.path.relpath(OUT_JSON, REPO)}")


if __name__ == "__main__":
    main()
