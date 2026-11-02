#!/usr/bin/env python3
import os
import sys
import time
import subprocess
import json
import threading

def read_proc_stat():
    with open('/proc/stat') as f:
        lines = f.readlines()
    cpus = {}
    for l in lines:
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
        utils[k] = 100.0 * (1.0 - (di / dt if dt > 0 else 0))
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

class Monitor(threading.Thread):
    def __init__(self, pid, interval=0.05):
        super().__init__()
        self.pid = pid
        self.interval = interval
        self.running = True
        self.cpu_samples = []
        self.gpu_samples = []
        self.thread_samples = []

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

            # Sample threads of process
            task_dir = f'/proc/{self.pid}/task'
            if os.path.exists(task_dir):
                try:
                    tids = os.listdir(task_dir)
                    self.thread_samples.append(len(tids))
                except Exception:
                    pass

    def stop(self):
        self.running = False

def run_bench(scenario, ticks, workers=None, serial=False):
    cmd = ['./bin/bench_sim', '--scenario', str(scenario), '--ticks', str(ticks)]
    if serial:
        cmd.append('--serial')
    elif workers is not None:
        cmd += ['--workers', str(workers)]

    c0 = read_proc_stat()
    g0 = read_gpu()
    t_start = time.time()

    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    mon = Monitor(p.pid, interval=0.02)
    mon.start()

    stdout, stderr = p.communicate()
    t_end = time.time()
    mon.stop()
    mon.join()

    c1 = read_proc_stat()
    g1 = read_gpu()
    total_cpu = calc_cpu_util(c0, c1)

    # Parse stdout
    lines = stdout.strip().splitlines()
    bench_line = ""
    for l in lines:
        if any(sc in l for sc in ['small', 'medium', 'large', 'custom']):
            bench_line = l
            break

    data = {
        "scenario": scenario,
        "ticks": ticks,
        "workers": workers,
        "serial": serial,
        "elapsed_sec": t_end - t_start,
        "total_cpu_util": total_cpu,
        "gpu_before": g0,
        "gpu_after": g1,
        "gpu_samples": mon.gpu_samples,
        "thread_count_max": max(mon.thread_samples) if mon.thread_samples else 1,
        "raw_bench_line": bench_line,
        "stdout": stdout,
        "stderr": stderr
    }

    # Extract parsed fields if possible
    parts = bench_line.split()
    if len(parts) >= 7:
        try:
            data["name"] = parts[0]
            data["units"] = parts[1]
            data["ticks_run"] = int(parts[2])
            data["mean_ms"] = float(parts[3])
            data["p95_ms"] = float(parts[4])
            data["max_ms"] = float(parts[5])
            data["total_ms"] = float(parts[6])
        except Exception as e:
            data["parse_error"] = str(e)

    return data

def main():
    print("=== RTS0 CPU/GPU UTILIZATION AND BENCHMARK SUITE ===")
    results = {}

    # 1. Thread Scaling Benchmark on 3000 units
    print("\n--- 1. Thread Scaling Sweep (Scenario 3000 units, 100 ticks) ---")
    thread_configs = [
        ("1_thread_serial", True, None),
        ("2_threads_1w", False, 1),
        ("4_threads_3w", False, 3),
        ("8_threads_7w", False, 7),
    ]
    results["thread_scaling_3000"] = []
    for name, serial, w in thread_configs:
        print(f"Running config: {name}...")
        res = run_bench(3000, 100, workers=w, serial=serial)
        mean_ms = res.get("mean_ms", 0)
        cpu = res["total_cpu_util"].get("cpu", 0)
        print(f"  Result: {mean_ms:.2f} ms/tick, CPU: {cpu:.1f}%, Workers: {w}, Max Threads: {res['thread_count_max']}")
        results["thread_scaling_3000"].append(res)

    # 2. Thread Scaling Benchmark on 5000 units
    print("\n--- 2. Thread Scaling Sweep (Scenario 5000 units, 50 ticks) ---")
    results["thread_scaling_5000"] = []
    for name, serial, w in thread_configs:
        print(f"Running config: {name}...")
        res = run_bench(5000, 50, workers=w, serial=serial)
        mean_ms = res.get("mean_ms", 0)
        cpu = res["total_cpu_util"].get("cpu", 0)
        print(f"  Result: {mean_ms:.2f} ms/tick, CPU: {cpu:.1f}%, Workers: {w}, Max Threads: {res['thread_count_max']}")
        results["thread_scaling_5000"].append(res)

    # 3. Unit Count Scaling Sweep with 8 threads (default pool)
    print("\n--- 3. Unit Count Scaling (150 -> 7600 units, 8 threads) ---")
    scenarios = [
        ("small", 150, 100),
        ("medium", 600, 100),
        ("large", 1500, 100),
        ("custom_3000", 3000, 100),
        ("custom_5000", 5000, 50),
        ("custom_7600", 7600, 30),
    ]
    results["unit_scaling"] = []
    for tag, n_units, ticks in scenarios:
        print(f"Running scenario {tag} ({n_units} units, {ticks} ticks)...")
        res = run_bench(n_units if "custom" in tag else tag, ticks, workers=7)
        mean_ms = res.get("mean_ms", 0)
        cpu = res["total_cpu_util"].get("cpu", 0)
        per_core = {k: f"{v:.1f}%" for k, v in res["total_cpu_util"].items() if k != "cpu"}
        print(f"  Result: {mean_ms:.2f} ms/tick, Total CPU: {cpu:.1f}%")
        print(f"  Per-Core CPU: {per_core}")
        results["unit_scaling"].append(res)

    # Save to JSON
    with open("build/perf_results.json", "w") as f:
        json.dump(results, f, indent=2)
    print("\nBenchmark results saved to build/perf_results.json")

if __name__ == "__main__":
    main()
