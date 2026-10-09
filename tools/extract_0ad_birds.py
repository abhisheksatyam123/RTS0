#!/usr/bin/env python3
"""Where 0 A.D. puts its ambient birds (template_bird: birds/buzzard, the hawk actor).

Birds have no gameplay effect in 0 A.D. (no vision, not selectable, not
attackable), so RTS0 draws them in the view only and the sim never sees them.

Ported maps: every birds/* entity of the 0 A.D. map, as a position relative to
the map centre in map widths (the same transform as tools/port_0ad_maps.py).
Random maps: the counts the 0 A.D. map scripts place (they roam the whole map,
RoamDistance 1000 m, so only the count matters).

Writes data/birds.json:
  {"maps": {"<map id>": [[u, v], ...]},
   "random": {"<map id>": {"per_player": n} | {"min": a, "max": b}}}
where min/max are scaleByMapSize(a, b) arguments.

Usage: python3 tools/extract_0ad_birds.py
"""
import json
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)
import port_0ad_maps as pm  # noqa: E402

# maps/random/*.js (0ad/0ad master): the hawk placements.
RANDOM = {
    "african_plains": {"per_player": 3},       # createFood([[oHawk, 1, 1]...], [3 * numPlayers, ...])
    "danubius": {"min": 5, "max": 10},         # createFood(... oHawk ..., scaleByMapSize(5, 10))
    "fields_of_meroe": {"min": 1, "max": 3},   # for (i < scaleByMapSize(1, 3)) placeEntityAnywhere(oHawk)
    "jebel_barkal": {"min": 0, "max": 2},      # for (i < scaleByMapSize(0, 2)) placeEntityAnywhere(oHawk)
}


def birds_of(source, xml_rel, pmp_rel):
    root = ET.fromstring(source.read(xml_rel))
    span = struct.unpack("<I", source.read(pmp_rel)[12:16])[0] * 16 * 4.0  # tiles * 4 m
    out = []
    for e in root.iter("Entity"):
        tpl = (e.findtext("Template") or "").strip()
        if not tpl.startswith("birds/"):
            continue
        p = e.find("Position")
        if p is None:
            continue
        x, z = float(p.get("x", 0)), float(p.get("z", 0))
        out.append([round(x / span - 0.5, 4), round(z / span - 0.5, 4)])
    return out


def main():
    jobs = [(mid, x, p) for mid, x, p, *_ in pm.maps_list]
    jobs += [(mid, f"maps/skirmishes/{n}.xml", f"maps/skirmishes/{n}.pmp") for n, (mid, _) in pm.github_skirmishes.items()]
    jobs += [(mid, f"maps/scenarios/{n}.xml", f"maps/scenarios/{n}.pmp") for n, (mid, _) in pm.github_scenarios.items()]
    maps = {}
    src = pm.GithubSource()
    for mid, xml_rel, pmp_rel in jobs:
        try:
            b = birds_of(src, xml_rel, pmp_rel)
        except Exception as ex:
            print(f"  !! {mid}: {ex}", file=sys.stderr)
            continue
        if b:
            maps[mid] = b
    path = os.path.join(os.path.dirname(ROOT), "data", "birds.json")
    with open(path, "w") as f:
        json.dump({"source": "0ad/0ad@master maps (birds/* entities) and maps/random/*.js (oHawk)",
                   "maps": dict(sorted(maps.items())), "random": RANDOM}, f, indent=1)
        f.write("\n")
    print(f"{sum(len(v) for v in maps.values())} birds on {len(maps)} of {len(jobs)} maps -> {path}", file=sys.stderr)


if __name__ == "__main__":
    main()
