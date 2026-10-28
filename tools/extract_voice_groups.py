#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the
voices it lists are CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

A unit's order voice is the sound group its template's SoundGroups name,
voice/{lang}/civ/civ_{phenotype}_<group>.xml (select, walk, attack, build,
repair, gather, garrison, heal, trade, collect_treasure), and each group file
lists the exact clips it plays: Greek men garrison with "As you wish", Latin
traders answer with "march", Greek women gather with "gather together".
This tool reads every such group of the 0.28 public.zip and writes
data/voice_groups.json:

  {"<lang>": {"<male|female>": {"<group>": ["voice/<lang>/civ/<clip>.ogg", ...]}}}

with each clip's path under assets/audio/ (a group may borrow another
language's clip: Kushite women say "my lord" in Greek).

Usage:
  python3 -I tools/extract_voice_groups.py --zip <0.28 public.zip> [--check]
"""

import argparse
import json
import os
import re
import sys
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "data", "voice_groups.json")
GROUP = re.compile(r"audio/voice/(\w+)/civ/civ_(male|female)_(\w+)\.xml$")


def build(zf):
    out = {}
    for name in sorted(zf.namelist()):
        m = GROUP.match(name)
        if not m:
            continue
        lang, sex, group = m.groups()
        text = zf.read(name).decode("utf-8")
        path = re.search(r"<Path>([^<]+)</Path>", text).group(1).strip()
        clips = [path[len("audio/"):] + "/" + c for c in re.findall(r"<Sound>([^<]+)</Sound>", text)]
        for c in clips:
            if not os.path.exists(os.path.join(REPO, "assets", "audio", c)):
                sys.exit(f"{name}: assets/audio/{c} is missing")
        out.setdefault(lang, {}).setdefault(sex, {})[group] = clips
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", required=True)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    data = build(zipfile.ZipFile(args.zip))
    text = json.dumps(data, indent=1, sort_keys=True) + "\n"
    if args.check:
        same = os.path.exists(OUT) and open(OUT).read() == text
        print("data/voice_groups.json", "up to date" if same else "differs")
        sys.exit(0 if same else 1)
    open(OUT, "w").write(text)
    n = sum(len(g) for lang in data.values() for g in lang.values())
    print(f"{n} voice groups in {len(data)} languages -> {os.path.relpath(OUT, REPO)}")


if __name__ == "__main__":
    main()
