#!/usr/bin/env bash
# setup_logging.sh — helper for RTS0 logging on Android logcat + desktop file + overlay
# Reference: 0 A.D. source/ps/Log.cpp style simplified for Godot FileAccess + GD.Print
# Usage: ./tools/setup_logging.sh [logcat|pull|tail|all|help]
set -euo pipefail

# Exact commands required by spec (also implemented below):
#   adb logcat -c; adb logcat godot:V Rts0:V *:S
#   adb pull /data/data/org.godotengine.rts0/files/logs
#   tail -f ~/.local/share/godot/app_userdata/RTS0/logs/*
PKG="org.godotengine.rts0"
# Android internal files dir: /data/data/<pkg>/files → user:// maps there
ANDROID_LOGS="/data/data/${PKG}/files/logs"
# Desktop Godot user data (XDG): ~/.local/share/godot/app_userdata/<AppName>/logs
DESKTOP_LOGS_LINUX="${HOME}/.local/share/godot/app_userdata/RTS0/logs"
DESKTOP_LOGS_MAC="${HOME}/Library/Application Support/Godot/app_userdata/RTS0/logs"
DESKTOP_LOGS_WIN="" # not used on bash

usage() {
  cat <<EOF
RTS0 logging helper — Android logcat + desktop file tail

Usage: $0 [command]
  logcat   - clear + stream filtered logcat (godot:V Rts0:V *:S)
  pull     - pull Android logs via adb to ./logs_android/
  tail     - tail desktop logs (Linux/macOS)
  all      - logcat + tail hint
  help     - this help

Examples:
  $0 logcat
    adb logcat -c; adb logcat godot:V Rts0:V *:S

  $0 pull
    adb pull ${ANDROID_LOGS} ./logs_android

  $0 tail
    tail -f ${DESKTOP_LOGS_LINUX}/*

Overlay: in-game ring buffer via Logger.GetRecent() — draw with Label/RichTextLabel.
File rotation: user://logs/rts0.log (512KB) → rts0.log.1 — same on Android/desktop.
Crash dumps: user://logs/crash_*.log — pulled via '$0 pull'
EOF
}

cmd_logcat() {
  echo "[RTS0] clearing logcat..."
  adb logcat -c || echo "[warn] adb logcat -c failed (no device?)"
  echo "[RTS0] streaming: adb logcat godot:V Rts0:V *:S (Ctrl+C to stop)"
  adb logcat godot:V Rts0:V *:S
}

cmd_pull() {
  local out="./logs_android"
  mkdir -p "${out}"
  echo "[RTS0] pulling ${ANDROID_LOGS} -> ${out}/"
  if adb pull "${ANDROID_LOGS}" "${out}/"; then
    echo "[RTS0] pulled to ${out}/"
    ls -lh "${out}/" || true
    # also try alternative path for Godot 4.3 export variations
    adb pull "/data/data/${PKG}/files/" "${out}/" 2>/dev/null || true
  else
    echo "[error] adb pull failed — is device connected? try: adb devices"
    echo "        ensure app ran at least once so user://logs exists"
    return 1
  fi
}

cmd_tail() {
  local dir=""
  if [[ -d "${DESKTOP_LOGS_LINUX}" ]]; then dir="${DESKTOP_LOGS_LINUX}"
  elif [[ -d "${DESKTOP_LOGS_MAC}" ]]; then dir="${DESKTOP_LOGS_MAC}"
  else
    echo "[RTS0] no desktop logs found, checking common locations:"
    echo "  ${DESKTOP_LOGS_LINUX}"
    echo "  ${DESKTOP_LOGS_MAC}"
    echo "  trying: find ~ -path '*godot*/*RTS0/logs*' 2>/dev/null | head"
    find "${HOME}" -path "*godot*/*RTS0/logs*" -type d 2>/dev/null | head -n 5 || true
    echo "[hint] run the game once to create user://logs/rts0.log"
    return 1
  fi
  echo "[RTS0] tailing ${dir}/* (Ctrl+C to stop)"
  # shellcheck disable=SC2086
  tail -f "${dir}"/* 2>/dev/null || tail -F "${dir}"/* || cat "${dir}"/*
}

case "${1:-all}" in
  logcat) cmd_logcat ;;
  pull)   cmd_pull ;;
  tail)   cmd_tail ;;
  all)
    echo "[RTS0] hint: run in two terminals:"
    echo "  term1: $0 logcat"
    echo "  term2: $0 tail  (or $0 pull after crash)"
    echo ""
    echo "Streaming logcat now... (Ctrl+C to stop)"
    cmd_logcat
    ;;
  help|-h|--help) usage ;;
  *) echo "unknown command: $1"; usage; exit 1 ;;
esac
