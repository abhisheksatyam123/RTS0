#!/usr/bin/env python3
"""Build all assets via code/programs — units/building/biome/resources. Mirrors 0ad pipeline but via Procedural.cs + PIL."""
import argparse, os, sys, pathlib, subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent

def run(cmd):
    print(f"> {' '.join(cmd)}")
    return subprocess.call(cmd)

def ensure_pillow():
    try:
        import PIL  # noqa
    except ImportError:
        print("installing Pillow...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "Pillow"])

def build(args):
    ensure_pillow()
    # 1. noise + splat via code (always deterministic)
    cmd = [sys.executable, str(ROOT/"tools"/"generate_noise.py"), "--size", str(args.size), "--seed", str(args.seed)]
    if run(cmd)!=0: return 1

    # 2. Validate glb imports (if any CC0 dropped into assets/imported)
    imported = list((ROOT/"assets"/"imported").glob("*.glb"))
    if imported:
        print(f"found {len(imported)} CC0 glb in assets/imported/ — will be optimized via godot import")
        # optional gltfpack check
        for glb in imported:
            print(f"  - {glb.name} ({glb.stat().st_size//1024}KB)")
    else:
        print("no imported glb — using Procedural fallback (thronefall primitives)")

    # 3. Verify canonical asset paths for MVP
    expected = [
        "assets/textures/noise/terrain_noise.png",
        "assets/textures/terrain/splat.png",
        "assets/materials/thronefall/unit.tres",
        "assets/materials/thronefall/terrain.tres",
        "assets/materials/thronefall/foliage.tres",
        "shaders/unit_thronefall.gdshader",
        "shaders/terrain.gdshader",
        "shaders/foliage_wind.gdshader",
    ]
    missing = [p for p in expected if not (ROOT/p).exists()]
    if missing:
        print("missing:", missing)
        if args.check:
            return 1

    # MVP unit/building/resource slots (procedural covers missing glb, so just warn)
    mvp_units = ["villager","swordsman","archer","spearman","cavalry"]
    mvp_buildings = ["house","town_center","barracks","fortress","tower","wall","market","farmstead"]
    mvp_veg = ["tree","gold_vein","stone_mine","fruit_bush","fish","treasure_chest"]
    for cat, ids, base in [
        ("units", mvp_units, "assets/models/units"),
        ("buildings", mvp_buildings, "assets/models/buildings"),
        ("vegetation", mvp_veg, "assets/models/vegetation"),
    ]:
        for id in ids:
            p = ROOT/f"{base}/{id}.glb"
            if not p.exists():
                print(f"procedural fallback: {cat}/{id} (no glb, will use Procedural.cs)")

    # 4. Optionally invoke godot --import headless to generate .ctex (if godot available)
    if args.import_godot:
        godot = args.godot or "godot"
        if run([godot, "--headless", "--path", str(ROOT), "--import"])!=0:
            print("godot import failed (godot not in PATH?) — skipping, will import on next editor launch")

    print(f"build_assets done size={args.size} seed={args.seed} (Procedural + PIL)")
    return 0

if __name__=="__main__":
    p=argparse.ArgumentParser(description="build things via code/programs")
    p.add_argument("--size", type=int, default=512, choices=[512,1024])
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--check", action="store_true", help="fail if required textures missing")
    p.add_argument("--import-godot", action="store_true", help="run godot --headless --import")
    p.add_argument("--godot", type=str, default=None)
    args=p.parse_args()
    sys.exit(build(args))
