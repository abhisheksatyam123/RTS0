#!/usr/bin/env python3
"""RTS0 Realistic Large-Scale Performance and Diagnostic Testing Runner.

Launches Godot with Vulkan Forward+ under realistic large-scale clash and skirmish scenarios.
Defaults to a deterministic fixed camera profile ("clash") and takes the median of N runs
to eliminate camera-path variance. Concurrently monitors per-core CPU utilization, GPU
frequency, thread states, and memory, and parses all exit RID/Shader/ObjectDB leak patterns.
"""

import argparse
import os
import sys
import time
import statistics
import subprocess
import json
import threading
import re

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LEAK_PATTERNS = (
    "leak",
    "rid allocations",
    "never freed",
    "resources still in use",
    "objectdb instances leaked",
    'parameter "renderingserver::get_singleton()" is null',
)


def audit_assets():
    assets_dir = os.path.join(REPO_ROOT, "assets")
    glb_count = 0
    png_count = 0
    total_bytes = 0
    if os.path.isdir(assets_dir):
        for root, _dirs, files in os.walk(assets_dir):
            for fname in files:
                low = fname.lower()
                if low.endswith(".glb"):
                    glb_count += 1
                elif low.endswith(".png"):
                    png_count += 1
                try:
                    total_bytes += os.path.getsize(os.path.join(root, fname))
                except OSError:
                    pass
    return {
        "glb_count": glb_count,
        "png_count": png_count,
        "size_mb": round(total_bytes / (1024.0 * 1024.0), 1),
    }


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
                            rss_pages = int(fp.read().split()[1])
                            self.rss_samples.append(round(rss_pages * 4 / 1024.0, 1))
                    except Exception:
                        pass

    def stop(self):
        self.running = False


def is_leak_line(line):
    low = line.lower()
    return any(pat in low for pat in LEAK_PATTERNS)


def run_single(units=1500, frames=180, mode="clash", camera="clash", run_idx=1, total_runs=1):
    print(f"\n>>> Launching Run {run_idx}/{total_runs}: mode={mode}, units={units}, frames={frames}, camera={camera}")
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

    perf_metrics = {}
    json_match = re.search(r"PERF_JSON_METRICS:\s*(\{.*\})", stdout)
    if json_match:
        try:
            perf_metrics = json.loads(json_match.group(1))
        except Exception as e:
            print("Failed to parse json metrics:", e)

    # Extract errors, warnings, and all exit leak signatures independently
    errors = []
    warnings = []
    leaks = []
    for line in stdout.splitlines():
        s = line.strip()
        if not s:
            continue
        if "ERROR:" in s:
            errors.append(s)
        elif "WARNING:" in s:
            warnings.append(s)
        if is_leak_line(s):
            leaks.append(s)

    return {
        "scenario": mode,
        "camera": camera,
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
        "raw_stdout": stdout,
    }


def run_test(units=1500, frames=180, mode="clash", camera="clash", runs=3):
    runs = max(1, int(runs))
    samples = [
        run_single(units=units, frames=frames, mode=mode, camera=camera, run_idx=i + 1, total_runs=runs)
        for i in range(runs)
    ]
    valid_metrics = [s["metrics"] for s in samples if s.get("metrics")]
    agg_metrics = {}
    if valid_metrics:
        agg_metrics = dict(valid_metrics[0])
        numeric_keys = [
            "fps", "frame_mean_ms", "frame_p50_ms", "frame_p95_ms", "frame_p99_ms",
            "frame_max_ms", "sim_mean_ms", "sim_p95_ms", "sim_max_ms",
            "draw_calls_mean", "draw_calls_p95", "primitives_mean",
            "gpu_act_freq_mhz", "gpu_cur_freq_mhz", "mem_static_mb",
        ]
        for k in numeric_keys:
            vals = [float(m[k]) for m in valid_metrics if k in m and m[k] is not None]
            if vals:
                agg_metrics[k] = round(statistics.median(vals), 2)
        agg_metrics["fps_runs"] = [round(float(m.get("fps", 0.0)), 2) for m in valid_metrics]

    all_errors = []
    all_warnings = []
    all_leaks = []
    for s in samples:
        for e in s.get("errors", []):
            if e not in all_errors:
                all_errors.append(e)
        for w in s.get("warnings", []):
            if w not in all_warnings:
                all_warnings.append(w)
        for lk in s.get("leaks", []):
            if lk not in all_leaks:
                all_leaks.append(lk)

    return {
        "scenario": mode,
        "camera": camera,
        "units": units,
        "frames_requested": frames,
        "runs": runs,
        "duration_sec": round(sum(s["duration_sec"] for s in samples), 2),
        "exit_code": max((s["exit_code"] for s in samples), default=0),
        "metrics": agg_metrics,
        "system_cpu_overall": samples[-1]["system_cpu_overall"] if samples else {},
        "peak_thread_count": max((s["peak_thread_count"] for s in samples), default=0),
        "peak_rss_mb": max((s["peak_rss_mb"] for s in samples), default=0),
        "errors": all_errors,
        "warnings": all_warnings,
        "leaks": all_leaks,
        "run_samples": [
            {
                "run": i + 1,
                "exit_code": s["exit_code"],
                "duration_sec": s["duration_sec"],
                "metrics": s["metrics"],
                "leaks": s["leaks"],
            }
            for i, s in enumerate(samples)
        ],
    }


def main():
    parser = argparse.ArgumentParser(description="RTS0 Realistic Performance & Leak Audit Runner")
    parser.add_argument("--camera", default="clash", choices=["clash", "base", "high", "dynamic"],
                        help="Camera profile (default: clash, fixed & deterministic)")
    parser.add_argument("--runs", type=int, default=3, help="Number of runs per scenario for median aggregation")
    parser.add_argument("--units", type=int, default=1500, help="Unit count for clash scenario")
    parser.add_argument("--frames", type=int, default=180, help="Frames per run")
    parser.add_argument("--mode", default="both", choices=["both", "clash", "skirmish"], help="Scenario mode")
    args = parser.parse_args()

    print("===============================================================")
    print("  RTS0 REALISTIC LARGE-SCALE SCENARIO PROFILER & AUDIT")
    print("===============================================================")
    asset_info = audit_assets()
    print(f"Assets Check: {asset_info['glb_count']} .glb models, {asset_info['png_count']} .png textures ({asset_info['size_mb']} MB)")
    if asset_info["png_count"] < 1000:
        print("WARNING: Sparse .png texture count detected (<1000). Ensure gitignored textures are present before comparing A/B runs across worktrees.")

    all_results = {"assets": asset_info}
    if args.mode in ("both", "clash"):
        all_results["clash_1500"] = run_test(
            units=args.units, frames=args.frames, mode="clash", camera=args.camera, runs=args.runs
        )
    if args.mode in ("both", "skirmish"):
        all_results["skirmish"] = run_test(
            units=400, frames=args.frames, mode="skirmish", camera=args.camera, runs=args.runs
        )

    os.makedirs(os.path.join(REPO_ROOT, "scratchpad"), exist_ok=True)
    with open(os.path.join(REPO_ROOT, "scratchpad/realistic_perf_summary.json"), "w") as fp:
        json.dump(all_results, fp, indent=2)

    print("\n\n===============================================================")
    print("  EXECUTIVE SUMMARY & PERFORMANCE FINDINGS (MEDIAN OF RUNS)")
    print("===============================================================")

    for name, res in all_results.items():
        if name == "assets":
            continue
        m = res.get("metrics", {})
        print(f"\n--- Scenario: {name} (Runs: {res.get('runs', 1)}, Camera: {res.get('camera')}, Exit Code: {res['exit_code']}) ---")
        if m:
            print(f"  Units:                {m.get('units')}")
            print(f"  Median FPS:           {m.get('fps'):.1f} fps (runs: {m.get('fps_runs')})")
            print(f"  Frame Time (Median):  {m.get('frame_mean_ms'):.2f} ms (p95: {m.get('frame_p95_ms'):.2f} ms, max: {m.get('frame_max_ms'):.2f} ms)")
            print(f"  Sim Physics Time:     {m.get('sim_mean_ms'):.2f} ms (p95: {m.get('sim_p95_ms'):.2f} ms)")
            print(f"  Draw Calls / Prims:   {m.get('draw_calls_mean'):.0f} draws | {m.get('primitives_mean'):.0f} prims")
            print(f"  GPU Active Clock:     {m.get('gpu_act_freq_mhz'):.0f} MHz")
        print(f"  Peak RSS:             {res.get('peak_rss_mb')} MB")
        print(f"  Peak Threads:         {res.get('peak_thread_count')}")
        print(f"  Errors Detected:      {len(res.get('errors', []))}")
        print(f"  Warnings Detected:    {len(res.get('warnings', []))}")
        print(f"  Leaks Detected:       {len(res.get('leaks', []))}")
        for lk in res.get("leaks", []):
            print(f"    [LEAK] {lk}")

    print("\nSummary written to scratchpad/realistic_perf_summary.json")


if __name__ == '__main__':
    main()
