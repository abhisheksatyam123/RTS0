#!/usr/bin/env python3
"""Copy the [hotkey.*] sections of 0 A.D.'s default.cfg into
src/presentation/hotkey_defaults.h, keeping RTS0's own [hotkey.rts0.*] block.

The sections are copied verbatim (GPL-2.0-or-later) and split into raw
string pieces at blank lines, to stay under MSVC's 16 KB string-literal limit.

    python3 -I tools/extract_0ad_hotkeys.py --cfg /path/to/0.28/binaries/data/config/default.cfg
    python3 -I tools/extract_0ad_hotkeys.py --cfg ... --check   # exit 1 if the header differs
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "src/presentation/hotkey_defaults.h")
SOURCE = "0 A.D. 0.28"
PIECE_LINES = 60
RTS0_MARK = "; ---------------------------------------------------------------------------\n; RTS0 additions"


def hotkey_sections(cfg):
    out = []
    inside = False
    for line in cfg.splitlines():
        m = re.match(r"^\[([^\]]+)\]", line)
        if m:
            name = m.group(1)
            if name == "hotkey" or name.startswith("hotkey."):
                inside = True
            elif inside:
                break
        if inside:
            out.append(line.rstrip())
    while out and out[-1] == "":
        out.pop()
    return out


def pieces(lines):
    """Split LINES at blank lines into chunks of about PIECE_LINES."""
    chunks, cur = [], []
    for line in lines:
        cur.append(line)
        if line == "" and len(cur) >= PIECE_LINES:
            chunks.append(cur)
            cur = []
    if cur:
        chunks.append(cur)
    return chunks


def render(lines, rts0_tail):
    chunks = pieces(lines)
    body = ")cfg\"\nR\"cfg(".join("\n".join(c) + ("\n" if i < len(chunks) - 1 else "") for i, c in enumerate(chunks))
    return f"""#ifndef RTSO_HOTKEY_DEFAULTS_H
#define RTSO_HOTKEY_DEFAULTS_H

// The [hotkey.*] sections of {SOURCE} binaries/data/config/default.cfg
// (GPL-2.0-or-later), copied verbatim by tools/extract_0ad_hotkeys.py, followed
// by RTS0's own [hotkey.rts0.*] keys.
// Parsed by hotkeys.cpp; user overrides load from user://config/user.cfg on top.

namespace rtso {{
namespace hotkeys {{
namespace detail {{

// Split into pieces to stay under MSVC's 16 KB string-literal limit.
static const char kDefaultCfg[] =
R"cfg({body}


{rts0_tail}"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cfg", required=True, help="0 A.D. binaries/data/config/default.cfg")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    current = open(HEADER, encoding="utf-8").read()
    at = current.index(RTS0_MARK)
    text = render(hotkey_sections(open(args.cfg, encoding="utf-8").read()), current[at:])
    if args.check:
        if text != current:
            print("hotkey_defaults.h differs from", args.cfg)
            sys.exit(1)
        print("hotkey_defaults.h matches")
        return
    with open(HEADER, "w", encoding="utf-8") as f:
        f.write(text)
    print("wrote", HEADER)


if __name__ == "__main__":
    main()
