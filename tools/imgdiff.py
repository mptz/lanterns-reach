#!/usr/bin/env python3
"""Compare two PNGs (8-bit RGB/RGBA, non-interlaced; the subset --capture and
the texture generator write). Standard library only.

usage: imgdiff.py A.png B.png [--max N [--allow K]]

Prints the maximum per-channel difference (0-255) and how many pixels
differ at all. With --max N, exits 1 if the maximum difference exceeds N;
with --allow K as well, exits 1 only if more than K pixels exceed N (a
recorded budget for known, isolated differences, such as the pinhole cracks
at T-junctions between merged quads, RELEASES.md v0.2.3).
"""
import struct
import sys
import zlib


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if tag == b"IHDR":
            w, h, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or color not in (2, 6) or interlace:
                raise ValueError("only 8-bit non-interlaced RGB/RGBA PNGs are supported")
            bpp = 3 if color == 2 else 4
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break
    if w is None:
        raise ValueError("no IHDR")
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        ftype, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ftype == 1:
                line[x] = (line[x] + a) & 0xFF
            elif ftype == 2:
                line[x] = (line[x] + b) & 0xFF
            elif ftype == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        prev = line
        px = [tuple(line[x:x + bpp]) + ((255,) if bpp == 3 else ()) for x in range(0, stride, bpp)]
        rows.append(px)
    return w, h, rows


def main(argv):
    limit = None
    allow = 0
    if "--max" in argv:
        i = argv.index("--max")
        limit = int(argv[i + 1])
        argv = argv[:i] + argv[i + 2:]
    if "--allow" in argv:
        i = argv.index("--allow")
        allow = int(argv[i + 1])
        argv = argv[:i] + argv[i + 2:]
    a, b = argv
    wa, ha, ra = read_png(a)
    wb, hb, rb = read_png(b)
    if (wa, ha) != (wb, hb):
        print(f"size differs: {wa}x{ha} vs {wb}x{hb}")
        return 1
    worst, differing, over = 0, 0, 0
    for row_a, row_b in zip(ra, rb):
        for pa, pb in zip(row_a, row_b):
            d = max(abs(pa[c] - pb[c]) for c in range(3))
            worst = max(worst, d)
            differing += d > 0
            over += limit is not None and d > limit
    note = f"; {over} over {limit}/255 (allowed {allow})" if limit is not None and allow else ""
    print(f"max per-channel difference {worst}/255; {differing} of {wa * ha} pixels differ{note}")
    return 1 if limit is not None and over > allow else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
