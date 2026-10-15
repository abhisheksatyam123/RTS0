#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the art
it converts is CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

Convert each civilization's 0 A.D. structure actors that RTS0 still draws with
a generic model (stables, ranges, forges, arsenals, theaters, outposts, sentry
towers, fields, palisades and the civ-specific special buildings) into
static GLBs under assets/models/buildings/<civ>_<model>.glb.

extract_0ad_assets.py reads the cached .pmd files of 0 A.D.'s release archive,
which this repository does not ship. Like extract_0ad_fauna_models.py this tool
reads the Collada sources of a 0 A.D. git checkout instead. Each civ's
structure template (simulation/templates/structures/<civ>/<name>.xml) names the
actor; the actor's most frequent variant in each group gives the mesh,
textures and props, and props sit on their prop points recursively, as in
0 A.D.'s CModel. The unique ambient-occlusion map (aoTex, UV1) is baked into
vertex colours like extract_0ad_assets.ao_vertex_colors. player_trans skins
keep their RGB and write the alpha as a player-colour mask into
assets/textures/player_masks/ and data/player_masks.json (see
tools/extract_player_masks.py and shaders/player_color.gdshader).

Each model is fitted like the generic model it replaces (ASSETS fit/target in
extract_0ad_assets.py), so it keeps the same footprint in the world.

Usage:
  git clone --depth 1 --filter=blob:none --sparse https://github.com/0ad/0ad.git 0ad
  git -C 0ad sparse-checkout set --no-cone /binaries/data/mods/public/simulation/templates/structures/ \\
      '/binaries/data/mods/public/simulation/templates/template_structure*' \\
      /binaries/data/mods/public/art/actors/structures/ /binaries/data/mods/public/art/actors/props/ \\
      /binaries/data/mods/public/art/variants/ /binaries/data/mods/public/art/meshes/structural/ \\
      /binaries/data/mods/public/art/meshes/props/ /binaries/data/mods/public/art/textures/skins/structural/ \\
      /binaries/data/mods/public/art/textures/skins/props/
  python3 -I tools/extract_0ad_structures.py --0ad 0ad [--only athen_stable ...]
"""

import argparse
import json
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_assets import ASSETS, GlbBuilder, _img_png, ao_vertex_colors  # noqa: E402
from extract_0ad_fauna_models import (COLLADA, ZUP, Art, _floats, _mat, _source, load_texture,  # noqa: E402
                                      resolve_actor, scene_nodes, world_matrices)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO, "assets", "models", "buildings")
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
MASK_DIR = os.path.join(REPO, "assets", "textures", "player_masks")
MASK_JSON = os.path.join(REPO, "data", "player_masks.json")
# <civ>_<model> -> GLB id, for ModelLibrary: civs sharing an actor share one GLB.
ALIAS_JSON = os.path.join(REPO, "data", "building_models.json")

# RTS0 civ code -> 0 A.D. template folder (the same codes).
CIVS = ["athen", "spart", "pers", "rome", "cart", "mace", "brit", "gaul", "iber", "maur", "kush",
        "ptol", "sele", "han"]

# RTS0 building model id -> 0 A.D. template names under structures/<civ>/, best first.
TEMPLATES = {
    "stable": ("stable",),
    "range": ("range",),
    "arsenal": ("arsenal",),
    "blacksmith": ("forge",),
    "theater": ("theater", "amphitheater_pompeii"),
    "embassy": ("embassy",),
    "outpost": ("outpost",),
    "sentry_tower": ("sentry_tower",),
    "stable_elephant": ("elephant_stable",),
    "great_hall": ("syssiton", "assembly", "hall"),
    "library": ("library",),
    "pyramid_minor": ("pyramid_small",),
    "gymnasium": ("gymnasium",),
    "prytaneion": ("prytaneion",),
    "palace": ("palace", "hall"),
    "amphitheater": ("amphitheater_pompeii",),
    "kennel": ("kennel",),
    "lighthouse": ("lighthouse",),
    "gate": ("wall_gate",),
    "wall_segment": ("wall_long",),
    "palisade": ("palisades_long",),
    # WallSet segments (Simulation::cmd_build_wall), drawn by Building::half_size.
    "wall_short": ("wall_short",),
    "wall_medium": ("wall_medium",),
    "wall_long": ("wall_long",),
    "wall_gate": ("wall_gate",),
}
CIV_BUILDING_DATA = os.path.join(REPO, "src", "simulation", "civ_building_data.h")


def unique_buildings():
    """(stand-in model, model id, template stem) of each civ-specific BuildingType
    whose model id is its own, from civ_building_data.h (extract_0ad_rosters.py)."""
    import re
    text = open(CIV_BUILDING_DATA).read()
    rows = re.findall(r'\{"[^"]+", "([^"]+)", "([^"]+)",[^}]*\}, /\* structures/\w+/(\w+) \*/', text)
    return [(stand_in, mid, stem) for stand_in, mid, stem in rows]


# extract_0ad_rosters.py UNIQUE_BUILDINGS draw these with a stand-in model until
# they have their own (the stand-in sets the fit).
UNIQUE_STAND_IN = {
    "gerousia": "great_hall", "syssiton": "great_hall", "assembly": "great_hall",
    "tachara": "palace", "army_camp": "barracks", "monument": "pyramid_minor",
    "pillar": "pyramid_minor", "mercenary_camp": "barracks", "military_colony": "town_center",
    "camp_blemmye": "barracks", "camp_noba": "barracks", "temple_amun": "temple",
    "temple_vesta": "temple", "temple_mars": "temple", "ice_house": "farmstead",
    "super_dock": "dock",
}
# Small monuments sized from their 0 A.D. footprint (7-8 m) rather than the stand-in.
UNIQUE_FIT = {"pillar": ("width", 1.4), "monument": ("width", 1.6)}

# Aliases onto GLBs that tools/extract_0ad_assets.py --zip converts from 0.28's
# public.zip (its ZIP_STRUCTURES): this script reads the Collada checkout,
# which has neither the Germans nor Carthage's 0.28 embassies and apartment,
# so a full run keeps these rows.
ZIP_ALIASES = {
    "germ_arsenal": "s_germans_workshop", "germ_blacksmith": "s_germans_forge",
    "germ_outpost": "s_celts_outpost", "germ_range": "s_germans_range",
    "germ_stable": "s_germans_stable", "germ_wall_gate": "s_germans_wooden_wall_gate",
    "germ_wall_long": "s_germans_wooden_wall_long", "germ_wall_medium": "s_germans_wooden_wall_medium",
    "germ_wall_short": "s_germans_wooden_wall_short",
    "cart_embassy": "s_carthaginians_embassy_celtic",
    "cart_embassy_iberian": "s_carthaginians_embassy_iberian", "embassy_iberian": "s_carthaginians_embassy_iberian",
    "cart_embassy_italic": "s_carthaginians_embassy_italic", "embassy_italic": "s_carthaginians_embassy_italic",
    "cart_apartment": "s_carthaginians_apartment", "apartment": "s_carthaginians_apartment",
    "germ_encampment": "s_germans_encampment", "encampment": "s_germans_encampment",
    "germ_encampment_fortified": "s_germans_encampment_fortified",
    "encampment_fortified": "s_germans_encampment_fortified",
    # The Gaia shrines and trading posts (BLD_SHRINE, BLD_TRADING_POST) keep their template's
    # civ (sim_types.h gaia_structure_template), so each civ's id names its template's actor;
    # kush_shrine is extract_0ad_assets.py's own, and Macedonia's shrine_greek_02 draws _01.
    "gaul_shrine": "s_celts_small_stone_monument", "han_shrine": "s_han_pagoda",
    "ptol_shrine": "s_ptolemies_kiosk", "mace_shrine": "s_hellenes_shrine_01",
    "rome_shrine": "s_romans_temple_vesta", "pers_shrine": "s_persians_shrine",
    "gaul_trading_post": "s_celts_tavern", "pers_trading_post": "s_persians_market",
    "trading_post": "s_persians_market",
    "inn": "s_persians_alt_building_04", "pers_inn": "s_persians_alt_building_04",
    "tent": "s_romans_rome_tent", "rome_tent": "s_romans_rome_tent",
    "warehouse": "s_persians_alt_building_01", "pers_warehouse": "s_persians_alt_building_01",
    # The Germans' Cerannus totem is their Revered Monument; their wallset is wooden.
    "germ_monument": "s_germans_cult_statue",
    "germ_gate": "s_germans_wooden_wall_gate", "germ_wall_segment": "s_germans_wooden_wall_long",
    # Rome's siege WallSet pieces (BLD_SIEGE_WALL by half_size, BLD_SIEGE_WALL_GATE).
    "siege_wall_long": "s_romans_siege_wall_long", "siege_wall_medium": "s_romans_siege_wall_medium",
    "siege_wall_short": "s_romans_siege_wall_short", "siege_wall_tower": "s_romans_siege_wall_tower",
    "siege_wall_gate": "s_romans_siege_wall_gate",
}

# Generic models with no GLB yet: model id -> template under structures/.
GENERIC = {"palisade": "palisades_long",
           # Palisade WallSet pieces (WALLSET_PALISADE), drawn by Building::half_size.
           "palisade_short": "palisades_short", "palisade_medium": "palisades_medium",
           "palisade_long": "palisades_long", "palisade_tower": "palisades_tower",
           "palisade_gate": "palisades_gate"}
# Fit for ids extract_0ad_assets.py never sized (width in model units).
EXTRA_FIT = {"palisade": ("width", 3.0),
             # 1 RTS0 unit = 4 m: WallSet segments keep 0 A.D.'s 12, 24 and 36 m.
             "wall_short": ("scale", 0.25), "wall_medium": ("scale", 0.25), "wall_long": ("scale", 0.25),
             "wall_gate": ("scale", 0.25),
             "palisade_short": ("scale", 0.25), "palisade_medium": ("scale", 0.25),
             "palisade_long": ("scale", 0.25), "palisade_tower": ("scale", 0.25),
             "palisade_gate": ("scale", 0.25)}


def template_actor(tdir, name):
    """VisualActor/Actor of a template, following its parents."""
    seen = 0
    while name and seen < 12:
        path = os.path.join(tdir, name + ".xml")
        if not os.path.exists(path):
            return None
        root = ET.parse(path).getroot()
        a = root.find("VisualActor/Actor")
        if a is not None and a.text:
            return a.text.strip()
        name = root.get("parent")
        seen += 1
    return None


def geometry_corners(doc, mesh):
    """Triangles of a <mesh> as (position index, normal, uv0, uv1) corners."""
    verts_el = mesh.find(COLLADA + "vertices")
    pos_src = verts_el.find(COLLADA + "input").get("source")
    positions = _floats(_source(doc, pos_src).find(COLLADA + "float_array")).reshape(-1, 3)
    corners = []
    for prim in list(mesh.findall(COLLADA + "triangles")) + list(mesh.findall(COLLADA + "polylist")):
        ins = prim.findall(COLLADA + "input")
        stride_p = max(int(i.get("offset")) for i in ins) + 1
        data = {}
        for i in ins:
            sem = i.get("semantic")
            if sem == "TEXCOORD":
                sem = "TEXCOORD1" if "TEXCOORD0" in data else "TEXCOORD0"
            if sem in data:
                continue
            arr = None
            if sem != "VERTEX":
                s = _source(doc, i.get("source"))
                acc = s.find(COLLADA + "technique_common/" + COLLADA + "accessor")
                arr = _floats(s.find(COLLADA + "float_array")).reshape(-1, int(acc.get("stride")))
            data[sem] = (int(i.get("offset")), arr)
        p = [int(x) for x in prim.find(COLLADA + "p").text.split()]
        counts = [3] * (len(p) // (3 * stride_p))
        if prim.tag == COLLADA + "polylist":
            counts = [int(x) for x in prim.find(COLLADA + "vcount").text.split()]
        k = 0
        for c in counts:
            poly = []
            for _ in range(c):
                rec = p[k: k + stride_p]
                k += stride_p
                vi = rec[data["VERTEX"][0]]
                nrm = data["NORMAL"][1][rec[data["NORMAL"][0]]][:3] if "NORMAL" in data else np.array([0, 0, 1.0])
                uv0 = data["TEXCOORD0"][1][rec[data["TEXCOORD0"][0]]][:2] if "TEXCOORD0" in data else np.zeros(2)
                uv1 = data["TEXCOORD1"][1][rec[data["TEXCOORD1"][0]]][:2] if "TEXCOORD1" in data else None
                poly.append((vi, tuple(np.round(nrm, 5)), tuple(np.round(uv0, 6)),
                             None if uv1 is None else tuple(np.round(uv1, 6))))
            for j in range(1, c - 1):
                corners += [poly[0], poly[j + 1], poly[j]]
    return positions, corners


def read_static(path):
    """Scene nodes and rest-pose geometries of a .dae (skinned meshes in their bind pose)."""
    doc = ET.parse(path).getroot()
    nodes = scene_nodes(doc)
    rest_world = world_matrices(nodes, {n: v[2] for n, v in nodes.items()})
    # 0 A.D.'s converter reads Y_UP files as (x, y, -z) and Z_UP ones as (x, z, y):
    # bring a Y_UP file into Z_UP space so props from both kinds compose.
    up = doc.find(COLLADA + "asset/" + COLLADA + "up_axis")
    to_zup = np.eye(4)
    if up is not None and (up.text or "").strip() == "Y_UP":
        to_zup = np.array([[1, 0, 0, 0], [0, 0, -1, 0], [0, 1, 0, 0], [0, 0, 0, 1]], dtype=np.float64)
        rest_world = {k: to_zup @ v for k, v in rest_world.items()}
    by_id = {v[0]: k for k, v in nodes.items()}
    geoms = {g.get("id"): g for g in doc.iter(COLLADA + "geometry")}
    pieces = []
    for n in doc.iter(COLLADA + "node"):
        place = rest_world.get(by_id.get(n.get("id")), to_zup)
        for ig in n.findall(COLLADA + "instance_geometry"):
            g = geoms.get(ig.get("url", "").lstrip("#"))
            if g is not None and g.find(COLLADA + "mesh") is not None:
                pieces.append((place, *geometry_corners(doc, g.find(COLLADA + "mesh"))))
        for ic in n.findall(COLLADA + "instance_controller"):
            ctrl = next((c for c in doc.iter(COLLADA + "controller") if c.get("id") == ic.get("url", "").lstrip("#")), None)
            skin = ctrl.find(COLLADA + "skin") if ctrl is not None else None
            if skin is None:
                continue
            g = geoms.get(skin.get("source").lstrip("#"))
            bsm_el = skin.find(COLLADA + "bind_shape_matrix")
            bsm = _mat(bsm_el.text) if bsm_el is not None else np.eye(4)
            # Bind pose: a static building's rig rests where it was bound.
            pieces.append((to_zup @ bsm, *geometry_corners(doc, g.find(COLLADA + "mesh"))))
    return {"nodes": nodes, "rest_world": rest_world, "pieces": pieces}


def prop_point(nodes, attach):
    """The scene node 0 A.D.'s CModel attaches a prop to: prop_<name> or prop-<name>
    (scene_nodes() prefixes non-joint nodes with "~")."""
    for cand in (f"prop_{attach}", f"prop-{attach}", attach):
        for pre in ("", "~"):
            if pre + cand in nodes:
                return pre + cand
    return None


class Part:
    def __init__(self, tex, ao, material):
        self.tex, self.ao, self.material = tex, ao, material
        self.pos, self.nrm, self.uv, self.uv1, self.idx = [], [], [], [], []


def add_mesh(parts, m, place, tex, ao, material):
    part = Part(tex, ao, material)
    keys = {}
    for local, positions, corners in m["pieces"]:
        xf = place @ local
        for vi, nrm, uv0, uv1 in corners:
            key = (id(local), vi, nrm, uv0, uv1)
            idx = keys.get(key)
            if idx is None:
                p = ZUP @ (xf @ np.append(positions[vi], 1.0))
                n3 = ZUP[:3, :3] @ (xf[:3, :3] @ np.array(nrm))
                n3 = n3 / (np.linalg.norm(n3) or 1.0)
                idx = len(part.pos)
                keys[key] = idx
                part.pos.append(p[:3])
                part.nrm.append(tuple(float(x) for x in n3))
                part.uv.append((float(uv0[0]), 1.0 - float(uv0[1])))
                part.uv1.append(None if uv1 is None else (float(uv1[0]), float(uv1[1])))
            part.idx.append(idx)
    if part.idx:
        parts.append(part)


def compose(art, actor, place, parts, sources, missing, depth=0):
    if depth > 6:
        return
    if actor.startswith("particle/") or not os.path.exists(art.path("actors", actor)):
        missing.add(actor)
        return
    a = resolve_actor(art, actor)
    sources.add(f"actor:art/actors/{actor}")
    m = None
    if a["mesh"]:
        mp = art.path("meshes", a["mesh"])
        if os.path.exists(mp):
            m = read_static(mp)
            sources.add(f"mesh:{a['mesh']}")
            tex = a["tex"].get("baseTex")
            if tex:
                sources.add(f"skin:{tex}")
                add_mesh(parts, m, place, tex, a["tex"].get("aoTex"), a["material"])
        else:
            missing.add(a["mesh"])
            MISSING.add("meshes/" + a["mesh"])
    for prop_actor, attach in a["props"]:
        if not prop_actor:
            continue
        pp = None
        if attach and attach != "root" and m is not None:
            pp = prop_point(m["nodes"], attach)
            if pp is None:
                continue  # 0 A.D. skips props whose point the mesh lacks
        sub = place if pp is None else place @ m["rest_world"][pp]
        compose(art, prop_actor, sub, parts, sources, missing, depth + 1)


MISSING = set()


def texture_rgba(art, ref, cache):
    if ref not in cache:
        try:
            cache[ref] = load_texture(art, ref)
        except SystemExit:
            cache[ref] = None
            MISSING.add("textures/skins/" + ref)
    return cache[ref]


def convert(art, model_id, actor, fit, target, masks, tex_cache):
    parts, sources, missing = [], set(), set()
    compose(art, actor, np.eye(4), parts, sources, missing)
    parts = [p for p in parts if texture_rgba(art, p.tex, tex_cache) is not None]
    # One primitive per skin: props sharing a texture draw in one call.
    merged = {}
    for p in parts:
        key = (p.tex, p.ao, p.material)
        if key not in merged:
            merged[key] = p
            continue
        m = merged[key]
        base = len(m.pos)
        m.pos += p.pos
        m.nrm += p.nrm
        m.uv += p.uv
        m.uv1 += p.uv1
        m.idx += [i + base for i in p.idx]
    parts = list(merged.values())
    if not parts:
        print(f"  ! {model_id}: nothing to draw ({actor})")
        return None
    pos = np.array([p for part in parts for p in part.pos])
    lo, hi = pos.min(axis=0), pos.max(axis=0)
    dim = hi - lo
    if fit == "scale":
        scale = target
    elif fit == "width":
        scale = target / max(dim[0], dim[2], 1e-6)
    else:
        scale = target / max(dim[1], 1e-6)
    # Walls keep 0 A.D.'s ground plane: their foundations reach below it and the
    # terrain hides them, as in 0 A.D. Everything else stands on its lowest vertex.
    centre = np.array([(lo[0] + hi[0]) / 2, 0.0 if fit == "scale" else lo[1], (lo[2] + hi[2]) / 2])

    glb = GlbBuilder()
    mask_rows = {}
    for part in parts:
        img = texture_rgba(art, part.tex, tex_cache)
        player = part.material.startswith("player_trans")
        cutout = not player and ("basic_trans" in part.material or "alphatest" in part.material)
        png = _img_png(img if cutout else img.convert("RGB"))
        img_idx = glb.add_image(png)
        mat_idx = glb.add_material(img_idx, "MASK" if cutout else "OPAQUE", True)
        if player and img.getextrema()[3][0] < 250:
            small = Image.open(__import__("io").BytesIO(png))
            w, h = small.size
            rel = f"{os.path.splitext(part.tex)[0]}_{w}x{h}.png"
            path = os.path.join(MASK_DIR, rel)
            if not os.path.exists(path):
                os.makedirs(os.path.dirname(path), exist_ok=True)
                img.getchannel("A").resize((w, h), Image.LANCZOS).point(
                    lambda a: 255 - a if a < 250 else 0).save(path, optimize=True)
            mask_rows[f"buildings/{model_id}_tex{img_idx}"] = {
                "mask": "res://" + os.path.relpath(path, REPO), "tint": [1.0, 1.0, 1.0]}
        colors = None
        if part.ao and any(u is not None for u in part.uv1):
            ao = texture_rgba(art, part.ao, tex_cache)
            if ao is not None:
                sources.add(f"skin:{part.ao}")
                colors = ao_vertex_colors(ao.convert("RGBA"), part.uv1)
        p = [tuple(float(x) for x in (v - centre) * scale) for v in part.pos]
        glb.primitives.append((p, part.nrm, part.uv, part.idx, mat_idx, True, "MASK", None, None, None, colors))
    blob = glb.build(model_id)
    out = os.path.join(OUT_DIR, model_id + ".glb")
    with open(out, "wb") as fh:
        fh.write(blob)
    masks.update(mask_rows)
    d = dim * scale
    print(f"[{model_id}] {actor}: {len(pos)} verts, {len(parts)} parts, {d[0]:.2f} x {d[1]:.2f} x {d[2]:.2f}, "
          f"{len(blob) // 1024} KB" + (f", skipped {len(missing)} missing actors/meshes" if missing else ""))
    return {"file": f"models/buildings/{model_id}.glb", "parts": len(parts), "origin": "ground-center",
            "size_meters": [round(float(x), 3) for x in d], "sources": sorted(sources),
            "upstream_material": resolve_actor(art, actor)["material"],
            "generator": "tools/extract_0ad_structures.py"}


def fit_of(base):
    return UNIQUE_FIT.get(base) or EXTRA_FIT.get(base) or tuple(ASSETS[base][1:3])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zeroad", required=True, help="0 A.D. git checkout")
    ap.add_argument("--only", nargs="+")
    ap.add_argument("--all", action="store_true", help="also replace civ models that already exist")
    args = ap.parse_args()
    art = Art(args.zeroad)
    tdir = os.path.join(args.zeroad, "binaries", "data", "mods", "public", "simulation", "templates")

    # One GLB per distinct (actor, fit): s_<actor path>, e.g. s_hellenes_wooden_tower.
    # A civ whose 0 A.D. actor is the one its generic model was made from, or
    # that already has its own <civ>_<model>.glb, keeps what it has.
    jobs = {}
    aliases = {}
    for mid, tmpl in GENERIC.items():
        actor = template_actor(tdir, "structures/" + tmpl)
        if actor:
            jobs[mid] = (actor, mid)
    for civ in CIVS:
        for mid, names in TEMPLATES.items():
            for n in names:
                actor = template_actor(tdir, f"structures/{civ}/{n}")
                if not actor:
                    continue
                cid = f"{civ}_{mid}"
                generic = ASSETS.get(mid, ("",))[0] == "art/actors/" + actor
                # The Han batch's han_wall_<length>.glb are sized for one cell, not the WallSet.
                if not generic and (mid.startswith("wall_") and mid != "wall_segment"
                                    or not os.path.exists(os.path.join(OUT_DIR, cid + ".glb"))):
                    gid = "s_" + os.path.splitext(actor)[0].replace("structures/", "").replace("/", "_")
                    if mid == "palisade" or fit_of(mid) != fit_of(jobs.get(gid, (None, mid))[1]):
                        gid += "_" + mid
                    jobs.setdefault(gid, (actor, mid))
                    aliases[cid] = gid
                break
    # Civ-specific BuildingTypes: <civ>_<model id> for every civ whose folder has
    # the template (the Seleucids build the Macedonian library, and so on).
    for _cur, mid, stem in unique_buildings():
        stand_in = UNIQUE_STAND_IN.get(mid)
        if not stand_in:
            continue
        for civ in CIVS:
            actor = template_actor(tdir, f"structures/{civ}/{stem}")
            if not actor:
                continue
            if ASSETS.get(mid, ("",))[0] == "art/actors/" + actor:
                continue  # <mid>.glb is this actor already
            gid = "s_" + os.path.splitext(actor)[0].replace("structures/", "").replace("/", "_")
            fit_key = mid if mid in UNIQUE_FIT else stand_in
            if gid in jobs and fit_of(jobs[gid][1]) != fit_of(fit_key):
                gid += "_" + fit_key
            jobs.setdefault(gid, (actor, fit_key))
            aliases[f"{civ}_{mid}"] = gid
            # A civ without the template (a captured building) draws the first civ's.
            aliases.setdefault(mid, gid)
    entries = {}
    masks = {}
    tex_cache = {}
    for model_id, (actor, base) in sorted(jobs.items()):
        if args.only and model_id not in args.only:
            continue
        if not args.only and not args.all and os.path.exists(os.path.join(OUT_DIR, model_id + ".glb")):
            continue
        fit, target = fit_of(base)
        e = convert(art, model_id, actor, fit, target, masks, tex_cache)
        if e:
            entries[model_id] = e
    if not args.only:
        with open(ALIAS_JSON, "w") as fh:
            aliases.update(ZIP_ALIASES)
            json.dump(dict(sorted(aliases.items())), fh, indent=0)
            fh.write("\n")
    if entries:
        with open(MANIFEST) as fh:
            man = json.load(fh)
        man["models"].update(entries)
        man["models"] = dict(sorted(man["models"].items()))
        with open(MANIFEST, "w") as fh:
            json.dump(man, fh, indent=1, sort_keys=True)
    if masks:
        with open(MASK_JSON) as fh:
            rows = json.load(fh)
        rows.update(masks)
        with open(MASK_JSON, "w") as fh:
            json.dump(rows, fh, indent=0, sort_keys=True)
            fh.write("\n")
    print(f"{len(entries)} models, {len(masks)} masked images")
    for m in sorted(MISSING):
        print("missing art/" + m)


if __name__ == "__main__":
    main()
