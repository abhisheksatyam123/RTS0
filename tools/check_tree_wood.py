#!/usr/bin/env python3
"""Checks the per-slot tree wood in terrain_biomes.h and script_terrains.h.

Every BiomeFlora tree slot (tree12, tree3, tree45) stands for one 0 A.D. template;
its *_wood number must be that template's ResourceSupply/Max, resolved through the
template's parent chain and mixins (parent="a|b": mixins/a.xml over b). The table
below names the template of each slot, from rmbiome/generic/<biome>.json and
setupBiome_<biome> or the map script's constants. None is a slot with no wood
template (temperate's tree3 is gaia/fruit/apple, food): its number must be 0, which
the game reads as TREE_WOOD_SUPPLY.

Usage: python3 tools/check_tree_wood.py [--templates ../0ad/binaries/data/mods/public/simulation/templates]
Compiles a small dumper of the two headers with g++ and compares.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# A 0 A.D. checkout next to the repository.
DEFAULT_TEMPLATES = os.path.join(os.path.dirname(ROOT), "0ad/binaries/data/mods/public/simulation/templates")

# name: ([[tree1, tree2] per option], [tree3 options], [[tree4, tree5] per option])
SLOTS = {
    # Biomes (terrain_biomes.h biome_flora).
    "aegean": ([["cypress_wild", "pine_maritime_short"]],
               ["olive", "juniper_prickly", "date_palm", "cretan_date_palm_short", "medit_fan_palm"],
               [["poplar_lombardy", "carob"]]),
    "alpine": ([["pine", "fir"]], ["oak_hungarian_autumn"], [["fir_sapling", "elm"]]),
    "polar": ([["fir_winter", "pine_w"]], ["pine_w"], [["fir_winter", "fir_winter"]]),
    "oasis": ([["cretan_date_palm_short", "date_palm"], ["date_palm", "cretan_date_palm_tall"]], ["juniper_prickly"],
              [["date_palm", "date_palm"], ["cretan_date_palm_patch", "cretan_date_palm_patch"]]),
    "temperate": ([["oak", "oak_hungarian"], ["oak_holly", "maple"], ["oak_hungarian", "oak_holly"]], [None],
                  [["pine", "pine_maritime"]]),
    "steppe": ([["bush_steppe_01", "bush_steppe_02"]], ["bush_steppe_01"], [["bush_steppe_03", "bush_steppe_02"]]),
    "savanna": ([["baobab", "acacia"]], ["acacia"], [["bush_tropic", "bush_temperate"]]),
    # "india" (banyan, strangler; teak; palm_palmyra, palm_tropical) is INDIA_BIOME below: the
    # india map script shares its name.
    # Map scripts (script_terrains.h). Where a script mixes several species in one
    # forest, the slots split them as the comment says.
    "aegean_sea": ([["carob", "cretan_date_palm_short"]], ["medit_fan_palm", "poplar_lombardy", "cypress"],
                   [["carob", "cretan_date_palm_tall"]]),
    "anatolian_plateau": ([["poplar_lombardy", "poplar_lombardy"]], ["bush_temperate", "poplar_lombardy"],
                          [["poplar_lombardy", "poplar_lombardy"]]),
    "arctic_summer": ([["bush_badlands", "bush_temperate"]], ["bush_badlands"], [["bush_badlands", "bush_temperate"]]),
    "ardennes_forest": ([["oak", "pine"]], ["oak_large"], [["fir", "pine"]]),
    "atlas_mountains": ([["carob", "carob"]], ["carob", "aleppo_pine"], [["aleppo_pine", "aleppo_pine"]]),
    "botswanan_haven": ([["toona", "baobab"]], ["toona", "baobab", "bush_temperate"],
                        [["bush_temperate", "bush_temperate"]]),
    "corsica": ([["aleppo_pine", "cretan_date_palm_tall"]], ["aleppo_pine", "medit_fan_palm"],
                [["medit_fan_palm", "cypress"]]),
    "cycladic_archipelago": ([["carob", "euro_beech"]], ["cretan_date_palm_short", "cypress"],
                             [["medit_fan_palm", "medit_fan_palm"], ["aleppo_pine", "aleppo_pine"],
                              ["poplar_lombardy", "poplar_lombardy"]]),
    "danubius": ([["oak_aut", "oak_aut_new"]], ["oak", "euro_beech", "acacia"],
                 [["euro_beech_aut", "euro_beech"], ["euro_beech", "acacia"]]),
    "dodecanese": ([["carob", "cretan_date_palm_short"]], ["poplar_lombardy", "cypress", "cretan_date_palm_tall"],
                   [["medit_fan_palm", "poplar_lombardy"], ["poplar_lombardy", "cypress"]]),
    # oPalms: tall, short, palm_tropic, date_palm, senegal_date_palm, medit_fan_palm at random;
    # five of the six hold 200, so the slot takes date_palm.
    "elephantine": ([["date_palm", "date_palm"]], ["acacia"], [["acacia", "acacia"]]),
    "english_channel": ([["euro_beech", "euro_beech"]], ["poplar"], [["oak", "oak"]]),
    "extinct_volcano": ([["dead", "euro_beech"]], ["euro_beech", "oak", "bush_temperate"], [["oak", "oak_dead"]]),
    "fields_of_meroe": ([["acacia", "acacia"]], ["baobab", "acacia"], [["date_palm", "cretan_date_palm_short"]]),
    "fortress": ([["euro_beech_aut", "euro_beech_aut"], ["oak_aut", "oak_aut"]], ["oak_aut", "euro_beech_aut", "pine"],
                 [["pine", "pine"]]),
    "guadalquivir_river": ([["poplar", "poplar"]], ["carob"], [["carob", "carob"]]),
    "india": ([["palm_tropic", "palm_tropic"]], ["palm_tropic"], [["palm_tropic", "palm_tropic"]]),
    "jebel_barkal": ([["date_palm", "date_palm"]], ["acacia"], [["acacia", "acacia"]]),  # oPalms, as elephantine
    "kerala": ([["toona", "toona"]], ["toona", "palm_tropic"], [["palm_tropic", "palm_tropic"]]),
    "latium": ([["carob", "euro_beech"]], ["poplar_lombardy", "aleppo_pine"],
               [["medit_fan_palm", "medit_fan_palm"], ["aleppo_pine", "aleppo_pine"],
                ["poplar_lombardy", "poplar_lombardy"]]),
    "lorraine_plain": ([["euro_beech", "euro_beech"]], ["oak", "euro_beech"], [["oak", "oak"]]),
    "northern_lights": ([["pine_w", "pine_w"]], ["pine_w"], [["pine_w", "pine_w"]]),
    "oasis_0ad": ([["cretan_date_palm_short", "cretan_date_palm_tall"]], ["cretan_date_palm_short"],
                  [["cretan_date_palm_tall", "cretan_date_palm_short"]]),
    "phoenician_levant": ([["cretan_date_palm_short", "cretan_date_palm_tall"], ["cretan_date_palm_short", "carob"]],
                          ["medit_fan_palm", "poplar_lombardy", "cypress"],
                          [["cretan_date_palm_tall", "cretan_date_palm_short"], ["cretan_date_palm_tall", "carob"]]),
    "polar_sea": ([], [], []),
    "pyrenean_sierra": ([["aleppo_pine", "euro_beech"]], ["aleppo_pine", "euro_beech"], [["aleppo_pine", "euro_beech"]]),
    "rhine_marshlands": ([["euro_beech", "euro_beech"]], ["oak", "euro_beech"], [["oak", "oak"]]),
    "river_archipelago": ([["toona", "toona"]], ["palm_tropical"], [["palm_tropic", "palm_tropical"]]),
    "saharan_oases": ([["date_palm", "cretan_date_palm_short"]], ["cretan_date_palm_short"],
                      [["cretan_date_palm_short", "date_palm"]]),
    "sahel": ([["baobab", "baobab"]], ["baobab"], [["baobab", "baobab"]]),
    "sahel_watering_holes": ([["baobab", "baobab"]], ["baobab"], [["baobab", "baobab"]]),
    "scythian_rivulet": ([["dead", "oak_dead"]], ["dead", "oak_dead", "pine", "bush_badlands"], [["dead", "dead"]]),
    "syria": ([["date_palm", "date_palm"]], ["date_palm", "tamarix", "aleppo_pine"], [["tamarix", "tamarix"]]),
    "volcanic_lands": ([["dead", "dead"]], ["dead"], [["dead", "dead"]]),
}

DUMPER = r"""
#include <cstdio>
#include "simulation/script_terrains.h"
using namespace rtso;
static void dump(const char *name, const BiomeFlora &f) {
	std::printf("\"%s\": [[", name);
	for (int i = 0; i < 3; ++i) std::printf("%s[%d, %d]", i ? ", " : "", f.tree12_wood[i][0], f.tree12_wood[i][1]);
	std::printf("], [");
	for (int i = 0; i < 5; ++i) std::printf("%s%d", i ? ", " : "", f.tree3_wood[i]);
	std::printf("], [");
	for (int i = 0; i < 3; ++i) std::printf("%s[%d, %d]", i ? ", " : "", f.tree45_wood[i][0], f.tree45_wood[i][1]);
	std::printf("]]");
}
int main(int argc, char **argv) {
	std::printf("{");
	bool first = true;
	for (int a = 1; a < argc; a += 2) {
		const BiomeFlora *f = argv[a + 1][0] == 'b' ? biome_flora(argv[a]) : &script_terrains(argv[a])->flora;
		if (!first) std::printf(", ");
		first = false;
		dump(argv[a], *f);
	}
	std::printf("}\n");
}
"""

INDIA_BIOME = ([["banyan", "strangler"]], ["teak"], [["palm_palmyra", "palm_tropical"]])

BIOMES = {"aegean", "alpine", "polar", "oasis", "temperate", "steppe", "savanna"}


def chain(templates, name):
    """The template and its ancestors, lowest priority first."""
    root = ET.parse(os.path.join(templates, name + ".xml")).getroot()
    out = []
    parent = root.get("parent")
    if parent:
        parts = parent.split("|")
        out += chain(templates, parts[-1])
        for mixin in reversed(parts[:-1]):
            out += chain(templates, "mixins/" + mixin)
    out.append(root)
    return out


def wood(templates, name):
    value = None
    for node in chain(templates, "gaia/tree/" + name):
        m = node.find("ResourceSupply/Max")
        if m is not None:
            value = int(m.text)
    return value


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--templates", default=DEFAULT_TEMPLATES)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = os.path.join(tmp, "dump.cpp"), os.path.join(tmp, "dump")
        with open(src, "w") as f:
            f.write(DUMPER)
        subprocess.run(["g++", "-std=c++20", "-I" + os.path.join(ROOT, "src"), src, "-o", exe], check=True)
        argv = [exe]
        for name in SLOTS:
            # "india" is both a biome and a map script.
            argv += [name, "b" if name in BIOMES else "s"]
        argv += ["india", "b"]
        out = subprocess.run(argv, check=True, capture_output=True, text=True).stdout
    # The india biome comes last and would shadow the script; read both in order.
    pairs = json.loads(out, object_pairs_hook=lambda kv: kv)
    biome_india = pairs[-1][1]
    tables = dict(pairs[:-1])
    errors, checked = 0, 0

    def check(where, names, values):
        nonlocal errors, checked
        for i, value in enumerate(values):
            want = 0 if i >= len(names) or names[i] is None else wood(args.templates, names[i])
            checked += i < len(names)
            if value != want:
                errors += 1
                print(f"{where}[{i}] {names[i] if i < len(names) else '(unused)'}: header {value}, template {want}")

    def check_entry(name, slots, got):
        t12, t3, t45 = slots
        for o in range(3):
            check(f"{name}.tree12_wood[{o}]", t12[o] if o < len(t12) else [], got[0][o])
            check(f"{name}.tree45_wood[{o}]", t45[o] if o < len(t45) else [], got[2][o])
        check(f"{name}.tree3_wood", t3, got[1])

    for name, slots in SLOTS.items():
        check_entry(name, slots, tables[name])
    check_entry("india (biome)", INDIA_BIOME, biome_india)
    print(f"{checked} slots checked, {errors} mismatches")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
