#!/usr/bin/env python3
"""Authored locomotion speed of each unit GLB's Walk and Run clips, in sim units per second.

The walk and run cycles are played at (speed / authored speed) so the feet keep pace with the
ground (0 A.D. syncs a locomotion clip to the distance it covers). The authored speed is what
the clip covers in a cycle: two steps, each the forward separation of the feet, over the clip's length.

Units: the GLB's units are 0 A.D. 0.28 metres times the root node's scale (ModelLibrary
Entry::metre), so metres = glb / root_scale; 0 A.D. speeds are converted to sim units with
SPEED_SCALE (0.356, extract_0ad_rosters.py), as the simulation's unit speeds are.

Usage: python3 tools/gait_speed.py [--out data/gait_speed.json] [GLB ...]
"""
import argparse, glob, json, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gait_speed_lib import analyse  # noqa: E402

SPEED_SCALE = 0.356  # 0.28 m/s -> sim units/s (extract_0ad_rosters.py)


def root_scale(path):
    import struct
    with open(path, 'rb') as f:
        b = f.read()
    ln = struct.unpack('<I', b[12:16])[0]
    j = json.loads(b[20:20 + ln])
    children = {c for n in j['nodes'] for c in n.get('children', [])}
    roots = [n for i, n in enumerate(j['nodes']) if i not in children]
    s = roots[0].get('scale', [1, 1, 1]) if roots else [1, 1, 1]
    return float(s[0]) or 1.0


def stride_m(path, clip):
    """Step length (rig units) and cycle length (s): the largest forward separation of the two
    feet over the cycle, along the principal axis of their separation. A walk cycle covers two steps."""
    r = analyse(path, clip)
    if r is None:
        return None
    _, _, feet, T = r
    d = (feet['foot_L'] - feet['foot_R'])[:, [0, 2]]
    _, _, vt = np.linalg.svd(d - d.mean(0), full_matrices=False)
    proj = d @ vt[0]
    step = float(max(proj.max(), -proj.min()))
    return step, T


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='data/gait_speed.json')
    ap.add_argument('glbs', nargs='*')
    a = ap.parse_args()
    files = a.glbs or sorted(glob.glob('assets/models/units/*.glb'))
    out = {}
    for f in files:
        name = os.path.basename(f)[:-4]
        rs = root_scale(f)
        entry = {}
        for clip, key in (('Walk', 'walk'), ('Run', 'run')):
            r = stride_m(f, clip)
            if r is None or r[0] <= 0.0 or r[1] <= 0.0:
                continue
            step, T = r
            m_per_s = 2.0 * (step / rs) / T  # 0.28 m/s: two steps per cycle
            if m_per_s * SPEED_SCALE < 0.05:  # no real travel in the clip (an in-place walk)
                continue
            entry[key] = round(m_per_s * SPEED_SCALE, 4)
        # A run clip slower than the walk is a measurement the probe can't stand behind (a
        # shared or walk-like clip): leave the run to the template's base speed.
        if 'run' in entry and 'walk' in entry and entry['run'] < entry['walk']:
            del entry['run']
        if entry:
            out[name] = entry
    os.makedirs(os.path.dirname(a.out) or '.', exist_ok=True)
    with open(a.out, 'w') as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
        fh.write('\n')
    print('wrote', len(out), 'models to', a.out)


if __name__ == '__main__':
    main()
