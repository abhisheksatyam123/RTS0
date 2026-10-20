#!/usr/bin/env python3
"""Import the RTS0 Godot project's assets (`godot --headless --import`) in batches.

Godot 4.3 keeps memory from every file it imports until the process exits. One import of all
of RTS0's assets (741 glTF models with ~2,600 embedded textures, ~2,000 PNGs, ~1,600 OGGs)
grows past 10 GB, which kills a 7 GB CI runner ("received a shutdown signal"). This script
works out which files Godot is going to import, moves them out to .godot/import_staging, and
brings them back one batch per Godot run.

Which files Godot imports: Godot 4.3 imports every file whose entry in its file cache
(.godot/editor/filesystem_cache8) is missing or has a different modified time for the file or
its .import file, even when the content is unchanged. That cache is only written when Godot
exits, so every run must exit cleanly: a killed run starts over. A watchdog (Linux) still kills
Godot above a memory cap before the machine runs out; that batch then goes back to staging and
comes back in halves.

Usage:
  python3 tools/godot_import.py [--godot PATH] [--batch 1000] [--max-rss-mb 5000]

The Godot binary comes from --godot, then the GODOT environment variable, then `godot` on PATH.
If the script is killed, its next run moves the staged files back before anything else.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
STAGE = REPO / ".godot" / "import_staging"
CACHE = REPO / ".godot" / "editor" / "filesystem_cache8"

IMPORTABLE = {".png", ".jpg", ".jpeg", ".webp", ".svg", ".tga", ".bmp", ".exr", ".hdr",
              ".ogg", ".wav", ".mp3", ".ttf", ".otf", ".woff", ".woff2", ".fnt", ".csv",
              ".glb", ".gltf", ".dae", ".obj", ".fbx", ".blend"}
MODELS = {".glb", ".gltf", ".dae", ".obj", ".fbx", ".blend"}
MODEL_WEIGHT = 16  # a model and its embedded textures cost about 16 plain files of memory


def log(msg: str) -> None:
    print(f"godot_import: {msg}", flush=True)


def tracked_importable() -> list[str]:
    """Tracked files Godot imports, models last (other files may be their dependencies)."""
    out = subprocess.run(["git", "ls-files", "-z"], cwd=REPO, check=True,
                         capture_output=True).stdout.decode("utf-8", "surrogateescape")
    files = [f for f in out.split("\0") if f and Path(f).suffix.lower() in IMPORTABLE]
    skipped: dict[str, bool] = {}

    def gdignored(folder: str) -> bool:  # Godot skips a folder holding a .gdignore, and its subfolders
        if folder not in skipped:
            skipped[folder] = bool(folder) and ((REPO / folder / ".gdignore").exists() or
                                                gdignored(os.path.dirname(folder)))
        return skipped[folder]

    files = [f for f in files if not gdignored(os.path.dirname(f))]
    return ([f for f in files if Path(f).suffix.lower() not in MODELS] +
            [f for f in files if Path(f).suffix.lower() in MODELS])


def cached_times() -> dict[str, tuple[int, int]]:
    """res:// path -> (modified time, .import modified time) from Godot's file cache."""
    times: dict[str, tuple[int, int]] = {}
    if not CACHE.exists():
        return times
    folder = ""
    with open(CACHE, encoding="utf-8", errors="surrogateescape") as f:
        next(f, None)  # project hash
        for line in f:
            fields = line.rstrip("\n").split("::")
            if line.startswith("::"):
                folder = fields[1][len("res://"):] if len(fields) > 1 else ""
            elif len(fields) > 4:
                try:
                    times[folder + fields[0]] = (int(fields[3]), int(fields[4]))
                except ValueError:
                    pass
    return times


def up_to_date(rel: str, times: dict[str, tuple[int, int]]) -> bool:
    entry = times.get(rel)
    if entry is None:
        return False
    try:
        return (entry[0] == int(os.stat(REPO / rel).st_mtime) and
                entry[1] == int(os.stat(REPO / (rel + ".import")).st_mtime))
    except FileNotFoundError:
        return False


def stage_out(rel: str) -> None:
    dst = STAGE / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    os.replace(REPO / rel, dst)


def stage_in(rel: str) -> None:
    dst = REPO / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    os.replace(STAGE / rel, dst)


def restore() -> None:
    """Moves every staged file back into the project."""
    if not STAGE.exists():
        return
    kept = 0
    for p in sorted(STAGE.rglob("*")):
        if p.is_file():
            rel = p.relative_to(STAGE).as_posix()
            if (REPO / rel).exists():
                kept += 1  # never overwrite a file that came back some other way
            else:
                stage_in(rel)
    if kept:
        log(f"{kept} staged files also exist in the project; left them in {STAGE}")
        return
    shutil.rmtree(STAGE, ignore_errors=True)


def rss_kb(pid: int) -> int:
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except (OSError, ValueError):
        pass
    return 0


def run_godot(godot: str, max_rss_kb: int) -> tuple[int, bool]:
    """One clean Godot import. Returns (exit status, killed by the watchdog)."""
    proc = subprocess.Popen([godot, "--headless", "--path", str(REPO), "--import"], cwd=REPO)
    peak = 0
    capped = False
    while proc.poll() is None:
        rss = rss_kb(proc.pid)
        peak = max(peak, rss)
        if rss > max_rss_kb:
            capped = True
            proc.kill()
            break
        time.sleep(1)
    status = proc.wait()
    log(f"Godot exit={status} peak_rss={peak // 1024}MB")
    return status, capped


def find_godot(arg: str | None) -> str:
    godot = arg or os.environ.get("GODOT") or shutil.which("godot")
    if not godot:
        sys.exit("godot_import: no Godot binary (pass --godot or set GODOT)")
    return godot


def compress_model_textures() -> int:
    """Switch the textures unpacked from models to VRAM compression (S3TC on desktop, ETC2
    on mobile), as the editor's "Detect 3D" does when it first draws them; a headless import
    never draws, so they would stay lossless and about twice the size in an export. Returns
    how many .import files changed: Godot reimports those on its next run."""
    changed = 0
    for imp in (REPO / "assets" / "models").rglob("*.import"):
        if Path(imp.stem).suffix.lower() not in {".png", ".jpg", ".jpeg", ".webp"}:
            continue
        text = imp.read_text()
        if "\ncompress/mode=0\n" in text and "\ndetect_3d/compress_to=1\n" in text:
            imp.write_text(text.replace("\ncompress/mode=0\n", "\ncompress/mode=2\n")
                           .replace("\ndetect_3d/compress_to=1\n", "\ndetect_3d/compress_to=0\n"))
            changed += 1
    if changed:
        log(f"{changed} model textures set to VRAM compression")
    return changed


def import_assets(godot: str, batch: int = 1000, max_rss_mb: int = 5000) -> int:
    max_rss_kb = max_rss_mb * 1024
    restore()
    times = cached_times()
    pending = [f for f in tracked_importable() if not up_to_date(f, times)]
    if not pending:
        compress_model_textures()
        log("every asset is in Godot's cache, checking for anything else")
        status, capped = run_godot(godot, max_rss_kb)
        return 1 if capped else status

    log(f"{len(pending)} files to import")
    for rel in pending:
        stage_out(rel)
    try:
        i = 0
        rounds = 0
        switched = 0
        while i < len(pending):
            rounds += 1
            first = i
            weight = 0
            while i < len(pending):
                w = MODEL_WEIGHT if Path(pending[i]).suffix.lower() in MODELS else 1
                if weight and weight + w > batch:
                    break
                stage_in(pending[i])
                weight += w
                i += 1
            start = time.time()
            log(f"round {rounds}: files {first + 1}-{i} of {len(pending)}")
            status, capped = run_godot(godot, max_rss_kb)
            log(f"round {rounds} took {time.time() - start:.0f}s")
            if capped:
                if batch <= 2 * MODEL_WEIGHT:
                    log(f"Godot passed {max_rss_mb} MB even on a small batch")
                    return 1
                batch //= 2
                log(f"over the memory cap, retrying the batch at size {batch}")
                for k in range(first, i):
                    stage_out(pending[k])
                i = first
            elif status != 0:
                log(f"Godot exited with {status}")
                return status
            else:
                # This round's unpacked textures reimport compressed in the next round.
                switched = compress_model_textures()
        if switched:
            log("reimporting the last round's model textures compressed")
            status, capped = run_godot(godot, max_rss_kb)
            if capped or status != 0:
                return 1 if capped else status
        log(f"done in {rounds} rounds")
        return 0
    finally:
        restore()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--godot", help="Godot 4.3 binary (default: $GODOT, then godot on PATH)")
    ap.add_argument("--batch", type=int, default=1000,
                    help=f"files per Godot run, a 3D model counting as {MODEL_WEIGHT} (default 1000)")
    ap.add_argument("--max-rss-mb", type=int, default=5000,
                    help="kill Godot above this resident memory and retry smaller (default 5000)")
    args = ap.parse_args()
    return import_assets(find_godot(args.godot), args.batch, args.max_rss_mb)


if __name__ == "__main__":
    sys.exit(main())
