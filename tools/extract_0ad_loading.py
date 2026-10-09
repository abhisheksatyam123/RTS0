#!/usr/bin/env python3
"""Copy 0 A.D.'s loading-screen tips and quotes and the in-game manual.

Reads (0 A.D. 61a3b95, CC BY-SA 3.0 text and art):
  gui/text/tips/*.txt              first line = title, other lines = bullets
  gui/text/quotes.txt              one quote per line, "\\n" = line break
  gui/manual/intro.txt             [font="..."] markup
  art/textures/ui/loading/**.png   tip images, parchment, progress bar

Writes data/reference/loading.json, data/reference/manual.json and
assets/textures/ui/loading/. Godot exports .json and .png, not .txt, so the
text goes into JSON.

    python3 -I tools/extract_0ad_loading.py --0ad /path/to/0ad
"""
import argparse
import json
import os
import re
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PUBLIC = "binaries/data/mods/public"


def unescape(line):
    # 0 A.D.'s GUI text escapes "[" as "\[" and writes "\n" for line breaks.
    return line.replace("\\[", "[").replace("\\n", "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zad", required=True, help="0 A.D. source checkout")
    args = ap.parse_args()
    pub = os.path.join(args.zad, PUBLIC)

    tips = {}
    tip_dir = os.path.join(pub, "gui/text/tips")
    for name in sorted(os.listdir(tip_dir)):
        if not name.endswith(".txt"):
            continue
        stem = name[:-4]
        lines = [unescape(l.strip()) for l in open(os.path.join(tip_dir, name), encoding="utf-8")]
        lines = [l for l in lines if l]
        tips[stem] = {"title": lines[0], "lines": lines[1:]}

    quotes = [unescape(l.strip()) for l in open(os.path.join(pub, "gui/text/quotes.txt"), encoding="utf-8")]
    quotes = [q for q in quotes if q]

    out_dir = os.path.join(ROOT, "data/reference")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "loading.json"), "w", encoding="utf-8") as f:
        json.dump({"tips": tips, "quotes": quotes}, f, ensure_ascii=False, indent="\t")
        f.write("\n")

    # [font="sans-bold-18"] -> heading level; Godot's RichTextLabel gets BBCode.
    intro = open(os.path.join(pub, "gui/manual/intro.txt"), encoding="utf-8").read()
    with open(os.path.join(out_dir, "manual.json"), "w", encoding="utf-8") as f:
        json.dump({"intro": intro}, f, ensure_ascii=False, indent="\t")
        f.write("\n")

    src_art = os.path.join(pub, "art/textures/ui/loading")
    dst_art = os.path.join(ROOT, "assets/textures/ui/loading")
    copied = 0
    for sub in ("", "tips", "progressbar"):
        os.makedirs(os.path.join(dst_art, sub), exist_ok=True)
        for name in sorted(os.listdir(os.path.join(src_art, sub))):
            if not name.endswith(".png") or name == "gradient_4x3.png":
                continue
            shutil.copyfile(os.path.join(src_art, sub, name), os.path.join(dst_art, sub, name))
            copied += 1
    missing = [t for t in tips if not os.path.exists(os.path.join(dst_art, "tips", t + ".png"))]
    print(f"wrote {len(tips)} tips, {len(quotes)} quotes, the manual and {copied} images"
          + (f"; tips without an image: {missing}" if missing else ""))


if __name__ == "__main__":
    main()
