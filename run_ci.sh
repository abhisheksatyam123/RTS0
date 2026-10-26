#!/usr/bin/env bash
# Headless CI runner: parses headless-tests steps from .github/workflows/ci.yml and executes them.
set -eo pipefail

LOG_FILE="${1:-/tmp/ci_run.log}"
echo "Running headless CI suite from .github/workflows/ci.yml (logging to ${LOG_FILE})..."
> "${LOG_FILE}"

python3 -c '
import re, subprocess, sys

with open(".github/workflows/ci.yml") as f:
    lines = f.readlines()

in_job = False
steps = []
current_name = None
current_cmd = []

for line in lines:
    if line.startswith("  headless-tests:"):
        in_job = True
        continue
    if in_job:
        if line.startswith("  export-desktop:") or (line.startswith("  ") and not line.startswith("   ") and not line.startswith("    ")):
            break
        if "- name:" in line:
            if current_name and current_cmd:
                steps.append((current_name, "\n".join(current_cmd)))
            current_name = line.split("- name:")[1].strip()
            current_cmd = []
        elif current_name is not None:
            stripped = line.strip()
            if stripped.startswith("run:"):
                pass
            elif line.startswith("          ") or line.startswith("        "):
                if stripped:
                    current_cmd.append(line.rstrip())

if current_name and current_cmd:
    steps.append((current_name, "\n".join(current_cmd)))

log_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ci_run.log"
total = len(steps)
print(f"Executing {total} CI steps...")

passed = 0
failed = 0

with open(log_path, "w") as log:
    for idx, (name, cmd) in enumerate(steps, 1):
        step_header = f"@@@ STEP: {name}"
        print(f"[{idx}/{total}] {name}...", flush=True)
        log.write(step_header + "\n")
        log.flush()

        # Run the command block in bash
        res = subprocess.run(["bash", "-eo", "pipefail", "-c", cmd], capture_output=True, text=True)
        if res.returncode == 0:
            log.write(f"@@@ OK: {name}\n")
            log.flush()
            passed += 1
        else:
            log.write(f"@@@ FAIL: {name}\n")
            log.write(res.stdout + "\n" + res.stderr + "\n")
            log.flush()
            print(f"FAILED: {name}")
            print(res.stdout)
            print(res.stderr)
            failed += 1
            sys.exit(1)

print(f"All {passed}/{total} CI steps passed successfully!")
' "${LOG_FILE}"
