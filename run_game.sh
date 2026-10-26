#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

mkdir -p logs
LOG_FILE="logs/game_$(date +%Y%m%d_%H%M%S).log"
LATEST_LINK="logs/latest.log"

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export DISPLAY="${DISPLAY:-:0}"

if [ -z "$XAUTHORITY" ]; then
    AUTH_FILE=$(find "$XDG_RUNTIME_DIR" -name ".mutter-Xwaylandauth.*" 2>/dev/null | head -n 1)
    if [ -n "$AUTH_FILE" ]; then
        export XAUTHORITY="$AUTH_FILE"
    fi
fi

# Use X11 display driver by default on Linux/Xwayland to ensure hardware mouse cursors & custom cursor textures render reliably.
DRIVER="x11"
for arg in "$@"; do
    if [[ "$arg" == "--display-driver" ]]; then
        DRIVER=""
        break
    fi
done

DRIVER_ARG=()
if [ -n "$DRIVER" ]; then
    DRIVER_ARG=(--display-driver "$DRIVER")
fi

echo "=== Starting RTS0 Game $(date) ===" | tee "$LOG_FILE"
echo "DISPLAY=$DISPLAY, XAUTHORITY=$XAUTHORITY, DRIVER=${DRIVER:-default}" | tee -a "$LOG_FILE"
ln -sf "$(basename "$LOG_FILE")" "$LATEST_LINK"

echo "Executing: /home/abhi/.local/bin/godot ${DRIVER_ARG[@]} $@" | tee -a "$LOG_FILE"
exec /home/abhi/.local/bin/godot "${DRIVER_ARG[@]}" "$@" >> "$LOG_FILE" 2>&1
