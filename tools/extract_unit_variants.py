#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the art
it copies is CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

0 A.D. picks each entity's actor variants at random, by their frequencies
(CUnit / CObjectBase::CalculateVariationKey). Most unit actors have a group whose
variants only swap the body skin (art/actors/units/athenians/infantry_spearman_b.xml:
six tunics), so a crowd of one unit type wears several. The unit GLBs bake one
of them. This tool finds that group for each unit GLB, matches the GLB's embedded
images against its skins to find the body (as tools/extract_player_masks.py
does), and writes:

  assets/textures/unit_variants/<skin>_<w>x<h>.png   the skin at the body image's size,
      RGB as 0 A.D. draws it and the player-colour mask in alpha (255 - alpha where
      the skin's alpha is below 250, as player_masks)
  data/unit_variants.json  {"units/<glb>_<image>": {"layers": [res paths], "freq": [n, ...]}}

Only bodies the player-colour shader draws (a key in data/player_masks.json) are
listed; shaders/player_color.gdshader picks a layer per entity from the
frequencies. At most MAX_LAYERS skins per group, the most frequent.

Usage:
  python3 -I tools/extract_unit_variants.py --zip <0.28 public.zip> [--check]
"""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET
import zipfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_assets import DEFAULT_ZIP, Vfs, decode_dds, resolve_variant_chain  # noqa: E402
from extract_player_masks import MATCH_LIMIT, glb_images, match_score  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
MASKS = os.path.join(REPO, "data", "player_masks.json")
OUT_JSON = os.path.join(REPO, "data", "unit_variants.json")
TEX_DIR = os.path.join(REPO, "assets", "textures", "unit_variants")
MAX_LAYERS = 8


def body_group(vfs, actor_path, cache):
    """{skin: frequency} of the actor's first group whose variants only set baseTex."""
    try:
        root = ET.fromstring(vfs.read(actor_path))
    except (KeyError, ET.ParseError):
        return None
    if root.tag == "qualitylevels" and root.find("inline") is not None:
        root = root.find("inline")
    for g in root.findall("group"):
        skins, only_tex = {}, True
        for var in g.findall("variant"):
            r = resolve_variant_chain(vfs, var, cache)
            if r is None:
                continue
            if r.find("mesh") is not None or r.find("props") is not None:
                only_tex = False
                break
            for t in r.iter("texture"):
                if (t.get("name") or "baseTex") == "baseTex" and t.get("file"):
                    f = t.get("file").strip()
                    skins[f] = skins.get(f, 0) + int(float(var.get("frequency") or r.get("frequency") or 1))
        if only_tex and len(skins) > 1:
            return skins
    return None


def load_skin(zf, names, skin, cache):
    if skin not in cache:
        img = None
        for p in (f"art/textures/skins/{skin}.cached.dds", f"art/textures/skins/{skin}"):
            if p in names:
                raw = zf.read(p)
                img = decode_dds(raw) if raw[:4] == b"DDS " else None
                break
        cache[skin] = img.convert("RGBA") if img is not None else None
    return cache[skin]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=os.path.normpath(DEFAULT_ZIP))
    ap.add_argument("--check", action="store_true", help="fail if data/unit_variants.json is stale")
    args = ap.parse_args()
    with open(MANIFEST) as f:
        models = json.load(f)["models"]
    with open(MASKS) as f:
        masked = json.load(f)
    table, files, stale = {}, {}, []
    with zipfile.ZipFile(args.zip) as zf:
        names = set(zf.namelist())
        vfs, cache, skins = Vfs(zf), {}, {}
        for gid in sorted(models):
            m = models[gid]
            if not m.get("file", "").startswith("models/units/"):
                continue
            actors = [s[len("actor:"):] for s in m.get("sources", []) if s.startswith("actor:art/actors/units/")]
            group = actors and body_group(vfs, actors[0], cache)
            if not group:
                continue
            group = dict(sorted(group.items(), key=lambda kv: (-kv[1], kv[0]))[:MAX_LAYERS])
            imgs = {s: load_skin(zf, names, s, skins) for s in group}
            imgs = {s: i for s, i in imgs.items() if i is not None}
            if len(imgs) < 2:
                continue
            base = os.path.basename(m["file"])[:-4]
            for name, baked in glb_images(os.path.join(REPO, "assets", m["file"])):
                key = f"units/{base}_{name}"
                if key not in masked:
                    continue
                best = min((match_score(baked, i if i.size == baked.size else i.resize(baked.size, Image.LANCZOS)), s)
                           for s, i in imgs.items())
                if best[0] > MATCH_LIMIT:
                    continue
                w, h = baked.size
                layers = []
                for s in imgs:
                    rel = f"{os.path.splitext(s)[0]}_{w}x{h}.png"
                    files[rel] = (s, (w, h))
                    layers.append("res://assets/textures/unit_variants/" + rel)
                table[key] = {"layers": layers, "freq": [group[s] for s in imgs]}
                break
        for rel, (s, size) in sorted(files.items()):
            path = os.path.join(TEX_DIR, rel)
            if args.check:
                if not os.path.isfile(path):
                    stale.append(path)
                continue
            if os.path.isfile(path):
                continue
            img = skins[s] if skins[s].size == size else skins[s].resize(size, Image.LANCZOS)
            r, g, b, a = img.split()
            out = Image.merge("RGBA", (r, g, b, a.point(lambda v: 255 - v if v < 250 else 0)))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            out.save(path, optimize=True)
    text = json.dumps(table, indent=1, sort_keys=True) + "\n"
    if args.check:
        with open(OUT_JSON) as f:
            if f.read() != text or stale:
                raise SystemExit("data/unit_variants.json or its textures are stale: rerun tools/extract_unit_variants.py")
        print(f"{len(table)} unit bodies, {len(files)} skins: up to date")
        return
    with open(OUT_JSON, "w") as f:
        f.write(text)
    print(f"{len(table)} unit bodies, {len(files)} skins -> {os.path.relpath(OUT_JSON, REPO)}")


if __name__ == "__main__":
    main()
