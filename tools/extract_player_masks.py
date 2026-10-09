#!/usr/bin/env python3
"""Recover 0 A.D.'s player-colour masks for the unit and building GLBs.

0 A.D.'s player_trans materials keep the player colour mask in the skin's
alpha: model_common.fs does diffuse *= mix(playerColor, 1, tex.a).
extract_0ad_assets.py baked one fixed tint into the RGB and dropped the
alpha, so every model showed the same colour. This tool fetches each model's
0 A.D. skins and actors from GitHub, matches every embedded GLB image to the
skin it was baked from, and writes:

  assets/textures/player_masks/<skin>_<w>x<h>.png   L8 mask, 255 = full player colour
  data/player_masks.json   {"units/spearman_tex0": {"mask": <res path>, "tint": [r, g, b]}}

"tint" is the colour the bake multiplied in (1, 1, 1 for untinted skins), so
the shader can divide it back out: out = baked * mix(player, 1, a) / mix(tint, 1, a).
Only skins used by a player_trans actor get a mask; other alpha is transparency.

Usage: python3 tools/extract_player_masks.py [--ref master] [--only id,id]
"""
import argparse
import concurrent.futures as cf
import io
import json
import os
import struct
import sys
import urllib.request
import xml.etree.ElementTree as ET

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import extract_0ad_assets as X  # noqa: E402  (decode_dds)

MASK_DIR = "assets/textures/player_masks"
OUT_JSON = "data/player_masks.json"
MATCH_LIMIT = 24.0
# extract_0ad_assets.py ASSETS tints (and none at all).
KNOWN_TINTS = [(1.0, 1.0, 1.0), (0.36, 0.62, 0.94), (0.92, 0.32, 0.28), (0.92, 0.9, 0.82)]  # mean |RGB| difference on unmasked texels (0-765)


def fetch(ref, path):
    url = f"https://raw.githubusercontent.com/0ad/0ad/{ref}/binaries/data/mods/public/{path}"
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=60) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
        except Exception:
            pass
    return None


def glb_images(path):
    """[(name, PIL RGBA)] for the GLB's embedded images, in index order."""
    d = open(path, "rb").read()
    n = struct.unpack_from("<I", d, 12)[0]
    j = json.loads(d[20:20 + n])
    off = 20 + n
    bin_len = struct.unpack_from("<I", d, off)[0]
    blob = d[off + 8:off + 8 + bin_len]
    out = []
    for im in j.get("images", []):
        bv = j["bufferViews"][im["bufferView"]]
        raw = blob[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]
        out.append((im.get("name"), Image.open(io.BytesIO(raw)).convert("RGBA")))
    return out


def actor_materials(xml):
    """(material, [baseTex files], [art/variants files]) of one actor or variant file."""
    try:
        root = ET.fromstring(xml)
    except ET.ParseError:
        return "", [], []
    mat = (root.findtext("material") or "").strip()
    texs = [t.get("file", "").strip() for t in root.iter("texture")
            if t.get("name") == "baseTex" and t.get("file")]
    files = [v.get("file").strip() for v in root.iter("variant") if v.get("file")]
    return mat, texs, files


def match_score(baked, src):
    tp, sp = baked.load(), src.load()
    w, h = baked.size
    n = diff = 0
    for y in range(0, h, max(1, h // 64)):
        for x in range(0, w, max(1, w // 64)):
            s = sp[x, y]
            if s[3] >= 250:
                t = tp[x, y]
                n += 1
                diff += abs(t[0] - s[0]) + abs(t[1] - s[1]) + abs(t[2] - s[2])
    # Too few opaque texels to compare: a mostly-masked skin matches anything.
    return diff / n if n >= 256 else 1e9


def estimate_tint(baked, src):
    """Colour the bake multiplied into masked texels (median per channel)."""
    tp, sp = baked.load(), src.load()
    w, h = baked.size
    est = ([], [], [])
    for y in range(0, h, max(1, h // 96)):
        for x in range(0, w, max(1, w // 96)):
            s = sp[x, y]
            a = s[3] / 255.0
            if a > 0.5:
                continue
            t = tp[x, y]
            for c in range(3):
                if s[c] > 40:
                    est[c].append(((t[c] / s[c]) - a) / (1.0 - a))
    tint = []
    for c in range(3):
        v = sorted(est[c])
        tint.append(round(min(max(v[len(v) // 2], 0.0), 1.5), 3) if v else 1.0)
    for known in KNOWN_TINTS:
        if all(abs(t - k) < 0.12 for t, k in zip(tint, known)):
            return list(known)
    return None  # not a tint the extractor used: a wrong skin match


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default="master")
    ap.add_argument("--only", default="")
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
        actor_xml = dict(zip(actors, ex.map(lambda a: fetch(args.ref, a), actors)))
        skin_raw = dict(zip(skins, ex.map(lambda s: fetch(args.ref, "art/textures/skins/" + s), skins)))

    # A skin takes the player colour when any actor using it is player_trans.
    # Shields and similar actors keep their textures in art/variants files.
    variant_xml = {}
    player_skins = set()
    pending = []
    for xml in actor_xml.values():
        if xml:
            mat, texs, files = actor_materials(xml)
            pending.append((mat, texs, files))
    while pending:
        need = {f for _, _, files in pending for f in files if f not in variant_xml}
        with cf.ThreadPoolExecutor(16) as ex:
            variant_xml.update(zip(need, ex.map(lambda f: fetch(args.ref, "art/variants/" + f), need)))
        nxt = []
        for mat, texs, files in pending:
            if mat.startswith("player_trans"):
                player_skins.update(texs)
            for f in files:
                if variant_xml.get(f):
                    _, vtex, vfiles = actor_materials(variant_xml[f])
                    vfiles = [g for g in vfiles if g not in variant_xml]
                    nxt.append((mat, vtex, vfiles))
        pending = [p for p in nxt if p[1] or p[2]]
    # Actors renamed upstream since the extraction 404; a body or structure
    # skin no fetched actor names is a player_trans skin in 0 A.D.
    named = set()
    for xml in list(actor_xml.values()) + list(variant_xml.values()):
        if xml:
            named.update(actor_materials(xml)[1])
    player_skins.update(s for s in skins if s not in named and s.startswith(("skeletal/", "structural/")))
    print(f"actors {sum(1 for x in actor_xml.values() if x)}/{len(actors)}, "
          f"skins {sum(1 for x in skin_raw.values() if x)}/{len(skins)}, player skins {len(player_skins)}")

    src_img = {}
    for s, raw in skin_raw.items():
        if raw is None or s not in player_skins:
            continue
        img = X.decode_dds(raw) if raw[:4] == b"DDS " else Image.open(io.BytesIO(raw)).convert("RGBA")
        if img is not None and img.getextrema()[3][0] < 250:
            src_img[s] = img

    out = json.load(open(OUT_JSON)) if only and os.path.exists(OUT_JSON) else {}
    written = set()
    unmatched = 0
    for mid, v in sorted(todo.items()):
        cands = [s[5:] for s in v["sources"] if s.startswith("skin:") and s[5:] in src_img]
        if not cands:
            continue
        cat = v["file"].split("/")[1]
        base = os.path.basename(v["file"])[:-4]
        for name, baked in glb_images(os.path.join("assets", v["file"])):
            best = None
            for s in cands:
                src = src_img[s]
                r = src if src.size == baked.size else src.resize(baked.size, Image.LANCZOS)
                sc = match_score(baked, r)
                if best is None or sc < best[0]:
                    best = (sc, s, r)
            if best is None or best[0] > MATCH_LIMIT:
                unmatched += 1
                continue
            _, s, r = best
            w, h = baked.size
            rel = f"{os.path.splitext(s)[0]}_{w}x{h}.png"
            path = os.path.join(MASK_DIR, rel)
            tint = estimate_tint(baked, r)
            if tint is None:
                unmatched += 1
                continue
            if rel not in written:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                r.getchannel("A").point(lambda a: 255 - a if a < 250 else 0).save(path, optimize=True)
                written.add(rel)
            out[f"{cat}/{base}_{name}"] = {"mask": "res://" + path, "tint": tint}

    with open(OUT_JSON, "w") as f:
        json.dump(out, f, indent=0, sort_keys=True)
        f.write("\n")
    print(f"{len(out)} masked images, {len(written)} mask files, {unmatched} images left as baked (no player skin matched)")


if __name__ == "__main__":
    main()
