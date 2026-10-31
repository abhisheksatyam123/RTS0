#!/usr/bin/env python3
"""RTS0 Realistic Large-Scale Performance and Diagnostic Testing Runner.

Launches Godot with Vulkan Forward+ under realistic large-scale clash and skirmish scenarios.
Concurrently monitors per-core CPU utilization, GPU frequency, thread states, and memory.
Captures all engine warnings, errors, bottlenecks, and outputs a diagnostic breakdown.
"""

import os
import sys
import time
import subprocess
import json
import threading
import re

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def read_proc_stat():
    cpus = {}
    with open('/proc/stat') as f:
        for l in f:
            parts = l.split()
            if parts[0].startswith('cpu'):
                vals = [int(x) for x in parts[1:8]]
                idle = vals[3] + vals[4]
                total = sum(vals)
                cpus[parts[0]] = (idle, total)
    return cpus

def calc_cpu_util(c0, c1):
    utils = {}
    for k in sorted(c0.keys()):
        i0, t0 = c0[k]
        i1, t1 = c1.get(k, (i0, t0))
        dt = t1 - t0
        di = i1 - i0
        utils[k] = round(100.0 * (1.0 - (di / dt if dt > 0 else 0)), 1)
    return utils

def read_gpu():
    res = {}
    for f in ['gt_act_freq_mhz', 'gt_cur_freq_mhz', 'gt_RP0_freq_mhz', 'gt_RP1_freq_mhz', 'gt_min_freq_mhz', 'gt_max_freq_mhz']:
        p = f'/sys/class/drm/card1/{f}'
        if os.path.exists(p):
            try:
                with open(p) as fp:
                    res[f] = int(fp.read().strip())
            except Exception:
                pass
    return res

class SysMonitor(threading.Thread):
    def __init__(self, pid_getter, interval=0.05):
        super().__init__()
        self.pid_getter = pid_getter
        self.interval = interval
        self.running = True
        self.cpu_samples = []
        self.gpu_samples = []
        self.thread_counts = []
        self.rss_samples = []

    def run(self):
        last_stat = read_proc_stat()
        while self.running:
            time.sleep(self.interval)
            cur_stat = read_proc_stat()
            self.cpu_samples.append(calc_cpu_util(last_stat, cur_stat))
            last_stat = cur_stat
            g = read_gpu()
            if g:
                self.gpu_samples.append(g)

            pid = self.pid_getter()
            if pid:
                task_dir = f'/proc/{pid}/task'
                if os.path.exists(task_dir):
                    try:
                        self.thread_counts.append(len(os.listdir(task_dir)))
                    except Exception:
                        pass
                statm_file = f'/proc/{pid}/statm'
                if os.path.exists(statm_file):
                    try:
                        with open(statm_file) as fp:
                            # pages: size, resident, shared... (4KB page)
                            rss_pages = int(fp.read().split()[1])
                            self.rss_samples.append(round(rss_pages * 4 / 1024.0, 1))
                    except Exception:
                        pass

    def stop(self):
        self.running = False

def run_test(units=1500, frames=200, mode="clash", camera="dynamic"):
    print(f"\n>>> Launching Realistic Performance Test: mode={mode}, units={units}, frames={frames}, camera={camera}")
    cmd = [
        "godot",
        "--rendering-driver", "vulkan",
        "--rendering-method", "forward_plus",
        "-s", "tools/bench_realistic_game.gd",
        "--",
        "--units", str(units),
        "--frames", str(frames),
        "--mode", mode,
        "--camera", camera,
        "--warmup", "30",
        "--vsync", "0"
    ]
    env = os.environ.copy()
    if "DISPLAY" not in env or not env["DISPLAY"]:
        env["DISPLAY"] = ":0"

    current_pid = [None]
    mon = SysMonitor(lambda: current_pid[0], interval=0.04)

    c0 = read_proc_stat()
    t_start = time.time()

    proc = subprocess.Popen(
        cmd,
        cwd=REPO_ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True
    )
    current_pid[0] = proc.pid
    mon.start()

    stdout, _ = proc.communicate()
    t_end = time.time()
    mon.stop()
    mon.join()

    c1 = read_proc_stat()
    total_cpu = calc_cpu_util(c0, c1)

    # Parse stdout for JSON metrics and errors/warnings
    perf_metrics = {}
    json_match = re.search(r"PERF_JSON_METRICS:\s*(\{.*\})", stdout)
    if json_match:
        try:
            perf_metrics = json.loads(json_match.group(1))
        except Exception as e:
            print("Failed to parse json metrics:", e)

    # Extract errors and warnings
    errors = []
    warnings = []
    leaks = []
    for line in stdout.splitlines():
        if "ERROR:" in line:
            errors.append(line.strip())
        elif "WARNING:" in line:
            warnings.append(line.strip())
        elif "leak" in line.lower():
            leaks.append(line.strip())

    report = {
        "scenario": mode,
        "units": units,
        "frames_requested": frames,
        "duration_sec": round(t_end - t_start, 2),
        "exit_code": proc.returncode,
        "metrics": perf_metrics,
        "system_cpu_overall": total_cpu,
        "peak_thread_count": max(mon.thread_counts) if mon.thread_counts else 0,
        "peak_rss_mb": max(mon.rss_samples) if mon.rss_samples else 0,
        "errors": errors,
        "warnings": warnings,
        "leaks": leaks,
        "raw_stdout": stdout
    }

    return report

def main():
    print("===============================================================")
    print("  RTS0 REALISTIC LARGE-SCALE SCENARIO PROFILER & AUDIT")
    print("===============================================================")

    # Test 1: Realistic Large-Scale Clash (1,500 Units, Dynamic Camera)
    clash_1500 = run_test(units=1500, frames=240, mode="clash", camera="dynamic")

    # Test 2: Full Skirmish Match (Petra AI Expansion & Economy)
    skirmish = run_test(units=400, frames=180, mode="skirmish", camera="dynamic")

    # Combine results
    all_results = {
        "clash_1500": clash_1500,
        "skirmish": skirmish
    }

    with open("scratchpad/realistic_perf_summary.json", "w") as fp:
        json.dump(all_results, fp, indent=2)

    print("\n\n===============================================================")
    print("  EXECUTIVE SUMMARY & PERFORMANCE FINDINGS")
    print("===============================================================")

    for name, res in all_results.items():
        m = res.get("metrics", {})
        print(f"\n--- Scenario: {name} (Exit Code: {res['exit_code']}) ---")
        if m:
            print(f"  Units:                {m.get('units')}")
            print(f"  FPS:                  {m.get('fps'):.1f} fps")
            print(f"  Frame Time (Mean):    {m.get('frame_mean_ms'):.2f} ms (p95: {m.get('frame_p95_ms'):.2f} ms, max: {m.get('frame_max_ms'):.2f} ms)")
            print(f"  Sim Physics Time:     {m.get('sim_mean_ms'):.2f} ms (p95: {m.get('sim_p95_ms'):.2f} ms)")
            print(f"  Draw Calls / Prims:   {m.get('draw_calls_mean'):.0f} draws | {m.get('primitives_mean'):.0f} prims")
            print(f"  GPU Active Clock:     {m.get('gpu_act_freq_mhz'):.0f} MHz")
        print(f"  Peak RSS:             {res.get('peak_rss_mb')} MB")
        print(f"  Peak Threads:         {res.get('peak_thread_count')}")
        print(f"  Errors Detected:      {len(res.get('errors', []))}")
        print(f"  Warnings Detected:    {len(res.get('warnings', []))}")
        print(f"  Leaks Detected:       {len(res.get('leaks', []))}")

    print("\nSummary written to scratchpad/realistic_perf_summary.json")

if __name__ == '__main__':
    main()
