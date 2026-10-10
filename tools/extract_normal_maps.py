#!/usr/bin/env python3
"""Recover 0 A.D.'s normal maps for the unit and building GLBs.

0 A.D.'s *_norm materials (model_common.fs USE_NORMAL_MAP) bend the lighting
with a tangent-space normTex paired with the skin in the actor's <textures>.
extract_0ad_assets.py baked only the albedo, so models lit flat. This tool
fetches each model's 0 A.D. actors and skins from GitHub, matches every
embedded GLB image to the skin it was baked from (as extract_player_masks.py
does), and for skins whose actor pairs them with a real normTex writes:

  assets/textures/normal_maps/<normTex>_<w>x<h>.png   RGB, OpenGL convention (green up)
  data/normal_maps.json   {"buildings/barracks_tex0": <res path>}

0 A.D. samples its normal maps upside down relative to the image as shown
(ntex green points down the image), so green is flipped for Godot. Flat
placeholders (default_norm, null textures) and actors whose material has no
normal mapping (neither *_norm* nor *_parallax*) are skipped.

Usage: python3 tools/extract_normal_maps.py [--ref master] [--only id,id] [--max 512]
"""
import argparse
import concurrent.futures as cf
import io
import json
import os
import sys
import xml.etree.ElementTree as ET

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import extract_0ad_assets as X  # noqa: E402  (decode_dds)
import extract_player_masks as M  # noqa: E402  (fetch, glb_images, match_score)

NORM_DIR = "assets/textures/normal_maps"
OUT_JSON = "data/normal_maps.json"
MATCH_LIMIT = 24.0


def is_flat(f):
    b = os.path.basename(f).lower()
    return b.startswith(("default_norm", "null")) or not b


def texture_blocks(xml):
    """(material, [{name: file} per <textures> block], [art/variants files])."""
    try:
        root = ET.fromstring(xml)
    except ET.ParseError:
        return "", [], []
    mat = (root.findtext("material") or "").strip()
    blocks = []
    for t in root.iter("textures"):
        b = {}
        for tex in t.iter("texture"):
            if tex.get("name") and tex.get("file"):
                b.setdefault(tex.get("name"), tex.get("file").strip())
        if b:
            blocks.append(b)
    files = [v.get("file").strip() for v in root.iter("variant") if v.get("file")]
    return mat, blocks, files


def decode(raw):
    if raw is None:
        return None
    try:
        return X.decode_dds(raw) if raw[:4] == b"DDS " else Image.open(io.BytesIO(raw)).convert("RGBA")
    except Exception:
        return None


def to_godot_normal(img, size):
    """Resize, flip green to OpenGL (up the image) and renormalise."""
    r, g, b, _ = img.convert("RGBA").resize(size, Image.LANCZOS).split()
    g = g.point(lambda v: 255 - v)
    out = Image.merge("RGB", (r, g, b))
    # Renormalise after the resize blurred unit vectors toward shorter ones.
    try:
        import numpy as np
        a = np.asarray(out).astype("float32") / 127.5 - 1.0
        a /= np.maximum(np.linalg.norm(a, axis=2, keepdims=True), 1e-4)
        out = Image.fromarray(((a + 1.0) * 127.5 + 0.5).clip(0, 255).astype("uint8"), "RGB")
    except ImportError:
        pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default="master")
    ap.add_argument("--only", default="")
    ap.add_argument("--max", type=int, default=512, help="largest side of a written normal map")
    args = ap.parse_args()
    os.chdir(ROOT)

    models = json.load(open("assets/models/manifest.json"))["models"]
    only = set(filter(None, args.only.split(",")))
    todo = {k: v for k, v in models.items()
            if v["file"].split("/")[1] in ("units", "buildings") and (not only or k in only)}

    skins, actors = set(), set()
    for v in todo.values():
        for s in v["sources"]:
            if s.startswith("skin:"):
                skins.add(s[5:])
            elif s.startswith("actor:"):
                actors.add(s[6:])

    with cf.ThreadPoolExecutor(16) as ex:
        actor_xml = dict(zip(actors, ex.map(lambda a: M.fetch(args.ref, a), actors)))

    # baseTex -> normTex. A block naming both pairs them; a block with only a
    # normTex (units list it in its own group) applies to every skin of the
    # actor. The actor's <material> decides whether 0 A.D. uses it at all.
    variant_xml = {}
    norm_of = {}
    votes = {}
    for xml in actor_xml.values():
        if not xml:
            continue
        mat, blocks, files = texture_blocks(xml)
        seen = set()
        pending = list(files)
        while pending:
            need = [f for f in pending if f not in variant_xml]
            with cf.ThreadPoolExecutor(16) as ex:
                variant_xml.update(zip(need, ex.map(lambda f: M.fetch(args.ref, "art/variants/" + f), need)))
            nxt = []
            for f in pending:
                if f in seen or not variant_xml.get(f):
                    continue
                seen.add(f)
                vmat, vblocks, vfiles = texture_blocks(variant_xml[f])
                mat = mat or vmat
                blocks += vblocks
                nxt += vfiles
            pending = nxt
        if "norm" not in mat and "parallax" not in mat:
            continue
        wide = [b["normTex"] for b in blocks if "normTex" in b and "baseTex" not in b]
        for b in blocks:
            base = b.get("baseTex")
            norm = b.get("normTex") or (wide[0] if wide else None)
            if base and norm and not is_flat(norm):
                votes.setdefault(base, {}).setdefault(norm, 0)
                votes[base][norm] += 1
    for base, vs in votes.items():
        norm_of[base] = max(vs.items(), key=lambda kv: kv[1])[0]

    need_skins = {s for v in todo.values() for s in (x[5:] for x in v["sources"] if x.startswith("skin:"))}
    if not any(s in norm_of for s in need_skins):
        print("no skin pairs with a normal map")
        return
    norms = {norm_of[s] for s in need_skins if s in norm_of}
    with cf.ThreadPoolExecutor(16) as ex:
        skin_img = dict(zip(need_skins, ex.map(lambda s: decode(M.fetch(args.ref, "art/textures/skins/" + s)), need_skins)))
        norm_img = dict(zip(norms, ex.map(lambda s: decode(M.fetch(args.ref, "art/textures/skins/" + s)), norms)))
    print(f"actors {sum(1 for x in actor_xml.values() if x)}/{len(actors)}, skins {len(need_skins)}, "
          f"with a normal map {sum(1 for s in need_skins if s in norm_of)}, normal maps fetched "
          f"{sum(1 for x in norm_img.values() if x)}/{len(norms)}")

    out = json.load(open(OUT_JSON)) if only and os.path.exists(OUT_JSON) else {}
    written = set()
    unmatched = 0
    for mid, v in sorted(todo.items()):
        cands = [s[5:] for s in v["sources"] if s.startswith("skin:") and skin_img.get(s[5:]) is not None]
        if not any(s in norm_of for s in cands):
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
            norm = norm_of.get(best[1])
            if not norm or norm_img.get(norm) is None:
                continue
            w, h = baked.size
            k = min(1.0, args.max / max(w, h))
            size = (max(1, int(w * k)), max(1, int(h * k)))
            rel = f"{os.path.splitext(norm)[0]}_{size[0]}x{size[1]}.png"
            if rel not in written:
                path = os.path.join(NORM_DIR, rel)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                to_godot_normal(norm_img[norm], size).save(path, optimize=True)
                written.add(rel)
            out[f"{cat}/{base}_{name}"] = "res://" + os.path.join(NORM_DIR, rel)

    with open(OUT_JSON, "w") as f:
        json.dump(out, f, indent=0, sort_keys=True)
        f.write("\n")
    print(f"{len(out)} images with a normal map, {len(written)} normal map files, {unmatched} images matched no skin")


if __name__ == "__main__":
    main()
