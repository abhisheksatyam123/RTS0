"""Maps 0 A.D. terrain names to RTS0's extracted terrain textures.

0 A.D. paints maps with terrain names (art/terrains/<folder>/<name>.xml, whose
baseTex is a texture under art/textures/terrain/types/). RTS0's textures are
assets/textures/terrain/<key>.png with the keys of assets/models/manifest.json
"terrain". This writes data/terrain/terrain_textures.json, {0 A.D. name: key},
for every terrain name the ported maps (data/maps/*.json "terrain") and the
random-map biomes (src/simulation/terrain_biomes.h) use. Names whose texture
was not extracted take the closest texture of the same group and kind, and are
listed in the file's "_approximate" array.

  python3 tools/map_0ad_terrains.py

0 A.D. art is CC BY-SA 3.0; see docs/0AD_ATTRIBUTION.md.
"""
import concurrent.futures as cf
import glob
import json
import os
import re
import urllib.request

TERRAINS = "https://raw.githubusercontent.com/0ad/0ad/master/binaries/data/mods/public/art/terrains/"
FOLDERS = ["biome-alpine", "biome-desert", "biome-india", "biome-mediterranean", "biome-persia", "biome-polar",
           "biome-savanna", "biome-steppe", "biome-temperate", "biome-tropic", "biome-nubia", "special", "water"]
# Name prefixes of the newer terrain sets and the texture group they draw from.
PREFIX_GROUPS = {"aegean": "aegean_anatolia", "african": "african_savanna", "alpine": "alpine_arctic",
                 "autumn": "temperate_autumn", "baltic": "baltic_nordic", "sahara": "sahara", "temperate": "temperate",
                 "india": "india", "italy": "italy", "nubia": "nubia", "persia": "persia", "steppe": "steppe",
                 "sudd": "sudd", "savanna": "savanna", "sandstone": "sahara"}
KINDS = ["forestfloor", "farmland", "paving", "cliff", "mountain", "rocks", "rock", "snow", "sand", "mud", "dirt", "grass",
         "cracks", "water", "ocean", "shore", "road"]


def used_names():
    names = set()
    for path in glob.glob("data/maps/*.json"):
        with open(path) as f:
            t = json.load(f).get("terrain")
        if t:
            names.update(t["names"])
    with open("src/simulation/sim_types.h") as f:
        map_ids = set(re.findall(r'\{MAP_[A-Z0-9_]+, "([a-z0-9_]+)"', f.read()))
    for header in ("src/simulation/terrain_biomes.h", "src/simulation/script_terrains.h"):
        if os.path.exists(header):
            with open(header) as f:
                # Every quoted terrain; the bare words are biome names and the ids map types, not terrains.
                names.update(n for n in re.findall(r'"([A-Za-z0-9_ ]+)"', f.read())
                             if not re.fullmatch(r"[a-z]+", n) and n not in map_ids)
    return sorted(names)


def fetch_base_textures(names):
    def get(job):
        folder, name = job
        try:
            with urllib.request.urlopen(TERRAINS + folder + "/" + name + ".xml", timeout=30) as r:
                m = re.search(r'name="baseTex"\s+file="([^"]+)"', r.read().decode("utf-8"))
                return name, m.group(1) if m else None
        except Exception:
            return name, None
    out = {}
    with cf.ThreadPoolExecutor(48) as ex:
        for name, tex in ex.map(get, [(f, n) for f in FOLDERS for n in names]):
            if tex:
                out[name] = tex
    return out


def kind_of(name):
    for k in KINDS:
        if k in name:
            # Rock faces: the newer sets call them rock or mountain, the older cliff.
            return "cliff" if k in ("rock", "mountain") else k
    return ""


def main():
    with open("assets/models/manifest.json") as f:
        textures = json.load(f)["terrain"]
    by_path = {os.path.splitext(v)[0]: k for k, v in textures.items()}
    names = used_names()
    base = fetch_base_textures(names)
    table, approx = {}, []
    for name in names:
        key = by_path.get(os.path.splitext(base[name])[0]) if name in base else None
        if not key and name in textures:
            key = name
        prefix = name.split("_")[0]
        group = PREFIX_GROUPS.get(prefix)
        if not key and group and group + "_" + name[len(prefix) + 1:] in textures:
            key = group + "_" + name[len(prefix) + 1:]
        if not key:
            # The closest extracted texture: same group, same kind, most shared words.
            words = set(name.split("_"))
            pool = [k for k in textures if group and k.startswith(group + "_")] or list(textures)
            same = ([k for k in pool if kind_of(k) == kind_of(name)] or
                    [k for k in textures if kind_of(k) == kind_of(name)] or pool)
            key = max(sorted(same), key=lambda k: len(words & set(k.split("_"))))
            approx.append(name)
        table[name] = key
    table["_approximate"] = approx
    os.makedirs("data/terrain", exist_ok=True)
    with open("data/terrain/terrain_textures.json", "w") as f:
        json.dump(table, f, indent=1, sort_keys=True)
    print(f"{len(names)} terrain names, {len(approx)} drawn with the closest texture")


if __name__ == "__main__":
    main()
