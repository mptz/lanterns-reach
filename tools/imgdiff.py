#!/usr/bin/env python3
"""Compare two PNGs (8-bit RGB/RGBA, non-interlaced; the subset --capture and
the texture generator write). Standard library only.

usage: imgdiff.py A.png B.png [--max N [--allow K]] [--mask X0,Y0,X1,Y1 ...]
                  [--tolerant R [--cluster N]]

Prints the maximum per-channel difference (0-255) and how many pixels
differ at all. With --max N, exits 1 if the maximum difference exceeds N;
with --allow K as well, exits 1 only if more than K pixels exceed N (a
recorded budget for known, isolated differences, such as the pinhole cracks
at T-junctions between merged quads, RELEASES.md v0.2.3).

--mask ignores a rectangle (x0, y0 inclusive; x1, y1 exclusive), e.g. a dev
panel whose text is live; it may be repeated.

--tolerant R (RELEASES.md v0.2.5) excuses a pixel over --max (default 1)
when its colour is within --max of some pixel of the other image no more
than R pixels away, checked both ways: a texel seam or an edge that moved by
a pixel shows a colour that is already there nearby, while a wrong tile or
tint, a missing face or sky through a crack shows one that isn't. The
pixels left are grouped into 8-connected clusters and the largest are
listed. With --cluster N, exits 1 if any cluster has N pixels or more;
otherwise if more than --allow pixels are left.
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


def take(argv, flag, convert, many=False):
    values = []
    while flag in argv:
        i = argv.index(flag)
        values.append(convert(argv[i + 1]))
        argv[i:i + 2] = []
        if not many:
            break
    return values if many else (values[0] if values else None)


def clusters(points):
    """8-connected components of a set of (x, y)."""
    left, out = set(points), []
    while left:
        stack, comp = [left.pop()], []
        while stack:
            x, y = stack.pop()
            comp.append((x, y))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    q = (x + dx, y + dy)
                    if q in left:
                        left.remove(q)
                        stack.append(q)
        out.append(comp)
    return sorted(out, key=len, reverse=True)


def main(argv):
    argv = list(argv)
    limit = take(argv, "--max", int)
    allow = take(argv, "--allow", int) or 0
    masks = take(argv, "--mask", lambda v: tuple(int(t) for t in v.split(",")), many=True)
    radius = take(argv, "--tolerant", int)
    cluster_limit = take(argv, "--cluster", int)
    a, b = argv
    wa, ha, ra = read_png(a)
    wb, hb, rb = read_png(b)
    if (wa, ha) != (wb, hb):
        print(f"size differs: {wa}x{ha} vs {wb}x{hb}")
        return 1
    if radius is not None and limit is None:
        limit = 1
    masked = lambda x, y: any(x0 <= x < x1 and y0 <= y < y1 for x0, y0, x1, y1 in masks)
    diff = lambda p, q: max(abs(p[c] - q[c]) for c in range(3))
    worst, differing, over = 0, 0, []
    for y, (row_a, row_b) in enumerate(zip(ra, rb)):
        for x, (pa, pb) in enumerate(zip(row_a, row_b)):
            if masks and masked(x, y):
                continue
            d = diff(pa, pb)
            worst = max(worst, d)
            differing += d > 0
            if limit is not None and d > limit:
                over.append((x, y, d))
    note = f"; {len(over)} over {limit}/255" if limit is not None and (allow or radius is not None) else ""
    if allow:
        note += f" (allowed {allow})"
    if masks:
        note += f"; {len(masks)} masked region(s)"
    if radius is None:
        print(f"max per-channel difference {worst}/255; {differing} of {wa * ha} pixels differ{note}")
        return 1 if limit is not None and len(over) > allow else 0

    def found(rows, x, y, colour):
        for yy in range(max(0, y - radius), min(ha, y + radius + 1)):
            row = rows[yy]
            for xx in range(max(0, x - radius), min(wa, x + radius + 1)):
                if diff(row[xx], colour) <= limit:
                    return True
        return False

    left = {(x, y): d for x, y, d in over if not (found(rb, x, y, ra[y][x]) and found(ra, x, y, rb[y][x]))}
    groups = clusters(left)
    largest = len(groups[0]) if groups else 0
    print(f"max per-channel difference {worst}/255; {differing} of {wa * ha} pixels differ{note}; "
          f"{len(left)} not within {radius} px of a matching colour, {len(groups)} cluster(s), largest {largest}")
    for comp in groups[:5]:
        xs, ys = [p[0] for p in comp], [p[1] for p in comp]
        print(f"  cluster of {len(comp)}: x {min(xs)}-{max(xs)}, y {min(ys)}-{max(ys)}, "
              f"max difference {max(left[p] for p in comp)}/255")
    if cluster_limit is not None:
        return 1 if largest >= cluster_limit else 0
    return 1 if len(left) > allow else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
