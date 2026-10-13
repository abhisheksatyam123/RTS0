#!/usr/bin/env python3
"""The 0 A.D. clips this reads are CC BY-SA 3.0, Wildfire Games (see
docs/0AD_ATTRIBUTION.md).

Adds 0 A.D.'s oar strokes to the ship GLBs.

0 A.D. hangs each oar on a ship as a prop actor (props/structures/*/ship_oar_*.xml)
with its own 2-bone skeleton and its own idle and Walk clips
(other/ship_oar_*_idle_*.dae, *_move_*.dae). extract_0ad_assets.py writes every
oar as a rigid node named oar_<k>, which world_view_node strokes with a sine.
This tool keys each oar node with its actor's clips instead: every oar vertex
sits on bone 1, so the node moves by that bone's pose times its inverse bind.

The keys go into the GLB's Idle and Walk/Run clips (Attack and Death take the
idle stroke), looped to the clip's length. A hull with no clips gets Idle, Walk
and Run. Keyed nodes are renamed oarx_<k>, so the runtime sine leaves them be.
Templates that borrow their class model's clips (manifest clips_from) are left
alone, as are hulls whose actors 0 A.D. trunk no longer has (the German ships).

Usage: python3 tools/add_0ad_oar_clips.py --0ad <0 A.D. checkout> [--only id ...]
"""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from add_0ad_clips import MANIFEST, REPO, add_accessor, read_glb, write_glb  # noqa: E402
from extract_0ad_assets import (Vfs, _actor_path, _psa_for_dae, compose_actor,  # noqa: E402
                                mat3_to_quat, parse_pmd, parse_psa, quat_to_mat)
from oad_collada_archive import ColladaArchive  # noqa: E402

MOVE_CLIPS = ("Walk", "Run")


def tq_mat(t, q):
    m = np.array(quat_to_mat(q), dtype=float)
    m[:3, 3] = t
    return m


def mat_tq(m):
    q = mat3_to_quat([[float(m[i][j]) for j in range(3)] for i in range(3)])
    return [float(x) for x in m[:3, 3]], [float(x) for x in q]


def oar_actor_clips(vfs, actor):
    """{'idle': (psa path, speed), 'walk': ...} from an oar prop actor."""
    root = ET.fromstring(vfs.read(actor))
    out = {}
    for an in root.iter("animation"):
        name = (an.get("name") or "").strip().lower()
        psa = _psa_for_dae(vfs, an.get("file"))
        if psa and name in ("idle", "walk"):
            out[name] = (psa, float(an.get("speed") or 100) / 100.0)
    return out


def oar_track(vfs, actor, clip, rest):
    """Seconds and node matrices for one cycle of an oar actor's clip:
    rest * pose(bone 1) * inverse bind(bone 1)."""
    root = ET.fromstring(vfs.read(actor))
    mesh = root.find(".//mesh").text.strip()
    md = parse_pmd(vfs.read(vfs.mesh_path(mesh)))
    bt, bq = md["bones"][1]
    inv_bind = np.linalg.inv(tq_mat(bt, bq))
    psa_path, speed = clip
    psa = parse_psa(vfs.read(psa_path))
    nk = psa["numKeys"]
    times, mats = [], []
    for f in range(psa["numFrames"]):
        k = psa["keys"][f * nk + 1]
        times.append(f * psa["frameTime"] / 1000.0 / max(speed, 1e-3))
        mats.append(rest @ tq_mat(k["t"], k["q"]) @ inv_bind)
    return np.array(times), mats


def oar_parts(vfs, actors):
    """(actor, world matrix) of every oar prop composed from the hull actors."""
    out = []
    for act in actors:
        if not vfs.exists(act):
            continue
        parts, _ = compose_actor(vfs, act)
        for p in parts:
            if "ship_oar" in (p.source or "") and p.transform is not None:
                out.append((p.source, np.array(p.transform, dtype=float)))
    return out


def anim_length(gltf, bin_, anim):
    end = 0.0
    for s in anim["samplers"]:
        acc = gltf["accessors"][s["input"]]
        end = max(end, float(acc.get("max", [0.0])[0]))
    return end


def add_channels(gltf, bin_, anim, node_idx, times, mats, length):
    """Loop one oar cycle to fill `length` seconds, whole cycles, stretched."""
    period = float(times[-1]) if len(times) > 1 else 0.0
    if period <= 0:
        return
    cycles = max(1, int(round(length / period))) if length > 0 else 1
    scale = (length / (cycles * period)) if length > 0 else 1.0
    ts, tr, rq = [], [], []
    for c in range(cycles):
        for i, (t, m) in enumerate(zip(times, mats)):
            if c and i == 0:
                continue
            tt, qq = mat_tq(m)
            ts.append((c * period + t) * scale)
            tr.append(tt)
            rq.append(qq)
    t_acc = add_accessor(gltf, bin_, np.array(ts, dtype=np.float32).reshape(-1, 1), "SCALAR")
    gltf["accessors"][t_acc]["type"] = "SCALAR"
    for path, vals, typ in (("translation", tr, "VEC3"), ("rotation", rq, "VEC4")):
        v_acc = add_accessor(gltf, bin_, np.array(vals, dtype=np.float32), typ)
        anim["samplers"].append({"input": t_acc, "output": v_acc, "interpolation": "LINEAR"})
        anim["channels"].append({"sampler": len(anim["samplers"]) - 1,
                                 "target": {"node": node_idx, "path": path}})


def fix(path, entry, vfs):
    gltf, bin_ = read_glb(path)
    nodes = gltf["nodes"]
    oars = [i for i, n in enumerate(nodes) if n.get("name", "").startswith("oar_")]
    if not oars:
        return 0, set()
    actors = sorted(s[len("actor:"):] for s in entry.get("sources", [])
                    if s.startswith("actor:art/actors/structures/"))
    parts = oar_parts(vfs, actors)
    matched = {}
    for i in oars:
        nd = nodes[i]
        rest = tq_mat(nd.get("translation", [0, 0, 0]), nd.get("rotation", [0, 0, 0, 1]))
        best = None
        for act, xf in parts:
            d = np.abs(xf - rest).max()
            if d < 1e-3 and (best is None or d < best[0]):
                best = (d, act)
        if best:
            matched[i] = (_actor_path(best[1]), rest)
    if not matched:
        return 0, set()
    anims = gltf.setdefault("animations", [])
    names = {a.get("name"): a for a in anims}
    if not anims:
        for name in ("Idle", "Walk", "Run"):
            a = {"name": name, "samplers": [], "channels": []}
            anims.append(a)
            names[name] = a
    lengths = {a["name"]: anim_length(gltf, bin_, a) for a in anims}
    used = set()
    for i, (actor, rest) in matched.items():
        clips = oar_actor_clips(vfs, actor)
        for a in anims:
            key = "walk" if a["name"] in MOVE_CLIPS else "idle"
            clip = clips.get(key) or clips.get("idle")
            if not clip:
                continue
            times, mats = oar_track(vfs, actor, clip, rest)
            used.add(clip[0])
            add_channels(gltf, bin_, a, i, times, mats, lengths[a["name"]])
        nodes[i]["name"] = "oarx_" + nodes[i]["name"][len("oar_"):]
    gltf["animations"] = [a for a in anims if a["channels"]]
    write_glb(path, gltf, bin_)
    return len(matched), used


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zeroad", required=True)
    ap.add_argument("--only", nargs="+")
    args = ap.parse_args()
    vfs = Vfs(ColladaArchive(args.zeroad))
    with open(MANIFEST) as fh:
        man = json.load(fh)
    models = man["models"]
    total = 0
    for mid, e in sorted(models.items()):
        if args.only and mid not in args.only:
            continue
        if not any("ship_oar" in s for s in e.get("sources", [])):
            continue
        # A template GLB that borrows its sail clips (world_view_node skips
        # the borrow once the GLB has clips of its own) keeps the runtime sine.
        if e.get("clips_from"):
            continue
        path = os.path.join(REPO, "assets", e["file"])
        if not os.path.exists(path):
            continue
        n, n_clips = fix(path, e, vfs)
        if n:
            total += 1
            gltf, _ = read_glb(path)
            e["animations"] = [a["name"] for a in gltf.get("animations", [])]
            e["sources"] = sorted(set(e["sources"]) | {f"anim:{p[len('art/animation/'):-len('.cached.psa')]}"
                                                      for p in n_clips})
        print(f"{mid}: {n} oars keyed")
    if total:
        with open(MANIFEST, "w") as fh:
            json.dump(man, fh, indent=1, sort_keys=True)
    print(f"{total} GLBs changed")


if __name__ == "__main__":
    main()
