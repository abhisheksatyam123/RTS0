#!/usr/bin/env bash
# Cross-platform build wrapper for RTS0 (Linux/macOS).
# Usage: ./tools/build.sh [windows|linux|android|ios|all]
# Requires: GODOT env var pointing to the Godot .NET editor binary,
# plus matching export templates installed (see docs/BUILD.md).
set -euo pipefail

TARGET="${1:-linux}"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$PROJECT_DIR/build"

if [[ -z "${GODOT:-}" ]]; then
    echo "Error: set GODOT to the Godot .NET editor executable." >&2
    exit 1
fi

echo "== Importing project resources =="
"$GODOT" --headless --path "$PROJECT_DIR" --import

export_one() {
    local preset="$1" out="$2"
    echo "== Exporting: $preset -> $out =="
    mkdir -p "$(dirname "$out")"
    "$GODOT" --headless --path "$PROJECT_DIR" --export-release "$preset" "$out"
}

case "$TARGET" in
    windows) export_one "Windows Desktop" "$OUT_DIR/windows/RTS0.exe" ;;
    linux)   export_one "Linux"           "$OUT_DIR/linux/RTS0.x86_64" ;;
    android) export_one "Android"         "$OUT_DIR/android/RTS0.apk" ;;
    ios)     export_one "iOS"             "$OUT_DIR/ios/RTS0" ;;
    all)
        export_one "Windows Desktop" "$OUT_DIR/windows/RTS0.exe"
        export_one "Linux"           "$OUT_DIR/linux/RTS0.x86_64"
        ;;
    *) echo "Unknown target: $TARGET" >&2; exit 1 ;;
esac

echo "== Done. Output in $OUT_DIR =="
