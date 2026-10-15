#!/usr/bin/env python
# RTS0 native GDExtension — high-performance C++ simulation & GDExtension nodes
# Build with: scons target=template_release  or  scons target=template_debug
# Requires godot-cpp at godot-cpp/ (branch 4.3) — clone via: git clone --branch 4.3 https://github.com/godotengine/godot-cpp.git godot-cpp

import os

env = SConscript("godot-cpp/SConstruct")

env.Append(CPPPATH=["src"])
env.Append(CPPFLAGS=["-std=c++20"])
if env["platform"] == "linux" or env["platform"] == "linuxbsd":
	env.Append(CCFLAGS=["-pthread"])
	env.Append(LINKFLAGS=["-pthread"])
if env["platform"] == "android":
	# The APK carries no libc++_shared.so, so link the C++ runtime in.
	env.Append(LINKFLAGS=["-static-libstdc++"])
if env["platform"] == "windows":
	# src/network sockets (MSVC also reads a #pragma comment, MinGW only this).
	env.Append(LIBS=["ws2_32"])
# Lockstep needs every build to compute the same bits. Clang fuses a*b+c into one FMA by
# default on arm64 (Apple Silicon, iOS, Android) and x86-64 builds never do, so multiplayer
# between them desyncs on turn 1. Keep contraction off everywhere, at link time too for LTO.
# Never add -ffast-math, -Ofast or /fp:fast (tools/test_cross_compiler.py checks).
if env.get("is_msvc", False):
	env.Append(CCFLAGS=["/fp:precise"])
else:
	env.Append(CCFLAGS=["-ffp-contract=off"])
	env.Append(LINKFLAGS=["-ffp-contract=off"])

if env["target"] == "template_release":
    env.Append(CCFLAGS=["-O3", "-DNDEBUG"])
elif env["target"] == "template_debug":
    env.Append(CCFLAGS=["-O0", "-g", "-DDEBUG_ENABLED"])
else:
    env.Append(CCFLAGS=["-O2", "-g"])

sources = []
for root, dirs, files in os.walk("src"):
    # 0ad vendor is reference only — not built (requires 0 A.D. lib stubs, SpiderMonkey, etc.)
    # Real game uses SoA rewrite in src/sim + src/core with cache-friendly arena/pool.
    # Vendor files under src/0ad are kept for reference & future porting.
    if "0ad" in root.split(os.sep):
        continue
    for f in files:
        if f.endswith(".cpp"):
            sources.append(os.path.join(root, f))

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "bin/librtso.{}.{}.framework/librtso.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
elif env["platform"] == "ios":
    if env.get("ios_simulator", False):
        library = env.StaticLibrary(
            "bin/librtso.{}.{}.simulator.a".format(env["platform"], env["target"]),
            source=sources,
        )
    else:
        library = env.StaticLibrary(
            "bin/librtso.{}.{}.a".format(env["platform"], env["target"]),
            source=sources,
        )
else:
    library = env.SharedLibrary(
        "bin/librtso{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

env.NoCache(library)
Default(library)

# After a Linux release library, copy it next to the export and rebuild
# RTS0.x86_64 when Godot is on PATH (skip with: scons skip_export=1).
def _export_linux_release(target, source, env):
	import shutil
	import subprocess
	from pathlib import Path

	so = Path(str(target[0]))
	dest_dir = Path("build/linux")
	dest_dir.mkdir(parents=True, exist_ok=True)
	shutil.copy2(so, dest_dir / so.name)
	if str(ARGUMENTS.get("skip_export", "0")) in ("1", "true", "yes"):
		print("skip_export=1 — left %s, no Godot pack" % (dest_dir / so.name))
		return
	godot = os.environ.get("GODOT") or shutil.which("godot") or shutil.which("godot4")
	if not godot:
		print("Copied %s; set GODOT or install godot to refresh RTS0.x86_64" % so.name)
		return
	exe = dest_dir / "RTS0.x86_64"
	print("Exporting Linux -> %s" % exe)
	subprocess.check_call([str(godot), "--headless", "--path", ".", "--import"])
	subprocess.check_call([str(godot), "--headless", "--path", ".", "--export-release", "Linux", str(exe)])
	try:
		exe.chmod(0o755)
	except OSError:
		pass

_plat = str(env.get("platform", ""))
if _plat in ("linux", "linuxbsd") and env["target"] == "template_release":
	env.AddPostAction(library, _export_linux_release)
