#!/usr/bin/env python3
"""Copy 0 A.D. 0.28's gameplay tips, loading-screen quotes and the manual intro.

Reads the 0.28 release's mods/public (unpacked public.zip; CC BY-SA 3.0 text
and art):
  gui/reference/tips/tipfiles.json     categories: name, SP/MP loading-screen
                                       weights, tip text files and images
  gui/reference/tips/texts/*.txt       first line = title, other lines = bullets
  gui/text/quotes.txt                  one quote per line, "\\n" = line break
  gui/manual/intro.txt                 [font="..."] markup
  art/textures/ui/tips/*.png           tip images (.png or .png.cached.dds)
  art/textures/ui/tipdisplay/*.png     parchment, ornaments, buttons (optional)
  art/textures/ui/loading/**           backdrop and progress bar

Writes data/reference/loading.json ({"categories": [{name, sp, mp, tips}],
"tips": {stem: {title, lines, images}}, "quotes": [...]}),
data/reference/manual.json and assets/textures/ui/{loading,tipdisplay}/.
The release ships textures as DXT .cached.dds; Pillow converts them to PNG,
leaving an existing PNG of the same picture alone.
Tip images 0.28 no longer uses are removed. Godot exports .json and .png,
not .txt, so the text goes into JSON.

    python3 -I tools/extract_0ad_loading.py --public /path/to/0.28/mods/public
"""
import argparse
import json
import os
import shutil

# Mean per-channel difference below which a DDS decode is the same picture.
DXT_NOISE = 8
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def unescape(line):
    # 0 A.D.'s GUI text escapes "[" as "\[" and writes "\n" for line breaks.
    return line.replace("\\[", "[").replace("\\n", "\n")


def copy_texture(src_dir, name, dst):
    """Copy NAME (a .png) from SRC_DIR to DST, converting the release's DDS cache."""
    src = os.path.join(src_dir, name)
    if os.path.exists(src):
        shutil.copyfile(src, dst)
        return True
    if os.path.exists(src + ".cached.dds"):
        from PIL import Image, ImageChops, ImageStat
        img = Image.open(src + ".cached.dds")
        # DXT is lossy: keep an existing lossless PNG of the same picture
        # rather than churning it for compression noise.
        if os.path.exists(dst):
            old = Image.open(dst).convert("RGB")
            if old.size == img.size and sum(ImageStat.Stat(ImageChops.difference(old, img.convert("RGB"))).mean) < 3 * DXT_NOISE:
                return True
        img.save(dst)
        return True
    return False


def texture_names(src_dir):
    if not os.path.isdir(src_dir):
        return []
    names = set()
    for n in os.listdir(src_dir):
        if n.endswith(".png"):
            names.add(n)
        elif n.endswith(".png.cached.dds"):
            names.add(n[:-len(".cached.dds")])
    return sorted(names)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--public", required=True, help="0.28 mods/public, unpacked")
    args = ap.parse_args()
    pub = args.public

    tip_root = os.path.join(pub, "gui/reference/tips")
    files = json.load(open(os.path.join(tip_root, "tipfiles.json"), encoding="utf-8"))
    categories = []
    tips = {}
    for cat in files:
        stems = []
        for entry in cat["files"]:
            stem = entry["textFile"][:-4]
            lines = [unescape(l.strip()) for l in open(os.path.join(tip_root, "texts", entry["textFile"]), encoding="utf-8")]
            lines = [l for l in lines if l]
            tips[stem] = {"title": lines[0], "lines": lines[1:], "images": entry["imageFiles"]}
            stems.append(stem)
        categories.append({"name": cat["name"], "sp": cat["loadingScreenOccurrence_SP"],
                           "mp": cat["loadingScreenOccurrence_MP"], "tips": stems})

    quotes = [unescape(l.strip()) for l in open(os.path.join(pub, "gui/text/quotes.txt"), encoding="utf-8")]
    quotes = [q for q in quotes if q]

    out_dir = os.path.join(ROOT, "data/reference")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "loading.json"), "w", encoding="utf-8") as f:
        json.dump({"categories": categories, "tips": tips, "quotes": quotes}, f, ensure_ascii=False, indent="\t")
        f.write("\n")

    intro = open(os.path.join(pub, "gui/manual/intro.txt"), encoding="utf-8").read()
    with open(os.path.join(out_dir, "manual.json"), "w", encoding="utf-8") as f:
        json.dump({"intro": intro}, f, ensure_ascii=False, indent="\t")
        f.write("\n")

    ui = os.path.join(pub, "art/textures/ui")
    dst_art = os.path.join(ROOT, "assets/textures/ui/loading")
    copied = 0
    for sub in ("", "progressbar"):
        os.makedirs(os.path.join(dst_art, sub), exist_ok=True)
        for name in texture_names(os.path.join(ui, "loading", sub)):
            if name != "gradient_4x3.png":
                copied += copy_texture(os.path.join(ui, "loading", sub), name, os.path.join(dst_art, sub, name))

    wanted = sorted({img for t in tips.values() for img in t["images"]})
    tip_dst = os.path.join(dst_art, "tips")
    os.makedirs(tip_dst, exist_ok=True)
    missing = [img for img in wanted if not copy_texture(os.path.join(ui, "tips"), img, os.path.join(tip_dst, img))]
    copied += len(wanted) - len(missing)
    removed = 0
    for name in os.listdir(tip_dst):
        if name.endswith(".png") and name not in wanted:
            os.remove(os.path.join(tip_dst, name))
            if os.path.exists(os.path.join(tip_dst, name + ".import")):
                os.remove(os.path.join(tip_dst, name + ".import"))
            removed += 1

    deco = os.path.join(ui, "tipdisplay")
    if os.path.isdir(deco):
        deco_dst = os.path.join(ROOT, "assets/textures/ui/tipdisplay")
        os.makedirs(deco_dst, exist_ok=True)
        for name in texture_names(deco):
            copied += copy_texture(deco, name, os.path.join(deco_dst, name))

    print(f"wrote {len(categories)} categories, {len(tips)} tips, {len(quotes)} quotes, the manual; "
          f"{copied} images, removed {removed} unused tip images"
          + (f"; missing images: {missing}" if missing else ""))


if __name__ == "__main__":
    main()
