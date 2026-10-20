#!/usr/bin/env python3
"""Recover 0 A.D.'s specular maps for the unit and building GLBs.

0 A.D.'s *_spec materials (model_common.fs USE_SPECULAR_MAP) add a Blinn-Phong
highlight sunColor * specTex.rgb * pow(N.H, specPow), with specPow the y of
the material's effectSettings (50 for most); a material without a specTex has
no highlight. shaders/player_color.gdshader draws the same term in its light()
from a map per skin:

  red    the specTex brightness (Rec. 601 luma; 0 A.D.'s specular maps are grey)
  green  specPow / 255

It pairs skins with specTex the way extract_normal_maps.py pairs normTex, and
matches each embedded GLB image to its skin with extract_player_masks.py.
Writes:

  assets/textures/specular_maps/<specTex>_<w>x<h>_p<specPow>.png
  data/specular_maps.json   {"buildings/barracks_tex0": <res path>}

Usage: python3 tools/extract_specular_maps.py [--ref master | --zip public.zip] [--only id,id] [--max 256]
"""
import argparse
import concurrent.futures as cf
import json
import zipfile
import os
import sys
import xml.etree.ElementTree as ET

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import extract_normal_maps as N  # noqa: E402  (texture_blocks, decode)
import extract_player_masks as M  # noqa: E402  (fetch, glb_images, match_score)

SPEC_DIR = "assets/textures/specular_maps"
OUT_JSON = "data/specular_maps.json"
MATCH_LIMIT = 24.0
DEFAULT_SPEC_POW = 50.0


def is_flat(f):
    b = os.path.basename(f).lower()
    return b.startswith(("null", "default_spec", "black")) or not b


def spec_power(xml):
    """effectSettings.y of a 0 A.D. material, the Blinn-Phong exponent."""
    try:
        root = ET.fromstring(xml)
    except (ET.ParseError, TypeError):
        return DEFAULT_SPEC_POW
    for u in root.iter("uniform"):
        if u.get("name") == "effectSettings":
            v = (u.get("value") or "").split()
            if len(v) > 1:
                return float(v[1])
    return DEFAULT_SPEC_POW


def to_specular(img, size, spec_pow):
    """RGB: specTex brightness in red, specPow / 255 in green."""
    r, g, b, _ = img.convert("RGBA").resize(size, Image.LANCZOS).split()
    lum = Image.merge("RGB", (r, g, b)).convert("L")
    return Image.merge("RGB", (lum, Image.new("L", size, int(round(spec_pow))), Image.new("L", size, 0)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default="master")
    ap.add_argument("--only", default="")
    ap.add_argument("--zip", help="read a 0 A.D. release public.zip instead of GitHub")
    ap.add_argument("--max", type=int, default=256, help="largest side of a written map")
    args = ap.parse_args()
    if args.zip:
        M.ZIP = zipfile.ZipFile(args.zip)
    os.chdir(ROOT)

    models = json.load(open("assets/models/manifest.json"))["models"]
    only = set(filter(None, args.only.split(",")))
    todo = {k: v for k, v in models.items()
            if v["file"].split("/")[1] in ("units", "buildings") and (not only or k in only)}

    actors = {s[6:] for v in todo.values() for s in v["sources"] if s.startswith("actor:")}
    with cf.ThreadPoolExecutor(16) as ex:
        actor_xml = dict(zip(actors, ex.map(lambda a: M.fetch(args.ref, a), actors)))

    # baseTex -> (specTex, material). A block naming both pairs them; a block
    # with only a specTex applies to every skin of the actor. The actor's
    # <material> decides whether 0 A.D. uses it at all.
    variant_xml, votes = {}, {}
    for xml in actor_xml.values():
        if not xml:
            continue
        mat, blocks, files = N.texture_blocks(xml)
        seen, pending = set(), list(files)
        while pending:
            need = [f for f in pending if f not in variant_xml]
            with cf.ThreadPoolExecutor(16) as ex:
                variant_xml.update(zip(need, ex.map(lambda f: M.fetch(args.ref, "art/variants/" + f), need)))
            nxt = []
            for f in pending:
                if f in seen or not variant_xml.get(f):
                    continue
                seen.add(f)
                vmat, vblocks, vfiles = N.texture_blocks(variant_xml[f])
                mat = mat or vmat
                blocks += vblocks
                nxt += vfiles
            pending = nxt
        if "spec" not in mat:
            continue
        wide = [b["specTex"] for b in blocks if "specTex" in b and "baseTex" not in b]
        for b in blocks:
            base = b.get("baseTex")
            spec = b.get("specTex") or (wide[0] if wide else None)
            if base and spec and not is_flat(spec):
                votes.setdefault(base, {}).setdefault((spec, mat), 0)
                votes[base][(spec, mat)] += 1
    spec_of = {base: max(vs.items(), key=lambda kv: kv[1])[0] for base, vs in votes.items()}

    need_skins = {s[5:] for v in todo.values() for s in v["sources"] if s.startswith("skin:")}
    specs = {spec_of[s][0] for s in need_skins if s in spec_of}
    mats = {spec_of[s][1] for s in need_skins if s in spec_of}
    with cf.ThreadPoolExecutor(16) as ex:
        skin_img = dict(zip(need_skins, ex.map(lambda s: N.decode(M.fetch(args.ref, "art/textures/skins/" + s)), need_skins)))
        spec_img = dict(zip(specs, ex.map(lambda s: N.decode(M.fetch(args.ref, "art/textures/skins/" + s)), specs)))
        pow_of = dict(zip(mats, ex.map(lambda m: spec_power(M.fetch(args.ref, "art/materials/" + m)), mats)))
    print(f"skins {len(need_skins)}, with a specular map {sum(1 for s in need_skins if s in spec_of)}, "
          f"specular maps fetched {sum(1 for x in spec_img.values() if x)}/{len(specs)}, powers {sorted(set(pow_of.values()))}")

    out = json.load(open(OUT_JSON)) if only and os.path.exists(OUT_JSON) else {}
    written, unmatched = set(), 0
    for mid, v in sorted(todo.items()):
        cands = [s[5:] for s in v["sources"] if s.startswith("skin:") and skin_img.get(s[5:]) is not None]
        if not any(s in spec_of for s in cands):
            continue
        cat = v["file"].split("/")[1]
        base = os.path.basename(v["file"])[:-4]
        for name, baked in M.glb_images(os.path.join("assets", v["file"])):
            best = None
            for s in cands:
                src = skin_img[s]
                r = src if src.size == baked.size else src.resize(baked.size, Image.LANCZOS)
                sc = M.match_score(baked, r)
                if best is None or sc < best[0]:
                    best = (sc, s)
            if best is None or best[0] > MATCH_LIMIT:
                unmatched += 1
                continue
            if best[1] not in spec_of:
                continue
            spec, mat = spec_of[best[1]]
            if spec_img.get(spec) is None:
                continue
            w, h = baked.size
            k = min(1.0, args.max / max(w, h))
            size = (max(1, int(w * k)), max(1, int(h * k)))
            p = pow_of.get(mat, DEFAULT_SPEC_POW)
            rel = f"{os.path.splitext(spec)[0]}_{size[0]}x{size[1]}_p{int(p)}.png"
            if rel not in written:
                path = os.path.join(SPEC_DIR, rel)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                to_specular(spec_img[spec], size, p).save(path, optimize=True)
                written.add(rel)
            out[f"{cat}/{base}_{name}"] = "res://" + os.path.join(SPEC_DIR, rel)

    with open(OUT_JSON, "w") as f:
        json.dump(out, f, indent=0, sort_keys=True)
        f.write("\n")
    print(f"{len(out)} images with a specular map, {len(written)} specular map files, {unmatched} images matched no skin")


if __name__ == "__main__":
    main()
