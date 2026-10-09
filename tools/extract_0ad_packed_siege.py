#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the art
it converts is CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

Build the packed siege engines (0 A.D.'s *_packed actors: a wagon carrying the
dismantled engine, drawn by two horses and walked by its crew) as animated
GLBs under assets/models/units/<civ>_<actor>.glb, and the table of which RTS0
siege model packs into which (src/graphics/assets/packed_siege_models.h).

A packed actor nests animated actors inside one another: the wagon has its own
skeleton, and the horses and crew are props with their own skeletons and
clips at the wagon's prop points (their helmets and heads are props again).
Each nested rig's bones join one flat skin, posed every frame as the parent's
prop point times the rig's own clip, the way 0 A.D.'s CModel poses a prop.
Everything else (Collada skinning, axes, the GLB layout) comes from
extract_0ad_fauna_models.py.

Every packed model shares one scale, the one that makes its horses as tall as
fauna_horse (1.7), and ModelLibrary keeps that height.

Usage:
  git -C 0ad sparse-checkout add binaries/data/mods/public/art/animation/mechanical
  python3 -I tools/extract_0ad_packed_siege.py --0ad 0ad [--only romans_siege_onager_packed ...]
"""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_assets import GlbBuilder, _img_png, mat3_to_quat  # noqa: E402
from extract_0ad_fauna_models import (  # noqa: E402
    ZUP, Art, load_texture, node_animations, pick_clip, read_mesh, resolve_actor,
    sample, scene_nodes, skin_part)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UNITS = os.path.join(REPO, "assets", "models", "units")
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
HEADER = os.path.join(REPO, "src", "graphics", "assets", "packed_siege_models.h")
# fauna_horse's scale per Collada unit (horse.xml and the wagon horses share a rig size).
SCALE = 0.30825

# RTS0 siege model -> 0 A.D. packed actor (the VisualActor of the template its
# unpacked template's Pack/Entity names; the Hellenic stone throwers and the
# onager take their civ's packed actor).
PACKED = {
    "athen_bolt_shooter": "units/athenians/siege_spear_packed.xml",
    "athen_catapult": "units/athenians/siege_rock_packed.xml",
    "bolt_shooter": "units/athenians/siege_spear_packed.xml",
    "cart_bolt_shooter": "units/carthaginians/siege_spear_packed.xml",
    "cart_catapult": "units/carthaginians/siege_rock_packed.xml",
    "catapult": "units/carthaginians/siege_rock_packed.xml",
    "mace_bolt_shooter": "units/athenians/siege_spear_packed.xml",
    "mace_catapult": "units/athenians/siege_rock_packed.xml",
    "ptol_bolt_shooter": "units/ptolemies/siege_spear_packed.xml",
    "ptol_catapult": "units/ptolemies/siege_rock_packed.xml",
    "rome_bolt_shooter": "units/romans/siege_rock_packed.xml",
    "rome_catapult": "units/romans/siege_onager_packed.xml",
    "sele_bolt_shooter": "units/athenians/siege_spear_packed.xml",
    "sele_catapult": "units/athenians/siege_rock_packed.xml",
    "siege_ballista": "units/romans/siege_rock_packed.xml",
    "siege_ballista_med": "units/romans/siege_rock_packed.xml",
    "siege_lithobolos": "units/hellenes/siege_heavy_artillery_packed.xml",
    "siege_lithobolos_light": "units/hellenes/siege_light_artillery_packed.xml",
    "siege_mangonel": "units/han/siege_mangonel_pivot_packed.xml",
    "siege_onager": "units/romans/siege_onager_packed.xml",
    "siege_oxybeles": "units/hellenes/siege_spear_packed.xml",
    "siege_polybolos": "units/ptolemies/siege_spear_packed.xml",
    "siege_scorpio": "units/romans/siege_scorpio_packed.xml",
    "spart_bolt_shooter": "units/athenians/siege_spear_packed.xml",
    "spart_catapult": "units/athenians/siege_rock_packed.xml",
}

CLIPS = {
    "Idle": ("idle",),
    "Walk": ("walk", "move"),
    "Run": ("run", "walk", "move"),
}


def prop_point(nodes, attach):
    """The scene node a prop hangs from. Joints are keyed by name; static meshes
    keep their prop points as plain nodes, keyed "~" + id (scene_nodes)."""
    cands = (f"prop_{attach}", f"prop-{attach}", attach)
    for c in cands:
        if c in nodes:
            return c
    for name, (nid, _, _) in nodes.items():
        if nid in cands or name.lstrip("~") in cands or (nid or "").endswith("_prop_" + attach):
            return name
    return None


def unscaled(m):
    """A prop point's transform as 0 A.D. keeps it (CModel props: position and
    rotation, no scale; the Han pivot wagon's points are scaled by its rig)."""
    u, _, vt = np.linalg.svd(m[:3, :3])
    r = u @ vt
    if np.linalg.det(r) < 0:
        u[:, -1] *= -1
        r = u @ vt
    out = np.eye(4)
    out[:3, :3] = r
    out[:3, 3] = m[:3, 3]
    return out


ROOT = object()  # a prop at its parent's origin ("root" with no such node)


class Rig:
    """One actor instance: its mesh, scene and clips, hung at a parent's node."""

    def __init__(self, art, actor, parent=None, attach=None, prefix=""):
        self.actor = actor
        self.a = resolve_actor(art, actor)
        self.m = read_mesh(art.path("meshes", self.a["mesh"]))
        self.parent, self.attach, self.prefix = parent, attach, prefix
        # GLB bones: the joints that weigh on a vertex (skin_part keeps four per
        # vertex), plus the prop points other parts hang from (need()). A crewed
        # wagon has five skeletons, and a GLB skin indexes joints in bytes.
        names = self.m["joint_names"]
        used = {names[j] for pairs in (self.m["infl"] or []) for j, _ in sorted(pairs, key=lambda p: -p[1])[:4]}
        self.bones = [n for n in names if n in used]

    def need(self, node):
        if node not in self.bones:
            self.bones.append(node)


def build_rigs(art, actor, sources):
    """The actor tree as Rigs plus rigid/foreign parts: [(rig, part_spec)]."""
    rigs, rigid = [], []

    def visit(actor, parent, attach):
        rig = Rig(art, actor, parent, attach, prefix=f"r{len(rigs)}_")
        rigs.append(rig)
        sources.update({f"actor:art/actors/{actor}", f"mesh:{rig.a['mesh']}"})
        if parent is not None:
            parent.need(attach)
        for prop_actor, point in rig.a["props"]:
            if prop_actor.startswith("particle/") or not os.path.exists(art.path("actors", prop_actor)):
                continue
            pa = resolve_actor(art, prop_actor)
            if not pa["mesh"] or not os.path.exists(art.path("meshes", pa["mesh"])):
                continue
            pm = read_mesh(art.path("meshes", pa["mesh"]))
            pp = prop_point(rig.m["nodes"], point)
            if pm["joint_names"] and all(j in rig.m["nodes"] for j in pm["joint_names"]):
                # Rigged to this rig's skeleton (a horse blanket): skin it to its bones.
                for j in pm["joint_names"]:
                    rig.need(j)
                rigid.append((rig, None, pa, pm, prop_actor))
            elif pp is None and point == "root" and parent is not None and not pm["joint_names"]:
                # 0 A.D.'s "root" point is the model's origin: ride the rig's anchor.
                rigid.append((rig, ROOT, pa, pm, prop_actor))
            elif pp is None:
                print(f"  ! {actor}: no prop point {point} for {prop_actor}")
            elif pm["joint_names"] or pa["props"]:
                visit(prop_actor, rig, pp)
            else:
                rig.need(pp)
                rigid.append((rig, pp, pa, pm, prop_actor))

    visit(actor, None, None)
    return rigs, rigid


def rig_worlds(rigs, clip, t):
    """Collada-space world matrix of every rig node at time t of `clip`
    (None: the rest pose), each nested rig carried by its parent's prop point."""
    out = {}
    for rig in rigs:  # parents come first
        nodes = rig.m["nodes"]
        locals_ = {n: v[2] for n, v in nodes.items()}
        tr = rig.tracks.get(clip) if clip else None
        if tr:
            by_name, length = tr
            tt = t % length if length > 0 else 0.0
            for n, track in by_name.items():
                locals_[n] = sample(track, tt)
        anchor = np.eye(4) if rig.parent is None else unscaled(out[id(rig.parent)][rig.attach])
        cache = {}

        def w(name):
            if name not in cache:
                p = nodes[name][1]
                cache[name] = locals_[name] if p is None else w(p) @ locals_[name]
            return cache[name]

        out[id(rig)] = {n: anchor @ w(n) for n in nodes}
    return out


def load_tracks(art, rig, sources):
    rig.tracks = {}
    for clip, names in CLIPS.items():
        f = pick_clip(rig.a["anims"], names)
        if not f or not os.path.exists(art.path("animation", f)):
            continue
        doc = ET.parse(art.path("animation", f)).getroot()
        anodes = scene_nodes(doc)
        tracks = node_animations(doc)
        by_name = {n: tracks[v[0]] for n, v in anodes.items() if v[0] in tracks and n in rig.m["nodes"]}
        if not by_name:
            continue
        length = max(tr[0][-1] for tr in by_name.values())
        rig.tracks[clip] = (by_name, length, max(len(tr[0]) for tr in by_name.values()))
        sources.add(f"anim:{f}")
    # rig_worlds wants (by_name, length).
    rig.keys = {c: v[2] for c, v in rig.tracks.items()}
    rig.tracks = {c: v[:2] for c, v in rig.tracks.items()}


def packed_id(actor):
    """units/athenians/siege_rock_packed.xml -> athenians_siege_rock_packed"""
    civ, stem = actor.split("/")[-2:]
    return f"{civ}_{os.path.splitext(stem)[0]}"


def convert(art, model_id, actor, out_dir):
    sources = set()
    rigs, rigid = build_rigs(art, actor, sources)
    for rig in rigs:
        load_tracks(art, rig, sources)
    order = [(rig, n) for rig in rigs for n in rig.bones]
    if len(order) > 255:
        raise SystemExit(f"{actor}: {len(order)} bones")
    bone_of = {(id(rig), n): i for i, (rig, n) in enumerate(order)}
    rest = rig_worlds(rigs, None, 0.0)

    parts = []
    for rig in rigs:
        anchor = np.eye(4) if rig.parent is None else unscaled(rest[id(rig.parent)][rig.attach])
        bi = {n: bone_of[(id(rig), n)] for n in rig.bones}
        if rig.m["joint_names"]:
            part = skin_part(rig.m, bi)
            # skin_part rest-poses in the rig's own space; carry it to the anchor.
            part.pos = [(ZUP @ anchor @ ZUP @ np.append(p, 1.0))[:3] for p in part.pos]
            part.nrm = [tuple(float(x) for x in (ZUP[:3, :3] @ anchor[:3, :3] @ ZUP[:3, :3]) @ np.array(n))
                        for n in part.nrm]
        else:
            bone = bone_of[(id(rig.parent), rig.attach)] if rig.parent else 0
            part = skin_part(rig.m, bi, anchor, bone)
        part.tex_ref = rig.a["tex"].get("baseTex")
        part.cutout = "trans" in rig.a["material"] and "player" not in rig.a["material"]
        parts.append(part)
        sources.add(f"skin:{part.tex_ref}")
    for rig, pp, pa, pm, prop_actor in rigid:
        anchor = np.eye(4) if rig.parent is None else unscaled(rest[id(rig.parent)][rig.attach])
        bi = {n: bone_of[(id(rig), n)] for n in rig.bones}
        if pp is None:
            part = skin_part(pm, bi)
            part.pos = [(ZUP @ anchor @ ZUP @ np.append(p, 1.0))[:3] for p in part.pos]
            part.nrm = [tuple(float(x) for x in (ZUP[:3, :3] @ anchor[:3, :3] @ ZUP[:3, :3]) @ np.array(n))
                        for n in part.nrm]
        elif pp is ROOT:
            part = skin_part(pm, bi, anchor, bone_of[(id(rig.parent), rig.attach)])
        else:
            part = skin_part(pm, bi, unscaled(rest[id(rig)][pp]), bi[pp])
        part.tex_ref = pa["tex"].get("baseTex")
        part.cutout = "trans" in pa["material"] and "player" not in pa["material"]
        parts.append(part)
        sources.update({f"actor:art/actors/{prop_actor}", f"mesh:{pa['mesh']}", f"skin:{part.tex_ref}"})

    pos = np.array([p for part in parts for p in part.pos])
    lo, hi = pos.min(axis=0), pos.max(axis=0)
    scale = SCALE
    centre = np.array([(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2])

    def bone_pose(world):
        w = ZUP @ world @ ZUP
        u, _, vt = np.linalg.svd(w[:3, :3])
        r = u @ vt
        if np.linalg.det(r) < 0:
            u[:, -1] *= -1
            r = u @ vt
        return w[:3, 3], r

    rest_pose = [bone_pose(rest[id(rig)][n]) for rig, n in order]

    glb = GlbBuilder()
    for part in parts:
        img = load_texture(art, part.tex_ref)
        if not part.cutout:
            img = img.convert("RGB")
        img_idx = glb.add_image(_img_png(img))
        mat_idx = glb.add_material(img_idx, "MASK" if part.cutout else "OPAQUE", part.cutout)
        glb._add_primitive([tuple(map(float, p)) for p in part.pos], part.nrm, part.uv, part.idx, mat_idx,
                           part.joints, part.weights)

    n_b, n_p = len(order), len(parts)
    nodes_out = [{"name": model_id, "translation": list(map(float, -centre * scale)),
                  "scale": [float(scale)] * 3, "children": list(range(1, n_b + n_p + 1))}]
    for (rig, n), (t, r) in zip(order, rest_pose):
        nodes_out.append({"name": rig.prefix + n.lstrip("~"), "translation": list(map(float, t)),
                          "rotation": list(mat3_to_quat(r.tolist()))})
    for k in range(n_p):
        nodes_out.append({"name": f"{model_id}_part{k}", "mesh": k, "skin": 0})

    animations = []
    for clip in CLIPS:
        # The outermost rig with the clip sets its length (the Han pivot has none;
        # its wagon does), and every other rig loops its own clip inside it.
        top = next((r for r in rigs if clip in r.tracks), None)
        if top is None:
            continue
        length = top.tracks[clip][1]
        nkeys = max(top.keys[clip], 2)
        times = [length * i / (nkeys - 1) for i in range(nkeys)]
        tracks = [{"translation": [], "rotation": []} for _ in order]
        for t in times:
            worlds = rig_worlds(rigs, clip, t)
            for i, (rig, n) in enumerate(order):
                tr, r = bone_pose(worlds[id(rig)][n])
                tracks[i]["translation"].append(tuple(map(float, tr)))
                tracks[i]["rotation"].append(tuple(mat3_to_quat(r.tolist())))
        animations.append({"name": clip, "times": [float(x) for x in times],
                           "tracks": {1 + i: tr for i, tr in enumerate(tracks)}})

    ibm = []
    for t, r in rest_pose:
        m = np.eye(4)
        m[:3, :3] = r
        m[:3, 3] = t
        ibm.append(np.linalg.inv(m).tolist())
    skins = [{"joints": list(range(1, n_b + 1)), "inverseBindMatrices": ibm, "skeleton": 1}]
    blob = glb.build(model_id, nodes=nodes_out, animations=animations or None, skins=skins)
    out = os.path.join(out_dir, model_id + ".glb")
    with open(out, "wb") as fh:
        fh.write(blob)
    dim = (hi - lo) * scale
    print(f"[{model_id}] {actor}: {len(rigs)} rigs, {n_p} parts, {n_b} bones, "
          f"clips {[a['name'] for a in animations]}, {dim[0]:.2f} x {dim[1]:.2f} x {dim[2]:.2f}, "
          f"{len(blob) // 1024} KB")
    return {
        "file": f"models/units/{model_id}.glb", "parts": n_p, "bones": n_b, "animated": bool(animations),
        "skeleton": model_id, "animations": [a["name"] for a in animations],
        "size_meters": [round(float(x), 3) for x in dim], "origin": "ground-center",
        "sources": sorted(s for s in sources if not s.endswith(":None")),
        "upstream_material": rigs[0].a["material"], "generator": "tools/extract_0ad_packed_siege.py",
    }


def write_header():
    rows = "".join(f'\t{{"{m}", "{packed_id(a)}"}},\n' for m, a in sorted(PACKED.items()))
    with open(HEADER, "w") as fh:
        fh.write(f"""// Generated by tools/extract_0ad_packed_siege.py; do not edit.
// The model a siege engine shows while packed (0 A.D. Pack/Entity's VisualActor:
// a wagon, its horses and crew), by the unpacked model it packs from.
#pragma once

#include <cstring>

namespace rtso {{

struct PackedSiegeModel {{
\tconst char *unpacked;
\tconst char *packed;
}};

inline constexpr PackedSiegeModel PACKED_SIEGE_MODELS[] = {{
{rows}}};

// The packed model for `unpacked`, or nullptr when it has none.
inline const char *packed_siege_model(const char *unpacked) {{
\tfor (const PackedSiegeModel &p : PACKED_SIEGE_MODELS)
\t\tif (std::strcmp(p.unpacked, unpacked) == 0) return p.packed;
\treturn nullptr;
}}

inline bool is_packed_siege_model(const char *id) {{
\tfor (const PackedSiegeModel &p : PACKED_SIEGE_MODELS)
\t\tif (std::strcmp(p.packed, id) == 0) return true;
\treturn false;
}}

}} // namespace rtso
""")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zeroad", required=True, help="0 A.D. git checkout")
    ap.add_argument("--only", nargs="+")
    ap.add_argument("--out", default=UNITS)
    ap.add_argument("--no-manifest", action="store_true")
    args = ap.parse_args()
    art = Art(args.zeroad)
    entries = {}
    for actor in sorted(set(PACKED.values())):
        pid = packed_id(actor)
        if args.only and pid not in args.only:
            continue
        entries[pid] = convert(art, pid, actor, args.out)
    if not args.only:
        write_header()
    if not args.no_manifest and entries:
        with open(MANIFEST) as fh:
            man = json.load(fh)
        man["models"].update(entries)
        man["models"] = dict(sorted(man["models"].items()))
        with open(MANIFEST, "w") as fh:
            json.dump(man, fh, indent=1, sort_keys=True)


if __name__ == "__main__":
    main()
