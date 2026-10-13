#!/usr/bin/env python3
"""Bake the random-map heightmap images into a C++ header.

0 A.D.'s Hellas, Red Sea, Mediterranean, Pompeii, Elephantine, Marmara, Ngorongoro,
Bahrain and Ratumacos scripts
paint the map from a greyscale PNG (Engine.LoadHeightmapImage). The headless
simulation has no image decoder, so this script decodes the PNGs in
assets/maps/heightmaps/ (8-bit or 1-bit grey; 1-bit samples become 0 or
255), halves them with a 2x2 box filter (512 -> 256, Hellas 720 -> 360; the
RTS0 grid is at most 257 vertices across) and writes the 8-bit samples to
src/simulation/heightmap_images.h as base64 chunks.

Run from the repo root after changing an image:
    python3 tools/gen_heightmap_images.py

Jebel Barkal's hill is a 0 A.D. terrain file (maps/random/jebel_barkal.pmp,
Engine.LoadMapTerrain): its 321x321 16-bit vertex heights. Convert it once to
the 8-bit image this script reads, the lowest height black and the highest
white (PMP_RANGE below), row 0 the top (north, the highest rmgen z):
    python3 tools/gen_heightmap_images.py --from-pmp path/to/jebel_barkal.pmp
"""

import base64
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGES = [
    # (symbol, file, downscale factor)
    ("red_sea", "assets/maps/heightmaps/red_sea.png", 2),
    ("hellas", "assets/maps/heightmaps/hellas.png", 2),
    ("marmara", "assets/maps/heightmaps/marmara.png", 2),
    ("bahrain", "assets/maps/heightmaps/bahrain.png", 2),
    ("ratumacos", "assets/maps/heightmaps/ratumacos.png", 2),
    ("mediterranean", "assets/maps/heightmaps/mediterranean.png", 2),
    ("pompeii", "assets/maps/heightmaps/pompeii.png", 2),
    ("elephantine", "assets/maps/heightmaps/elephantine.png", 2),
    ("jebel_barkal", "assets/maps/heightmaps/jebel_barkal.png", 2),
    ("ngorongoro", "assets/maps/heightmaps/ngorongoro.png", 2),
]
# jebel_barkal.pmp's lowest and highest raw heights, the black and white of jebel_barkal.png.
PMP_RANGE = (23170, 33144)
PMP_OUT = ROOT / "assets/maps/heightmaps/jebel_barkal.png"
OUT = ROOT / "src/simulation/heightmap_images.h"
CHUNK = 4000  # characters per string literal (MSVC caps single literals)


def decode_grey8(path):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth in (1, 8) and ctype == 0 and interlace == 0, "need 8-bit or 1-bit grey, not interlaced"
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = (w * depth + 7) // 8
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - 1] if x else 0
            b = prev[x]
            c = prev[x - 1] if x else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        prev = line
        if depth == 1:
            line = bytearray(255 if (line[x >> 3] >> (7 - (x & 7))) & 1 else 0 for x in range(w))
        rows.append(line)
    return w, h, rows


def downscale(w, h, rows, k):
    ow, oh = w // k, h // k
    out = bytearray(ow * oh)
    for y in range(oh):
        for x in range(ow):
            s = sum(rows[y * k + j][x * k + i] for j in range(k) for i in range(k))
            out[y * ow + x] = (s + k * k // 2) // (k * k)
    return ow, oh, out


def write_grey8(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def from_pmp(src):
    data = Path(src).read_bytes()
    assert data[:4] == b"PSMP", src
    patches = struct.unpack("<I", data[12:16])[0]
    n = patches * 16 + 1
    heights = struct.unpack(f"<{n * n}H", data[16:16 + 2 * n * n])
    lo, hi = PMP_RANGE
    assert min(heights) == lo and max(heights) == hi, (min(heights), max(heights))
    # PMP rows run from z = 0 (south) up; the image's row 0 is the north edge.
    rows = [bytearray(round((heights[z * n + x] - lo) * 255 / (hi - lo)) for x in range(n)) for z in reversed(range(n))]
    write_grey8(PMP_OUT, n, n, rows)
    print(f"wrote {PMP_OUT.relative_to(ROOT)} ({n}x{n})")


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--from-pmp":
        from_pmp(sys.argv[2])
        return
    parts = [
        "// Generated by tools/gen_heightmap_images.py; do not edit.\n",
        "// Greyscale heightmaps from 0 A.D. (maps/random/*.png), imagery by Jesse\n",
        "// Allen, NASA's Earth Observatory, from GEBCO bathymetry; public domain.\n",
        "// elephantine: river outline from OpenStreetMap data (c) OpenStreetMap\n",
        "// contributors, ODbL 1.0.\n",
        "// jebel_barkal: 0 A.D.'s maps/random/jebel_barkal.pmp heights (CC BY-SA 3.0).\n",
        "// Row-major, row 0 = top (north) of the image, base64 of 8-bit samples.\n",
        "#pragma once\n\nnamespace rtso::heightmap_images {\n\n",
    ]
    for sym, rel, k in IMAGES:
        w, h, rows = decode_grey8(ROOT / rel)
        ow, oh, px = downscale(w, h, rows, k)
        b64 = base64.b64encode(bytes(px)).decode()
        chunks = [b64[i:i + CHUNK] for i in range(0, len(b64), CHUNK)]
        parts.append(f"// {rel}: {w}x{h} halved to {ow}x{oh}\n")
        parts.append(f"constexpr int {sym}_size = {ow};\n")
        parts.append(f"constexpr const char *{sym}_b64[] = {{\n")
        for c in chunks:
            parts.append(f'\t"{c}",\n')
        parts.append("\tnullptr\n};\n\n")
    parts.append("} // namespace rtso::heightmap_images\n")
    OUT.write_text("".join(parts))
    print(f"wrote {OUT.relative_to(ROOT)} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
