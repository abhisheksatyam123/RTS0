#!/usr/bin/env python3
"""
Cross-platform build driver for RTS0: builds the C++ GDExtension for a platform,
imports the assets, and exports a playable build with the matching preset.

Usage:
  python tools/build.py linux        # -> build/linux/RTS0.x86_64
  python tools/build.py windows      # -> build/windows/RTS0.exe
  python tools/build.py android      # -> build/android/RTS0.apk (signed, sideloadable)
  python tools/build.py macos        # -> build/macos/RTS0.zip (universal, ad-hoc signed)
  python tools/build.py ios          # -> build/ios/RTS0.xcodeproj (macOS host with Xcode only)
  python tools/build.py all          # every target this host can build
  python tools/build.py linux windows android macos --package   # also zip each into build/dist/

Host requirements (docs/BUILD.md has the details):
  all       Godot 4.3-stable on PATH or in GODOT, its export templates, scons, godot-cpp/ (branch 4.3)
  windows   on Linux/macOS: mingw-w64 (x86_64-w64-mingw32-g++)
  android   ANDROID_HOME with ndk/<godot-cpp's ndk_version or ANDROID_NDK_VERSION> and build-tools,
            a JDK (keytool). Signs with build/android-keystore/rts0.keystore (made on first run,
            never committed) unless GODOT_ANDROID_KEYSTORE_RELEASE_PATH/_USER/_PASSWORD are set.
  macos     on macOS: Xcode command-line tools. On Linux: zig (pip install ziglang) + llvm-lipo.
  ios       macOS with Xcode; the export is an Xcode project you sign and run from Xcode.

Env:
  GODOT  path to the Godot 4.3 editor/headless binary
"""
import argparse
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BUILD = REPO / "build"
BIN = REPO / "bin"

PRESETS = {
    "linux":   ("Linux",           BUILD / "linux" / "RTS0.x86_64"),
    "windows": ("Windows Desktop", BUILD / "windows" / "RTS0.exe"),
    "android": ("Android",         BUILD / "android" / "RTS0.apk"),
    "macos":   ("macOS",           BUILD / "macos" / "RTS0.zip"),
    "ios":     ("iOS",             BUILD / "ios" / "RTS0.xcodeproj"),
}

KEYSTORE = BUILD / "android-keystore" / "rts0.keystore"
KEY_ALIAS = "rts0"
KEY_PASS = "rts0-sideload"


def find_godot() -> Path:
    env = os.environ.get("GODOT")
    if env:
        p = Path(env)
        if p.exists():
            return p
        sys.exit(f"GODOT={env} not found")
    which = shutil.which("godot") or shutil.which("godot4") or shutil.which("Godot")
    if which:
        return Path(which)
    candidates = []
    if sys.platform == "win32":
        candidates += [Path(r"C:\Godot\Godot_v4.3-stable_win64.exe"), Path(r"C:\Program Files\Godot\Godot.exe")]
    elif sys.platform == "darwin":
        candidates += [Path("/Applications/Godot.app/Contents/MacOS/Godot")]
    else:
        candidates += [Path("/usr/local/bin/godot"), Path("/opt/godot/godot"), Path.home() / ".local/bin/godot"]
    for c in candidates:
        if c.exists():
            return c
    sys.exit("Godot not found: set GODOT to the Godot 4.3 binary.")


def run(cmd, cwd=REPO, env=None):
    print(f"+ {' '.join(str(c) for c in cmd)}", flush=True)
    r = subprocess.run([str(c) for c in cmd], cwd=cwd, env=env)
    if r.returncode != 0:
        sys.exit(r.returncode)


def scons(*args, env=None):
    exe = shutil.which("scons")
    if not exe:
        sys.exit("scons not found (pip install scons)")
    jobs = f"-j{os.cpu_count() or 4}"
    run([exe, "target=template_release", "skip_export=1", jobs, *args], env=env)


def build_extension(target: str):
    print(f"\n== Building GDExtension for {target} ==", flush=True)
    if target == "linux":
        scons("platform=linux")
    elif target == "windows":
        scons("platform=windows", *([] if sys.platform == "win32" else ["use_mingw=yes"]))
    elif target == "android":
        if not os.environ.get("ANDROID_HOME"):
            sys.exit("android: set ANDROID_HOME to an Android SDK that has the NDK")
        extra = []
        if os.environ.get("ANDROID_NDK_VERSION"):
            extra.append(f"ndk_version={os.environ['ANDROID_NDK_VERSION']}")
        scons("platform=android", "arch=arm64", *extra)
    elif target == "macos":
        build_macos_extension()
    elif target == "ios":
        build_ios_extension()


def build_macos_extension():
    fw = BIN / "librtso.macos.template_release.framework"
    lib = fw / "librtso.macos.template_release"
    if sys.platform == "darwin":
        scons("platform=macos", "arch=universal")
    else:
        # No macOS SDK on Linux: zig ships the libSystem headers and stubs, so
        # tools/zig-osxcross stands in for osxcross. One build per arch, then lipo.
        zig = os.environ.get("ZIG") or shutil.which("zig")
        if not zig:
            try:
                import ziglang  # pip install ziglang
                zig = str(Path(ziglang.__file__).parent / "zig")
            except ImportError:
                sys.exit("macos: needs zig on Linux (pip install ziglang) or a macOS host")
        lipo = shutil.which("llvm-lipo") or next((shutil.which(f"llvm-lipo-{v}") for v in range(25, 13, -1) if shutil.which(f"llvm-lipo-{v}")), None)
        if not lipo:
            sys.exit("macos: needs llvm-lipo (apt install llvm)")
        root = REPO / "tools" / "zig-osxcross"
        (root / ".zig-path").write_text(zig)  # SCons drops ZIG from the tools' environment
        env = dict(os.environ, OSXCROSS_ROOT=str(root), ZIG=zig)
        parts = []
        for arch in ("arm64", "x86_64"):
            scons("platform=macos", f"arch={arch}", "macos_deployment_target=11.0", env=env)
            part = BUILD / "macos-ext" / f"librtso.{arch}"
            part.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(lib, part)
            parts.append(part)
        run([lipo, "-create", *parts, "-output", lib])
    # Godot writes the framework's Info.plist and ad-hoc signs it at export.


def build_ios_extension():
    if sys.platform != "darwin":
        sys.exit("ios: the GDExtension and the Xcode project need a macOS host with Xcode")
    scons("platform=ios", "arch=arm64")
    # One static library holding the extension and godot-cpp, wrapped as the
    # xcframework rts0.gdextension names, so Xcode links both into the app.
    xcf = BIN / "librtso.ios.template_release.xcframework"
    combined = BUILD / "ios-ext" / "librtso.ios.template_release.a"
    combined.parent.mkdir(parents=True, exist_ok=True)
    cpp = next((REPO / "godot-cpp" / "bin").glob("libgodot-cpp.ios.template_release.arm64*.a"))
    run(["libtool", "-static", "-o", combined, BIN / "librtso.ios.template_release.a", cpp])
    shutil.rmtree(xcf, ignore_errors=True)
    run(["xcodebuild", "-create-xcframework", "-library", combined, "-output", xcf])


def ensure_keystore():
    """A release keystore for sideloading. Kept under build/ (gitignored)."""
    if os.environ.get("GODOT_ANDROID_KEYSTORE_RELEASE_PATH"):
        return
    if not KEYSTORE.exists():
        KEYSTORE.parent.mkdir(parents=True, exist_ok=True)
        keytool = shutil.which("keytool") or sys.exit("android: keytool (a JDK) not found")
        run([keytool, "-genkeypair", "-keystore", KEYSTORE, "-alias", KEY_ALIAS, "-keyalg", "RSA",
             "-keysize", "2048", "-validity", "10000", "-storepass", KEY_PASS, "-keypass", KEY_PASS,
             "-dname", "CN=RTS0 sideload, O=RTS0, C=US"])
    os.environ["GODOT_ANDROID_KEYSTORE_RELEASE_PATH"] = str(KEYSTORE)
    os.environ["GODOT_ANDROID_KEYSTORE_RELEASE_USER"] = KEY_ALIAS
    os.environ["GODOT_ANDROID_KEYSTORE_RELEASE_PASSWORD"] = KEY_PASS


def write_android_editor_settings(godot: Path):
    """Godot reads the SDK path from its editor settings, not the environment."""
    sdk = os.environ["ANDROID_HOME"]
    java = os.environ.get("JAVA_HOME") or str(Path(shutil.which("java") or "/usr/bin/java").resolve().parents[1])
    if sys.platform == "darwin":
        cfg = Path.home() / "Library" / "Application Support" / "Godot"
    elif sys.platform == "win32":
        cfg = Path(os.environ["APPDATA"]) / "Godot"
    else:
        cfg = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "godot"
    cfg.mkdir(parents=True, exist_ok=True)
    f = cfg / "editor_settings-4.3.tres"
    lines = f.read_text().splitlines() if f.exists() else ['[gd_resource type="EditorSettings" format=3]', "", "[resource]"]
    want = {"export/android/android_sdk_path": sdk, "export/android/java_sdk_path": java}
    lines = [l for l in lines if l.split(" = ")[0] not in want]
    lines += [f'{k} = "{v}"' for k, v in want.items()]
    f.write_text("\n".join(lines) + "\n")


def package(target: str, out: Path) -> Path:
    dist = BUILD / "dist"
    dist.mkdir(parents=True, exist_ok=True)
    if target in ("android", "macos"):
        dest = dist / (f"RTS0-android-arm64.apk" if target == "android" else "RTS0-macos-universal.zip")
        shutil.copy2(out, dest)
        return dest
    names = {"linux": "RTS0-linux-x86_64.zip", "windows": "RTS0-windows-x86_64.zip", "ios": "RTS0-ios-xcode.zip"}
    dest = dist / names[target]
    with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as z:
        for p in sorted(out.parent.rglob("*")):
            if p.is_file() and not p.name.endswith("_console.exe"):
                info = zipfile.ZipInfo.from_file(p, Path("RTS0") / p.relative_to(out.parent))
                if target == "linux" and p.name == out.name:
                    info.external_attr = 0o755 << 16
                with open(p, "rb") as fh:
                    z.writestr(info, fh.read(), zipfile.ZIP_DEFLATED)
    return dest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("targets", nargs="*", default=["linux"], choices=[*PRESETS.keys(), "all"])
    ap.add_argument("--godot", default=None, help="override GODOT binary")
    ap.add_argument("--skip-scons", action="store_true", help="reuse the GDExtension libraries already in bin/")
    ap.add_argument("--skip-import", action="store_true", help="the assets are already imported")
    ap.add_argument("--package", action="store_true", help="also put one zip/apk per target in build/dist/")
    args = ap.parse_args()

    targets = list(PRESETS.keys()) if "all" in args.targets else list(dict.fromkeys(args.targets))
    if sys.platform != "darwin" and "ios" in targets:
        print("iOS needs macOS + Xcode: skipping on this host.", file=sys.stderr)
        targets.remove("ios")
    if not targets:
        return

    if not args.skip_scons:
        for t in targets:
            build_extension(t)

    godot = Path(args.godot) if args.godot else find_godot()
    print(f"Using Godot: {godot}")

    if not args.skip_import:
        # Import in batches: one Godot 4.3 import of every asset runs out of memory.
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from godot_import import import_assets
        status = import_assets(str(godot))
        if status != 0:
            sys.exit(status)

    for t in targets:
        preset, out = PRESETS[t]
        if t == "android":
            ensure_keystore()
            write_android_editor_settings(godot)
        if out.parent.exists():
            shutil.rmtree(out.parent)
        out.parent.mkdir(parents=True, exist_ok=True)
        print(f"\n== Exporting {preset} -> {out} ==", flush=True)
        run([godot, "--headless", "--path", REPO, "--export-release", preset, out])
        if not out.exists():
            sys.exit(f"{preset}: Godot exited 0 but wrote no {out}")
        if t == "linux":
            out.chmod(0o755)
        if args.package:
            print(f"Packaged {package(t, out)}")

    print(f"\nDone. Outputs in {BUILD}")


if __name__ == "__main__":
    main()
