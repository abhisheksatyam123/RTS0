#!/usr/bin/env python3
"""Fetches 0 A.D.'s map preview images (art/textures/ui/session/icons/mappreview/,
CC BY-SA 3.0) into assets/maps/previews/<map id>.jpg, the path game setup and the
replay menu load (scenes/menu/map_preview.gd).

  * Ported maps (data/maps/*.json): the Preview named in the map XML's ScriptSettings.
  * Random maps (RMS_MAPS in scenes/menu/gamesetup.gd, ids "random_<script>"): the
    Preview in maps/random/<script>.json, or the one of the 0 A.D. script RTS0's map
    is modelled on (RANDOM_SOURCES).
  * RTS0's own random maps (RTS0_OWN) have no 0 A.D. preview: they are drawn from
    above from a generated map (tools/dump_map_tiles.cpp), each tile in its 0 A.D.
    terrain's average colour, hill-shaded, with water, trees and mines.

0 A.D. previews are 512x512 textures holding a 400x300 picture; that part is kept,
scaled to PREVIEW_W x PREVIEW_H and saved as JPEG to keep the repository small. A map
with no image shows only its description.

  g++ -std=c++20 -O2 -Isrc tools/dump_map_tiles.cpp src/simulation/simulation.cpp \
      src/simulation/rms_generator.cpp src/core/job_system.cpp -lpthread -o /tmp/dump_map_tiles
  python3 tools/fetch_0ad_previews.py /tmp/dump_map_tiles
"""
import http.client
import io
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import port_0ad_maps as port  # noqa: E402  (map lists and their 0 A.D. XML paths)

RAW = "https://raw.githubusercontent.com/0ad/0ad/master/binaries/data/mods/public/"
PREVIEW_DIR = "art/textures/ui/session/icons/mappreview/"
OUT = "assets/maps/previews"
PREVIEW_W, PREVIEW_H = 320, 240

# RTS0 random maps whose id is not their 0 A.D. script's file name.
RANDOM_SOURCES = {
    "archipelago_0ad": "archipelago",
    "oasis_0ad": "oasis",
    "mediterranean_0ad": "mediterranean",
    "nearestern_badlands": "neareastern_badlands",
}
# RTS0's own random maps (no 0 A.D. script; some share a 0 A.D. script's name).
RTS0_OWN = {"continental", "highlands", "valley", "acropolis", "spartan_valley", "archipelago", "oasis",
            "mediterranean"}


def fetch(rel, tries=4):
    for attempt in range(tries):
        try:
            with urllib.request.urlopen(RAW + urllib.parse.quote(rel), timeout=60) as r:
                return r.read()
        except urllib.error.HTTPError:
            return None  # not in 0 A.D.
        except (OSError, http.client.HTTPException):
            if attempt == tries - 1:
                raise
            time.sleep(2 ** attempt)


def ported_maps():
    """(map id, XML path) for every map port_0ad_maps.py ports."""
    out = {mid: xrel for mid, xrel, *_ in port.maps_list}
    for name, (mid, _b) in port.github_skirmishes.items():
        out.setdefault(mid, f"maps/skirmishes/{name}.xml")
    for name, (mid, _b) in port.github_scenarios.items():
        out.setdefault(mid, f"maps/scenarios/{name}.xml")
    return out


def xml_preview(xrel):
    raw = fetch(xrel)
    if not raw:
        return None
    m = re.search(rb"<ScriptSettings><!\[CDATA\[(.*?)\]\]></ScriptSettings>", raw, re.S)
    if not m:
        return None
    try:
        return json.loads(m.group(1).decode("utf-8")).get("Preview")
    except ValueError:
        return None


def random_preview(script):
    raw = fetch(f"maps/random/{script}.json")
    if not raw:
        return None
    try:
        return json.loads(raw).get("settings", {}).get("Preview")
    except ValueError:
        return None


def random_ids():
    src = open("scenes/menu/gamesetup.gd", encoding="utf-8").read()
    return re.findall(r'^\s*\["(random_[a-z0-9_]+)"', src, re.M)


def save(map_id, png_name, cache):
    if png_name not in cache:
        cache[png_name] = fetch(PREVIEW_DIR + png_name)
    data = cache[png_name]
    if not data:
        return False
    img = Image.open(io.BytesIO(data)).convert("RGB")
    if img.width == img.height:  # the 400x300 picture in a power-of-two texture
        img = img.crop((0, 0, round(img.width * 400 / 512), round(img.height * 300 / 512)))
    img.resize((PREVIEW_W, PREVIEW_H), Image.LANCZOS).save(f"{OUT}/{map_id}.jpg", quality=85, optimize=True)
    return True


_COLOURS = {}


def terrain_colour(name, table):
    """Average colour of a 0 A.D. terrain's texture (data/terrain/terrain_textures.json)."""
    if name not in _COLOURS:
        key = table.get(name, name)
        path = f"assets/textures/terrain/{key}.png"
        rgb = (110, 100, 80)
        if os.path.exists(path):
            rgb = Image.open(path).convert("RGB").resize((1, 1), Image.BOX).getpixel((0, 0))
        _COLOURS[name] = rgb
    return _COLOURS[name]


def render(map_id, dumper):
    """Top-down preview of a generated RTS0 random map."""
    d = json.loads(subprocess.run([dumper, map_id, "256", "1"], check=True, capture_output=True, text=True).stdout)
    table = json.load(open("data/terrain/terrain_textures.json"))
    res, water, half = d["res"], d["water"], d["half"]
    names, tiles, hts = d["names"], d["tiles"], d["heights"]
    img = Image.new("RGB", (res, res))
    px = img.load()
    for z in range(res):
        for x in range(res):
            h = hts[z * res + x]
            r, g, b = terrain_colour(names[tiles[z * res + x]], table)
            # Light from the north-west, as 0 A.D.'s previews are lit.
            hx = hts[z * res + min(x + 1, res - 1)] - hts[z * res + max(x - 1, 0)]
            hz = hts[min(z + 1, res - 1) * res + x] - hts[max(z - 1, 0) * res + x]
            shade = max(0.55, min(1.35, 1.0 - 0.35 * (hx + hz)))
            r, g, b = (min(255, int(c * shade)) for c in (r, g, b))
            if h < water:  # deeper water is darker blue
                k = min(1.0, 0.45 + (water - h) * 0.25)
                r, g, b = (int(r * (1 - k) + w * k) for (r, w) in ((r, 30), (g, 70), (b, 110)))
            px[x, z] = (r, g, b)
    for rtype, rx, rz in d["resources"]:
        x, z = int((rx + half) / (2 * half) * res), int((rz + half) / (2 * half) * res)
        if 0 <= x < res and 0 <= z < res:
            px[x, z] = (24, 52, 20) if rtype == 0 else (150, 140, 120) if rtype == 2 else (190, 160, 70) if rtype == 1 else (120, 40, 60)
    frame = Image.new("RGB", (PREVIEW_W, PREVIEW_H))
    side = PREVIEW_H - 16
    frame.paste(img.resize((side, side), Image.LANCZOS), ((PREVIEW_W - side) // 2, 8))
    frame.save(f"{OUT}/{map_id}.jpg", quality=85, optimize=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    jobs = {}
    ported = {mid: x for mid, x in ported_maps().items() if os.path.exists(f"data/maps/{mid}.json")}
    with ThreadPoolExecutor(16) as ex:
        for mid, name in zip(ported, ex.map(xml_preview, ported.values())):
            jobs[mid] = name
        rids = [r for r in random_ids() if r[len("random_"):] not in RTS0_OWN]
        scripts = [RANDOM_SOURCES.get(r[len("random_"):], r[len("random_"):]) for r in rids]
        for rid, name in zip(rids, ex.map(random_preview, scripts)):
            jobs[rid] = name
    cache = {}
    missing = []
    for mid, name in sorted(jobs.items()):
        if not (name and save(mid, name, cache)):
            missing.append(mid)
    own = [r for r in random_ids() if r[len("random_"):] in RTS0_OWN]
    if len(sys.argv) > 1:
        for rid in own:
            render(rid, sys.argv[1])
    for stale in os.listdir(OUT):
        if stale[:-4] not in jobs and stale[:-4] not in own:
            os.remove(f"{OUT}/{stale}")
    print(f"{len(jobs) - len(missing)} 0 A.D. previews and {len(own) if len(sys.argv) > 1 else 0} drawn ones in {OUT}; "
          f"none for: {', '.join(missing) or 'no map'}")


if __name__ == "__main__":
    main()
