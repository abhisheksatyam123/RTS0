#!/usr/bin/env python3
"""Build data/anim_walk_rate.json: the 0 A.D. 0.28 Walk and Run animation speed attribute of
each unit rig, from a rig map produced by the 0.28 art (the actor XMLs in public.zip).

0 A.D. 0.28 plays a movement clip at (meanSpeed in m/s) x (the actor's animation speed attribute / 100)
(UnitAnimation.cpp, CUnitAnimation::Update; ObjectBase.cpp, at_speed). The attribute defaults to
100 when the actor sets none. RTS0's ported renderer plays the same rule (world_view_node.cpp,
walk_rate_attr): the rate is the unit's sim speed converted to metres a second (SPEED_SCALE 0.356)
times the attribute / 100.

Input: a JSON map, a list of
    {"rig": "athen_infantry_spearman_b", "walk": 10, "run": 4}
where walk and run are the animation speed attributes (omit a key to use the 0 A.D. default, 100).

Usage: python3 tools/anim_walk_rate.py MAP.json [--out data/anim_walk_rate.json]
"""
import argparse
import json
import os
import sys

DEFAULT_ATTR = 100  # 0 A.D.: an animation with no speed attribute plays at 100 percent


def build(rows):
    out = {}
    for r in rows:
        rig = r.get("rig")
        if not rig:
            raise ValueError("row without a rig name: %r" % (r,))
        walk = int(r.get("walk", DEFAULT_ATTR))
        run = int(r.get("run", DEFAULT_ATTR))
        if walk <= 0 or run <= 0:
            raise ValueError("%s: non-positive speed attribute (walk %d, run %d)" % (rig, walk, run))
        out[rig] = {"walk": walk, "run": run}
    return dict(sorted(out.items()))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("map")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "data", "anim_walk_rate.json"))
    args = ap.parse_args()
    with open(args.map, encoding="utf-8") as f:
        rows = json.load(f)
    table = build(rows)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(table, f, indent=1, sort_keys=True)
        f.write("\n")
    print("wrote %d rigs to %s" % (len(table), os.path.normpath(args.out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
