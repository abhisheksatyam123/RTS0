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

The props' actors (heads, helmets, shields, capes) have such groups too; each
GLB image is matched against all of them and takes the group of its skin. A
skin without player colour (no key in data/player_masks.json) is written with
alpha 0 as <skin>_<w>x<h>_plain.png. A head's objectcolor material tints its
hair by a colour its actor picks by variant too: those skins keep 255 - alpha as
<skin>_<w>x<h>_obj.png (one skin is enough), and the entry adds "colors" (0-255
RGB) and "color_freq". shaders/player_color.gdshader picks a layer and a colour
per entity from the frequencies. At most MAX_LAYERS skins per group, the most
frequent.

A horse's coat group sets the skin and the mane and tail props together
(art/variants/quadraped/horse/*.xml): the coats go to the body image and, coat by
coat, the hair actor's skin to the hair image (its first most frequent: 0 A.D.
picks the hair's colour apart, from two), alpha kept for the alpha test, as
<skin>_<w>x<h>_cut.png. Both entries carry the same "salt", so one draw picks a
coat and its hair. At most MAX_COATS coats.

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
MAX_COATS = 12
HAIR_PROPS = "props/horse/horse_hair_"


def body_group(vfs, actor_path, cache):
    """({skin: frequency}, {(r, g, b): frequency} or None) of the actor's first group
    whose variants only set baseTex, and for an objectcolor material (heads: 0.28's
    hair colours) its group of <color> variants; then a single skin is enough."""
    try:
        root = ET.fromstring(vfs.read(actor_path))
    except (KeyError, ET.ParseError):
        return None, None
    if root.tag == "qualitylevels" and root.find("inline") is not None:
        root = root.find("inline")
    colors = None
    if "objectcolor" in (root.findtext("material") or ""):
        for g in root.findall("group"):
            found = {}
            for var in g.findall("variant"):
                r = resolve_variant_chain(vfs, var, cache)
                c = (r.findtext("color") or "").split() if r is not None else []
                if len(c) == 3 and r.find("mesh") is None and r.find("textures") is None:
                    rgb = tuple(int(float(x)) for x in c)
                    found[rgb] = found.get(rgb, 0) + int(float(var.get("frequency") or r.get("frequency") or 1))
            if found:
                colors = found
                break
    first = None
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
            return skins, colors
        if skins and first is None:
            first = skins
    return (first, colors) if colors and first else (None, None)


def base_tex(r):
    return next((t.get("file").strip() for t in r.iter("texture")
                 if (t.get("name") or "baseTex") == "baseTex" and t.get("file")), None)


def hair_skin(vfs, actor, cache):
    """The hair actor's first most frequent skin."""
    try:
        root = ET.fromstring(vfs.read("art/actors/" + actor))
    except (KeyError, ET.ParseError):
        return None
    for g in root.findall("group"):
        best = None
        for var in g.findall("variant"):
            r = resolve_variant_chain(vfs, var, cache)
            tex = base_tex(r) if r is not None else None
            f = int(float(var.get("frequency") or (r.get("frequency") if r is not None else None) or 1))
            if tex and (best is None or f > best[1]):
                best = (tex, f)
        if best:
            return best[0]
    return None


def coat_group(vfs, actor_path, cache):
    """[(coat skin, frequency, hair skin)] of the actor's group whose variants set the
    skin with only horse hair props, or None."""
    try:
        root = ET.fromstring(vfs.read(actor_path))
    except (KeyError, ET.ParseError):
        return None
    for g in root.findall("group"):
        coats = []
        for var in g.findall("variant"):
            r = resolve_variant_chain(vfs, var, cache)
            if r is None or r.find("mesh") is not None:
                break
            props = [p.get("actor") or "" for p in r.iter("prop")]
            tex = base_tex(r)
            if not tex or not props or not all(a.startswith(HAIR_PROPS) for a in props):
                break
            hair = hair_skin(vfs, props[0], cache)
            if not hair:
                break
            coats.append((tex, int(float(var.get("frequency") or r.get("frequency") or 1)), hair))
        else:
            if len({c[0] for c in coats}) > 1:
                return sorted(coats, key=lambda c: (-c[1], c[0]))[:MAX_COATS]
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
            # The unit actor's body group, then its props' (heads, helmets, shields...).
            # Animals too: most 0.28 fauna actors swap coats the same way (deer, horses, cattle).
            actors = [s[len("actor:"):] for s in m.get("sources", [])
                      if s.startswith(("actor:art/actors/units/", "actor:art/actors/props/units/",
                                       "actor:art/actors/fauna/"))]
            actors.sort(key=lambda a: not a.startswith("art/actors/units/"))
            # Each group: the skins its images match against, and per matched skin the
            # layers ([(skin, frequency)]), colours, salt and alpha kind.
            groups = []
            for a in actors:
                coats = coat_group(vfs, a, cache)
                if coats:
                    body = {c[0]: load_skin(zf, names, c[0], skins) for c in coats}
                    hair = {c[2]: load_skin(zf, names, c[2], skins) for c in coats}
                    if all(i is not None for i in list(body.values()) + list(hair.values())):
                        salt = a + "#coat"
                        groups.append({"imgs": body, "layers": [(c[0], c[1]) for c in coats], "salt": salt})
                        groups.append({"imgs": hair, "layers": [(c[2], c[1]) for c in coats], "salt": salt,
                                       "kind": "cut"})
                        continue
                group, colors = body_group(vfs, a, cache)
                if not group:
                    continue
                group = dict(sorted(group.items(), key=lambda kv: (-kv[1], kv[0]))[:MAX_LAYERS])
                imgs = {s: load_skin(zf, names, s, skins) for s in group}
                imgs = {s: i for s, i in imgs.items() if i is not None}
                if len(imgs) >= (1 if colors else 2) and all(set(imgs) != set(g["imgs"]) for g in groups):
                    if colors:
                        colors = dict(sorted(colors.items(), key=lambda kv: (-kv[1], kv[0]))[:MAX_LAYERS])
                    groups.append({"imgs": imgs, "layers": [(s, group[s]) for s in imgs], "colors": colors})
            if not groups:
                continue
            base = os.path.basename(m["file"])[:-4]
            for name, baked in glb_images(os.path.join(REPO, "assets", m["file"])):
                key = f"units/{base}_{name}"
                best = min((match_score(baked, i if i.size == baked.size else i.resize(baked.size, Image.LANCZOS)), gi, s)
                           for gi, g in enumerate(groups) for s, i in g["imgs"].items())
                if best[0] > MATCH_LIMIT:
                    continue
                g = groups[best[1]]
                colors = g.get("colors")
                w, h = baked.size
                # alpha: the player-colour mask, the object-colour (hair) mask, the
                # alpha-tested coverage, or 0
                kind = g.get("kind") or ("obj" if colors else ("mask" if key in masked else "plain"))
                layers = []
                for s, _f in g["layers"]:
                    rel = f"{os.path.splitext(s)[0]}_{w}x{h}{'' if kind == 'mask' else '_' + kind}.png"
                    files[rel] = (s, (w, h), kind)
                    layers.append("res://assets/textures/unit_variants/" + rel)
                table[key] = {"layers": layers, "freq": [f for _s, f in g["layers"]]}
                if g.get("salt"):
                    table[key]["salt"] = g["salt"]
                if colors:
                    table[key]["colors"] = [list(c) for c in colors]
                    table[key]["color_freq"] = list(colors.values())
        for rel, (s, size, kind) in sorted(files.items()):
            path = os.path.join(TEX_DIR, rel)
            if args.check:
                if not os.path.isfile(path):
                    stale.append(path)
                continue
            if os.path.isfile(path):
                continue
            img = skins[s] if skins[s].size == size else skins[s].resize(size, Image.LANCZOS)
            r, g, b, a = img.split()
            if kind == "cut":
                pass  # the skin's own alpha, for the alpha test
            elif kind == "plain":
                a = a.point(lambda v: 0)
            elif kind == "obj":  # model_common.fs: color *= mix(objectColor, 1, tex.a)
                a = a.point(lambda v: 255 - v)
            else:
                a = a.point(lambda v: 255 - v if v < 250 else 0)
            out = Image.merge("RGBA", (r, g, b, a))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            out.save(path, optimize=True)
    text = json.dumps(table, indent=1, sort_keys=True) + "\n"
    if args.check:
        with open(OUT_JSON) as f:
            if f.read() != text or stale:
                raise SystemExit("data/unit_variants.json or its textures are stale: rerun tools/extract_unit_variants.py")
        print(f"{len(table)} unit images, {len(files)} skins: up to date")
        return
    with open(OUT_JSON, "w") as f:
        f.write(text)
    print(f"{len(table)} unit images, {len(files)} skins -> {os.path.relpath(OUT_JSON, REPO)}")


if __name__ == "__main__":
    main()
