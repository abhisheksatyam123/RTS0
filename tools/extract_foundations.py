#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the art
it converts is CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

0 A.D. draws a structure under construction with its template's
VisualActor/FoundationActor (structures/fndn_*.xml: the ground plan, stakes,
bricks and tools), and Foundation.js switches that actor to its "scaffold"
variant once building starts, which props a construction scaffold
(props/structures/construction/scaf_*.xml). RTS0 drew one of eight generic
foundations and two scaffolds by building type. This tool converts each
FoundationActor the RTS0 civs' structures name (0.28 release public.zip) and
the scaffolds their "scaffold" variants prop, and writes
data/foundations.json:

  {"structures": {"<civ code>": {"<BuildingType>": "<foundation GLB id>"}},
   "foundations": {"<foundation GLB id>": {"scale": s, "origin": [x, y, z],
                   "scaffold": {"id": "<scaffold GLB id>", "scale": s, "origin": [x, y, z]}}},
   "frames": {"<building GLB id>": {"metre": m, "origin": [x, y, z]}}}

`scale` is GLB units per 0 A.D. metre and `origin` is where the actor's origin
(the entity's position) lies in GLB units, for the foundations, the scaffolds
and every building GLB drawn from a structure actor (whose scale the
extractors fitted), so the 3D view can lay a foundation and its scaffold
under any building model as 0 A.D. does. Structures come from
data/reference/templates.json (tools/extract_0ad_reference.py).

Usage:
  python3 -I tools/extract_foundations.py --zip <0.28 public.zip> [--check]
"""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_assets import DEFAULT_ZIP, ZIP_STRUCTURES, Vfs, compile_asset, compose_actor, mat_apply, trs  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS_DIR = os.path.join(REPO, "assets")
MANIFEST = os.path.join(ASSETS_DIR, "models", "manifest.json")
REFERENCE = os.path.join(REPO, "data", "reference", "templates.json")
OUT_JSON = os.path.join(REPO, "data", "foundations.json")
SCALE = 0.21  # GLB units per metre for the converted foundations and scaffolds
SCAF_PREFIX = "props/structures/construction/"


def template_value(zf, names, name, path, depth=0):
    """A template's value at `path`, following its parent chain (a|b layers a on b)."""
    if depth > 24:
        return None
    p = f"simulation/templates/{name}.xml"
    if p not in names:
        return None
    root = ET.fromstring(zf.read(p))
    el = root.find(path)
    if el is not None and el.text and el.text.strip():
        return el.text.strip()
    for parent in (root.get("parent") or "").split("|"):
        if parent:
            v = template_value(zf, names, parent, path, depth + 1)
            if v:
                return v
    return None


def foundation_glb_id(actor):
    """structures/fndn_4x4.xml -> fndn_4x4; structures/carthaginians/fndn_house.xml -> fndn_carthaginians_house"""
    stem = os.path.splitext(actor)[0]
    parts = stem.split("/")
    base = parts[-1]
    if not base.startswith("fndn_"):
        base = "fndn_" + base
    if len(parts) > 2:
        base = "fndn_" + parts[-2] + "_" + base[len("fndn_"):]
    return base


def scaffold_glb_id(actor):
    """props/structures/construction/scaf_4x4.xml -> scaffold_up_4x4 (extract_upgrade_scaffolds.py's ids)."""
    base = os.path.splitext(os.path.basename(actor))[0]
    return "scaffold_up_" + (base[len("scaf_"):] if base.startswith("scaf_") else base)


def scaffold_of(vfs, actor_path):
    """The construction scaffold the actor's "scaffold" variant props, or None."""
    try:
        root = ET.fromstring(vfs.read(actor_path))
    except (KeyError, ET.ParseError):
        return None
    for var in root.iter("variant"):
        if (var.get("name") or "").lower() != "scaffold":
            continue
        for prop in var.iter("prop"):
            a = (prop.get("actor") or "").replace("\\", "/")
            if a.startswith(SCAF_PREFIX):
                return a
    return None


class Bounds:
    """Actor bounds as compose_actor builds them, cached by actor path."""

    def __init__(self, vfs):
        self.vfs = vfs
        self.cache = {}

    def __call__(self, path):
        if path not in self.cache:
            parts, _ = compose_actor(self.vfs, path, 0, None, set(), None)
            xs, ys, zs = [], [], []
            for part in parts:
                xf = part.transform if part.transform is not None else trs((0, 0, 0), (0, 0, 0, 1))
                for p, _n in part.mesh_data["verts"]:
                    x, y, z = mat_apply(xf, p)
                    xs.append(x); ys.append(y); zs.append(z)
            self.cache[path] = (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)) if xs else None
        return self.cache[path]


def origin_in_glb(b, scale):
    """GLB position of the actor origin: the extractors move (cx, min y, cz) to 0."""
    return [round(-(b[0] + b[1]) / 2 * scale, 4), round(-b[2] * scale, 4), round(-(b[4] + b[5]) / 2 * scale, 4)]


def frames(bounds, models):
    """Metre scale and actor origin of every building GLB drawn from a structure actor."""
    out = {}
    for gid in sorted(models):
        m = models[gid]
        if not m.get("file", "").startswith("models/buildings/") or "size_meters" not in m:
            continue
        actors = [s[len("actor:"):] for s in m.get("sources", []) if s.startswith("actor:art/actors/structures/")]
        if not actors:
            continue
        # The structure's own actor: the one whose name the GLB carries, else the first.
        actor = next((a for a in actors if os.path.splitext(os.path.basename(a))[0] in gid), actors[0])
        b = bounds(actor)
        if not b:
            continue
        dim = (b[1] - b[0], b[3] - b[2], b[5] - b[4])
        size = m["size_meters"]
        r = sorted(size[i] / dim[i] for i in range(3) if dim[i] > 1e-6)
        r = r[len(r) // 2]
        out[gid] = {"metre": round(r, 5), "origin": origin_in_glb(b, r)}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=os.path.normpath(DEFAULT_ZIP))
    ap.add_argument("--check", action="store_true", help="fail if data/foundations.json is stale")
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
                actor = tmpl and template_value(zf, names, tmpl, "VisualActor/FoundationActor")
                if not actor or not actor.startswith("structures/") or "fndn" not in actor:
                    continue
                fid = foundation_glb_id(actor)
                wanted[fid] = "art/actors/" + actor
                structures.setdefault(civ, {})[btype] = fid
        foundations, new_entries = {}, {}
        for fid, actor in sorted(wanted.items()):
            fb = bounds(actor)
            if not fb:
                continue
            e = {"scale": SCALE, "origin": origin_in_glb(fb, SCALE)}
            scaf = scaffold_of(vfs, actor)
            if scaf:
                sid = scaffold_glb_id(scaf)
                sb = bounds("art/actors/" + scaf)
                s_scale = ZIP_STRUCTURES[sid][2] if sid in ZIP_STRUCTURES else SCALE
                if sb:
                    e["scaffold"] = {"id": sid, "scale": s_scale, "origin": origin_in_glb(sb, s_scale)}
                    if sid not in models and not args.check:
                        new_entries[sid] = compile_asset(vfs, sid, "art/actors/" + scaf, "scale", s_scale, None,
                                                         ASSETS_DIR, "models/buildings")
            if fid not in models and not args.check:
                new_entries[fid] = compile_asset(vfs, fid, actor, "scale", SCALE, None, ASSETS_DIR, "models/buildings")
            foundations[fid] = e
        if new_entries:
            models.update(new_entries)
            raw = open(MANIFEST).read()
            text = json.dumps(manifest, indent=1, sort_keys=True)
            with open(MANIFEST, "w") as f:
                f.write(text + ("\n" if raw.endswith("\n") else ""))
        table = {"structures": structures, "foundations": foundations, "frames": frames(bounds, models)}
    text = json.dumps(table, indent=1, sort_keys=True) + "\n"
    if args.check:
        missing = [k for k in foundations if k not in models]
        with open(OUT_JSON) as f:
            if f.read() != text or missing:
                raise SystemExit("data/foundations.json is stale: rerun tools/extract_foundations.py")
        print(f"{len(foundations)} foundations, {len(table['frames'])} building frames: up to date")
        return
    with open(OUT_JSON, "w") as f:
        f.write(text)
    print(f"{len(foundations)} foundations ({len(new_entries)} GLBs converted), "
          f"{len(table['frames'])} building frames -> {os.path.relpath(OUT_JSON, REPO)}")


if __name__ == "__main__":
    main()
