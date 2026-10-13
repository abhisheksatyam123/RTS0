#!/usr/bin/env python3
"""Generate RTS0's per-map counts of Gaia mines, fruit and hunt from 0 A.D.'s random map scripts.

Each 0 A.D. random map script (maps/random/<script>.js) places its own mines, fruit
bushes and trees, and animals, with counts that depend on the map size and the number
of players (scaleByMapSize, createBalancedMetalMines, createFood, rmgen2's
addMetal/addStone/addBerries/addAnimals). Rather than port every script's placement
code, this runs each script under node (tools/rmgen_harness.js) at 128, 256, 384 and
512 tiles with 2, 4 and 8 players over a few seeds and records, outside the player
bases:
  * metal and stone mines: large ones (ResourceSupply/Max of 2000 or more) and small
    ones, with the Max the script's most common template has;
  * fruit (food.fruit): the count and the two commonest kinds (berry, apple, date) with
    their share and Max;
  * hunt (food.meat animals, wild or Gaia-owned livestock): the count and, for scripts
    with their own templates (no "generic/" biome), the four commonest species.
RmsGenerator::top_up_resources tops each map up to these counts with RTS0's own
placement rules.

Usage:
  python3 -I tools/extract_0ad_resources.py --0ad PATH [--jobs N] [--seeds 1,2] [--check]

PATH is an extracted 0 A.D. public mod (globalscripts/, maps/random/,
simulation/templates/, simulation/data/), or a tree holding it under
binaries/data/mods/public. Needs node and Pillow (for the heightmap images).
Writes src/simulation/resource_counts_data.h; --check fails if it would change.
"""

import argparse
import concurrent.futures
import glob
import json
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_HEADER = os.path.join(REPO, "src", "simulation", "resource_counts_data.h")
HARNESS = os.path.join(REPO, "tools", "rmgen_harness.js")
TILES = (128, 256, 384, 512)
PLAYERS = (2, 4, 8)
# RTS0 map ids whose 0 A.D. script has another name.
ALIAS = {"archipelago_0ad": "archipelago", "mediterranean_0ad": "mediterranean", "oasis_0ad": "oasis",
         "nearestern_badlands": "neareastern_badlands"}
# Maps whose entities come from their own place_*_entities (Triggers), not the generic passes.
SKIP = {"elephantine", "jebel_barkal", "survivalofthefittest", "danubius"}


def load_templates(root):
    """simulation/templates/*.xml with their parents merged, as Engine.GetTemplate gives them."""
    raw = {}
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.endswith(".xml"):
                p = os.path.join(d, f)
                raw[os.path.relpath(p, root)[:-4].replace(os.sep, "/")] = ET.parse(p).getroot()

    def merge(base, new, replace=False):
        if replace or not isinstance(base, dict) or not isinstance(new, dict):
            return new
        r = dict(base)
        for k, v in new.items():
            r[k] = None if v is None else merge(r.get(k), v)
        return r

    def node(el):
        o = {"@" + k: v for k, v in el.attrib.items()
             if k not in ("parent", "filtered", "merge", "replace", "disable", "datatype")}
        for c in el:
            o[c.tag] = None if "disable" in c.attrib else merge(o.get(c.tag), node(c), "replace" in c.attrib)
        text = (el.text or "").strip()
        if text and not len(el):
            return text if not o else dict(o, _string=text)
        return o

    cache = {}

    def load(name):
        if name not in cache:
            el = raw.get(name)
            if el is None:
                return None
            base = {}
            for par in reversed([p for p in el.attrib.get("parent", "").split("|") if p]):
                pb = load(par)
                if pb:
                    base = merge(base, pb)
            cache[name] = {k: v for k, v in merge(base, node(el)).items() if v is not None}
        return cache[name]

    return {n: load(n) for n in raw}


def decode_heightmaps(public, out):
    from PIL import Image
    for f in glob.glob(os.path.join(public, "maps", "random", "*.png")):
        im = Image.open(f).convert("L")
        with open(os.path.join(out, os.path.basename(f) + ".json"), "w") as fh:
            json.dump([v * 257 for v in im.tobytes()], fh)


def rts0_maps():
    """(id, biome) of every procedural map in sim_types.h's map table."""
    src = open(os.path.join(REPO, "src", "simulation", "sim_types.h")).read()
    return re.findall(r'\{MAP_[A-Z0-9_]+, "([a-z0-9_]+)", "[^"]*", "([a-z_]*)", true,', src)


def biome_for(public, script, rts0_biome):
    settings = json.load(open(os.path.join(public, "maps", "random", script + ".json")))["settings"]
    b = settings.get("SupportedBiomes")
    if not b:
        return "generic/temperate", False
    if isinstance(b, list):
        return next((x for x in b if x.endswith("/" + rts0_biome)), b[0]), False
    if b in ("generic/", "generic"):  # hyrcanian_shores.json says "generic"
        own = os.path.exists(os.path.join(public, "maps", "random", "rmbiome", "generic", rts0_biome + ".json"))
        return "generic/" + (rts0_biome if own else "temperate"), True
    names = sorted(os.path.basename(f)[:-5] for f in glob.glob(os.path.join(public, "maps", "random", "rmbiome", b + "*.json")))
    if not names:
        return "generic/temperate", True
    return b + (rts0_biome if rts0_biome in names else names[0]), False


def fruit_kind(tmpl):
    name = tmpl.rsplit("/", 1)[-1]
    for keys, kind in ((["berry", "grape"], "berry"), (["apple", "fig"], "apple"), (["date"], "date")):
        if any(k in name for k in keys):
            return kind
    return "berry"


def run(args):
    """The harness's output for one script, size, player count and seed. A script that
    gives up (0 A.D. throws and the engine generates again with another seed) runs again
    with the next seed, up to five times."""
    public, tpl, hm, script, tiles, players, seed, biome, cache = args
    key = os.path.join(cache, "%s_%s_%d_%d_%d.json" % (script, biome.replace("/", "-"), tiles, players, seed)) if cache else None
    if key and os.path.exists(key):
        return json.load(open(key))
    out = None
    for attempt in range(5):
        p = subprocess.run(["node", HARNESS, public, tpl, script, str(tiles), str(players), str(seed + 1000 * attempt), biome],
                           capture_output=True, text=True, timeout=1200, env=dict(os.environ, RMGEN_HEIGHTMAPS=hm))
        if not p.returncode:
            out = json.loads(p.stdout.strip().splitlines()[-1])
            break
    if out is None:
        # Some scripts can't fit 8 players on a 128-tile map; summarise fills the cell in.
        err = [l for l in p.stderr.splitlines() if "Error:" in l]
        print("%s %d tiles %d players failed: %s" % (script, tiles, players, err[-1].strip() if err else "?"), file=sys.stderr)
    if key:
        with open(key, "w") as fh:
            json.dump(out, fh)
    return out


def summarise(runs):
    """Per-map counts from the harness outputs runs[(tiles, players)] = [per-seed dicts]."""
    grid = {k: [[0] * len(PLAYERS) for _ in TILES] for k in ("ml", "ms", "sl", "ss", "fruit", "hunt")}
    amounts = {"ml": {}, "ms": {}, "sl": {}, "ss": {}}
    fruit, hunt = {}, {}
    missing = []
    for (ti, pi), outs in runs.items():
        outs = [o for o in outs if o is not None]
        if not outs:
            missing.append((ti, pi))
            continue
        acc = dict.fromkeys(grid, 0.0)
        for out in outs:
            for tmpl, e in out.items():
                t, n, amt = e["type"], e["n"] / len(outs), e["amount"]
                if t in ("metal.ore", "stone.rock"):
                    k = ("m" if t == "metal.ore" else "s") + ("l" if amt >= 2000 else "s")
                    acc[k] += n
                    amounts[k][amt] = amounts[k].get(amt, 0) + n
                elif t == "food.fruit":
                    acc["fruit"] += n
                    key = (fruit_kind(tmpl), amt)
                    fruit[key] = fruit.get(key, 0) + n
                elif t == "food.meat" and tmpl.startswith("gaia/fauna_"):
                    acc["hunt"] += n
                    stem = tmpl.split("/", 1)[1]
                    hunt[stem] = hunt.get(stem, 0) + n
        for k in grid:
            grid[k][ti][pi] = int(round(acc[k]))
    # A size and player count the script never managed takes the nearest one it did:
    # fewer players on the same size first, then the next size up.
    done = set(runs) - set(missing)
    for ti, pi in missing:
        near = min(done, key=lambda c: (abs(c[0] - ti) * 10 + abs(c[1] - pi), -c[0]))
        for k in grid:
            grid[k][ti][pi] = grid[k][near[0]][near[1]]
    amount = {k: (max(v, key=v.get) if v else (5000 if k[1] == "l" else 1000)) for k, v in amounts.items()}
    return grid, amount, fruit, hunt


def shares(d, n):
    tot = sum(d.values())
    top = sorted(d.items(), key=lambda kv: (-kv[1], kv[0]))[:n]
    return [(k, max(1, int(round(100.0 * v / tot)))) for k, v in top] if tot else []


def emit(rows):
    g = lambda a: "{" + ", ".join("{" + ", ".join(str(v) for v in r) + "}" for r in a) + "}"
    out = [
        "#pragma once",
        "",
        "// Generated by tools/extract_0ad_resources.py from 0 A.D. 0.28's random map scripts",
        "// (maps/random/*.js, run by tools/rmgen_harness.js). Do not edit by hand.",
        "//",
        "// Gaia mines, fruit and hunt each script places outside the player bases, averaged",
        "// over seeds, by map size (128, 256, 384, 512 tiles) and players (2, 4, 8).",
        "",
        "#include <cstdint>",
        "#include <cstring>",
        "",
        "namespace rtso {",
        "",
        "struct ResourceKindShare {",
        "\tconst char *name;   // FloraKind name (berry, apple, date) or fauna template stem",
        "\tuint8_t percent;    // of the map's fruit or hunt",
        "\tint32_t amount;     // ResourceSupply/Max (fruit)",
        "};",
        "",
        "struct MapResourceCounts {",
        "\tconst char *map_id; // MapTypeInfo::id",
        "\tuint16_t metal_large[4][3], metal_small[4][3], stone_large[4][3], stone_small[4][3], fruit[4][3], hunt[4][3];",
        "\tint32_t metal_large_amount, metal_small_amount, stone_large_amount, stone_small_amount;",
        "\tResourceKindShare fruit_kinds[2];",
        "\tResourceKindShare hunt_kinds[4]; // empty for generic-biome scripts: the biome's animals",
        "};",
        "",
        "inline constexpr MapResourceCounts kMapResourceCounts[] = {",
    ]
    for r in rows:
        fk = ", ".join('{"%s", %d, %d}' % (k[0], p, k[1]) for k, p in r["fruit"]) or ""
        hk = ", ".join('{"%s", %d, 0}' % (k, p) for k, p in r["hunt"]) or ""
        out.append('\t{"%s", %s, %s, %s, %s, %s, %s, %d, %d, %d, %d, {%s}, {%s}},' % (
            r["id"], g(r["grid"]["ml"]), g(r["grid"]["ms"]), g(r["grid"]["sl"]), g(r["grid"]["ss"]),
            g(r["grid"]["fruit"]), g(r["grid"]["hunt"]), r["amount"]["ml"], r["amount"]["ms"],
            r["amount"]["sl"], r["amount"]["ss"], fk, hk))
    out += [
        "};",
        "",
        "inline const MapResourceCounts *map_resource_counts(const char *map_id) {",
        "\tfor (const auto &r : kMapResourceCounts)",
        "\t\tif (std::strcmp(r.map_id, map_id) == 0) return &r;",
        "\treturn nullptr;",
        "}",
        "",
        "// A grid's count at a map of tiles x tiles for players: linear between the sizes and",
        "// player counts above, held at the ends.",
        "inline int map_resource_count(const uint16_t (&grid)[4][3], float tiles, int players) {",
        "\tconst float ts = tiles < 128.0f ? 0.0f : tiles > 512.0f ? 3.0f : (tiles - 128.0f) / 128.0f;",
        "\tconst float ps = players <= 2 ? 0.0f : players >= 8 ? 2.0f : players <= 4 ? (players - 2) / 2.0f : 1.0f + (players - 4) / 4.0f;",
        "\tconst int t0 = ts >= 3.0f ? 2 : (int)ts, p0 = ps >= 2.0f ? 1 : (int)ps;",
        "\tconst float tf = ts - (float)t0, pf = ps - (float)p0;",
        "\tconst float a = grid[t0][p0] + (grid[t0][p0 + 1] - grid[t0][p0]) * pf;",
        "\tconst float b = grid[t0 + 1][p0] + (grid[t0 + 1][p0 + 1] - grid[t0 + 1][p0]) * pf;",
        "\treturn (int)(a + (b - a) * tf + 0.5f);",
        "}",
        "",
        "} // namespace rtso",
        "",
    ]
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zad", required=True)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--seeds", default="1,2")
    ap.add_argument("--only", default="", help="comma-separated RTS0 map ids, for a quick look (writes a table of only those)")
    ap.add_argument("--cache", default="", help="directory to keep each run's output in, so a rerun skips it")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    public = a.zad
    if os.path.isdir(os.path.join(public, "binaries", "data", "mods", "public")):
        public = os.path.join(public, "binaries", "data", "mods", "public")
    seeds = [int(s) for s in a.seeds.split(",")]
    work = tempfile.mkdtemp(prefix="rts0_rmgen_")
    tpl = os.path.join(work, "templates.json")
    with open(tpl, "w") as fh:
        json.dump(load_templates(os.path.join(public, "simulation", "templates")), fh)
    decode_heightmaps(public, work)
    maps = []
    for mid, rb in rts0_maps():
        script = ALIAS.get(mid, mid)
        if mid in SKIP or not os.path.exists(os.path.join(public, "maps", "random", script + ".js")):
            continue
        if a.only and mid not in a.only.split(","):
            continue
        biome, generic = biome_for(public, script, rb)
        maps.append((mid, script, biome, generic))
    jobs = [(m, ti, pi, s) for m in maps for ti in range(len(TILES)) for pi in range(len(PLAYERS)) for s in seeds]
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        outs = list(ex.map(lambda j: run((public, tpl, work, j[0][1], TILES[j[1]], PLAYERS[j[2]], j[3], j[0][2], a.cache)), jobs))
    rows = {}
    for (m, ti, pi, s), out in zip(jobs, outs):
        rows.setdefault(m, {}).setdefault((ti, pi), []).append(out)
    result = []
    for m in maps:
        if all(o is None for outs in rows[m].values() for o in outs):
            sys.exit("%s: no run of its script finished" % m[1])
        grid, amount, fruit, hunt = summarise(rows[m])
        result.append({"id": m[0], "grid": grid, "amount": amount, "fruit": shares(fruit, 2),
                       "hunt": [] if m[3] else shares(hunt, 4)})
    text = emit(result)
    if a.check:
        if not os.path.exists(OUT_HEADER) or open(OUT_HEADER).read() != text:
            sys.exit("resource_counts_data.h is out of date: run tools/extract_0ad_resources.py")
        print("resource_counts_data.h is up to date")
        return
    with open(OUT_HEADER, "w") as fh:
        fh.write(text)
    print("wrote %s (%d maps)" % (os.path.relpath(OUT_HEADER, REPO), len(result)))


if __name__ == "__main__":
    main()
