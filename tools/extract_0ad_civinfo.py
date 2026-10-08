#!/usr/bin/env python3
"""Writes data/civs/civinfo.json from 0 A.D.'s simulation/data/civs/*.json
(CivBonuses with their History, AINames) for the civ info page
(scenes/menu/encyclopedia.gd, port of gui/reference/civinfo). The text is
0 A.D. data, CC BY-SA 3.0; see docs/0AD_ATTRIBUTION.md.

Usage: python3 tools/extract_0ad_civinfo.py <0ad>/binaries/data/mods/public
"""
import json
import os
import sys

# RTS0 civ ids (src/simulation/sim_types.h Civilization).
CODES = ["athen", "spart", "pers", "rome", "cart", "mace", "brit", "gaul", "iber", "maur", "kush", "ptol", "sele", "han"]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    civs_dir = os.path.join(sys.argv[1], "simulation", "data", "civs")
    out = []
    for code in CODES:
        with open(os.path.join(civs_dir, code + ".json"), encoding="utf-8") as f:
            d = json.load(f)
        out.append({
            "code": code,
            "culture": d.get("Culture", ""),
            "bonuses": [{"name": b.get("Name", ""), "description": b.get("Description", ""), "history": b.get("History", "")}
                        for b in d.get("CivBonuses", [])],
            "ai_names": d.get("AINames", []),
        })
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    path = os.path.join(root, "data", "civs", "civinfo.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent="\t", ensure_ascii=False)
        f.write("\n")
    print(f"wrote {path}: {len(out)} civs, {sum(len(c['bonuses']) for c in out)} civ bonuses")


if __name__ == "__main__":
    main()
