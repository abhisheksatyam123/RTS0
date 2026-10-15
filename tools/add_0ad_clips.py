#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the clips
it adds are CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

Add 0 A.D. animation clips, read from the source Collada files, to unit GLBs
that extract_0ad_assets.py already wrote.

Those GLBs keep every bone as a flat child of the root with a root-relative
pose (0 A.D.'s CModel bone matrices), named after the skeleton's standard
bones. A Collada clip names its joints the same way, so each frame's joint
world matrices, after 0 A.D.'s (x, z, y) axis swap, are the bone poses as they
stand. A standard bone the clip lacks keeps its rest offset from its nearest
animated ancestor.

Usage:
  python3 -I tools/add_0ad_clips.py --0ad PATH/TO/0ad --clip Capture=biped/infantry/capturing_a.dae \\
      [--skeleton biped] [--only spearman ...] [--replace] [--check-against Walk]
  python3 -I tools/add_0ad_clips.py --0ad PATH/TO/0ad --variants [--skeleton biped] [--only ...]
  python3 -I tools/add_0ad_clips.py --0ad PATH/TO/0ad --variants --mounts [--only ...]

--variants adds each unit's second Idle and Death as Idle2 and Death2, read
from its 0 A.D. actor (the manifest's units/ actor source): 0 A.D. picks one of
an actor's same-named animations at random, and the extractors kept the first.
With --mounts it does the same for the horse, camel and elephant GLBs: their
joints map to the clips' through assets/skeletons/<name>.xml, and the rider's
biped skin rides the mount bone that carries the actor's rider prop point.

--check-against NAME rebuilds a clip the GLB already has from its Collada
source and prints the largest difference instead of writing anything.
"""

import argparse
import functools
import json
import os
import struct
import sys
import xml.etree.ElementTree as ET

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_assets import mat3_to_quat  # noqa: E402
from extract_0ad_fauna_models import (  # noqa: E402
    ZUP, Art, node_animations, prop_point, resolve_actor, sample, scene_nodes, world_matrices)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")


def read_glb(path):
    with open(path, "rb") as fh:
        data = fh.read()
    jlen = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + jlen])
    off = 20 + jlen
    blen = struct.unpack_from("<I", data, off)[0]
    return gltf, bytearray(data[off + 8: off + 8 + blen])


def write_glb(path, gltf, bin_):
    while len(bin_) % 4:
        bin_ += b"\x00"
    gltf["buffers"][0]["byteLength"] = len(bin_)
    js = json.dumps(gltf, separators=(",", ":")).encode()
    while len(js) % 4:
        js += b" "
    total = 12 + 8 + len(js) + 8 + len(bin_)
    out = struct.pack("<4sII", b"glTF", 2, total)
    out += struct.pack("<I4s", len(js), b"JSON") + js
    out += struct.pack("<I4s", len(bin_), b"BIN\0") + bytes(bin_)
    with open(path, "wb") as fh:
        fh.write(out)


def read_accessor(gltf, bin_, idx):
    acc = gltf["accessors"][idx]
    bv = gltf["bufferViews"][acc["bufferView"]]
    n = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[acc["type"]]
    off = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    return np.frombuffer(bytes(bin_[off: off + 4 * n * acc["count"]]), dtype="<f4").reshape(acc["count"], n)


def add_accessor(gltf, bin_, arr, typ):
    while len(bin_) % 4:
        bin_ += b"\x00"
    data = np.ascontiguousarray(arr, dtype="<f4").tobytes()
    gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(bin_), "byteLength": len(data)})
    bin_ += data
    acc = {"bufferView": len(gltf["bufferViews"]) - 1, "componentType": 5126,
           "count": int(arr.shape[0]), "type": typ}
    if typ == "SCALAR":
        acc["min"] = [float(arr.min())]
        acc["max"] = [float(arr.max())]
    gltf["accessors"].append(acc)
    return len(gltf["accessors"]) - 1


def bone_parents(skeleton_xml):
    """Standard bone -> parent standard bone, from assets/skeletons/<name>.xml."""
    parents = {}
    root = ET.parse(skeleton_xml).getroot()
    std = root.find("standard_skeleton")

    def walk(el, parent):
        for b in el.findall("bone"):
            parents[b.get("name")] = parent
            walk(b, b.get("name"))

    if std is not None:
        walk(std, None)
    return parents


def to_pose(world):
    w = ZUP @ world @ ZUP
    u, _, vt = np.linalg.svd(w[:3, :3])
    r = u @ vt
    if np.linalg.det(r) < 0:
        u[:, -1] *= -1
        r = u @ vt
    return w[:3, 3], r


def trs(t, q):
    x, y, z, w = q
    r = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    m = np.eye(4)
    m[:3, :3] = r
    m[:3, 3] = t
    return m


def skeleton_names(std_id, nodes):
    """Standard bone -> Collada joint name, from the assets/skeletons/*.xml
    <skeleton> whose target is `std_id` and whose identifying root joint the
    file has (horse.xml maps Horse_Spine_1 to spine1). Bipeds name their joints
    after the standard bones, so they map to themselves."""
    for f in sorted(os.listdir(os.path.join(REPO, "assets", "skeletons"))):
        root = ET.parse(os.path.join(REPO, "assets", "skeletons", f)).getroot()
        for sk in root.findall("skeleton"):
            ident = sk.find("identifier/root")
            if sk.get("target") != std_id or ident is None or (ident.text or "").strip() not in nodes:
                continue
            names = {}
            for b in sk.iter("bone"):
                t = b.find("target")
                if t is not None and t.text:
                    # Several joints may share a target (horse tails); the first wins.
                    names.setdefault(t.text.strip(), b.get("name"))
            return names
    return {}


def standard_id(skeleton_xml):
    std = ET.parse(skeleton_xml).getroot().find("standard_skeleton")
    return std.get("id") if std is not None else None


@functools.lru_cache(maxsize=None)
def load_clip(art, rel):
    doc = ET.parse(art.path("animation", rel)).getroot()
    nodes = scene_nodes(doc)
    tracks = node_animations(doc)
    by_name = {n: tracks[v[0]] for n, v in nodes.items() if v[0] in tracks}
    if not by_name:
        raise SystemExit(f"{rel}: no animated joints")
    return nodes, by_name


def sample_poses(art, rel, bones, rest, parents, std_id=None, times=None, loop=True):
    """Root-relative 4x4 pose per bone per frame, at the clip's own frame times
    unless `times` is given (looped, or held on the last frame)."""
    nodes, by_name = load_clip(art, rel)
    names = skeleton_names(std_id, nodes) if std_id else {}
    t_end = max(tr[0][-1] for tr in by_name.values())
    nkeys = max(len(tr[0]) for tr in by_name.values())
    own = np.array([t_end * i / max(nkeys - 1, 1) for i in range(nkeys)], dtype=np.float32)
    if times is None:
        times = own
    rest_m = {b: trs(*rest[b]) for b in bones}
    out = np.zeros((len(bones), len(times), 4, 4))
    for k, t in enumerate(times):
        # The clip's own last frame stays the last frame; later times loop or hold.
        t = (t % t_end if loop and t > t_end else min(t, t_end)) if t_end > 0 else 0.0
        locals_ = {n: (sample(by_name[n], t) if n in by_name else v[2]) for n, v in nodes.items()}
        world = world_matrices(nodes, locals_)
        posed = {}

        def pose(b):
            if b in posed:
                return posed[b]
            c = names.get(b, b)
            if c in nodes:
                tr, r = to_pose(world[c])
                m = np.eye(4)
                m[:3, :3] = r
                m[:3, 3] = tr
            else:
                p = parents.get(b)
                m = rest_m[b] if p is None or p not in rest_m else pose(p) @ np.linalg.inv(rest_m[p]) @ rest_m[b]
            posed[b] = m
            return m

        for i, b in enumerate(bones):
            out[i, k] = pose(b)
    return np.asarray(times, dtype=np.float32), out


def to_tq(mats):
    """(translations, quaternions) for each bone's frames, the quaternions kept
    on one hemisphere so linear interpolation never flips."""
    nb, nf = mats.shape[:2]
    out_t = mats[:, :, :3, 3].astype(np.float32)
    out_q = np.zeros((nb, nf, 4), dtype=np.float32)
    for i in range(nb):
        for k in range(nf):
            out_q[i, k] = mat3_to_quat(mats[i, k, :3, :3].tolist())
            if k and np.dot(out_q[i, k], out_q[i, k - 1]) < 0:
                out_q[i, k] = -out_q[i, k]
    return out_t, out_q


def sample_clip(art, rel, bones, rest, parents, std_id=None):
    """Root-relative (translation, quaternion) per bone per frame."""
    times, mats = sample_poses(art, rel, bones, rest, parents, std_id)
    out_t, out_q = to_tq(mats)
    return times, out_t, out_q


def glb_bones(gltf):
    skin = gltf["skins"][0]
    names = [gltf["nodes"][j]["name"] for j in skin["joints"]]
    rest = {gltf["nodes"][j]["name"]: (gltf["nodes"][j].get("translation", [0, 0, 0]),
                                       gltf["nodes"][j].get("rotation", [0, 0, 0, 1])) for j in skin["joints"]}
    return skin["joints"], names, rest


def add_clip(path, name, art, rel, parents, replace):
    gltf, bin_ = read_glb(path)
    anims = gltf.setdefault("animations", [])
    if any(a["name"] == name for a in anims):
        if not replace:
            return False
        anims[:] = [a for a in anims if a["name"] != name]
    joints, bones, rest = glb_bones(gltf)
    times, tt, qq = sample_clip(art, rel, bones, rest, parents)
    t_acc = add_accessor(gltf, bin_, times.reshape(-1, 1), "SCALAR")
    samplers, channels = [], []
    for i, node in enumerate(joints):
        for path_name, arr, typ in (("translation", tt[i], "VEC3"), ("rotation", qq[i], "VEC4")):
            o = add_accessor(gltf, bin_, arr, typ)
            samplers.append({"input": t_acc, "output": o, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": path_name}})
    anims.append({"name": name, "samplers": samplers, "channels": channels})
    write_glb(path, gltf, bin_)
    return True


def check_clip(path, name, art, rel, parents):
    gltf, bin_ = read_glb(path)
    joints, bones, rest = glb_bones(gltf)
    anim = next(a for a in gltf["animations"] if a["name"] == name)
    times, tt, qq = sample_clip(art, rel, bones, rest, parents)
    node_bone = {n: bones.index(gltf["nodes"][n]["name"]) for n in joints}
    worst_t = worst_q = 0.0
    for ch in anim["channels"]:
        b = node_bone.get(ch["target"]["node"])
        if b is None:
            continue
        s = anim["samplers"][ch["sampler"]]
        have = read_accessor(gltf, bin_, s["output"])
        mine = tt[b] if ch["target"]["path"] == "translation" else qq[b]
        n = min(len(have), len(mine))
        # Both start at frame 0; compare the first frame and the shared frame count.
        d = np.abs(have[:1] - mine[:1]).max()
        if ch["target"]["path"] == "rotation":
            d = min(d, np.abs(have[:1] + mine[:1]).max())
            worst_q = max(worst_q, d)
        else:
            worst_t = max(worst_t, d)
        del n
    print(f"{os.path.basename(path)} {name} <- {rel}: frame 0 max |dt| {worst_t:.4f}, max |dq| {worst_q:.4f}")


def actor_animations(art, actor, name):
    """(file, frequency) of every animation called `name` in the actor, in the
    order 0 A.D. lists them. Idles come from the variants the extractors used
    (the most frequent per group); deaths sit in their own "death" variant, so
    every variant counts for them."""
    if name != "death":
        out = resolve_actor(art, actor)["anims"].get(name, [])
    else:
        out = []
        for v in art.xml("actors", actor).iter("variant"):
            chain = [v]
            while chain[-1].get("file"):
                chain.append(art.xml("variants", chain[-1].get("file")))
            for el in reversed(chain):
                for a in el.findall("animations/animation"):
                    if (a.get("name") or "").lower() == name:
                        out.append((a.get("file"), float(a.get("frequency") or 1)))
    seen, uniq = set(), []
    for f, fr in out:
        if f not in seen:
            seen.add(f)
            uniq.append((f, fr))
    return uniq


def second_clip(art, actor, name):
    """The variant to add beside the first (which the GLB already plays): the
    most frequent of the rest, as 0 A.D. would pick most often after it."""
    cands = actor_animations(art, actor, name)
    if len(cands) < 2:
        return None
    rest = [c for c in cands[1:] if os.path.exists(art.path("animation", c[0]))]
    return max(rest, key=lambda c: c[1])[0] if rest else None


MOUNT_SKELETONS = ("horse", "camel", "elephant")
# The generic mount and rider idles extract_0ad_assets.py gave some mounted
# units in place of their actors' own (its "horse_idle" and "cavalry_idle").
EXTRACTOR_IDLES = {
    "horse": ("quadraped/horse_idle_01.dae", "biped/rider/cavalry/generic/idle_relax_01.dae"),
    "camel": ("quadraped/camel_idle_01.dae", None),
    "elephant": ("quadraped/elephant_asian_idle_01.dae", None),
}


def skin_bones(gltf, si):
    joints = gltf["skins"][si]["joints"]
    names = [gltf["nodes"][j]["name"] for j in joints]
    rest = {gltf["nodes"][j]["name"]: (gltf["nodes"][j].get("translation", [0, 0, 0]),
                                       gltf["nodes"][j].get("rotation", [0, 0, 0, 1])) for j in joints}
    return joints, names, rest


def glb_frames(gltf, bin_, clip, joints):
    """(times, 4x4 pose per joint per frame) of a clip the GLB already has."""
    anim = next(a for a in gltf["animations"] if a["name"] == clip)
    tq = {}
    times = None
    for ch in anim["channels"]:
        s = anim["samplers"][ch["sampler"]]
        tq[(ch["target"]["node"], ch["target"]["path"])] = read_accessor(gltf, bin_, s["output"])
        times = read_accessor(gltf, bin_, s["input"])[:, 0]
    out = np.zeros((len(joints), len(times), 4, 4))
    for i, j in enumerate(joints):
        for k in range(len(times)):
            out[i, k] = trs(tq[(j, "translation")][k], tq[(j, "rotation")][k])
    return times, out


class Mount:
    """A mounted unit GLB: the mount's skin, and the rider's 46-bone biped skin
    when it has one. extract_0ad_assets.py poses each rider bone as
    mount_bone(t) . inv(mount_bone_rest) . attach . rider_pose(t), the rider
    prop point riding one mount bone (CModel's prop transform)."""

    def __init__(self, path, art, skeleton, actors):
        self.path, self.art, self.skeleton = path, art, skeleton
        self.gltf, self.bin = read_glb(path)
        sk = os.path.join(REPO, "assets", "skeletons", skeleton + ".xml")
        self.std = standard_id(sk)
        self.parents = bone_parents(sk)
        self.joints, self.bones, self.rest = skin_bones(self.gltf, 0)
        self.mount_actor = next((a for a in actors if any(
            f.startswith("quadraped/") for f, _ in actor_animations(art, a, "idle"))), None)
        self.rider = None
        if len(self.gltf["skins"]) > 1 and len(self.gltf["skins"][1]["joints"]) == 46:
            self.rider = self.solve_rider([a for a in actors if any(
                f.startswith("biped/") for f, _ in actor_animations(art, a, "idle"))])

    def solve_rider(self, candidates):
        """(actor, mount bone, attach, rider rest, held bones) that rebuild the
        GLB's Idle rider, or None. extract_0ad_assets.py played either the
        actors' first idles or its generic ones (EXTRACTOR_IDLES)."""
        rparents = bone_parents(os.path.join(REPO, "assets", "skeletons", "biped.xml"))
        rbones = list(rparents)
        rjoints, rnames, rrest = skin_bones(self.gltf, 1)
        if not any(an["name"] == "Idle" for an in self.gltf["animations"]):
            return None
        times, have = glb_frames(self.gltf, self.bin, "Idle", rjoints)
        probe = [0, len(times) // 2, len(times) - 1]
        t, have = times[probe], have[:, probe]
        m_gen, r_gen = EXTRACTOR_IDLES.get(self.skeleton, (None, None))
        mrels = {actor_animations(self.art, self.mount_actor, "idle")[0][0], m_gen} - {None}
        ident = {b: ([0, 0, 0], [0, 0, 0, 1]) for b in rbones}
        best = None
        for actor in candidates:
            ks = self.prop_bones(actor) or range(len(self.bones))
            rrels = {actor_animations(self.art, actor, "idle")[0][0], r_gen} - {None}
            for mrel in sorted(mrels):
                _, hm = sample_poses(self.art, mrel, self.bones, self.rest, self.parents, self.std, t)
                for rrel in sorted(rrels):
                    _, lm0 = sample_poses(self.art, rrel, rbones, ident, rparents, None, t)
                    for k in ks:
                        hr = trs(*self.rest[self.bones[k]])
                        # The hip is in every rider clip: solve the attach from frame 0.
                        a = hr @ np.linalg.inv(hm[k, 0]) @ have[0, 0] @ np.linalg.inv(lm0[0, 0])
                        # Bones a clip lacks keep their rest offset in prop space.
                        rest_l = {b: mat_tq(np.linalg.inv(a) @ trs(*rrest[n])) for b, n in zip(rbones, rnames)}
                        _, lm = sample_poses(self.art, rrel, rbones, rest_l, rparents, None, t)
                        e_clip = np.zeros(len(rbones))
                        e_rest = np.zeros(len(rbones))
                        for f in range(len(t)):
                            pre = hm[k, f] @ np.linalg.inv(hr) @ a
                            for i in range(len(rbones)):
                                e_clip[i] = max(e_clip[i], np.abs(pre @ lm[i, f] - have[i, f]).max())
                                # The PSAs carry the elbow IK helpers as identity keys.
                                e_rest[i] = max(e_rest[i], np.abs(pre - have[i, f]).max())
                        held = {b for i, b in enumerate(rbones) if e_rest[i] < e_clip[i]}
                        err = float(np.minimum(e_clip, e_rest).max())
                        if best is None or err < best[0] - 1e-6:
                            best = (err, actor, k, a, rest_l, held)
        if best is None or best[0] > 1e-3:
            print(f"{os.path.basename(self.path)}: no rider match ({best[0] if best else '-'})")
            return None
        return best[1:] + (rbones, rparents, rjoints)

    def prop_bones(self, rider_actor):
        """The mount bone carrying the prop point the mount actor props the
        rider on (the spine bones move as one in an idle, so the clip alone
        cannot tell them apart)."""
        a = resolve_actor(self.art, self.mount_actor)
        attach = next((ap for act, ap in a["props"] if act and act.endswith(rider_actor)), None)
        if not attach or not a["mesh"]:
            return []
        nodes = scene_nodes(ET.parse(self.art.path("meshes", a["mesh"])).getroot())
        node = prop_point(nodes, attach)
        names = {c: b for b, c in skeleton_names(self.std, nodes).items()}
        while node:
            b = names.get(node, node)
            if b in self.bones:
                return [self.bones.index(b)]
            node = nodes[node][1]
        return []

    def add(self, name, mount_rel, rider_rel, replace):
        anims = self.gltf.setdefault("animations", [])
        if any(a["name"] == name for a in anims):
            if not replace:
                return False
            anims[:] = [a for a in anims if a["name"] != name]
        loop = not name.startswith("Death")
        times, mm = sample_poses(self.art, mount_rel, self.bones, self.rest, self.parents, self.std)
        tracks = list(zip(self.joints, mm))
        if self.rider:
            _, k, a, rest_l, held, rbones, rparents, rjoints = self.rider
            r_end = sample_poses(self.art, rider_rel, rbones, rest_l, rparents)[0][-1]
            if not loop and r_end > times[-1] and len(times) > 1:
                # A death holds both on their last frames: run until the longer one ends.
                dt = times[1] - times[0]
                times = np.arange(0.0, r_end + dt * 0.5, dt, dtype=np.float32)
                times[-1] = r_end
                times, mm = sample_poses(self.art, mount_rel, self.bones, self.rest, self.parents,
                                         self.std, times, loop)
                tracks = list(zip(self.joints, mm))
            _, lm = sample_poses(self.art, rider_rel, rbones, rest_l, rparents, None, times, loop)
            for i, b in enumerate(rbones):
                if b in held:
                    lm[i] = np.eye(4)
            prop = np.linalg.inv(trs(*self.rest[self.bones[k]])) @ a
            rm = np.einsum("fij,jk,bfkl->bfil", mm[k], prop, lm)
            tracks += list(zip(rjoints, rm))
        t_acc = add_accessor(self.gltf, self.bin, times.reshape(-1, 1), "SCALAR")
        samplers, channels = [], []
        joints = [j for j, _ in tracks]
        tt, qq = to_tq(np.stack([m for _, m in tracks]))
        for i, node in enumerate(joints):
            for path_name, arr, typ in (("translation", tt[i], "VEC3"), ("rotation", qq[i], "VEC4")):
                o = add_accessor(self.gltf, self.bin, arr, typ)
                samplers.append({"input": t_acc, "output": o, "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1, "target": {"node": node, "path": path_name}})
        anims.append({"name": name, "samplers": samplers, "channels": channels})
        return True

    def jobs(self):
        """(name, mount clip, rider clip) for the second idle and death: the
        rider plays its own second clip, or its only one."""
        out = []
        for n in ("idle", "death"):
            m = second_clip(self.art, self.mount_actor, n)
            if not m:
                continue
            r = None
            if self.rider:
                cands = actor_animations(self.art, self.rider[0], n)
                r = second_clip(self.art, self.rider[0], n) or (cands[0][0] if cands else None)
                if not r:
                    continue
            out.append((n.capitalize() + "2", m, r))
        return out


def mat_tq(m):
    return list(m[:3, 3]), mat3_to_quat(m[:3, :3].tolist())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zeroad", required=True)
    ap.add_argument("--clip", help="Name=animation/relative/path.dae")
    ap.add_argument("--variants", action="store_true", help="add Idle2/Death2 from each unit's actor")
    ap.add_argument("--mounts", action="store_true",
                    help="with --variants: the horse, camel and elephant GLBs, rider included")
    ap.add_argument("--skeleton", default="biped", help="manifest skeleton the GLBs must use")
    ap.add_argument("--only", nargs="+")
    ap.add_argument("--replace", action="store_true")
    ap.add_argument("--check-against")
    args = ap.parse_args()
    art = Art(args.zeroad)
    if not args.variants and not args.clip:
        ap.error("--clip or --variants")
    parents = bone_parents(os.path.join(REPO, "assets", "skeletons", args.skeleton + ".xml"))
    with open(MANIFEST) as fh:
        man = json.load(fh)
    changed = 0
    added = {}
    if args.mounts:
        if not args.variants:
            ap.error("--mounts needs --variants")
        for mid, e in sorted(man["models"].items()):
            if args.only and mid not in args.only:
                continue
            if e.get("skeleton") not in MOUNT_SKELETONS or not e["file"].startswith("models/units/"):
                continue
            if e.get("clips_from"):  # plays the clips of another model's GLB
                continue
            actors = [x[len("actor:art/actors/"):] for x in e["sources"] if x.startswith("actor:art/actors/units/")]
            actors = [a for a in actors if os.path.exists(art.path("actors", a))]
            m = Mount(os.path.join(REPO, "assets", e["file"]), art, e["skeleton"], actors)
            if not m.mount_actor or (len(m.gltf["skins"]) > 1 and len(m.gltf["skins"][1]["joints"]) == 46
                                     and not m.rider):
                print(f"{mid}: skipped")
                continue
            done = []
            for name, mrel, rrel in m.jobs():
                if m.add(name, mrel, rrel, args.replace):
                    done.append(name)
                    added[name] = added.get(name, 0) + 1
                    if name not in e["animations"]:
                        e["animations"].append(name)
                    for rel in (mrel, rrel):
                        if rel and f"anim:{rel}" not in e["sources"]:
                            e["sources"] = sorted(e["sources"] + [f"anim:{rel}"])
            if done:
                write_glb(m.path, m.gltf, m.bin)
                changed += 1
            print(f"{mid}: {' '.join(done) or 'nothing'}"
                  + (f" (rider {m.rider[0]} on {m.bones[m.rider[1]]})" if m.rider else ""))
    for mid, e in sorted(man["models"].items()):
        if args.mounts:
            break
        if args.only and mid not in args.only:
            continue
        # Unit GLBs only: the Han boats rig their rowers on the biped skeleton too.
        if e.get("skeleton") != args.skeleton or not e.get("animated") or not e["file"].startswith("models/units/"):
            continue
        path = os.path.join(REPO, "assets", e["file"])
        if args.variants:
            actors = [x[len("actor:art/actors/"):] for x in e["sources"] if x.startswith("actor:art/actors/units/")]
            if not actors or not os.path.exists(art.path("actors", actors[0])):
                continue
            jobs = [(n.capitalize() + "2", second_clip(art, actors[0], n)) for n in ("idle", "death")]
            jobs = [(n, r) for n, r in jobs if r]
        else:
            jobs = [tuple(args.clip.split("=", 1))]
        for name, rel in jobs:
            if args.check_against:
                check_clip(path, args.check_against, art, rel, parents)
                continue
            if add_clip(path, name, art, rel, parents, args.replace):
                changed += 1
                added[name] = added.get(name, 0) + 1
                if name not in e["animations"]:
                    e["animations"].append(name)
                src = f"anim:{rel}"
                if src not in e["sources"]:
                    e["sources"] = sorted(e["sources"] + [src])
    if changed:
        with open(MANIFEST, "w") as fh:
            json.dump(man, fh, indent=1, sort_keys=True)
    if not args.check_against:
        for name, n in sorted(added.items()):
            print(f"{name}: added to {n} GLBs")


if __name__ == "__main__":
    main()
