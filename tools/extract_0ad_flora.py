#!/usr/bin/env python3
"""Generates src/simulation/flora_data.h: one FloraKind for each 0 A.D. gaia/tree and
gaia/fruit template whose actor has no model among RTS0's 27 flora families.

The 27 families (FLORA_DEFAULT .. FLORA_TEAK in sim_types.h) each draw one species
model (tools/fetch_0ad_trees.py, assets/models/vegetation). Every other template
gets its own kind from FLORA_TEMPLATE_FIRST on. Its model is
assets/models/vegetation/flora_<tree|fruit>_<stem>.glb (tools/extract_0ad_assets.py
ZIP_FLORA), its name in map JSON is "<tree|fruit>/<stem>"
(tools/port_0ad_maps.py), and its family is the family kind the template drew
before, which game logic still uses (flora_family, flora_is_fruit). Kinds are only
ever appended, so saved FloraKind bytes keep their meaning: a template that leaves
0 A.D. stays in ORDER, and a new one goes at its end.

  python3 -I tools/extract_0ad_flora.py --0ad PATH/TO/0ad [--check]
"""

import argparse
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_rosters import TemplateLoader  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_HEADER = os.path.join(REPO, "src", "simulation", "flora_data.h")
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")

# FloraKind 0-26 (sim_types.h), in order.
FAMILIES = ["tree", "broadleaf", "pine", "palm", "oak", "baobab", "acacia", "bamboo", "bush", "berry", "apple", "date",
            "carob", "poplar", "fan_palm", "beech", "dead", "cypress", "aleppo_pine", "fir", "tropic_bush", "olive",
            "banyan", "tropic_palm", "maple", "juniper", "teak"]
FLORA_TEMPLATE_FIRST = len(FAMILIES)


def family_of_tree(name):
    """The family a gaia/tree/<name> template draws as (first match wins: the specific
    species before the families they share words with)."""
    for keys, kind in ((["dead"], "dead"), (["bush_tropic"], "tropic_bush"), (["bush", "sapling", "shrub"], "bush"),
                       (["baobab"], "baobab"), (["acacia"], "acacia"), (["bamboo"], "bamboo"),
                       (["fan_palm"], "fan_palm"), (["palm_tropic", "palmyra", "palm_doum", "senegal_date"], "tropic_palm"),
                       (["palm", "date"], "palm"), (["aleppo", "pine_maritime", "pine_black"], "aleppo_pine"),
                       (["fir", "pine_w"], "fir"), (["cypress"], "cypress"), (["juniper"], "juniper"),
                       (["pine", "cedar", "spruce", "larch", "yew"], "pine"), (["oak"], "oak"), (["carob"], "carob"),
                       (["poplar"], "poplar"), (["beech"], "beech"), (["olive"], "olive"),
                       (["banyan", "strangler"], "banyan"), (["maple"], "maple"), (["teak", "toona"], "teak")):
        if any(k in name for k in keys):
            return kind
    return "broadleaf"


def family_of_fruit(name):
    for keys, kind in ((["berry", "grape"], "berry"), (["apple", "fig"], "apple"), (["date"], "date")):
        if any(k in name for k in keys):
            return kind
    return "tree"  # FLORA_DEFAULT: the biome's fruit bush


# Kinds already handed out, in order (append only). Generated from the templates once;
# --check fails when a template is missing here so a new one is appended deliberately.
ORDER = [
    "fruit/apple", "fruit/banana", "fruit/berry_02", "fruit/berry_03", "fruit/berry_04", "fruit/berry_05",
    "fruit/date", "fruit/fig", "fruit/grapes",
    "tree/acacia_large", "tree/bamboo_dragon", "tree/bamboo_single", "tree/baobab_1_sapling", "tree/baobab_2_young",
    "tree/baobab_3_mature", "tree/baobab_4_dead", "tree/bush_badlands", "tree/bush_mediterranean",
    "tree/bush_mediterranean_dry", "tree/bush_steppe_01", "tree/bush_steppe_02", "tree/bush_steppe_03",
    "tree/bush_temperate", "tree/bush_temperate_autumn", "tree/bush_temperate_winter", "tree/cedar_atlas_1_sapling",
    "tree/cedar_atlas_2_young", "tree/cedar_atlas_3_mature", "tree/cedar_atlas_4_dead", "tree/cherry_blossom",
    "tree/cretan_date_palm_patch", "tree/cretan_date_palm_short", "tree/cretan_date_palm_tall", "tree/cypress_wild",
    "tree/cypress_windswept", "tree/date_palm", "tree/date_palm_dead", "tree/elm", "tree/elm_dead",
    "tree/euro_beech_aut", "tree/euro_birch", "tree/fir_sapling", "tree/fir_winter", "tree/mangrove",
    "tree/maple_autumn", "tree/oak_aut", "tree/oak_aut_new", "tree/oak_dead", "tree/oak_holly", "tree/oak_holly_autumn",
    "tree/oak_hungarian", "tree/oak_hungarian_autumn", "tree/oak_large", "tree/palm_areca", "tree/palm_doum",
    "tree/palm_palmyra", "tree/palm_royal", "tree/palm_tropical", "tree/pine_black", "tree/pine_black_dead",
    "tree/pine_maritime", "tree/pine_maritime_short", "tree/pine_w", "tree/poplar", "tree/poplar_dead",
    "tree/poplar_lombardy_autumn", "tree/poplar_lombardy_dead", "tree/senegal_date_palm", "tree/strangler",
    "tree/tamarix", "tree/temperate", "tree/temperate_autumn", "tree/temperate_winter", "tree/toona",
    "tree/tropic_rainforest",
]


def family_actors():
    """0 A.D. actors the family models are made from (not the flora_* models)."""
    with open(MANIFEST) as fh:
        models = json.load(fh)["models"]
    have = set()
    for mid, m in models.items():
        if m.get("file", "").startswith("models/vegetation/") and not mid.startswith("flora_"):
            for s in m.get("sources", []):
                if s.startswith("actor:"):
                    have.add(s[len("actor:"):].replace("art/actors/", "", 1))
    return have


def obstruction_width(t):
    """Obstruction/Static width in metres (template_gaia_tree 1.6, template_gaia_fruit 2.5)."""
    n = t.get("Obstruction/Static")
    return float(n.attrs.get("width", 0)) if n is not None else 0.0


def family_widths(loader):
    """The obstruction width of each family, from the templates it draws (0 when none does)."""
    have = family_actors()
    widths = [0.0] * len(FAMILIES)
    for i, fam in enumerate(FAMILIES[1:], 1):
        seen = []
        for kind in ("tree", "fruit"):
            for path in sorted(glob.glob(os.path.join(loader.root, "gaia", kind, "*.xml"))):
                stem = os.path.basename(path)[:-4]
                if (family_of_fruit(stem) if kind == "fruit" else family_of_tree(stem)) != fam:
                    continue
                t = loader.load(f"gaia/{kind}/{stem}")
                if t.value("VisualActor/Actor", "") in have:
                    seen.append(obstruction_width(t))
        widths[i] = max(set(seen), key=seen.count) if seen else 0.0
    return widths


def template_rows(loader):
    """name -> (actor, title, family, fruit, width) for each template with no family model."""
    have = family_actors()
    rows = {}
    for kind in ("tree", "fruit"):
        for path in sorted(glob.glob(os.path.join(loader.root, "gaia", kind, "*.xml"))):
            stem = os.path.basename(path)[:-4]
            t = loader.load(f"gaia/{kind}/{stem}")
            actor = t.value("VisualActor/Actor", "")
            if not actor or actor in have:
                continue
            title = t.value("Identity/SpecificName", "") or t.value("Identity/GenericName", stem)
            fam = family_of_fruit(stem) if kind == "fruit" else family_of_tree(stem)
            rows[f"{kind}/{stem}"] = (actor, title, fam, kind == "fruit", obstruction_width(t))
    return rows


def model_id(name):
    return "flora_" + name.replace("/", "_")


def emit(rows, fam_widths):
    out = []
    w = out.append
    w("// Generated by tools/extract_0ad_flora.py from 0 A.D. 0.28 simulation/templates/gaia/{tree,fruit}.")
    w("// Do not edit by hand. Code: GPL-2.0+; the 0 A.D. data it describes: CC BY-SA 3.0.")
    w("#ifndef RTSO_FLORA_DATA_H")
    w("#define RTSO_FLORA_DATA_H")
    w("")
    w("#include <cstdint>")
    w("")
    w("namespace rtso {")
    w("")
    w("// FloraKinds from here on are single 0 A.D. templates (after the 27 families in sim_types.h).")
    w(f"constexpr int FLORA_TEMPLATE_FIRST = {FLORA_TEMPLATE_FIRST};")
    w(f"constexpr int FLORA_TEMPLATE_COUNT = {len(ORDER)};")
    w("")
    w("enum FloraTemplateKind : uint8_t {")
    for i, name in enumerate(ORDER):
        w(f"\tFLORA_{name.replace('/', '_').upper()} = {FLORA_TEMPLATE_FIRST + i},")
    w("};")
    w("")
    w("struct FloraTemplateInfo {")
    w("\tconst char *name;     // map JSON resource type: <tree|fruit>/<template stem>")
    w("\tconst char *title;    // Identity/SpecificName (GenericName when none)")
    w("\tconst char *model_id; // assets/models/vegetation/<model_id>.glb (its VisualActor)")
    w("\tuint8_t family;       // the FloraKind (0-26) game logic treats it as")
    w("\tbool fruit;           // gaia/fruit: a food supply")
    w("\tfloat obstruction;    // Obstruction/Static width = depth, metres")
    w("};")
    w("")
    w("// i = kind - FLORA_TEMPLATE_FIRST.")
    w("inline const FloraTemplateInfo &flora_template_info(int i) {")
    w("\tstatic const FloraTemplateInfo kTable[FLORA_TEMPLATE_COUNT] = {")
    for name in ORDER:
        actor, title, fam, fruit, width = rows[name]
        w(f"\t\t{{\"{name}\", \"{title}\", \"{model_id(name)}\", {FAMILIES.index(fam)}, {'true' if fruit else 'false'}, {width}f}}, // {actor}")
    w("\t};")
    w("\treturn kTable[i];")
    w("}")
    w("")
    w("// The obstruction width (metres) of the templates a family kind (0-26) draws as; 0 where")
    w("// no template draws as it (FLORA_DEFAULT among them): its supply decides (template_gaia_tree 1.6, template_gaia_fruit 2.5).")
    w("inline float flora_family_obstruction(int kind) {")
    w(f"\tstatic const float kWidth[{len(FAMILIES)}] = {{" + ", ".join(f"{x}f" for x in fam_widths) + "};")
    w(f"\treturn kind >= 0 && kind < {len(FAMILIES)} ? kWidth[kind] : 0.0f;")
    w("}")
    w("")
    w("} // namespace rtso")
    w("")
    w("#endif")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zeroad", required=True)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    loader = TemplateLoader(os.path.join(a.zeroad, "binaries", "data", "mods", "public", "simulation"))
    rows = template_rows(loader)
    new = sorted(set(rows) - set(ORDER))
    if new:
        print("templates with no kind yet (append them to ORDER): " + ", ".join(new))
        return 1
    text = emit(rows, family_widths(loader))
    if a.check:
        with open(OUT_HEADER) as fh:
            if fh.read() != text:
                print(f"{OUT_HEADER} is out of date; rerun tools/extract_0ad_flora.py")
                return 1
        print("flora_data.h is up to date")
        return 0
    with open(OUT_HEADER, "w") as fh:
        fh.write(text)
    print(f"wrote {OUT_HEADER}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
