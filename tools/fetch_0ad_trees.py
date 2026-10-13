#!/usr/bin/env python3
"""Fetches 0 A.D. tree actors (art/actors/flora/trees/, CC BY-SA 3.0, see
docs/0AD_ATTRIBUTION.md) from the source tree on GitHub and converts each into a
static GLB at assets/models/vegetation/<id>.glb, with its entry in
assets/models/manifest.json.

  * The actor is the one named by the gaia template's <VisualActor><Actor>
    (simulation/templates/gaia/tree/<name>.xml, TREES below).
  * Actors are composed the way 0 A.D.'s ObjectBase does it, simplified: the first
    variant of every <group> is picked (frequencies ignored), and its <mesh>,
    baseTex <texture> and <props> are merged. A variant's file="..." pulls in
    art/variants/<file> first. Each prop actor is composed recursively and placed
    at the parent mesh's "prop_<attachpoint>" node ("root" = parent origin).
    Particle, decal and empty props are skipped.
  * Meshes are the Collada sources (art/meshes/*.dae); skins are DDS/PNG decoded to
    PNG. Skins with transparency become alpha-scissored, double-sided materials.
  * Godot assembles the GLB (tools/compose_tree_glb.gd) from a scratch project in
    the work directory, never from this repository.

The release archive's cached .pmd meshes (extract_0ad_assets.py) are not shipped
here; raw.githubusercontent.com serves the source files.

  python3 tools/fetch_0ad_trees.py <godot 4.3 binary> [--work DIR] [--only ID ...]
"""
import argparse
import http.client
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

from PIL import Image

RAW = "https://raw.githubusercontent.com/0ad/0ad/master/binaries/data/mods/public/"
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO, "assets", "models", "vegetation")
MANIFEST = os.path.join(REPO, "assets", "models", "manifest.json")
COMPOSER = os.path.join(REPO, "tools", "compose_tree_glb.gd")

# RTS0 model id -> 0 A.D. gaia template (simulation/templates/gaia/tree/<name>.xml).
TREES = {
    "carob": "carob",
    "poplar": "poplar_lombardy",
    "fan_palm": "medit_fan_palm",
    "beech": "euro_beech",
    "dead_tree": "dead",
    "cypress": "cypress",
    "aleppo_pine": "aleppo_pine",
    "fir": "fir",
    "tropic_bush": "bush_tropic",
    "olive": "olive",
    "banyan": "banyan",
    "tropic_palm": "palm_tropic",
    "maple": "maple",
    "juniper": "juniper_prickly",
    "teak": "teak",
}

MAX_PROP_DEPTH = 4
# A skin counts as transparent when this share of its texels is below the
# scissor threshold (0.5); trunk bark has none, leaf cards have plenty.
ALPHA_SHARE = 0.01


class Source:
    """Downloads files under RAW into a cache directory, once each."""

    def __init__(self, cache):
        self.cache = cache

    def path(self, rel, tries=4):
        dst = os.path.join(self.cache, rel)
        if os.path.exists(dst):
            return dst
        for attempt in range(tries):
            try:
                with urllib.request.urlopen(RAW + urllib.parse.quote(rel), timeout=60) as r:
                    data = r.read()
                break
            except urllib.error.HTTPError as e:
                if e.code == 404:
                    return None
                if attempt == tries - 1:
                    raise
            except (urllib.error.URLError, http.client.HTTPException, OSError):
                if attempt == tries - 1:
                    raise
            time.sleep(1 + attempt)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as fh:
            fh.write(data)
        return dst

    def xml(self, rel):
        p = self.path(rel)
        if p is None:
            raise SystemExit(f"missing {rel}")
        return ET.parse(p).getroot()


def template_actor(src, name):
    """The <VisualActor><Actor> of a gaia tree template, e.g. flora/trees/carob.xml."""
    actor = src.xml(f"simulation/templates/gaia/tree/{name}.xml").findtext("VisualActor/Actor")
    if not actor:
        raise SystemExit(f"gaia/tree/{name}.xml has no VisualActor")
    return actor.strip()


def _skipped_prop(src, actor):
    """Props that carry no static geometry: empty, particles, decals and shadows."""
    if not actor:
        return True
    low = actor.lower()
    if "particle" in low or "decal" in low or "shadow" in low:
        return True
    material = src.xml("art/actors/" + actor).findtext("material") or ""
    return "decal" in material.lower()


def _variant_parts(src, variant):
    """(mesh, baseTex, [(actor, attachpoint)]) of one <variant>, file= included."""
    mesh = tex = None
    props = []
    if variant.get("file"):
        inc = src.xml("art/variants/" + variant.get("file"))
        inc = inc if inc.tag == "variant" else inc.find("variant")
        mesh, tex, props = _variant_parts(src, inc)
    if variant.findtext("mesh"):
        mesh = variant.findtext("mesh").strip()
    for t in variant.findall("textures/texture"):
        if t.get("name") == "baseTex" and t.get("file"):
            tex = t.get("file")
    own = [(p.get("actor", ""), p.get("attachpoint", "root")) for p in variant.findall("props/prop")]
    return mesh, tex, _merge_props(props, own)


def _merge_props(props, later):
    """A variant's props replace every earlier prop at the same attach point (an
    empty actor just clears the point); props of one variant can share a point."""
    points = {q[1] for q in later}
    return [q for q in props if q[1] not in points] + later


def compose(src, actor, used, depth=0):
    """Plan node for one actor: {"mesh", "skin", "props": [{"attach", "node"}]}.

    `used` collects every source path (actor:/mesh:/skin:) for the manifest."""
    root = src.xml("art/actors/" + actor)
    used.append("actor:art/actors/" + actor)
    mesh = tex = None
    props = []
    for group in root.findall("group"):
        variant = group.find("variant")
        if variant is None:
            continue
        m, t, p = _variant_parts(src, variant)
        mesh, tex = m or mesh, t or tex
        props = _merge_props(props, p)
    node = {"actor": actor, "mesh": mesh, "skin": tex,
            "material": (root.findtext("material") or "").strip(), "props": []}
    if mesh:
        used.append("mesh:" + mesh)
    if tex and mesh:
        used.append("skin:" + os.path.splitext(tex)[0] + ".png")
    if depth >= MAX_PROP_DEPTH:
        return node
    for prop_actor, point in props:
        if _skipped_prop(src, prop_actor):
            continue
        node["props"].append({"attach": point, "node": compose(src, prop_actor, used, depth + 1)})
    return node


def _flat(rel):
    """Scratch-project file name for a 0 A.D. relative path."""
    return rel.replace("/", "__")


def stage(src, node, proj):
    """Copy a plan's meshes and decoded skins into the Godot project; rewrite paths."""
    if node["mesh"]:
        dae = src.path("art/meshes/" + node["mesh"])
        if dae is None:
            raise SystemExit(f"missing mesh {node['mesh']}")
        node["mesh"] = _flat(node["mesh"])
        shutil.copyfile(dae, os.path.join(proj, node["mesh"]))
    if node["mesh"] and node["skin"]:
        stem = os.path.splitext(node["skin"])[0]
        img_path = src.path(f"art/textures/skins/{node['skin']}")
        if img_path is None:  # actors name .png for files shipped as .dds and vice versa
            for ext in (".dds", ".png"):
                img_path = img_path or src.path(f"art/textures/skins/{stem}{ext}")
        if img_path is None:
            raise SystemExit(f"missing skin {node['skin']}")
        img = Image.open(img_path).convert("RGBA")
        alpha = img.getchannel("A").histogram()
        node["alpha"] = sum(alpha[:128]) > ALPHA_SHARE * img.width * img.height
        node["skin"] = _flat(stem) + ".png"
        img.save(os.path.join(proj, node["skin"]))
    else:
        node["skin"] = None
    for p in node["props"]:
        stage(src, p["node"], proj)


def count_meshes(node):
    return (1 if node["mesh"] else 0) + sum(count_meshes(p["node"]) for p in node["props"])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("godot", help="Godot 4.3 editor binary")
    ap.add_argument("--work", help="scratch directory (downloads + Godot project); default: a temp dir")
    ap.add_argument("--only", nargs="+", choices=sorted(TREES))
    ap.add_argument("--no-manifest", action="store_true")
    args = ap.parse_args()

    work = os.path.abspath(args.work or tempfile.mkdtemp(prefix="rts0_trees_"))
    src = Source(os.path.join(work, "download"))
    proj = os.path.join(work, "project")
    os.makedirs(proj, exist_ok=True)
    with open(os.path.join(proj, "project.godot"), "w") as fh:
        fh.write('config_version=5\n\n[application]\n\nconfig/name="tree_compose"\n')

    ids = args.only or list(TREES)
    plans = {}
    for tree_id in ids:
        used = []
        actor = template_actor(src, TREES[tree_id])
        plan = compose(src, actor, used)
        stage(src, plan, proj)
        plans[tree_id] = (plan, sorted(set(used)))
        print(f"{tree_id}: {actor}, {count_meshes(plan)} meshes")

    # Godot imports the .dae/.png files once, then builds each GLB.
    subprocess.run([args.godot, "--headless", "--path", proj, "--import"], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.makedirs(OUT_DIR, exist_ok=True)
    entries = {}
    for tree_id, (plan, used) in plans.items():
        plan_path = os.path.join(proj, f"plan_{tree_id}.json")
        info_path = os.path.join(proj, f"info_{tree_id}.json")
        with open(plan_path, "w") as fh:
            json.dump(plan, fh, indent=1)
        out = os.path.join(OUT_DIR, tree_id + ".glb")
        subprocess.run([args.godot, "--headless", "--path", proj, "-s", COMPOSER, "--",
                        plan_path, out, info_path], check=True)
        with open(info_path) as fh:
            size = json.load(fh)["size"]
        if not 1.0 <= size[1] <= 40.0:  # 0 A.D. sizes: bushes ~2 m, teaks ~30 m
            print(f"warning: {tree_id}: implausible height {size[1]:.2f}", file=sys.stderr)
        entries[tree_id] = {
            "file": f"models/vegetation/{tree_id}.glb",
            "origin": "ground-center",
            "parts": count_meshes(plan),
            "size_meters": [round(v, 3) for v in size],
            "sources": used,
            "upstream_material": plan["material"],
        }
        print(f"{tree_id}: {os.path.getsize(out)} bytes, size {entries[tree_id]['size_meters']}")

    if not args.no_manifest and entries:
        with open(MANIFEST) as fh:
            man = json.load(fh)
        man["models"].update(entries)
        with open(MANIFEST, "w") as fh:
            json.dump(man, fh, indent=1, sort_keys=True)


if __name__ == "__main__":
    main()
