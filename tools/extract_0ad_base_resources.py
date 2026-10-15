#!/usr/bin/env python3
"""Generate RTS0's per-map player-base Gaia resources from 0 A.D. 0.28's random map scripts.

Every 0 A.D. random map script gives rmgen-common's placePlayerBases (player.js) the
resources round each civic centre, or rmgen2's createBases does (setup.js createBase):
  * "StartingAnimal": groupCount (default 2) groups of the template (default
    gaia/fauna_chicken; rmgen2 maps pass the biome's startingAnimal), each of
    round(5 * chicken Max / template Max) animals unless min/maxGroupCount say otherwise;
  * "Berries": min/maxCount (default 5) bushes of the template (usually the biome's fruitBush);
  * "Trees": count (default scaleByMapSize(7, 20)) trees of the template (usually tree1).
A script that leaves one of them out places none. This reads those arguments from the
scripts' source and writes src/simulation/base_resources_data.h, which
RmsGenerator::distribute_player_bases follows. Scripts with their own base code (no
placePlayerBases or createBases call) are left out and keep RTS0's defaults.

Usage:
  python3 -I tools/extract_0ad_base_resources.py --maps PATH [--check]

PATH is 0 A.D.'s maps/random directory (rmgen2/setup.js beside the scripts).
"""

import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_HEADER = os.path.join(REPO, "src", "simulation", "base_resources_data.h")
# RTS0 map ids whose 0 A.D. script has another name (as extract_0ad_resources.py).
ALIAS = {"archipelago_0ad": "archipelago", "mediterranean_0ad": "mediterranean", "oasis_0ad": "oasis",
         "nearestern_badlands": "neareastern_badlands"}


def block_after(text, start):
    """The {...} block that opens at or after `start`, braces matched, strings and comments skipped."""
    i = text.index("{", start)
    depth, j = 0, i
    while j < len(text):
        c = text[j]
        if c in "\"'":
            k = j + 1
            while text[k] != c:
                k += 2 if text[k] == "\\" else 1
            j = k
        elif text.startswith("//", j):
            j = text.index("\n", j)
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
        j += 1
    raise ValueError("unbalanced braces")


def sub_block(obj, key):
    m = re.search(r'"%s"\s*:\s*\{' % key, obj)
    return block_after(obj, m.start()) if m else None


def field(block, key):
    """The expression after "key": up to the next top-level comma or the block's end."""
    m = re.search(r'"%s"\s*:\s*' % key, block)
    if not m:
        return None
    i, depth, out = m.end(), 0, []
    while i < len(block):
        c = block[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                break
            depth -= 1
        elif c == "," and depth == 0:
            break
        out.append(c)
        i += 1
    return "".join(out).strip()


def resolve(expr, script):
    """A template expression: a string literal, g_Gaia.<slot>, or a const naming one of those."""
    if expr is None:
        return None
    expr = expr.strip()
    m = re.fullmatch(r'"([^"]+)"', expr)
    if m:
        return m.group(1)
    m = re.fullmatch(r"g_Gaia\.(\w+)", expr)
    if m:
        return "biome:" + m.group(1)
    if re.fullmatch(r"\w+", expr):
        m = re.search(r"\b(?:const|let|var)\s+%s\s*=\s*([^;\n]+);" % re.escape(expr), script)
        if m:
            return resolve(m.group(1), script)
    return "expr:" + expr


def count_range(expr, default):
    """(min, max) of a count: a number, scaleByMapSize(a, b) (min at 128 tiles, max at 512), or unknown."""
    if expr is None:
        return default
    expr = expr.strip()
    if re.fullmatch(r"\d+", expr):
        return (int(expr), int(expr))
    m = re.fullmatch(r"(?:Math\.floor\()?scaleByMapSize\((\d+),\s*(\d+)\)\)?", expr)
    if m:
        return (int(m.group(1)), int(m.group(2)))
    return None


def base_args(script, setup):
    """The object literal the script gives placePlayerBases, or rmgen2's createBase one."""
    m = re.search(r"\bplacePlayerBases\s*\(", script)
    if m:
        return block_after(script, m.end())
    if re.search(r"\bcreateBases\s*\(", script):
        m = re.search(r"\bplacePlayerBase\s*\(", setup)
        return block_after(setup, m.end())
    return None


def extract(maps_dir):
    setup = open(os.path.join(maps_dir, "rmgen2", "setup.js")).read()
    rows, skipped = {}, []
    for f in sorted(os.listdir(maps_dir)):
        if not f.endswith(".js") or f.endswith("_triggers.js"):
            continue
        name = f[:-3]
        script = open(os.path.join(maps_dir, f)).read()
        obj = base_args(script, setup)
        if obj is None:
            skipped.append(name)
            continue
        row = {}
        a = sub_block(obj, "StartingAnimal")
        if a is not None:
            tpl = resolve(field(a, "template"), script) or "gaia/fauna_chicken"
            groups = count_range(field(a, "groupCount"), (2, 2))
            lo = count_range(field(a, "minGroupCount"), None)
            hi = count_range(field(a, "maxGroupCount"), None)
            row["animal"] = (tpl, groups[0] if groups else 2, lo[0] if lo else 0, hi[0] if hi else 0)
        b = sub_block(obj, "Berries")
        if b is not None:
            tpl = resolve(field(b, "template"), script)
            lo = count_range(field(b, "minCount"), (5, 5))
            hi = count_range(field(b, "maxCount"), (5, 5))
            row["berries"] = (tpl, lo[0] if lo else 5, hi[0] if hi else 5)
        t = sub_block(obj, "Trees")
        if t is not None:
            tpl = resolve(field(t, "template"), script)
            expr = field(t, "count")
            savanna = 0
            m = re.fullmatch(r'currentBiome\(\)\s*==\s*"generic/savanna"\s*\?\s*(\d+)\s*:\s*(.+)', expr or "")
            if m:
                savanna, expr = int(m.group(1)), m.group(2)
            rng = count_range(expr, (7, 20))
            if rng is None:
                print(f"{name}: Trees count {field(t, 'count')!r} not understood, using scaleByMapSize(7, 20)",
                      file=sys.stderr)
                rng = (7, 20)
            row["trees"] = (tpl, rng[0], rng[1], savanna)
        rows[name] = row
    return rows, skipped


def c_str(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"') if s is not None else "nullptr"


def header(rows):
    rev = {v: k for k, v in ALIAS.items()}
    out = ["#pragma once",
           "",
           "// Generated by tools/extract_0ad_base_resources.py from 0 A.D. 0.28's random map scripts",
           "// (placePlayerBases / rmgen2 createBase arguments). Do not edit by hand.",
           "//",
           "// Templates are 0 A.D. template names, or \"biome:<slot>\" for the biome's g_Gaia slot",
           "// (fruitBush, tree1, startingAnimal); nullptr where the script names none.",
           "",
           "#include <cstdint>",
           "#include <cstring>",
           "",
           "namespace rtso {",
           "",
           "struct MapBaseResources {",
           "\tconst char *map_id;          // MapTypeInfo::id",
           "\tbool animal;                  // StartingAnimal given",
           "\tconst char *animal_template;  // default gaia/fauna_chicken",
           "\tuint8_t animal_groups;        // groupCount (default 2)",
           "\tuint8_t animal_min, animal_max; // min/maxGroupCount; 0: round(5 * 40 / template Max)",
           "\tbool berries;",
           "\tconst char *berry_template;",
           "\tuint8_t berry_min, berry_max; // min/maxCount (default 5)",
           "\tbool trees;",
           "\tconst char *tree_template;",
           "\tuint8_t tree_min, tree_max;   // count: scaleByMapSize(min, max), or min == max",
           "\tuint8_t tree_savanna;         // the count on the savanna biome, if the script sets one (else 0)",
           "};",
           "",
           "inline constexpr MapBaseResources kMapBaseResources[] = {"]
    for name in sorted(rows):
        r = rows[name]
        a = r.get("animal")
        b = r.get("berries")
        t = r.get("trees")
        out.append("\t{%s, %s, %s, %d, %d, %d, %s, %s, %d, %d, %s, %s, %d, %d, %d}," % (
            c_str(rev.get(name, name)),
            "true" if a else "false", c_str(a[0] if a else None), a[1] if a else 0, a[2] if a else 0, a[3] if a else 0,
            "true" if b else "false", c_str(b[0] if b else None), b[1] if b else 0, b[2] if b else 0,
            "true" if t else "false", c_str(t[0] if t else None), t[1] if t else 0, t[2] if t else 0, t[3] if t else 0))
    out += ["};",
            "",
            "inline const MapBaseResources *map_base_resources(const char *map_id) {",
            "\tfor (const MapBaseResources &r : kMapBaseResources)",
            "\t\tif (!std::strcmp(r.map_id, map_id)) return &r;",
            "\treturn nullptr;",
            "}",
            "",
            "} // namespace rtso",
            ""]
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--maps", required=True, help="0 A.D. maps/random directory")
    ap.add_argument("--check", action="store_true", help="fail if the header would change")
    args = ap.parse_args()
    rows, skipped = extract(args.maps)
    text = header(rows)
    print(f"{len(rows)} scripts; own base code (left out): {', '.join(skipped)}", file=sys.stderr)
    if args.check:
        have = open(OUT_HEADER).read() if os.path.exists(OUT_HEADER) else ""
        if have != text:
            print(f"{OUT_HEADER} is out of date", file=sys.stderr)
            return 1
        return 0
    with open(OUT_HEADER, "w") as f:
        f.write(text)
    print(f"wrote {OUT_HEADER}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
