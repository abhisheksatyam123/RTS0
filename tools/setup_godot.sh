#!/usr/bin/env bash
# Install the Godot build ci.yml uses (GODOT_RELEASE, 4.3-stable Standard, Linux x86_64) so the
# headless GDScript checks (tools/e2e_*.gd) run in a fresh container, then optionally import the
# project's assets so they can load scenes. Build the GDExtension first (docs/BUILD.md).
#
#   tools/setup_godot.sh            # install to /usr/local/bin/godot (GODOT_DEST to change)
#   tools/setup_godot.sh --import   # also run tools/godot_import.py (a fresh checkout took 51 min on 4 cores)
#
# Then: godot --headless --path . --fixed-fps 60 -s tools/e2e_options.gd
# With godot on PATH, scons also re-exports RTS0.x86_64 after a build unless given skip_export=1.
set -euo pipefail

VERSION="${GODOT_RELEASE:-4.3-stable}"
DEST="${GODOT_DEST:-/usr/local/bin/godot}"
BASE="https://github.com/godotengine/godot/releases/download/${VERSION}"
ZIP="Godot_v${VERSION}_linux.x86_64.zip"
WANT="${VERSION/-/.}" # 4.3-stable -> 4.3.stable, the start of `godot --version`

cd "$(dirname "$0")/.."

if [ -x "$DEST" ] && "$DEST" --version 2>/dev/null | grep -q "^${WANT}\."; then
	echo "Godot ${VERSION} already at ${DEST}"
else
	tmp="$(mktemp -d)"
	trap 'rm -rf "$tmp"' EXIT
	echo "Downloading ${BASE}/${ZIP}"
	curl -fsSL -o "$tmp/$ZIP" "$BASE/$ZIP"
	curl -fsSL -o "$tmp/SHA512-SUMS.txt" "$BASE/SHA512-SUMS.txt"
	(cd "$tmp" && grep " ${ZIP}\$" SHA512-SUMS.txt | sha512sum -c -)
	unzip -q -d "$tmp" "$tmp/$ZIP"
	mkdir -p "$(dirname "$DEST")"
	install -m 755 "$tmp/Godot_v${VERSION}_linux.x86_64" "$DEST"
	echo "Installed $("$DEST" --version) at ${DEST}"
fi

if [ "${1:-}" = "--import" ]; then
	python3 tools/godot_import.py --godot "$DEST"
fi
