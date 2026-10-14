#!/usr/bin/env python3
"""Copy the 0 A.D. session portraits the selection panel needs into
assets/textures/ui/archive/portraits/.

Every Identity/Icon that data/reference/templates.json names (roster units, each civ's
structures, Gaia animals, treasures, ruins, trees and mines) must exist there as a PNG.
This tool decodes the missing ones from a 0 A.D. release's public.zip
(art/textures/ui/session/portraits/<icon>.cached.dds). 0 A.D. art is CC BY-SA 3.0,
see docs/0AD_ATTRIBUTION.md.

Usage: python3 -I tools/extract_0ad_portraits.py --zip public.zip
       python3 -I tools/extract_0ad_portraits.py --check   (fail if an icon is missing)
New PNGs need a Godot import (.import files) before the game can load them.
"""
import argparse
import io
import json
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

OUT = os.path.join(ROOT, "assets", "textures", "ui", "archive", "portraits")
TEMPLATES = os.path.join(ROOT, "data", "reference", "templates.json")


def wanted_icons():
    with open(TEMPLATES, encoding="utf-8") as f:
        d = json.load(f)
    icons = {v.get("icon", "") for v in d["units"].values()}
    icons |= {v.get("icon", "") for rows in d["structures"].values() for v in rows.values()}
    icons |= {v.get("icon", "") for v in d.get("gaia", {}).values()}
    return sorted(i for i in icons if i)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", help="0 A.D. release public.zip")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    missing = [i for i in wanted_icons() if not os.path.exists(os.path.join(OUT, i))]
    if args.check or not args.zip:
        if missing:
            sys.exit("missing portraits (rerun with --zip): " + " ".join(missing))
        print("all portraits present")
        return
    import extract_0ad_assets as X  # noqa: E402  (decode_dds)
    with zipfile.ZipFile(args.zip) as z:
        for icon in missing:
            name = "art/textures/ui/session/portraits/" + icon + ".cached.dds"
            try:
                data = z.read(name)
            except KeyError:
                print("not in the release:", icon)
                continue
            img = X.decode_dds(data)
            dest = os.path.join(OUT, icon)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            buf = io.BytesIO()
            img.save(buf, "PNG", optimize=True)
            with open(dest, "wb") as f:
                f.write(buf.getvalue())
            print("wrote", os.path.relpath(dest, ROOT), img.size)


if __name__ == "__main__":
    main()
