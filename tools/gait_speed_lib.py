import json, struct, sys, numpy as np

def load(path):
    b = open(path, 'rb').read()
    ln = struct.unpack('<I', b[12:16])[0]
    j = json.loads(b[20:20 + ln])
    bin_off = 20 + ln
    blen = struct.unpack('<I', b[bin_off:bin_off + 4])[0]
    binb = b[bin_off + 8: bin_off + 8 + blen]
    return j, binb

def acc(j, binb, idx):
    a = j['accessors'][idx]
    bv = j['bufferViews'][a['bufferView']]
    ncomp = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
    off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = bv.get('byteStride', 0) or 4 * ncomp
    out = np.zeros((a['count'], ncomp), dtype=np.float64)
    for i in range(a['count']):
        out[i] = struct.unpack_from('<%df' % ncomp, binb, off + i * stride)
    return out

def quat_mat(q):
    x, y, z, w = q
    n = x*x + y*y + z*z + w*w
    s = 2.0 / n if n > 0 else 0.0
    return np.array([
        [1 - s*(y*y + z*z), s*(x*y - z*w), s*(x*z + y*w)],
        [s*(x*y + z*w), 1 - s*(x*x + z*z), s*(y*z - x*w)],
        [s*(x*z - y*w), s*(y*z + x*w), 1 - s*(x*x + y*y)]])

def local_at(node, t, tracks):
    """4x4 local matrix at time t; tracks[node] = {'translation': (times, vals), ...}"""
    T = np.array(node.get('translation', [0, 0, 0]), float)
    R = np.array(node.get('rotation', [0, 0, 0, 1]), float)
    S = np.array(node.get('scale', [1, 1, 1]), float)
    tr = tracks.get(node['_i'], {})
    def sample(times, vals):
        if t <= times[0]: return vals[0]
        if t >= times[-1]: return vals[-1]
        k = np.searchsorted(times, t) - 1
        f = (t - times[k]) / (times[k + 1] - times[k])
        return vals[k] * (1 - f) + vals[k + 1] * f
    if 'translation' in tr: T = sample(*tr['translation'])
    if 'rotation' in tr:
        times, vals = tr['rotation']
        # slerp-lite: normalised lerp (small steps only)
        if t <= times[0]: R = vals[0]
        elif t >= times[-1]: R = vals[-1]
        else:
            k = np.searchsorted(times, t) - 1
            f = (t - times[k]) / (times[k + 1] - times[k])
            q0, q1 = vals[k], vals[k + 1]
            if np.dot(q0, q1) < 0: q1 = -q1
            R = q0 * (1 - f) + q1 * f
        R = R / np.linalg.norm(R)
    if 'scale' in tr: S = sample(*tr['scale'])
    M = np.eye(4)
    M[:3, :3] = quat_mat(R) @ np.diag(S)
    M[:3, 3] = T
    return M

def analyse(path, clip_name):
    j, binb = load(path)
    nodes = j['nodes']
    for i, n in enumerate(nodes): n['_i'] = i
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get('children', []): parent[c] = i
    anim = [a for a in j.get('animations', []) if a['name'] == clip_name]
    if not anim: return None
    anim = anim[0]
    tracks = {}
    tmax = 0.0
    for ch in anim['channels']:
        s = anim['samplers'][ch['sampler']]
        times = acc(j, binb, s['input'])[:, 0]
        vals = acc(j, binb, s['output'])
        tmax = max(tmax, times[-1])
        tracks.setdefault(ch['target']['node'], {})[ch['target']['path']] = (times, vals)
    def world(i, t):
        M = local_at(nodes[i], t, tracks)
        p = parent.get(i)
        while p is not None:
            M = local_at(nodes[p], t, tracks) @ M
            p = parent.get(p)
        return M
    idx = {n['name']: i for i, n in enumerate(nodes)}
    need = ['foot_L', 'foot_R', 'hip']
    if any(k not in idx for k in need): return None
    N = 64
    ts = np.linspace(0, tmax, N, endpoint=False)
    feet = {k: [] for k in ('foot_L', 'foot_R')}
    hips = []
    for t in ts:
        hips.append(world(idx['hip'], t)[:3, 3])
        for k in feet: feet[k].append(world(idx[k], t)[:3, 3])
    return ts, np.array(hips), {k: np.array(v) for k, v in feet.items()}, tmax

