#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games).

List the overlay icon of each 0 A.D. aura (simulation/data/auras/**.json
"overlayIcon"). StatusBars.js AddAuraIcons draws the icons of the auras that
reach an entity from sources whose status bars are shown; the icons live in
assets/textures/ui/archive/auras/ under their 0 A.D. file names.

Writes data/aura_icons.json: {"aura_id": "attack_bonus", ...}, keyed by the
aura file's name (the id civ_roster_data.h's CivHeroAura carries), auras
without an icon left out.

Usage: python3 -I tools/extract_aura_icons.py --auras PATH [--check]

PATH is the 0.28 simulation/data/auras directory.
The 0 A.D. data is CC BY-SA 3.0, (C) Wildfire Games; see docs/0AD_ATTRIBUTION.md.
"""

import argparse
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_JSON = os.path.join(REPO, "data", "aura_icons.json")
ICON_DIR = os.path.join(REPO, "assets", "textures", "ui", "archive", "auras")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--auras", required=True)
    ap.add_argument("--check", action="store_true", help="fail if data/aura_icons.json is stale")
    args = ap.parse_args()
    icons = {}
    for root, _, files in os.walk(args.auras):
        for f in sorted(files):
            if not f.endswith(".json"):
                continue
            with open(os.path.join(root, f), encoding="utf-8") as fh:
                data = json.load(fh)
            icon = data.get("overlayIcon")
            if not icon:
                continue
            name = os.path.splitext(os.path.basename(icon))[0]
            if not os.path.exists(os.path.join(ICON_DIR, name + ".png")):
                print(f"warning: {f}: no assets/textures/ui/archive/auras/{name}.png", file=sys.stderr)
                continue
            icons[os.path.splitext(f)[0]] = name
    out = json.dumps({"source": "0 A.D. 0.28 simulation/data/auras (overlayIcon)",
                      "icons": dict(sorted(icons.items()))}, indent=1) + "\n"
    if args.check:
        with open(OUT_JSON, encoding="utf-8") as fh:
            if fh.read() != out:
                print("data/aura_icons.json is stale", file=sys.stderr)
                return 1
        return 0
    with open(OUT_JSON, "w", encoding="utf-8") as fh:
        fh.write(out)
    print(f"{len(icons)} auras with icons -> {os.path.relpath(OUT_JSON, REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
