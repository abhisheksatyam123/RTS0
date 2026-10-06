#!/usr/bin/env python3
"""Full 0 A.D. public.zip dump into RTS0 assets (roster GLBs + archive media).

Never passes --only. Leftover actor composition can take hours; skip with
--skip-archive-actors.
"""
import argparse
import os
import subprocess
import sys
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TOOLS = os.path.join(ROOT, "tools")
ASSETS = os.path.join(ROOT, "assets")
_CANDIDATE_ZIPS = [
    os.path.join(os.path.dirname(__file__), "..", "..", "0ad",
                 "binaries", "data", "mods", "public", "public.zip"),
    "/home/abhi/project/0ad/binaries/data/mods/public/public.zip",
]
DEFAULT_ZIP = next((p for p in _CANDIDATE_ZIPS if os.path.exists(p)), _CANDIDATE_ZIPS[0])


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    r = subprocess.run(cmd)
    if r.returncode:
        sys.exit(r.returncode)


def zip_coverage(zip_path, assets_dir):
    with zipfile.ZipFile(zip_path) as zf:
        names = zf.namelist()
    zip_ogg = sum(1 for n in names if n.startswith("audio/") and n.endswith(".ogg"))
    zip_psa = sum(1 for n in names if n.endswith(".psa"))
    zip_skel = sum(1 for n in names if n.startswith("art/skeletons/") and n.endswith(".xml"))
    zip_act = sum(1 for n in names if n.startswith("art/actors/") and n.endswith(".xml"))

    def count_files(rel, ext):
        folder = os.path.join(assets_dir, rel)
        n = 0
        if not os.path.isdir(folder):
            return 0
        for root, _dirs, files in os.walk(folder):
            n += sum(1 for f in files if f.endswith(ext))
        return n

    disk_ogg = count_files("audio", ".ogg")
    disk_psa = count_files("animations", ".psa")
    disk_skel = count_files("skeletons", ".xml")
    disk_glb = count_files("models", ".glb")
    disk_arch = count_files(os.path.join("models", "archive"), ".glb")
    print("[coverage] zip vs disk")
    print(f"  ogg       zip={zip_ogg:5d}  assets/audio={disk_ogg:5d}")
    print(f"  psa       zip={zip_psa:5d}  assets/animations={disk_psa:5d}")
    print(f"  skeleton  zip={zip_skel:5d}  assets/skeletons={disk_skel:5d}")
    print(f"  actor xml zip={zip_act:5d}  models GLB={disk_glb:5d} (archive={disk_arch})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", default=DEFAULT_ZIP)
    ap.add_argument("--out", default=ASSETS)
    ap.add_argument("--skip-archive-actors", action="store_true",
                    help="do not compose leftover actors into models/archive/")
    args = ap.parse_args()
    out = os.path.abspath(args.out)
    py = sys.executable
    extract = os.path.join(TOOLS, "extract_0ad_assets.py")
    audit = os.path.join(TOOLS, "audit_assets.py")
    cmd = [py, extract, "--zip", args.zip, "--out", out,
           "--all", "--all-animations", "--all-skeletons"]
    if args.skip_archive_actors:
        cmd.append("--skip-archive-actors")
    run(cmd)
    sys.path.insert(0, TOOLS)
    import extract_media
    extract_media.main(args.zip, out)
    run([py, audit, "--zip", args.zip, "--assets", out])
    zip_coverage(args.zip, out)
    print("[extract_all] done")


if __name__ == "__main__":
    main()
