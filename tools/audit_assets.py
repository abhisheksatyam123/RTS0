#!/usr/bin/env python3
"""Fail if any res://assets/ path in src/ or scenes/ is missing on disk.

Also reports roster GLB clip coverage (must be 0 missing wanted clips).
"""
import argparse
import json
import os
import re
import sys
from pathlib import Path

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ASSETS = os.path.join(ROOT, "assets")
SRC_ROOTS = [os.path.join(ROOT, "src"), os.path.join(ROOT, "scenes")]
_CANDIDATE_ZIPS = [
    os.path.join(os.path.dirname(__file__), "..", "..", "0ad",
                 "binaries", "data", "mods", "public", "public.zip"),
    "/home/abhi/project/0ad/binaries/data/mods/public/public.zip",
]
DEFAULT_ZIP = next((p for p in _CANDIDATE_ZIPS if os.path.exists(p)), _CANDIDATE_ZIPS[0])

PATH_RE = re.compile(r"res://assets/([A-Za-z0-9_./-]+)")
FILE_EXTS = {".ogg", ".png", ".glb", ".xml", ".psa", ".json", ".tres", ".wav"}
SCAN_EXTS = {".cpp", ".h", ".hpp", ".gd", ".tscn", ".cs"}


def collect_refs():
    refs = {}
    for src_root in SRC_ROOTS:
        if not os.path.isdir(src_root):
            continue
        for dirpath, _dirs, files in os.walk(src_root):
            for name in files:
                ext = os.path.splitext(name)[1].lower()
                if ext not in SCAN_EXTS:
                    continue
                path = os.path.join(dirpath, name)
                try:
                    text = open(path, encoding="utf-8", errors="ignore").read()
                except OSError:
                    continue
                for m in PATH_RE.finditer(text):
                    rel = m.group(1)
                    refs.setdefault(rel, []).append(os.path.relpath(path, ROOT))
    return refs


def on_disk(assets_dir, rel):
    p = os.path.join(assets_dir, rel)
    if os.path.isfile(p):
        return True
    # Godot ResourceLoader also accepts dirs (animations/, skeletons/)
    ext = os.path.splitext(rel)[1].lower()
    if not ext or ext not in FILE_EXTS:
        if os.path.isdir(p):
            return True
        parent = os.path.dirname(p)
        base = os.path.basename(p)
        if os.path.isdir(parent) and any(f.startswith(base) for f in os.listdir(parent)):
            return True
    return False


def audit_paths(assets_dir):
    refs = collect_refs()
    missing = []
    for rel in sorted(refs):
        if on_disk(assets_dir, rel):
            continue
        missing.append((rel, refs[rel][:3]))
    print(f"[audit] {len(refs)} unique res://assets/ paths, {len(missing)} missing")
    for rel, srcs in missing:
        print(f"  MISSING assets/{rel}  (from {', '.join(srcs)})")
    return missing


def audit_manifest_files(assets_dir):
    """Resolve every manifest file and translate flattened terrain names."""
    man_path = os.path.join(assets_dir, "models", "manifest.json")
    if not os.path.isfile(man_path):
        print("[audit] no models/manifest.json — skip manifest files")
        return 0
    with open(man_path) as f:
        manifest = json.load(f)
    missing = []
    for key in ("models", "animations", "skeletons"):
        for name, meta in manifest.get(key, {}).items():
            rel = meta.get("file") if isinstance(meta, dict) else meta
            if rel and not os.path.isfile(os.path.join(assets_dir, rel)):
                missing.append(f"{key}.{name} -> {rel}")
    terrain_root = os.path.join(assets_dir, "textures", "terrain")
    terrain_missing = []
    for name, src in manifest.get("terrain", {}).items():
        stem = os.path.splitext(os.path.basename(src))[0]
        # Extracted terrain names are normalized to <biome>_<material>;
        # source paths retain a types/<biome>/ directory. Accept both forms.
        normalized = stem
        src_parts = Path(src).parts
        if len(src_parts) >= 2 and src_parts[0] == "types":
            normalized = src_parts[-2] + "_" + stem
        found = any(os.path.isfile(os.path.join(terrain_root, candidate + ext))
                    for candidate in (stem, normalized)
                    for ext in (".png", ".jpg", ".dds"))
        if not found:
            terrain_missing.append(f"terrain.{name} -> {src}")
    missing.extend(terrain_missing)
    print(f"[audit] manifest files: {len(missing)} unresolved "
          f"({len(terrain_missing)} terrain translation gaps)")
    for item in missing[:20]:
        print("  MISSING", item)
    return len(missing)

def audit_clips(assets_dir):
    sys.path.insert(0, os.path.dirname(__file__))
    from extract_0ad_assets import audit_extracted_clips
    man_path = os.path.join(assets_dir, "models", "manifest.json")
    if not os.path.isfile(man_path):
        print("[audit] no models/manifest.json — skip clip coverage")
        return 0
    with open(man_path) as f:
        manifest = json.load(f)
    return audit_extracted_clips(manifest)



def audit_pmd_diff(zip_path, assets_dir):
    """Print manifest models vs public.zip PMD count + numeric diff (read-only)."""
    import zipfile
    man_path = os.path.join(assets_dir, "models", "manifest.json")
    try:
        with open(man_path) as f:
            manifest = json.load(f)
    except OSError:
        print("[pmd-diff] no models/manifest.json — cannot diff")
        return
    shipped = len(manifest.get("models", {}))
    try:
        with zipfile.ZipFile(zip_path) as zf:
            names = zf.namelist()
    except OSError as e:
        print(f"[pmd-diff] cannot read zip {zip_path}: {e}")
        return
    pmd = [n for n in names if ".pmd" in n]
    pmd_count = len(pmd)

    def stem(n):
        b = os.path.basename(n).lower()
        return (b.replace(".dae.cached.pmd", "").replace(".cached.pmd", "")
                 .replace(".pmd", ""))

    pmd_stems = {stem(n) for n in pmd}
    srcs = set()
    for _k, v in manifest.get("models", {}).items():
        if isinstance(v, dict):
            for s in v.get("sources", []) or []:
                srcs.add(s.lower())
    mesh_stems = {os.path.basename(s.split("mesh:")[-1]).replace(".dae", "")
                  for s in srcs if s.startswith("mesh:")}
    actor_srcs = {s for s in srcs if s.startswith("actor:")}
    actor_xml = [n.lower() for n in names
                 if n.startswith("art/actors/") and n.endswith(".xml")]
    mesh_hit = len(mesh_stems & pmd_stems)
    actor_hit = sum(1 for a in actor_xml if ("actor:" + a) in srcs)
    gap = pmd_count - shipped
    cov = (100.0 * shipped / pmd_count) if pmd_count else 0.0
    print(f"[pmd-diff] zip: {zip_path}")
    print(f"[pmd-diff] manifest models: {shipped}  "
          f"public.zip PMD: {pmd_count}  gap: {gap}  coverage: {cov:.1f}%")
    print(f"[pmd-diff] mesh-basename overlap: {mesh_hit}/{len(pmd_stems)} "
          f"({100.0 * mesh_hit / len(pmd_stems):.1f}% of PMD stems composed into a GLB)")
    print(f"[pmd-diff] actor-XML overlap: {actor_hit}/{len(actor_xml)} "
          f"({100.0 * actor_hit / len(actor_xml):.1f}% of actor XMLs referenced as sources)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--assets", default=ASSETS)
    ap.add_argument("--zip", default=DEFAULT_ZIP)
    ap.add_argument("--pmd-diff", action="store_true",
                    help="print manifest models vs public.zip PMD count diff and exit 0")
    args = ap.parse_args()
    assets_dir = os.path.abspath(args.assets)
    if args.pmd_diff:
        audit_pmd_diff(os.path.abspath(args.zip), assets_dir)
        return
    missing = audit_paths(assets_dir)
    manifest_missing = audit_manifest_files(assets_dir)
    clip_miss = audit_clips(assets_dir)
    if missing:
        sys.exit(f"[audit] {len(missing)} res://assets/ files missing on disk")
    if manifest_missing:
        sys.exit(f"[audit] {manifest_missing} manifest files unresolved")
    if clip_miss:
        sys.exit(f"[audit] {clip_miss} roster models missing wanted clips")
    print("[audit] ok")


if __name__ == "__main__":
    main()
