#!/usr/bin/env python3
"""
test_cross_compiler.py: the same match on every compiler (REMAINING_GAPS G30).

Multiplayer and replays need every build of RTS0 to compute the same bits. The x86-64
builds (Linux, Windows) come from g++ and never fuse a*b+c; the arm64 builds (Apple
Silicon, iOS, Android) come from clang, which fuses it into one FMA unless told not to.
This builds the map-hash tool and the late-game bench twice, with g++ and with
`clang++ -mfma` (the arm64 stand-in on an x86 runner), both with SConstruct's
-ffp-contract=off, and fails unless every random map and a 12-minute 4-player Petra
match end on the same hashes.

It also lints the build files: SConstruct must turn contraction off, and nothing may
add -ffast-math, -Ofast, -ffp-contract=fast or /fp:fast.

Usage:
  python3 tools/test_cross_compiler.py [--jobs N] [--seeds N]
"""
import argparse
import glob
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = ['src/simulation/simulation.cpp', 'src/simulation/rms_generator.cpp', 'src/core/job_system.cpp',
       'src/network/turn_manager.cpp', 'src/network/net_host.cpp']
TOOLS = ['tools/map_hashes.cpp', 'tools/bench_late.cpp']
FMA = ['-mfma'] if platform.machine() in ('x86_64', 'AMD64') else []  # arm64 always has FMA
BUILDS = {
    'g++': ['g++', '-std=c++20', '-O2', '-ffp-contract=off', '-Isrc'],
    'clang++ fma': ['clang++', '-std=c++20', '-O2', *FMA, '-ffp-contract=off', '-Isrc'],
}
UNSAFE = re.compile(r'-ffast-math|-Ofast|-ffp-contract=fast|/fp:fast|-funsafe-math-optimizations|'
                    r'-fassociative-math|-freciprocal-math')


def lint():
    errors = []
    sconstruct = open(os.path.join(ROOT, 'SConstruct')).read()
    for flag in ('-ffp-contract=off', '/fp:precise'):
        if flag not in sconstruct:
            errors.append(f'SConstruct does not add {flag}')
    me = os.path.abspath(__file__)
    scripts = glob.glob(os.path.join(ROOT, 'tools', '*.py')) + glob.glob(os.path.join(ROOT, 'tools', '*.sh'))
    files = ['SConstruct', '.github/workflows/ci.yml'] + sorted(os.path.relpath(p, ROOT) for p in scripts)
    for rel in files:
        path = os.path.join(ROOT, rel)
        if os.path.abspath(path) == me or not os.path.exists(path):
            continue
        for n, line in enumerate(open(path, errors='replace'), 1):
            m = None if line.lstrip().startswith('#') else UNSAFE.search(line)  # comments may name them
            if m:
                errors.append(f'{rel}:{n}: {m.group(0)} changes float results between builds')
    return errors


def fuses_by_default(cxx):
    """True when this compiler, without -ffp-contract=off, turns a*b+c into an FMA, so the
    clang build below really stands in for an arm64 one."""
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, 'f.cpp')
        open(src, 'w').write('float f(float a, float b, float c) { return a * b + c; }\n')
        out = subprocess.run([cxx, '-O2', *FMA, '-S', '-o', '-', src], capture_output=True, text=True)
        return out.returncode == 0 and re.search(r'\bv?fn?madd|\bfmla', out.stdout) is not None


def build(name, flags, out, jobs):
    objdir = os.path.join(out, name.replace(' ', '_').replace('+', 'p'))
    os.makedirs(objdir, exist_ok=True)

    def compile_one(src):
        obj = os.path.join(objdir, os.path.basename(src)[:-4] + '.o')
        r = subprocess.run(flags + ['-c', src, '-o', obj], cwd=ROOT, capture_output=True, text=True)
        if r.returncode:
            sys.exit(f'{name}: {src} failed to compile\n{r.stderr[-3000:]}')
        return obj

    with ThreadPoolExecutor(jobs) as ex:
        objs = dict(zip(SIM + TOOLS, ex.map(compile_one, SIM + TOOLS)))
    bins = {}
    for tool in TOOLS:
        exe = os.path.join(objdir, os.path.basename(tool)[:-4])
        r = subprocess.run([flags[0], objs[tool]] + [objs[s] for s in SIM] + ['-lpthread', '-o', exe],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit(f'{name}: linking {tool} failed\n{r.stderr[-3000:]}')
        bins[tool] = exe
    return bins


def run(cmd):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f'{" ".join(cmd)} exited {r.returncode}\n{r.stderr[-3000:]}')
    return r.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 2)
    ap.add_argument('--seeds', type=int, default=2)
    ap.add_argument('--save', help='write each build\'s map hashes to this directory')
    args = ap.parse_args()

    errors = lint()
    for e in errors:
        print('FAIL', e)
    print(f'{"FAIL" if errors else "ok"}: build flags keep float results exact')

    missing = [b[0] for b in BUILDS.values() if not shutil.which(b[0])]
    if missing:
        print(f'skip: {", ".join(missing)} not installed, so the cross-compiler match is not run')
        sys.exit(1 if errors else 0)
    if not fuses_by_default('clang++'):
        print('note: this clang++ does not fuse a*b+c by default, so the comparison below proves less')

    out = tempfile.mkdtemp(prefix='rts0_xcc_')
    try:
        results = {}
        for name, flags in BUILDS.items():
            bins = build(name, flags, out, args.jobs)
            maps = run([bins['tools/map_hashes.cpp'], '--seeds', str(args.seeds), '--size', '256']).splitlines()
            bench = run([bins['tools/bench_late.cpp'], '--players', '4', '--size', '256', '--minutes', '12',
                         '--threads', '0'])
            end = [json.loads(l) for l in bench.splitlines() if l.startswith('{"players"')][-1]
            results[name] = (maps, end['hash'])
            if args.save:
                os.makedirs(args.save, exist_ok=True)
                with open(os.path.join(args.save, 'maps_' + name.split()[0].replace('+', 'p') + '.txt'), 'w') as f:
                    f.write('\n'.join(maps) + '\n')
            print(f'{name}: {len(maps)} maps generated, match ended on hash {end["hash"]}')

        (a, (maps_a, hash_a)), (b, (maps_b, hash_b)) = results.items()
        diff = [(x, y) for x, y in zip(maps_a, maps_b) if x != y]
        if len(maps_a) != len(maps_b) or not maps_a:
            errors.append(f'{a} printed {len(maps_a)} maps, {b} printed {len(maps_b)}')
        for x, y in diff:
            errors.append(f'map {x.split()[0]} seed {x.split()[1]}: {a} "{x}" but {b} "{y}"')
        if diff:
            errors.append('a map differs between compilers: look for an expression in rms_generator.cpp that '
                          'draws from rng twice (g++ and clang order the draws differently) and split it into '
                          'named locals in right-to-left order')
        print(f'{"FAIL" if diff else "ok"}: all {len(maps_a)} random maps hash the same under {a} and {b}')
        if hash_a != hash_b:
            errors.append(f'the 12-minute match ended on {hash_a} under {a} but {hash_b} under {b}')
        print(f'{"FAIL" if hash_a != hash_b else "ok"}: a 12-minute 4-player Petra match ends on the same hash')
    finally:
        shutil.rmtree(out, ignore_errors=True)

    for e in errors:
        print('FAIL', e)
    print(f'RESULT: {"FAIL" if errors else "PASS"}')
    sys.exit(1 if errors else 0)


if __name__ == '__main__':
    main()
