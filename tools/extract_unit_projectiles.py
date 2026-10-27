#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the
actors it reads are CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

What each roster unit launches. 0 A.D.'s LaunchProjectile draws the actor a
unit's actor props at its "projectile" attachpoint (ObjectEntry.cpp plucks it),
usually from the attack_ranged_* variant: javelinists throw
props/units/weapons/jav_projectile.xml, slingers slingrock.xml, archers
arrow_front.xml. RTS0's sim launches an arrow, a bolt or a stone; this table
lets the world view draw a javelin or a sling stone for the units whose 0.28
actor (or rider actor) launches one.

Writes data/unit_projectiles.json {"<template id>": "javelin" | "javelin_fire" |
"sling"} (javelin_fire: the flaming jav_projectile_flame.xml), only
for templates that launch something other than the arrow.

Usage:
  python3 -I tools/extract_unit_projectiles.py --zip <0.28 public.zip> [--check]
"""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unit_template_models as U  # noqa: E402

OUT = os.path.join(U.REPO, "data", "unit_projectiles.json")

# Projectile prop actor -> RTS0 projectile kind (arrows, bolts and stones keep the sim's).
KINDS = {
    "props/units/weapons/jav_projectile.xml": "javelin",
    "props/units/weapons/jav_projectile_flame.xml": "javelin_fire",
    "props/units/weapons/slingrock.xml": "sling",
}


def projectile_of(zf, names, actor):
    """The projectile prop the actor, its variant files or its prop actors attach (first found)."""
    seen = set()

    def visit(path, depth):
        if path in seen or depth > 4 or path not in names:
            return None
        seen.add(path)
        root = ET.fromstring(zf.read(path))
        for el in root.iter("variant"):
            f = el.get("file")
            if f:
                got = visit("art/variants/" + f, depth + 1)
                if got:
                    return got
        for el in root.iter("prop"):
            act = el.get("actor") or ""
            if (el.get("attachpoint") or "").lower() == "projectile" and act:
                return act
        for el in root.iter("prop"):
            act = el.get("actor") or ""
            if act and (el.get("attachpoint") or "").lower() != "projectile":
                got = visit("art/actors/" + act, depth + 1)
                if got:
                    return got
        return None

    return visit("art/actors/" + actor, 0)


def build(zf):
    names = set(zf.namelist())
    read = zf.read
    out = {}
    for _i, tid, _code, _kind in U.roster_rows():
        actor = U.template_actor(names, read, tid)
        if not actor:
            continue
        kind = KINDS.get(projectile_of(zf, names, actor) or "")
        if kind:
            out[tid] = kind
    return dict(sorted(out.items()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", required=True)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    data = build(zipfile.ZipFile(args.zip))
    text = json.dumps(data, indent=1) + "\n"
    if args.check:
        same = os.path.exists(OUT) and open(OUT).read() == text
        print("data/unit_projectiles.json", "up to date" if same else "differs")
        sys.exit(0 if same else 1)
    open(OUT, "w").write(text)
    counts = {}
    for k in data.values():
        counts[k] = counts.get(k, 0) + 1
    print(f"{len(data)} templates launch their own projectile {counts} -> {os.path.relpath(OUT, U.REPO)}")


if __name__ == "__main__":
    main()
