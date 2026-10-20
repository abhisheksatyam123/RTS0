#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the meshes
and textures it converts are CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

Many 0.28 unit actors pick the helmet itself at random: a group whose variants
each set only the "helmet" prop to another helmet actor (Greek hoplites wear
Corinthian, Chalcidian, Thracian or Illyrian helmets, each with its own skin,
crest and cheek pieces). The unit GLBs bake the most frequent one. This tool
finds, for each such unit GLB, the baked helmet's parts (the helmet mesh and the
crest and mask props it carries, each under the one rigid transform the helmet
mesh's .pmd fits) and writes:

  assets/models/units/helm_<actor>.glb   each other helmet actor, composed in
      its own space with its props and skins (extract_0ad_assets.compile_asset),
      registered in assets/models/manifest.json so the player-mask, normal and
      specular tables cover its skins
  data/unit_helmets.json  {"<glb id>": {"parts": [baked part, ...], "joint": skin
      joint, "xf": [12 floats, basis columns then origin: helmet space to the
      parts'], "helmets": ["" (the baked one), "res://...glb", ...],
      "freq": [n, ...], "salt": "<actor>#helmet"}}

ModelLibrary::set_unit_helmet draws one per unit by its id. Crowds keep the
baked helmet.

Usage: python3 -I tools/extract_unit_helmets.py --zip <0.28 public.zip> [--check]
"""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET
import zipfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from add_0ad_clips import read_glb  # noqa: E402
from add_0ad_mesh_looks import read  # noqa: E402
import extract_0ad_assets as X  # noqa: E402
from extract_unit_prop_looks import fit  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
OUT_JSON = os.path.join(REPO, "data", "unit_helmets.json")
MAX_HELMETS = 12
FIT_TOL = 2e-3  # of the part's size


def helmet_group(vfs, unit_actor, cache):
    """[(helmet actor path, frequency)] of the unit actor's first group whose
    variants each set only the helmet prop, the baked (most frequent) first."""
    try:
        root = ET.fromstring(vfs.read(unit_actor))
    except (KeyError, ET.ParseError):
        return None
    for g in root.findall("group"):
        live = [v for v in g.findall("variant") if X._variant_freq(v) > 0]
        if len(live) < 2:
            continue
        out = []
        for v in live:
            r = X.resolve_variant_chain(vfs, v, cache)
            if r is None or (r.findtext("mesh") or "").strip() or r.find("textures") is not None:
                out = None
                break
            props = list(r.iter("prop"))
            if not props or any((p.get("attachpoint") or "").lower() != "helmet" for p in props):
                out = None
                break
            out.append((props[0].get("actor") or "", X._variant_freq(v), v))
        if not out or len({a for a, _f, _v in out}) < 2:
            continue
        best = X._pick_variant(g)
        out.sort(key=lambda e: e[2] is not best)
        freq = {}
        for a, f, _v in out:
            freq[a] = freq.get(a, 0) + f
        order = list(dict.fromkeys(a for a, _f, _v in out))
        return [(a, freq[a]) for a in order]
    return None


def helmet_parts(vfs, actor):
    """The helmet actor composed as the unit extraction composes it: [(positions
    in helmet space, vertex count)] per part, the helmet mesh first."""
    parts, _mat = X.compose_actor(vfs, "art/actors/" + actor, 1)
    out = []
    for p in parts or []:
        xf = p.transform if p.transform is not None else X.trs((0, 0, 0), (0, 0, 0, 1))
        pos = np.array([X.mat_apply(xf, v[0]) for v in p.mesh_data["verts"]], dtype=np.float64)
        if len(pos):
            out.append(pos)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=os.path.normpath(X.DEFAULT_ZIP))
    ap.add_argument("--check", action="store_true", help="fail if data/unit_helmets.json is stale")
    args = ap.parse_args()
    with open(MANIFEST) as f:
        manifest = json.load(f)
    models = manifest["models"]
    zf = zipfile.ZipFile(args.zip)
    vfs, cache = X.Vfs(zf), {}
    table, wanted = {}, {}
    composed = {}

    for gid in sorted(models):
        m = models[gid]
        if not m.get("file", "").startswith("models/units/") or gid.startswith("helm_"):
            continue
        units = [s[len("actor:"):] for s in m.get("sources", []) if s.startswith("actor:art/actors/units/")]
        if not units:
            continue
        group = helmet_group(vfs, units[0], cache)
        if not group or not group[0][0]:
            continue
        baked = group[0][0]
        if "actor:art/actors/" + baked not in m["sources"]:
            continue
        if baked not in composed:
            composed[baked] = helmet_parts(vfs, baked)
        hparts = composed[baked]
        if not hparts:
            continue
        gltf, bin_ = read_glb(os.path.join(REPO, "assets", m["file"]))
        parts = []
        for nd in gltf["nodes"]:
            if "mesh" not in nd or nd.get("name", "").startswith("tool_") or nd.get("skin") is None:
                continue
            prims = gltf["meshes"][nd["mesh"]]["primitives"]
            if len(prims) != 1:
                continue
            p = prims[0]
            joints = read(gltf, bin_, p["attributes"]["JOINTS_0"])
            weights = read(gltf, bin_, p["attributes"]["WEIGHTS_0"])
            live = np.unique(joints[weights > 0])
            if len(live) != 1:
                continue
            parts.append((nd["name"], int(live[0]), read(gltf, bin_, p["attributes"]["POSITION"]).astype(np.float64)))
        # The helmet mesh fixes the transform; its props must sit under the same one.
        head = hparts[0]
        found = None
        for name, joint, pos in parts:
            if len(pos) != len(head):
                continue
            res = fit(head, pos)
            if res is None or res[3] > FIT_TOL * max(np.ptp(pos, 0).max(), 1e-6):
                continue
            found = (name, joint, res)
            break
        if not found:
            print(f"[{gid}] baked helmet {baked} not found")
            continue
        name, joint, (s, r, t, _miss) = found
        hide = [name]
        for hp in hparts[1:]:
            moved = s * hp @ r.T + t
            for pn, pj, pos in parts:
                if pn in hide or pj != joint or len(pos) != len(moved):
                    continue
                if np.abs(pos - moved).max() <= FIT_TOL * max(np.ptp(pos, 0).max(), 1e-6) + 1e-4:
                    hide.append(pn)
                    break
        if len(hide) != len(hparts):
            print(f"[{gid}] {baked}: {len(hide)}/{len(hparts)} parts found, skipped")
            continue
        helmets, freqs = [""], [group[0][1]]
        for actor, f in group[1:]:
            if len(helmets) >= MAX_HELMETS:
                freqs[0] += f
                continue
            if not actor:  # a bare head: 0.28 draws no helmet
                helmets.append("none")
                freqs.append(f)
                continue
            hid = "helm_" + os.path.splitext(os.path.basename(actor))[0]
            wanted[hid] = actor
            helmets.append(f"res://assets/models/units/{hid}.glb")
            freqs.append(f)
        basis = s * r
        xf = [float(x) for x in list(basis[:, 0]) + list(basis[:, 1]) + list(basis[:, 2]) + list(t)]
        table[gid] = {"parts": hide, "joint": joint, "xf": [round(x, 6) for x in xf],
                      "helmets": helmets, "freq": freqs, "salt": units[0] + "#helmet"}

    text = json.dumps(table, indent=1, sort_keys=True) + "\n"
    if args.check:
        old = open(OUT_JSON).read() if os.path.exists(OUT_JSON) else ""
        missing = [h for h in wanted if h not in models]
        if old != text or missing:
            print(f"stale: {OUT_JSON}" + (f", {len(missing)} helmet GLBs missing" if missing else ""))
            sys.exit(1)
        print(f"{len(table)} unit GLBs, {len(wanted)} helmets: up to date")
        return
    for hid, actor in sorted(wanted.items()):
        rec = X.compile_asset(vfs, hid, "art/actors/" + actor, "scale", 1.0, None,
                              os.path.join(REPO, "assets"), "models/units", origin=(0.0, 0.0, 0.0))
        rec["origin"] = "actor"
        models[hid] = rec
    with open(MANIFEST, "w") as f:
        json.dump(manifest, f, indent=1)
        f.write("\n")
    with open(OUT_JSON, "w") as f:
        f.write(text)
    print(f"{len(table)} unit GLBs, {len(wanted)} helmet GLBs -> {os.path.relpath(OUT_JSON, REPO)}")


if __name__ == "__main__":
    main()
