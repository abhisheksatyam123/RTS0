#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the clips
it adds are CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

Add 0.28's "feeding" clip to the animal GLBs whose actor has one, as "Feeding".

0.28 UnitAI's animal IDLE.LINGERING plays SelectAnimation("feeding") (a grazing
loop); actors without one fall back to their idle. The fauna GLBs keep 0 A.D.'s
flat, root-relative bone poses, which are what a cached PSA stores per standard
bone, so the release public.zip's .psa drops onto the GLB's joints by name. A
joint outside the standard skeleton (a prop point) follows the bone it rode in
the GLB's own Idle.

Each GLB is checked first: its Idle must come back from the idle PSA it was made
from (max error CHECK_TOL), or the GLB is left alone.

Usage: python3 -I tools/add_0ad_feeding_clips.py [--zip public.zip] [--check]
"""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET
import zipfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from add_0ad_clips import add_accessor, glb_bones, glb_frames, read_glb, to_tq, trs, write_glb  # noqa: E402
from extract_0ad_assets import DEFAULT_ZIP, parse_psa  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
CHECK_TOL = 1e-3

# GLB -> (the idle PSA its Idle came from, the actor's feeding PSA): base_bovidae_wild
# (bull, cow, sanga_cattle, wildebeest, zebu_wild) and base_fox (fox_red, fox_arctic).
# The other 0.28 animals have no feeding (horse, zebra: idle) or feed with their
# idles (pony, hippopotamus). horse_breed (horse_breed_idle_04, named "feeding" in
# base_horse_breed_fauna.xml) and the peacock (peacock_feeding.dae) DO have their own
# feeding clips, but this PSA tool cannot reach them: the breed rig is Blender-named
# (no standard skeleton fits) and the peacock's rest pose was re-exported after the
# release (its GLB Idle does not rebuild from the PSA). They are appended from the
# source .dae instead: tools/extract_0ad_fauna_models.py --feeding-only.
BOVID = ("quadraped/bovidae_idle_a.dae", "quadraped/bovidae_feeding.dae")
FEEDING = {
    "fauna_bull": BOVID,
    "cow": BOVID,
    "fauna_sanga_cattle": BOVID,
    "fauna_wildebeest": BOVID,
    "fauna_zebu_wild": BOVID,
    "fox": ("quadraped/fox_idle.dae", "quadraped/fox_feeding.dae"),
    "fauna_fox_arctic": ("quadraped/fox_idle.dae", "quadraped/fox_feeding.dae"),
}


def psa(zf, rel):
    return parse_psa(zf.read(f"art/animation/{rel}.cached.psa"))


def standard_orders(zf):
    out = {}
    for n in zf.namelist():
        if n.startswith("art/skeletons/") and n.endswith(".xml"):
            for st in ET.fromstring(zf.read(n)).findall("standard_skeleton"):
                out[st.get("id")] = [b.get("name") for b in st.iter("bone")]
    return out


def clip_poses(clip, idx):
    """4x4 root-relative pose per mapped joint per frame (None for unmapped)."""
    nk, nf = clip["numKeys"], clip["numFrames"]
    out = []
    for k in idx:
        if k < 0:
            out.append(None)
            continue
        out.append(np.array([trs(clip["keys"][f * nk + k]["t"], clip["keys"][f * nk + k]["q"]) for f in range(nf)]))
    return out


def fill_unmapped(poses, idle_glb):
    """Each unmapped joint rides the mapped joint whose offset to it stays most
    constant through the GLB's own Idle, at that offset."""
    nf = len(next(p for p in poses if p is not None))
    for i, p in enumerate(poses):
        if p is not None:
            continue
        best = None
        for j, q in enumerate(poses):
            if q is None:
                continue
            rel = [np.linalg.inv(idle_glb[j, f]) @ idle_glb[i, f] for f in range(idle_glb.shape[1])]
            spread = max(np.abs(r - rel[0]).max() for r in rel)
            if best is None or spread < best[0]:
                best = (spread, j, rel[0])
        _, j, rel = best
        poses[i] = np.array([poses[j][f] @ rel for f in range(nf)])
    return poses


def build(zf, orders, path, idle_rel, feed_rel):
    gltf, bin_ = read_glb(path)
    joints, names, _ = glb_bones(gltf)
    idle, feed = psa(zf, idle_rel), psa(zf, feed_rel)
    order = next((o for o in orders.values() if len(o) == idle["numKeys"] and sum(n in o for n in names) * 3 >= len(names) * 2), None)
    if order is None or feed["numKeys"] != idle["numKeys"]:
        return None, "no standard skeleton fits"
    idx = [order.index(n) if n in order else -1 for n in names]
    have_t, have = glb_frames(gltf, bin_, "Idle", joints)
    rebuilt = fill_unmapped(clip_poses(idle, idx), have)
    # Most Idles keep the PSA's frames, maybe at another speed; the arctic fox's was
    # resampled (25 keys over 124 frames, looping), so compare its keys that land on
    # a PSA frame (the first and last, the last one being the loop's first frame).
    nf = min(have.shape[1], idle["numFrames"])
    err = max(np.abs(rebuilt[i][f][:3, 3] - have[i, f, :3, 3]).max() for i in range(len(joints)) for f in range(nf))
    if err > CHECK_TOL:
        pairs = []
        for k, t in enumerate(have_t):
            x = int(round(t * 1000.0 / idle["frameTime"]))
            if abs(t * 1000.0 / idle["frameTime"] - x) < 1e-3:
                pairs.append((k, {min(x, idle["numFrames"] - 1), x % idle["numFrames"]} | ({0} if k == len(have_t) - 1 else set())))
        err = max(min(np.abs(rebuilt[i][f][:3, 3] - have[i, k, :3, 3]).max() for f in fs) for i in range(len(joints)) for k, fs in pairs)
    if err > CHECK_TOL:
        return None, f"Idle does not rebuild from {idle_rel} (max error {err:.3g})"
    poses = fill_unmapped(clip_poses(feed, idx), have)
    times = np.array([feed["frameTime"] * f / 1000.0 for f in range(feed["numFrames"])], dtype=np.float32)
    return (gltf, bin_, joints, times, np.stack(poses)), f"Idle rebuilt (max error {err:.2g}), {feed['numFrames']} frames"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=DEFAULT_ZIP)
    ap.add_argument("--check", action="store_true", help="fail if a listed GLB has no Feeding clip")
    args = ap.parse_args()
    with open(MANIFEST) as fh:
        manifest = json.load(fh)
    if args.check:
        missing = [g for g in FEEDING if "Feeding" not in manifest["models"][g].get("animations", [])]
        if missing:
            sys.exit(f"no Feeding clip: {', '.join(missing)}")
        return
    zf = zipfile.ZipFile(args.zip)
    orders = standard_orders(zf)
    for gid, (idle_rel, feed_rel) in FEEDING.items():
        path = os.path.join(REPO, "assets", manifest["models"][gid]["file"])
        if "Feeding" in manifest["models"][gid].get("animations", []):
            print(f"{gid}: has Feeding")  # a rerun would leave the old clip's data behind
            continue
        res, msg = build(zf, orders, path, idle_rel, feed_rel)
        print(f"{gid}: {msg}")
        if res is None:
            continue
        gltf, bin_, joints, times, mats = res
        anims = gltf.setdefault("animations", [])
        tt, qq = to_tq(mats)
        t_acc = add_accessor(gltf, bin_, times.reshape(-1, 1), "SCALAR")
        samplers, channels = [], []
        for i, node in enumerate(joints):
            for path_name, arr, typ in (("translation", tt[i], "VEC3"), ("rotation", qq[i], "VEC4")):
                samplers.append({"input": t_acc, "output": add_accessor(gltf, bin_, arr, typ), "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": path_name}})
        anims.append({"name": "Feeding", "samplers": samplers, "channels": channels})
        write_glb(path, gltf, bin_)
        rec = manifest["models"][gid]
        if "Feeding" not in rec.setdefault("animations", []):
            rec["animations"].append("Feeding")
        src = f"anim:{feed_rel}"
        if src not in rec.setdefault("sources", []):
            rec["sources"] = sorted(rec["sources"] + [src])
    with open(MANIFEST, "w") as fh:
        json.dump(manifest, fh, indent=1)
        fh.write("\n")


if __name__ == "__main__":
    main()
