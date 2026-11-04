#!/usr/bin/env python3
"""Check data/anim_walk_rate.json: every key must be a unit model the renderer loads, i.e. a
file assets/models/units/<key>.glb. A key with no file is silently never looked up, so its rig
keeps the base pacing and the port does nothing for it.

Usage: python3 tools/check_anim_walk_rate.py  (exit 0 = all keys resolve)
"""
import json
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def main():
    table = json.load(open(os.path.join(ROOT, "data", "anim_walk_rate.json"), encoding="utf-8"))
    bad = []
    for rig, attrs in table.items():
        if not os.path.isfile(os.path.join(ROOT, "assets", "models", "units", rig + ".glb")):
            bad.append(rig)
        for k in ("walk", "run"):
            if not isinstance(attrs.get(k), int) or attrs[k] <= 0:
                bad.append("%s.%s" % (rig, k))
    for b in bad:
        print("FAIL no model or bad value:", b)
    print("%d rigs, %d failures" % (len(table), len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
