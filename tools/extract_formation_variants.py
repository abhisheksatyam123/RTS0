#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games).

List the formation animation variants each unit model's 0 A.D. actor carries.

Formation.js GetFormationAnimationVariant names a variant per member slot
("testudo_top", "phalanx_front", ...), and UnitAI SetAnimationVariant switches the
actor to it; an actor without that variant keeps its default animations. The
variants live in art/variants/biped/formations/ and an actor opts in with
<variant file="biped/formations/NAME.xml"/>, so this reads each unit model's
actor (assets/models/manifest.json sources) and writes data/formation_variants.json:
{"model_id": ["phalanx_back", ...]}, models without any left out.

Usage: python3 -I tools/extract_formation_variants.py [--ref master]
"""

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
OUT_JSON = os.path.join(REPO, "data", "formation_variants.json")
VARIANT_RE = re.compile(r'<variant\s+file="biped/formations/([a-z_0-9]+)\.xml"')


def fetch(ref, path):
    url = f"https://raw.githubusercontent.com/0ad/0ad/{ref}/binaries/data/mods/public/{path}"
    for _ in range(4):
        try:
            with urllib.request.urlopen(url, timeout=60) as r:
                return r.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
        except Exception:
            pass
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default="master")
    args = ap.parse_args()
    models = json.load(open(MANIFEST))["models"]
    actors = {}
    for mid, m in models.items():
        if not m.get("file", "").startswith("models/units/"):
            continue
        own = [s[6:] for s in m.get("sources", []) if s.startswith("actor:art/actors/units/")]
        if own:
            actors[mid] = own
    paths = sorted({p for own in actors.values() for p in own})
    with ThreadPoolExecutor(16) as ex:
        texts = dict(zip(paths, ex.map(lambda p: fetch(args.ref, p), paths)))
    missing = [p for p in paths if texts[p] is None]
    if missing:
        print(f"{len(missing)} actors not found, e.g. {missing[:3]}", file=sys.stderr)
    out = {}
    for mid, own in sorted(actors.items()):
        names = sorted({v for p in own for v in VARIANT_RE.findall(texts[p] or "")})
        if names:
            out[mid] = names
    with open(OUT_JSON, "w") as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print(f"{len(out)} of {len(actors)} unit models carry formation variants -> {OUT_JSON}")


if __name__ == "__main__":
    main()
