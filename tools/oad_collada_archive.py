#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games):
source/collada/{PMDConvert,PSAConvert,CommonConvert,StdSkeletons}.cpp.

A stand-in for 0 A.D.'s public mod archive built from a git checkout of the
0 A.D. repository, which ships Collada sources instead of the converted
caches. It answers extract_0ad_assets.py's reads of
`art/meshes/X.dae.cached.pmd` and `art/animation/X.dae.cached.psa` by
converting `X.dae` the way 0 A.D.'s own converter does, so the extractor runs
unchanged:

  python3 -I tools/extract_0ad_assets.py --zip PATH/TO/0ad --only ...

PATH/TO/0ad is the checkout (or its binaries/data/mods/public folder).
"""

import os
import struct
import xml.etree.ElementTree as ET

import numpy as np

COLLADA = "{http://www.collada.org/2005/11/COLLADASchema}"
MAX_INFLUENCES = 4


def _q(path):
    return "/".join(COLLADA + p for p in path.split("/"))


def _floats(el):
    return np.array([float(x) for x in el.text.split()], dtype=np.float64) if el is not None and el.text else np.zeros(0)


def _mat(text):
    return np.array([float(x) for x in text.split()], dtype=np.float64).reshape(4, 4)


def decompose(m):
    """decomp_affine's translation and rotation quaternion (x, y, z, w) of a
    4x4 matrix: the polar rotation of its linear part."""
    u, _, vt = np.linalg.svd(m[:3, :3])
    r = u @ vt
    if np.linalg.det(r) < 0:
        u[:, -1] *= -1
        r = u @ vt
    return m[:3, 3].copy(), _quat(r)


def _quat(r):
    tr = r[0, 0] + r[1, 1] + r[2, 2]
    if tr > 0:
        s = np.sqrt(tr + 1.0) * 2
        w, x, y, z = 0.25 * s, (r[2, 1] - r[1, 2]) / s, (r[0, 2] - r[2, 0]) / s, (r[1, 0] - r[0, 1]) / s
    elif r[0, 0] > r[1, 1] and r[0, 0] > r[2, 2]:
        s = np.sqrt(1.0 + r[0, 0] - r[1, 1] - r[2, 2]) * 2
        w, x, y, z = (r[2, 1] - r[1, 2]) / s, 0.25 * s, (r[0, 1] + r[1, 0]) / s, (r[0, 2] + r[2, 0]) / s
    elif r[1, 1] > r[2, 2]:
        s = np.sqrt(1.0 + r[1, 1] - r[0, 0] - r[2, 2]) * 2
        w, x, y, z = (r[0, 2] - r[2, 0]) / s, (r[0, 1] + r[1, 0]) / s, 0.25 * s, (r[1, 2] + r[2, 1]) / s
    else:
        s = np.sqrt(1.0 + r[2, 2] - r[0, 0] - r[1, 1]) * 2
        w, x, y, z = (r[1, 0] - r[0, 1]) / s, (r[0, 2] + r[2, 0]) / s, (r[1, 2] + r[2, 1]) / s, 0.25 * s
    return np.array([x, y, z, w])


def game_space(t, q, y_up):
    """TransformBones / prop point handling: Collada right-handed Z-up (or
    Y-up) to the game's left-handed Y-up."""
    t, q = np.array(t, dtype=np.float64), np.array(q, dtype=np.float64)
    if y_up:
        return np.array([t[0], t[1], -t[2]]), np.array([q[0], q[1], -q[2], -q[3]])
    return np.array([t[0], t[2], t[1]]), np.array([q[0], q[2], q[1], -q[3]])


# ----------------------------------------------------------- skeletons ---

class Skeletons:
    """StdSkeletons.cpp: standard skeletons by id, and the mapped skeletons
    (Collada joint name -> standard bone) by the name of their root joint."""

    def __init__(self, files):
        self.standard = {}  # id -> [bone names]
        # identifier root -> ({joint name: (target id, real target id)}, standard bones).
        # A later file may redefine a standard id; mapped skeletons keep the one they were read with.
        self.mapped = {}
        for path in files:
            root = ET.parse(path).getroot()
            for el in root:
                if el.tag == "standard_skeleton":
                    bones = []

                    def walk(e):
                        for b in e.findall("bone"):
                            bones.append(b.get("name"))
                            walk(b)

                    walk(el)
                    self.standard[el.get("id")] = bones
            for el in root:
                if el.tag != "skeleton":
                    continue
                std = self.standard.get(el.get("target"))
                if std is None:
                    continue
                bones = {}
                used = set()

                def walk2(e, target):
                    for b in e.findall("bone"):
                        t = b.find("target")
                        tgt = (t.text or "").strip() if t is not None and t.text else target
                        tid = std.index(tgt)
                        bones.setdefault(b.get("name"), (tid, -1 if tid in used else tid))
                        used.add(tid)
                        walk2(b, tgt)

                walk2(el, "")
                ident = (el.findtext("identifier/root") or "").strip()
                self.mapped[ident] = (bones, std)

    def find(self, joint, parents):
        """FindSkeleton: the first ancestor (joint included) naming a mapped skeleton."""
        while joint is not None:
            if joint in self.mapped:
                return self.mapped[joint]
            joint = parents.get(joint)
        raise ValueError("unrecognised skeleton structure")


# ------------------------------------------------------------- Collada ---

class Doc:
    def __init__(self, path):
        self.root = ET.parse(path).getroot()
        self.sources = {s.get("id"): s for s in self.root.iter(COLLADA + "source")}
        up = self.root.findtext(_q("asset/up_axis")) or "Y_UP"
        self.y_up = up.strip() == "Y_UP"
        tool = self.root.findtext(_q("asset/contributor/authoring_tool")) or ""
        self.xsi = "XSI" in tool
        # Scene nodes by element, with parents and the names FCollada matches joints by.
        self.parent = {}
        self.nodes = []
        vs = next(self.root.iter(COLLADA + "visual_scene"))
        self.scene = vs

        def walk(n, p):
            self.parent[n] = p
            self.nodes.append(n)
            for c in n.findall(COLLADA + "node"):
                walk(c, n)

        for n in vs.findall(COLLADA + "node"):
            walk(n, None)
        self.anims = self._animations()

    def source(self, ref):
        return self.sources[ref.lstrip("#")]

    def array(self, ref):
        s = self.source(ref)
        acc = s.find(_q("technique_common/accessor"))
        stride = int(acc.get("stride") or 1) if acc is not None else 1
        return _floats(s.find(COLLADA + "float_array")).reshape(-1, stride)

    def _animations(self):
        """node id -> (times, local matrices) for baked transform channels."""
        out = {}
        samplers = {s.get("id"): s for s in self.root.iter(COLLADA + "sampler")}
        for ch in self.root.iter(COLLADA + "channel"):
            target = ch.get("target") or ""
            node_id, _, what = target.partition("/")
            if what not in ("transform", "matrix"):
                continue
            samp = samplers[ch.get("source").lstrip("#")]
            ins = {i.get("semantic"): i.get("source") for i in samp.findall(COLLADA + "input")}
            times = self.array(ins["INPUT"])[:, 0]
            mats = self.array(ins["OUTPUT"]).reshape(-1, 4, 4)
            out[node_id] = (times, mats)
        return out

    @staticmethod
    def name(n):
        return n.get("name") or n.get("id") or n.get("sid")

    def local(self, n, t=None):
        if t is not None and n.get("id") in self.anims:
            times, mats = self.anims[n.get("id")]
            if t <= times[0]:
                return mats[0]
            if t >= times[-1]:
                return mats[-1]
            i = int(np.searchsorted(times, t)) - 1
            f = (t - times[i]) / max(times[i + 1] - times[i], 1e-9)
            return mats[i] * (1 - f) + mats[i + 1] * f
        m = np.eye(4)
        for c in n:
            tag = c.tag[len(COLLADA):]
            if tag == "matrix":
                m = m @ _mat(c.text)
            elif tag == "translate":
                x = np.eye(4)
                x[:3, 3] = [float(v) for v in c.text.split()]
                m = m @ x
            elif tag == "rotate":
                x, y, z, ang = [float(v) for v in c.text.split()]
                ax = np.array([x, y, z])
                ln = np.linalg.norm(ax)
                if ln > 0:
                    ax /= ln
                    a = np.radians(ang)
                    k = np.array([[0, -ax[2], ax[1]], [ax[2], 0, -ax[0]], [-ax[1], ax[0], 0]])
                    r = np.eye(4)
                    r[:3, :3] = np.eye(3) + np.sin(a) * k + (1 - np.cos(a)) * k @ k
                    m = m @ r
            elif tag == "scale":
                x = np.eye(4)
                x[0, 0], x[1, 1], x[2, 2] = [float(v) for v in c.text.split()]
                m = m @ x
        return m

    def world(self, n, t=None):
        m = self.local(n, t)
        p = self.parent[n]
        while p is not None:
            m = self.local(p, t) @ m
            p = self.parent[p]
        return m

    def instance(self):
        """FindSingleInstance: the one visible geometry or controller instance
        (preferring nodes marked "export"), with its node."""
        found = []
        for n in self.nodes:
            if not self._visible(n):
                continue
            for tag in ("instance_geometry", "instance_controller"):
                for inst in n.findall(COLLADA + tag):
                    found.append((n, inst))
        marked = [f for f in found if self._note(f[0]) == "export"]
        found = marked or found
        if len(found) != 1:
            raise ValueError(f"{len(found)} convertible objects")
        return found[0]

    @staticmethod
    def _visible(n):
        """IsVisible: the XSI visibility parameter, else FCollada's <visibility>."""
        for extra in n.findall(COLLADA + "extra"):
            for e in extra.iter():
                tag = e.tag.rsplit("}", 1)[-1]
                if tag == "xsi_param" and e.get("sid") == "visibility" and e.text:
                    return e.text.strip().upper() != "FALSE"
                if tag == "visibility" and e.text:
                    return float(e.text) != 0.0
        return True

    @staticmethod
    def _note(n):
        """FCDSceneNode::GetNote: the node's FCOLLADA user properties."""
        for e in n.findall(_q("extra/technique/user_properties")):
            if e.text:
                return e.text.strip()
        return ""

    def geometry(self, geom_id):
        return next(g for g in self.root.iter(COLLADA + "geometry") if g.get("id") == geom_id)

    def triangles(self, geom):
        """GeomReindex: unique (position, normal, texcoords...) corners as
        vertices, and the triangle index list over them."""
        mesh = geom.find(COLLADA + "mesh")
        verts_el = mesh.find(COLLADA + "vertices")
        vinputs = {i.get("semantic"): i.get("source") for i in verts_el.findall(COLLADA + "input")}
        positions = self.array(vinputs["POSITION"])[:, :3]
        normals_v = self.array(vinputs["NORMAL"])[:, :3] if "NORMAL" in vinputs else None
        key_index = {}
        verts = []  # (position index, normal, [uv sets])
        faces = []
        for prim in list(mesh.findall(COLLADA + "triangles")) + list(mesh.findall(COLLADA + "polylist")) \
                + list(mesh.findall(COLLADA + "polygons")):
            ins = prim.findall(COLLADA + "input")
            stride = max(int(i.get("offset")) for i in ins) + 1
            normal = None
            tex = []
            vert_off = None
            for i in ins:
                sem, off = i.get("semantic"), int(i.get("offset"))
                if sem == "VERTEX":
                    vert_off = off
                elif sem == "NORMAL" and normal is None:
                    normal = (off, self.array(i.get("source")))
                elif sem == "TEXCOORD":
                    tex.append((int(i.get("set") or 0), off, self.array(i.get("source"))))
            tex.sort(key=lambda x: x[0])
            if prim.tag == COLLADA + "polygons":
                plist = [[int(x) for x in p.text.split()] for p in prim.findall(COLLADA + "p")]
                counts = [len(p) // stride for p in plist]
                p = [x for pp in plist for x in pp]
            else:
                p = [int(x) for x in prim.find(COLLADA + "p").text.split()]
                counts = [3] * (len(p) // (3 * stride))
                if prim.tag == COLLADA + "polylist":
                    counts = [int(x) for x in prim.find(COLLADA + "vcount").text.split()]
            k = 0
            for c in counts:
                poly = []
                for _ in range(c):
                    rec = p[k: k + stride]
                    k += stride
                    vi = rec[vert_off]
                    if normal is not None:
                        nrm = normal[1][rec[normal[0]]]
                    elif normals_v is not None:
                        nrm = normals_v[vi]
                    else:
                        nrm = np.array([0.0, 0.0, 1.0])
                    uvs = tuple(tuple(arr[rec[off]][:2]) for _, off, arr in tex)
                    key = (vi, tuple(nrm[:3]), uvs)
                    idx = key_index.get(key)
                    if idx is None:
                        idx = key_index[key] = len(verts)
                        verts.append((vi, np.array(nrm[:3], dtype=np.float64), uvs))
                    poly.append(idx)
                for j in range(1, c - 1):
                    faces += [poly[0], poly[j], poly[j + 1]]
        return positions, verts, faces


def _write_pmd(verts, faces, bones, props):
    nuv = max((len(v[2]) for v in verts), default=0)
    out = [struct.pack("<II", len(verts), nuv)]
    for pos, nrm, uvs, (bi, bw) in verts:
        out.append(struct.pack("<3f3f", *pos, *nrm))
        for s in range(nuv):
            u, v = uvs[s] if s < len(uvs) else (0.0, 0.0)
            out.append(struct.pack("<2f", u, v))
        out.append(struct.pack("<4B4f", *bi, *bw))
    out.append(struct.pack("<I", len(faces) // 3))
    out.append(struct.pack(f"<{len(faces)}H", *faces))
    out.append(struct.pack("<I", len(bones)))
    for t, q in bones:
        out.append(struct.pack("<7f", *t, *q))
    out.append(struct.pack("<I", len(props)))
    for name, t, q, bone in props:
        nb = name.encode()
        out.append(struct.pack("<I", len(nb)) + nb + struct.pack("<7fB", *t, *q, bone))
    payload = b"".join(out)
    return b"PSMD" + struct.pack("<II", 4, len(payload)) + payload


def _skin(doc, ctrl):
    skin = ctrl.find(COLLADA + "skin")
    bsm_el = skin.find(COLLADA + "bind_shape_matrix")
    bsm = _mat(bsm_el.text) if bsm_el is not None else np.eye(4)
    jin = {i.get("semantic"): i.get("source") for i in skin.findall(_q("joints/input"))}
    js = doc.source(jin["JOINT"])
    names_el = js.find(COLLADA + "Name_array")
    if names_el is None:
        names_el = js.find(COLLADA + "IDREF_array")
    jnames = names_el.text.split()
    ibms = doc.array(jin["INV_BIND_MATRIX"]).reshape(-1, 4, 4)
    vw = skin.find(COLLADA + "vertex_weights")
    win = {i.get("semantic"): (i.get("source"), int(i.get("offset"))) for i in vw.findall(COLLADA + "input")}
    weights = doc.array(win["WEIGHT"][0])[:, 0]
    vcount = [int(x) for x in vw.find(COLLADA + "vcount").text.split()]
    v = [int(x) for x in vw.find(COLLADA + "v").text.split()]
    stride = len(win)
    infl, k = [], 0
    for n in vcount:
        pairs = []
        for _ in range(n):
            pairs.append((v[k + win["JOINT"][1]], float(weights[v[k + win["WEIGHT"][1]]])))
            k += stride
        infl.append(pairs)
    return skin, bsm, jnames, ibms, infl


def _joint_nodes(doc, inst, jnames):
    """The scene node of each skin joint, looked up under the instance's
    <skeleton> roots by sid, then anywhere by sid, name or id."""
    roots = [s.text.strip().lstrip("#") for s in inst.findall(COLLADA + "skeleton") if s.text]
    by = {}
    scope = [n for n in doc.nodes if any(_under(doc, n, r) for r in roots)] if roots else doc.nodes
    for n in scope:
        for key in (n.get("sid"), n.get("name"), n.get("id")):
            if key:
                by.setdefault(key, n)
    for n in doc.nodes:
        for key in (n.get("sid"), n.get("name"), n.get("id")):
            if key:
                by.setdefault(key, n)
    return [by.get(j) for j in jnames]


def _under(doc, n, root_id):
    while n is not None:
        if n.get("id") == root_id:
            return True
        n = doc.parent[n]
    return False


def _reduce(pairs):
    """SkinReduceInfluences(4, 0.001)."""
    merged = {}
    order = []
    for j, w in pairs:
        if j not in merged:
            order.append(j)
            merged[j] = 0.0
        merged[j] += w
    out = sorted(((j, merged[j]) for j in order), key=lambda p: -p[1])[:MAX_INFLUENCES]
    while out and out[-1][1] < 0.001:
        out.pop()
    tot = sum(w for _, w in out)
    return [(j, w / tot) for j, w in out] if tot > 0 else out


def collada_to_pmd(path, skeletons):
    doc = Doc(path)
    node, inst = doc.instance()
    props = [("root", (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0), 0xFF)]
    if inst.tag == COLLADA + "instance_geometry":
        geom = doc.geometry(inst.get("url").lstrip("#"))
        positions, verts, faces = doc.triangles(geom)
        xf = doc.world(node)
        lin = xf[:3, :3]
        out_v = []
        for vi, nrm, uvs in verts:
            p = (xf @ np.append(positions[vi], 1.0))[:3]
            n = lin @ nrm
            ln = np.linalg.norm(n)
            n = n / ln if ln > 0 else np.array([1.0, 0, 0])
            p, n = _flip(p, doc.y_up), _flip(n, doc.y_up)
            out_v.append((p, n, uvs, ((0xFF,) * 4, (0.0,) * 4)))
        up = np.eye(4)
        if doc.y_up:
            c, s = np.cos(1.57), np.sin(1.57)
            up = np.array([[1, 0, 0, 0], [0, c, -s, 0], [0, s, c, 0], [0, 0, 0, 1.0]])
        # AddStaticPropPoints over the instance's node and everything below it.
        stack = [node]
        while stack:
            n = stack.pop(0)
            nm = Doc.name(n) or ""
            if nm.startswith("prop-") or nm.startswith("prop_"):
                t, q = decompose(up @ doc.world(n))
                props.append((nm[5:], (t[0], t[2], t[1]), (q[0], q[2], q[1], -q[3]), 0xFF))
            stack[:0] = n.findall(COLLADA + "node")
        return _write_pmd(out_v, faces, [], props)

    ctrl = next(c for c in doc.root.iter(COLLADA + "controller") if c.get("id") == inst.get("url").lstrip("#"))
    skin, bsm, jnames, ibms, infl = _skin(doc, ctrl)
    jnodes = _joint_nodes(doc, inst, jnames)
    njoint = len(jnames)
    parents = {Doc.name(n): Doc.name(doc.parent[n]) if doc.parent[n] is not None else None for n in doc.nodes}
    mapped, std = skeletons.find(Doc.name(jnodes[0]), parents)

    def bone_id(n, real=False):
        b = mapped.get(Doc.name(n)) if n is not None else None
        return -1 if b is None else b[1 if real else 0]

    blends = []
    for pairs in infl:
        pairs = _reduce(pairs)
        bi, bw = [0xFF] * 4, [0.0] * 4
        if not pairs:
            bi[0], bw[0] = njoint, 1.0
        for j, (ji, w) in enumerate(pairs):
            if ji == -1:
                bi[j], bw[j] = njoint, w
                continue
            b = bone_id(jnodes[ji]) if ji < len(jnodes) else -1
            if b < 0:
                continue
            bi[j], bw[j] = b, w
        blends.append((tuple(bi), tuple(bw)))

    bones = [((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))] * len(std)
    for i, n in enumerate(jnodes):
        b = bone_id(n, real=True)
        if b < 0:
            continue
        bones[b] = decompose(np.linalg.inv(ibms[i]))
    for i, n in enumerate(jnodes):
        b = bone_id(n)
        if b < 0:
            continue
        for c in n.findall(COLLADA + "node"):
            nm = Doc.name(c) or ""
            if nm.startswith("prop-") or nm.startswith("prop_"):
                t, q = decompose(doc.local(c))
                props.append((nm[5:], t, q, b))

    geom = doc.geometry(skin.get("source").lstrip("#"))
    positions, verts, faces = doc.triangles(geom)
    xf = bsm
    if doc.xsi:
        xf = _scale_of(doc.world(node)) @ bsm
    lin = xf[:3, :3]
    out_v = []
    for vi, nrm, uvs in verts:
        p = (xf @ np.append(positions[vi], 1.0))[:3]
        n = lin @ nrm
        ln = np.linalg.norm(n)
        n = n / ln if ln > 0 else np.array([1.0, 0, 0])
        out_v.append((_flip(p, doc.y_up), _flip(n, doc.y_up), uvs, blends[vi]))
    scale = _scale_of(doc.world(node)) if doc.xsi else np.eye(4)
    bones = [game_space((scale @ np.append(t, 1.0))[:3], q, doc.y_up) for t, q in bones]
    gprops = [props[0]]
    for name, t, q, b in props[1:]:
        gt, gq = game_space(t, q, doc.y_up)
        gprops.append((name, gt, gq, b))
    return _write_pmd(out_v, faces, bones, gprops)


def _flip(v, y_up):
    return np.array([v[0], v[1], -v[2]]) if y_up else np.array([v[0], v[2], v[1]])


def _scale_of(m):
    s = np.eye(4)
    for i in range(3):
        s[i, i] = np.linalg.norm(m[:3, i])
    return s


def _anim_range(doc, mapped, jnodes):
    extra = doc.root.find(_q("library_visual_scenes/visual_scene/extra"))
    if extra is not None:
        st = en = None
        for e in extra.iter():
            if e.tag.endswith("start_time"):
                st = float(e.text)
            elif e.tag.endswith("end_time"):
                en = float(e.text)
        if st is not None and en is not None:
            return st, en
    lo, hi = np.inf, -np.inf
    for n in jnodes:
        if n is None or Doc.name(n) not in mapped or n.get("id") not in doc.anims:
            continue
        times = doc.anims[n.get("id")][0]
        lo, hi = min(lo, times[0]), max(hi, times[-1])
    return lo, hi


def collada_to_psa(path, skeletons):
    doc = Doc(path)
    node, inst = doc.instance()
    if inst.tag != COLLADA + "instance_controller":
        raise ValueError("animation without a skinned mesh")
    ctrl = next(c for c in doc.root.iter(COLLADA + "controller") if c.get("id") == inst.get("url").lstrip("#"))
    skin, bsm, jnames, ibms, infl = _skin(doc, ctrl)
    jnodes = _joint_nodes(doc, inst, jnames)
    parents = {Doc.name(n): Doc.name(doc.parent[n]) if doc.parent[n] is not None else None for n in doc.nodes}
    mapped, std = skeletons.find(Doc.name(jnodes[0]), parents)
    nbones = len(std)
    t0, t1 = _anim_range(doc, mapped, jnodes)
    if not t1 > t0:
        raise ValueError("animation end frame must come after start frame")
    # PSAConvert counts frames in float32, which decides clips ending on a half frame.
    f32 = np.float32
    flen = f32(1.0) / f32(30.0)
    nframes = int((f32(t1) - f32(t0)) / flen - f32(0.5))
    if nframes <= 0:
        raise ValueError("animation must have frames")
    scale = _scale_of(doc.world(node)) if doc.xsi else np.eye(4)
    out = [struct.pack("<I", 0), struct.pack("<f", 1000.0 / 30.0), struct.pack("<II", nbones, nframes)]
    for f in range(nframes):
        t = float(f32(t0) + flen * f32(f))
        frame = [((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))] * nbones
        for n in jnodes:
            if n is None:
                continue
            b = mapped.get(Doc.name(n))
            if b is None or b[1] < 0:
                continue
            frame[b[1]] = decompose(doc.world(n, t))
        for tr, q in frame:
            gt, gq = game_space((scale @ np.append(tr, 1.0))[:3], q, doc.y_up)
            out.append(struct.pack("<7f", *gt, *gq))
    payload = b"".join(out)
    return b"PSSA" + struct.pack("<II", 1, len(payload)) + payload


# ------------------------------------------------------------- archive ---

class ColladaArchive:
    """The parts of zipfile.ZipFile extract_0ad_assets.py uses, over a 0 A.D.
    checkout: real files under their archive paths, plus a `.cached.pmd` /
    `.cached.psa` name for every Collada mesh and animation."""

    def __init__(self, path):
        pub = os.path.join(path, "binaries", "data", "mods", "public")
        self.root = pub if os.path.isdir(pub) else path
        self._names = {}
        for dirpath, _, files in os.walk(self.root):
            if "/.git" in dirpath:
                continue
            for f in files:
                full = os.path.join(dirpath, f)
                rel = os.path.relpath(full, self.root).replace(os.sep, "/")
                self._names[rel] = full
                if rel.endswith(".dae"):
                    if rel.startswith("art/meshes/"):
                        self._names[rel + ".cached.pmd"] = full
                    elif rel.startswith("art/animation/"):
                        self._names[rel + ".cached.psa"] = full
        skel_dir = os.path.join(self.root, "art", "skeletons")
        files = sorted(os.path.join(skel_dir, f) for f in os.listdir(skel_dir) if f.endswith(".xml")) \
            if os.path.isdir(skel_dir) else []
        self.skeletons = Skeletons(files)
        self._cache = {}

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def namelist(self):
        return list(self._names)

    def getinfo(self, name):
        class Info:
            file_size = os.path.getsize(self._names[name])
        return Info()

    def read(self, name):
        full = self._names.get(name)
        if full is None:
            raise KeyError(name)
        if name in self._cache:
            return self._cache[name]
        try:
            if name.endswith(".dae.cached.pmd"):
                data = collada_to_pmd(full, self.skeletons)
            elif name.endswith(".dae.cached.psa"):
                data = collada_to_psa(full, self.skeletons)
            else:
                with open(full, "rb") as fh:
                    return fh.read()
        except ValueError as e:
            raise ValueError(f"{name}: {e}") from e
        self._cache[name] = data
        return data
