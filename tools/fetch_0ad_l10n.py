#!/usr/bin/env python3
"""Copy 0 A.D.'s translations into assets/l10n/<lang>.po.

0 A.D. (GPL-2.0-or-later, translations by the 0 A.D. Transifex teams) keeps one
.po file per language and domain under binaries/data/mods/public/l10n/ and
binaries/data/l10n/. This merges each language's domains into one file that
Godot's TranslationServer can load, keeping only what RTS0 looks up:

- translated, non-fuzzy entries only;
- no msgctxt: Godot's Control auto-translation looks strings up without a
  context, so a context entry is kept under its bare msgid unless a
  context-free entry for the same msgid exists (the first one wins);
- plurals become two plain entries (msgid -> msgstr[0], msgid_plural ->
  msgstr[1]).

Languages at or above --threshold percent translated are copied (0 A.D.'s
own release scripts drop incomplete languages the same way).

Usage: tools/fetch_0ad_l10n.py [--src DIR] [--threshold 75]
Without --src it sparse-clones the 0 A.D. GitHub mirror at the commit RTS0's
ports use (61a3b95).
"""
import argparse, collections, glob, os, re, subprocess, sys, tempfile

COMMIT = "61a3b9507d974084e6badb88a0826bd89a6d5b8b"
REPO = "https://github.com/0ad/0ad.git"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "l10n")
SKIP = {"long"}  # 0 A.D.'s pseudo-locale for testing long strings


def translators(path):
    """The "# Translators:" block of a 0 A.D. .po header."""
    names, inside = [], False
    with open(path, encoding="utf-8") as f:
        for line in f:
            if not line.startswith("#"):
                break
            if line.startswith("# Translators:"):
                inside = True
            elif inside and line.startswith("# ") and line.strip() != "#":
                names.append(line[2:].strip())
    return names


def parse(path):
    ents, cur, key = [], {}, None
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.strip():
                if cur:
                    ents.append(cur)
                cur, key = {}, None
                continue
            if line.startswith("#"):
                if line.startswith("#,"):
                    cur["flags"] = line
                continue
            m = re.match(r'(msgctxt|msgid_plural|msgid|msgstr(?:\[\d+\])?) "(.*)"$', line)
            if m:
                key = m.group(1)
                cur[key] = m.group(2)
                continue
            m = re.match(r'"(.*)"$', line)
            if m and key:
                cur[key] += m.group(1)
    if cur:
        ents.append(cur)
    return ents


def header_field(ents, name):
    for e in ents:
        if e.get("msgid") == "":
            for part in e.get("msgstr", "").split("\\n"):
                if part.startswith(name + ":"):
                    return part.split(":", 1)[1].strip()
    return ""


def fetch(tmp):
    repo = os.path.join(tmp, "0ad")
    subprocess.check_call(["git", "clone", "-q", "--filter=blob:none", "--no-checkout", REPO, repo])
    subprocess.check_call(["git", "-C", repo, "sparse-checkout", "set", "--no-cone",
                           "binaries/data/mods/public/l10n/*.po", "binaries/data/l10n/*.po"])
    subprocess.check_call(["git", "-C", repo, "checkout", "-q", COMMIT])
    return repo


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", help="an existing 0 A.D. checkout")
    ap.add_argument("--threshold", type=float, default=75.0)
    args = ap.parse_args()
    src = args.src or fetch(tempfile.mkdtemp())
    files = collections.defaultdict(list)
    for d in ("binaries/data/mods/public/l10n", "binaries/data/l10n"):
        for p in sorted(glob.glob(os.path.join(src, d, "*.po"))):
            lang = os.path.basename(p).split(".")[0]
            if lang not in SKIP:
                files[lang].append(p)
    os.makedirs(OUT, exist_ok=True)
    kept = []
    for lang in sorted(files):
        total = done = 0
        plain, ctx = {}, {}
        plural_forms = ""
        credits = []
        for p in files[lang]:
            credits += [n for n in translators(p) if n not in credits]
            ents = parse(p)
            plural_forms = plural_forms or header_field(ents, "Plural-Forms")
            for e in ents:
                mid = e.get("msgid")
                if not mid:
                    continue
                total += 1
                if "fuzzy" in e.get("flags", ""):
                    continue
                pairs = []
                if "msgid_plural" in e:
                    pairs = [(mid, e.get("msgstr[0]", "")), (e["msgid_plural"], e.get("msgstr[1]", e.get("msgstr[0]", "")))]
                else:
                    pairs = [(mid, e.get("msgstr", ""))]
                if not pairs[0][1]:
                    continue
                done += 1
                table = ctx if "msgctxt" in e else plain
                for k, v in pairs:
                    if v:
                        table.setdefault(k, v)
        pct = 100.0 * done / max(total, 1)
        if pct < args.threshold:
            continue
        for k, v in ctx.items():
            plain.setdefault(k, v)
        out = os.path.join(OUT, lang + ".po")
        with open(out, "w", encoding="utf-8") as f:
            f.write("# 0 A.D. translations (%s), merged by tools/fetch_0ad_l10n.py from 0 A.D. %s.\n" % (lang, COMMIT[:7]))
            f.write("# Copyright (C) Wildfire Games. Same license as 0 A.D.: GPL-2.0-or-later for\n"
                    "# strings from 0 A.D.'s code, CC BY-SA 3.0 for strings from its data.\n")
            f.write("# Translators:\n")
            for n in sorted(credits, key=str.lower):
                f.write("# %s\n" % n)
            f.write('msgid ""\nmsgstr ""\n"Language: %s\\n"\n"MIME-Version: 1.0\\n"\n'
                    '"Content-Type: text/plain; charset=UTF-8\\n"\n' % lang)
            if plural_forms:
                f.write('"Plural-Forms: %s\\n"\n' % plural_forms)
            f.write("\n")
            for k in sorted(plain):
                f.write('msgid "%s"\nmsgstr "%s"\n\n' % (k, plain[k]))
        kept.append((lang, round(pct, 1), len(plain)))
    for lang, pct, n in kept:
        print("%-6s %5.1f%% %5d strings" % (lang, pct, n))
    print("%d languages written to %s" % (len(kept), os.path.normpath(OUT)))


if __name__ == "__main__":
    sys.exit(main())
