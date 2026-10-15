#!/usr/bin/env bash
# osxcross stand-in for building the macOS GDExtension on Linux with zig
# (pip install ziglang). tools/build.py points OSXCROSS_ROOT here; the
# arm64-/x86_64-apple-darwin16-* names are symlinks to this file.
set -euo pipefail
here="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
# SCons runs tools with a scrubbed environment (no HOME), so zig gets its cache here.
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-$here/.cache}"
export ZIG_LOCAL_CACHE_DIR="${ZIG_LOCAL_CACHE_DIR:-$here/.cache}"
if [ -n "${ZIG:-}" ]; then :
elif command -v zig >/dev/null; then ZIG=zig
elif [ -f "$here/.zig-path" ]; then ZIG="$(cat "$here/.zig-path")"
else ZIG="python3 -m ziglang"
fi
name="$(basename "$0")"
case "$name" in
	arm64-*) tgt=aarch64-macos.11.0 ;;
	x86_64-*) tgt=x86_64-macos.11.0 ;;
esac
tool="${name##*-}"
case "$tool" in
	ar | ranlib) exec $ZIG "$tool" "$@" ;;
	as) exec $ZIG cc -target "$tgt" "$@" ;;
esac
args=()
skip=0
for a in "$@"; do
	if [ $skip = 1 ]; then skip=0; continue; fi
	if [ "${prev:-}" = -o ]; then prev=; args+=("$a"); continue; fi
	prev="$a"
	case "$a" in
		# zig picks the arch from -target and links no Cocoa: the extension needs only
		# libSystem, and Godot resolves the rest at load (-undefined dynamic_lookup).
		-arch | -framework) skip=1 ;;
		# zig picks a linker input's kind by extension, and SCons names shared objects .os.
		*.os) [ -n "${objs:-}" ] || { objs="$(mktemp -d)"; trap 'rm -rf "$objs"' EXIT; }
			n=$((${n:-0} + 1)); ln -s "$(readlink -f "$a")" "$objs/$n.o"; args+=("$objs/$n.o") ;;
		*) args+=("$a") ;;
	esac
done
drv=cc; [ "$tool" = clang++ ] && drv=c++
$ZIG $drv -target "$tgt" "${args[@]}"
