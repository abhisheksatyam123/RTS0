#!/usr/bin/env bash
# Wrapper — delegates to cross-platform Python driver (Vulkan: forward_plus).
# Usage: ./tools/build.sh [linux|windows|android|macos|ios|all ...] [--package]
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$DIR/build.py" "$@"
