#!/usr/bin/env python3
"""
track_progress.py - LIVE signals for migration progress.
Combines check_parity.py overall % and checklist counts.
"""

import subprocess
import re
import sys
from pathlib import Path

def run_cmd(cmd):
    try:
        res = subprocess.run(cmd, shell=True, check=True, capture_output=True, text=True)
        return res.stdout.strip()
    except subprocess.CalledProcessError as e:
        return f"[Error running {cmd}: {e.stderr.strip()}]"
    except Exception as e:
        return f"[Error: {e}]"

def parse_parity_pct():
    out = run_cmd("python3 tools/check_parity.py")
    if "[Error" in out:
        return "N/A"
    
    match = re.search(r"Overall parity \(avg G1-G10\):\s*(\d+)%", out)
    if match:
        return match.group(1) + "%"
    return "N/A"

def count_checkboxes(file_path):
    p = Path(file_path)
    if not p.exists():
        return 0, 0
    
    text = p.read_text(errors='ignore')
    checked = len(re.findall(r"\[x\]", text, re.IGNORECASE))
    unchecked = len(re.findall(r"\[ \]", text))
    return checked, checked + unchecked

def main():
    print("==============================================================================")
    print("RTS0 Migration Progress - LIVE STATUS")
    print("==============================================================================")
    
    # Git stats
    last_commit = run_cmd("git log -1 --oneline")
    git_status = run_cmd("git status --porcelain")
    uncommitted = len(git_status.splitlines()) if git_status and not git_status.startswith("[Error") else 0
    
    print(f"HEAD: {last_commit}")
    print(f"Uncommitted files: {uncommitted}\n")
    
    # 1. Headline Number (check_parity)
    parity_pct = parse_parity_pct()
    print(f"WIRING/PARITY CHECKER (Overall): {parity_pct}")
    print("-" * 78)
    
    # 2. Checkbox counts
    files = ["docs/REMAINING_GAPS.md", "docs/UI_PARITY.md"]
    total_checked = 0
    total_boxes = 0
    
    for f in files:
        c, t = count_checkboxes(f)
        total_checked += c
        total_boxes += t
        pct = (c / t * 100) if t > 0 else 0
        print(f"{f:30} {c:3}/{t:<3} checkboxes ({pct:.1f}%)")
        
    total_pct = (total_checked / total_boxes * 100) if total_boxes > 0 else 0
    print("-" * 78)
    print(f"{'Total Checklist Completion':30} {total_checked:3}/{total_boxes:<3} checkboxes ({total_pct:.1f}%)")
    
    print("\nNote: Checklist completion indicates stubs/scaffolding ported.")
    print("      Wiring/Parity Checker % indicates true fidelity to 0 A.D. semantics.")
    print("==============================================================================")

if __name__ == "__main__":
    main()
