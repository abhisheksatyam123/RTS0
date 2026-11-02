#!/usr/bin/env python3
"""
Scan and automatically connect to Android Wireless Debugging on the local network.
"""

import argparse
import socket
import subprocess
import sys
import time

DEFAULT_ADB = "/home/abhi/miniconda3/envs/0ad/bin/adb"
DEFAULT_IP = "192.168.1.2"

def main():
    parser = argparse.ArgumentParser(description="Auto-detect and connect to Android Wireless Debugging")
    parser.add_argument("--ip", default=DEFAULT_IP, help=f"Target IP address (default: {DEFAULT_IP})")
    parser.add_argument("--adb", default=DEFAULT_ADB, help="Path to adb binary")
    parser.add_argument("--timeout", type=int, default=30, help="Search timeout in seconds (default: 30)")
    args = parser.parse_args()

    print(f"[*] Watching for Wireless Debugging port on {args.ip} (timeout {args.timeout}s)...")
    start_time = time.time()
    while time.time() - start_time < args.timeout:
        for p in range(30000, 50000, 2):
            s = socket.socket()
            s.settimeout(0.003)
            if s.connect_ex((args.ip, p)) == 0:
                print(f"[+] Found open port: {args.ip}:{p}")
                s.close()
                res = subprocess.run([args.adb, "connect", f"{args.ip}:{p}"], stdout=subprocess.PIPE, text=True)
                print(res.stdout.strip())
                return 0
            s.close()
        time.sleep(0.5)

    print("[-] Timeout waiting for port.")
    return 1

if __name__ == "__main__":
    sys.exit(main())
