#!/usr/bin/env bash
# 0 A.D.-inspired debug launcher — mirrors 0ad's --debug wrappers
# Usage: ./tools/debug.sh [gdb|valgrind|strace|run] [--verbose]
set -euo pipefail
MODE="${1:-gdb}"
EXTRA="${2:-}"
BUILD="build/linux/RTS0.x86_64"
LOGDIR="$HOME/.local/share/godot/app_userdata/RTS0/logs"
mkdir -p "$LOGDIR"
echo "== RTS0 debug ($MODE) =="
echo "GPU: $(vulkaninfo --summary 2>/dev/null | grep -m1 "deviceName" || echo unknown)"
echo "Build: $BUILD $(ls -lh $BUILD 2>/dev/null | awk '{print $5,$9}')"
echo "Logs: $LOGDIR/rts0.log + crash_*.log"
case "$MODE" in
  gdb)
    echo "-> gdb --args $BUILD $EXTRA"
    gdb --args "$BUILD" $EXTRA
    ;;
  valgrind)
    echo "-> valgrind --leak-check=full $BUILD"
    valgrind --leak-check=full --track-origins=yes --log-file="$LOGDIR/valgrind.log" "$BUILD" $EXTRA
    echo "valgrind log: $LOGDIR/valgrind.log"
    ;;
  strace)
    strace -f -tt -o "$LOGDIR/strace.log" "$BUILD" $EXTRA
    echo "strace log: $LOGDIR/strace.log"
    ;;
  run)
    "$BUILD" $EXTRA 2>&1 | tee "$LOGDIR/run.log"
    ;;
  *)
    echo "unknown mode $MODE — use gdb|valgrind|strace|run"
    exit 1
    ;;
esac
