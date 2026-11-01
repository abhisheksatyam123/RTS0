#!/usr/bin/env python3
"""
Test RTS0 on connected Android device across scenarios:
  1. Frontline Clash (1,500 units stress test, crowd LODs, touch responsiveness)
  2. Skirmish Demo (Base building, economy, unit production)
  3. Coastal / Naval (Water rendering, ship combat)
  4. Fortress / Siege (Wall pathfinding, projectile culling, destruction)

Usage:
  python3 tools/test_android_scenarios.py [--serial <serial>] [--apk <path>] [--scenarios <list>]
"""

import argparse
import os
import subprocess
import sys
import time

ADB = "/home/abhi/miniconda3/envs/0ad/bin/adb"
DEFAULT_APK = "build/android/RTS0.apk"
PACKAGE = "com.abhisheksatyam123.rts0"
ACTIVITY = "com.godot.game.GodotApp"

SCENARIOS = [
    {
        "name": "frontline_clash",
        "title": "Frontline Clash (1,500 units Pitch Battle)",
        "duration": 20,
        "map": "frontline_clash",
        "actions": [
            ("tap", 640, 360, "Center tap (select/order)"),
            ("swipe", 400, 300, 800, 300, 200, "1-finger pan right"),
            ("swipe", 600, 500, 600, 200, 200, "1-finger pan down"),
            ("tap", 700, 360, "Troop attack order tap"),
        ]
    },
    {
        "name": "skirmish_demo",
        "title": "Skirmish Demo (Economy & Base Building)",
        "duration": 20,
        "map": "skirmish_demo",
        "actions": [
            ("tap", 640, 360, "Select worker"),
            ("tap", 720, 340, "Gather resource tap"),
            ("swipe", 500, 300, 700, 300, 200, "Camera pan across base"),
        ]
    },
    {
        "name": "cycladic_archipelago",
        "title": "Cycladic Archipelago (Naval & Coastal Water)",
        "duration": 15,
        "map": "cycladic_archipelago",
        "actions": [
            ("swipe", 400, 400, 800, 400, 300, "Water camera pan"),
        ]
    }
]

def run_cmd(cmd, check=True, capture=True):
    if capture:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    else:
        res = subprocess.run(cmd)
    if check and res.returncode != 0:
        err = res.stderr.strip() if capture else f"code {res.returncode}"
        raise RuntimeError(f"Command failed ({cmd}): {err}")
    return res

def get_connected_devices():
    res = run_cmd([ADB, "devices", "-l"])
    lines = res.stdout.strip().split("\n")[1:]
    devices = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        serial = parts[0]
        state = parts[1]
        extra = " ".join(parts[2:])
        devices.append((serial, state, extra))
    return devices

def wait_for_device(serial=None, timeout=10):
    start = time.time()
    while time.time() - start < timeout:
        devices = get_connected_devices()
        ready = [d for d in devices if d[1] == "device"]
        if ready:
            if serial:
                for d in ready:
                    if d[0] == serial:
                        return d[0]
            else:
                return ready[0][0]
        time.sleep(1)
    return None

def install_apk(serial, apk_path):
    print(f"[*] Installing {apk_path} onto device {serial}...")
    run_cmd([ADB, "-s", serial, "install", "-r", "-d", apk_path], check=True, capture=False)
    print("[+] APK installed successfully.")

def get_gfxinfo(serial):
    res = run_cmd([ADB, "-s", serial, "shell", "dumpsys", "gfxinfo", PACKAGE])
    return res.stdout

def parse_gfxinfo(raw):
    # Parse total frames rendered and janky frames
    janky = None
    total = None
    percent = None
    for line in raw.split("\n"):
        line = line.strip()
        if "Total frames rendered:" in line:
            total = int(line.split(":")[-1].strip())
        elif "Janky frames:" in line:
            parts = line.split(":")[-1].strip().split()
            janky = int(parts[0])
            if "(" in line and "%" in line:
                percent = line.split("(")[1].split("%")[0].strip()
    return total, janky, percent

def test_scenario(serial, scenario, output_dir="android_test_results"):
    os.makedirs(output_dir, exist_ok=True)
    name = scenario["name"]
    title = scenario["title"]
    duration = scenario["duration"]
    map_name = scenario["map"]
    actions = scenario.get("actions", [])

    print(f"\n{'='*70}\n[SCENARIO] {title} (map={map_name}, duration={duration}s)\n{'='*70}")

    # Force-stop app and reset gfxinfo
    run_cmd([ADB, "-s", serial, "shell", "am", "force-stop", PACKAGE])
    run_cmd([ADB, "-s", serial, "shell", "dumpsys", "gfxinfo", PACKAGE, "reset"])
    run_cmd([ADB, "-s", serial, "shell", "logcat", "-c"])

    # Launch scenario via intent
    # Passing --es scenario <map_name>
    cmd = [
        ADB, "-s", serial, "shell", "am", "start",
        "-n", f"{PACKAGE}/{ACTIVITY}",
        "--es", "scenario", map_name
    ]
    print(f"[*] Launching: {' '.join(cmd)}")
    run_cmd(cmd)

    # Allow startup and initial rendering
    time.sleep(4)

    # Perform automated interaction actions
    for action in actions:
        atype = action[0]
        desc = action[-1]
        print(f"  -> Action: {desc}")
        if atype == "tap":
            _, x, y, _ = action
            run_cmd([ADB, "-s", serial, "shell", "input", "tap", str(x), str(y)])
        elif atype == "swipe":
            _, x1, y1, x2, y2, dur, _ = action
            run_cmd([ADB, "-s", serial, "shell", "input", "swipe", str(x1), str(y1), str(x2), str(y2), str(dur)])
        time.sleep(1.5)

    # Wait remaining duration
    remaining = max(0, duration - len(actions) * 1.5 - 4)
    if remaining > 0:
        time.sleep(remaining)

    # Take screenshot
    shot_path = os.path.join(output_dir, f"{name}.png")
    with open(shot_path, "wb") as f:
        res = subprocess.run([ADB, "-s", serial, "exec-out", "screencap", "-p"], stdout=f)
    print(f"[+] Captured screenshot: {shot_path}")

    # Query performance stats
    gfx = get_gfxinfo(serial)
    total, janky, percent = parse_gfxinfo(gfx)
    if total is not None:
        print(f"[PERF] Total frames: {total}, Janky frames: {janky} ({percent}%)")
    else:
        print("[PERF] Gfxinfo stats unavailable.")

    # Check for crashes in logcat
    log = run_cmd([ADB, "-s", serial, "shell", "logcat", "-d", "-s", "godot:*", "DEBUG:*", "CRASH:*"]).stdout
    crashes = [l for l in log.split("\n") if "FATAL" in l or "backtrace" in l.lower() or "SIGSEGV" in l]
    if crashes:
        print(f"[!] WARNING: Crash detected in logcat:\n" + "\n".join(crashes[:5]))
    else:
        print("[+] Zero crashes detected.")

    # Stop app
    run_cmd([ADB, "-s", serial, "shell", "am", "force-stop", PACKAGE])
    return {
        "scenario": name,
        "total_frames": total,
        "janky_frames": janky,
        "janky_percent": percent,
        "crashes": len(crashes),
        "screenshot": shot_path
    }

def main():
    parser = argparse.ArgumentParser(description="Test RTS0 on Android")
    parser.add_argument("--serial", help="ADB device serial (e.g. 192.168.1.2:40869)")
    parser.add_argument("--apk", default=DEFAULT_APK, help="Path to APK")
    parser.add_argument("--install", action="store_true", help="Force APK install")
    parser.add_argument("--pair", nargs=2, metavar=("IP:PORT", "CODE"), help="Pair wireless debugging: IP:PORT CODE")
    args = parser.parse_args()

    if args.pair:
        target, code = args.pair
        print(f"[*] Pairing {target} with code {code}...")
        res = run_cmd([ADB, "pair", target, code], check=False)
        print(res.stdout or res.stderr)
        return

    devices = get_connected_devices()
    print("[*] ADB devices status:")
    for d in devices:
        print(f"    - {d[0]} [{d[1]}] {d[2]}")
    if not devices:
        print("    (No devices currently attached)")

    serial = wait_for_device(args.serial, timeout=3)
    if not serial:
        print("\n[!] No authorized Android device found!")
        print("To connect your device:")
        print("  Option A (USB): Plug phone into computer with USB cable, enable USB debugging, and allow the prompt.")
        print("  Option B (Wi-Fi): Go to Settings -> Developer options -> Wireless debugging -> 'Pair device with pairing code'")
        print("         Then run: python3 tools/test_android_scenarios.py --pair 192.168.1.2:<pairing_port> <pairing_code>")
        sys.exit(1)

    print(f"\n[+] Using device: {serial}")
    if args.install or True:
        install_apk(serial, args.apk)

    results = []
    for sc in SCENARIOS:
        res = test_scenario(serial, sc)
        results.append(res)

    print("\n" + "="*70)
    print("ALL SCENARIO TESTS COMPLETE")
    print("="*70)
    for r in results:
        print(f"  * {r['scenario']}: {r['total_frames']} frames, jank: {r['janky_percent']}%, crashes: {r['crashes']}, screenshot: {r['screenshot']}")

if __name__ == "__main__":
    main()
