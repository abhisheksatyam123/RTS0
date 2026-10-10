#!/usr/bin/env python3
"""Fetch 0 A.D.'s sky boxes (art/textures/skies/<set>/{front,back,left,right,top}.dds).

0 A.D.'s SkyManager builds a cube map from the five faces of the map's
Environment SkySet. This writes each face as
assets/textures/skies/<set>/<face>.png (spaces in set names become "_"), which
shaders/sky_box.gdshader samples with the same face layout.

Usage: python3 tools/extract_sky_boxes.py [--ref master] [--sets cirrus,sunny]
"""
import argparse
import io
import os
import sys
from urllib.parse import quote

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import extract_0ad_assets as X  # noqa: E402  (decode_dds)
import extract_player_masks as M  # noqa: E402  (fetch)

OUT_DIR = "assets/textures/skies"
FACES = ("front", "back", "left", "right", "top")
SETS = ("cirrus", "cloudless", "cumulus", "dark", "dark2", "desert", "fog", "mountainous",
        "overcast", "rain", "stormy", "stratus", "stratus_2", "sunny", "sunny 1", "sunrise",
        "sunset", "sunset 1", "sunset 2", "twilight")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default="master")
    ap.add_argument("--sets", default=",".join(SETS))
    args = ap.parse_args()
    os.chdir(ROOT)
    for s in filter(None, args.sets.split(",")):
        out = os.path.join(OUT_DIR, s.replace(" ", "_"))
        os.makedirs(out, exist_ok=True)
        for f in FACES:
            raw = M.fetch(args.ref, f"art/textures/skies/{quote(s)}/{f}.dds")
            if raw is None:
                print(f"missing {s}/{f}")
                continue
            img = X.decode_dds(raw) if raw[:4] == b"DDS " else Image.open(io.BytesIO(raw))
            img.convert("RGB").save(os.path.join(out, f + ".png"), optimize=True)
        print(s)


if __name__ == "__main__":
    main()
