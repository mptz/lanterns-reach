#!/usr/bin/env python3
"""Generate the 16x16 block textures as PNGs (stdlib only).

usage: gen_block_texture.py [out-dir]       (default: assets/blocks; created if absent)
       gen_block_texture.py --fixtures not16|wrong_tiles DIR
                                            negative-test fixture textures (tests/blocks_bad/)

Deterministic (fixed seeds, hash-based noise). Every face texture is a real
16x16 source (render/block-textures); textures need no rotational symmetry
(rotational invariance comes from canonical orientations,
world/block-orientation).

  - single textures: brick (a pinwheel bond, which still reads as a bond on
    every face a uniform block's faces are turned onto), lamp, polished,
    gold, glow, log end, grate (an iron lattice with transparent holes, the
    cutout test block), and the six chiral furnace faces. Each furnace face
    carries a letter in its top-left corner (F front, B back, R right,
    L left, U top, D bottom), so mirroring or a wrong turn is visible; the
    front has an opening and the top an arrow toward the front.
  - variant sets: atlases of 16x16 tiles, 4 per row, tile i at column i % 4,
    row i / 4 (render/block-textures, "three principles"):
    - `random`: stone, dirt, grass (8 tiles each, independent per-texel
      noise) and stone brick (4 tiles, subtle wear and tint);
    - `matched`: log bark (ns, 4 tiles: grain continuous end to end) and
      gravel (nsew, 16 tiles: pebbles crossing edges, plain corners);
    - `connected`: glass_frames, 16 frame variants, a pale-grey band one
      texel wide on each framed edge, alpha = coverage (0.85 on the band, 0
      elsewhere), so tile 0 is fully transparent.
    Matched edges are continuous by construction; nothing checks how they
    look (the engine checks structure only).
"""
import math
import os
import struct
import sys
import zlib

N = 16          # texels per face edge
SET_COLS = 4    # a variant set's atlas has 4 tiles per row
N_EDGE, E_EDGE, S_EDGE, W_EDGE = 1, 2, 4, 8


def clamp8(v):
    return max(0, min(255, int(v)))


def h01(seed, x, y):
    """Deterministic hash of (seed, x, y) in [0, 1)."""
    v = (seed * 0x9E3779B1 + x * 0x85EBCA77 + y * 0xC2B2AE3D) & 0xFFFFFFFF
    v ^= v >> 15
    v = (v * 0x2C1B3C6D) & 0xFFFFFFFF
    v ^= v >> 12
    v = (v * 0x297A2D39) & 0xFFFFFFFF
    v ^= v >> 15
    return v / 2**32


def smooth(t):
    return t * t * (3 - 2 * t)


def noise(seed, cell, period=None):
    """Value noise in [0, 1] on a `cell`-texel lattice, evaluated at texel
    centres. With `period` (texels) it wraps; otherwise it is defined over the
    whole plane (negative coordinates included)."""
    def lattice(i, j):
        if period:
            n = period // cell
            i, j = i % n, j % n
        return h01(seed, i & 0xFFFF, j & 0xFFFF)

    def at(x, y):
        fx, fy = (x + 0.5) / cell, (y + 0.5) / cell
        i, j = math.floor(fx), math.floor(fy)
        tx, ty = smooth(fx - i), smooth(fy - j)
        a = lattice(i, j) * (1 - tx) + lattice(i + 1, j) * tx
        b = lattice(i, j + 1) * (1 - tx) + lattice(i + 1, j + 1) * tx
        return a * (1 - ty) + b * ty
    return at


# --- single textures ---------------------------------------------------------------

def brick(seed):
    # Pinwheel bond: four 10x6 bricks turning about a 4x4 centre square.
    def region(x, y):
        if x < 10 and y < 6:
            return 1
        if x < 6 and y >= 6:
            return 2
        if x >= 6 and y >= 10:
            return 3
        if x >= 10 and y < 10:
            return 4
        return 0  # centre square

    shade = noise(seed, 4, period=N)

    def px(x, y):
        # Shifted so the wrap seam runs through bricks, not along a joint
        # (a joint on the seam reads as a seam to the tileable metric).
        x, y = (x + 3) % N, (y + 3) % N
        r = region(x, y)
        # Mortar on each brick's top and left edge (wrapping), so every joint
        # is one texel wide.
        mortar = region((x - 1) % N, y) != r or region(x, (y - 1) % N) != r
        if mortar:
            v = 74 + h01(seed, x, y) * 12
            return (v, v, v - 4)
        base = 148 if r else 126
        v = base + (h01(seed + 1, r, 0) - 0.5) * 20 + shade(x, y) * 16 + h01(seed + 2, x, y) * 14
        return (v, v - 10, v - 24)
    return px


def lamp(seed):
    # A glowing pane in a dark iron frame with a cross of mullions.
    c = (N - 1) / 2

    def px(x, y):
        dx, dy = abs(x - c), abs(y - c)
        if max(dx, dy) > 6 or min(dx, dy) < 1:  # frame and mullions
            v = 46 + h01(seed, x, y) * 10
            return (v, v * 0.95, v * 0.9)
        glow = 1.0 - (dx * dx + dy * dy) / (2 * 6.5 ** 2)
        v = 200 + 55 * glow + h01(seed, x, y) * 10
        return (v, v * 0.82, v * 0.5)
    return px


def polished(seed):
    # Pale marble: soft veins over a light base. Shared by the low- and
    # high-roughness test blocks, so only roughness differs between them.
    coarse = noise(seed, 8, period=N)
    fine = noise(seed + 1, 4, period=N)

    def px(x, y):
        f = fine(x, y)
        v = 196 + coarse(x, y) * 30 + h01(seed, x, y) * 6
        if abs(f - 0.5) < 0.05:
            v -= 36
        return (v, v * 0.98, v * 0.95)
    return px


def gold(seed):
    coarse = noise(seed, 4, period=N)

    def px(x, y):
        v = 0.85 + coarse(x, y) * 0.15 + h01(seed, x, y) * 0.05
        return (255 * v, 196 * v, 74 * v)
    return px


def glow(seed):
    c = (N - 1) / 2

    def px(x, y):
        d = max(abs(x - c), abs(y - c)) / c
        v = 255 - 60 * d + h01(seed, x, y) * 8
        return (v, v * 0.85, v * 0.55)
    return px


def log_end(seed):
    c = (N - 1) / 2

    def px(x, y):
        dx, dy = x - c, y - c
        if max(abs(dx), abs(dy)) > 6:  # bark rim
            v = 70 + h01(seed, x, y) * 20
            return (v, v * 0.75, v * 0.5)
        ring = int((dx * dx + dy * dy) ** 0.5 / 1.6)
        v = (176 if ring % 2 else 152) + h01(seed + 1, x, y) * 14
        if ring == 0:
            v -= 30  # pith
        return (v, v * 0.78, v * 0.52)
    return px


GLYPHS = {
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "D": ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
}


def glyph_at(letter, x, y, ox=1, oy=1):
    rows = GLYPHS[letter]
    gx, gy = x - ox, y - oy
    return 0 <= gy < len(rows) and 0 <= gx < len(rows[0]) and rows[gy][gx] == "#"


def furnace(letter, extra):
    def make(seed):
        shade = noise(seed, 4)

        def px(x, y):
            v = 108 + shade(x, y) * 30 + h01(seed, x, y) * 14
            if x in (0, N - 1) or y in (0, N - 1):  # frame
                v -= 30
            col = extra(x, y, (v, v, v + 3))
            if glyph_at(letter, x, y):
                col = (235, 225, 90)
            return col
        return px
    return make


def front_extra(x, y, col):
    if 7 <= x <= 14 and 9 <= y <= 14:  # the opening, glowing at the bottom
        if x in (7, 14) or y == 9:
            return (40, 36, 34)
        glow = (y - 9) / 5
        return (60 + 190 * glow, 25 + 90 * glow, 15 + 10 * glow)
    return col


def top_extra(x, y, col):
    # Arrow pointing to image-down = local -y = the front (the +z frame has up = +y).
    shaft = 7 <= x <= 8 and 4 <= y <= 9
    head = 10 <= y <= 14 and abs(x - 7.5) <= (14 - y) + 0.5
    return (210, 70, 50) if shaft or head else col


def plain(x, y, col):
    return col


# --- variant sets ------------------------------------------------------------------
# Atlases of 16x16 tiles, 4 per row, tile i at column i % 4, row i / 4.

def tile_set(make, count):
    """make(seed) -> px(tile, x, y); returns an atlas function (unused slots black)."""
    def atlas(seed):
        px = make(seed)

        def at(X, Y):
            t = (Y // N) * SET_COLS + X // N
            return px(t, X % N, Y % N) if t < count else (0, 0, 0)
        return at
    return atlas


def atlas_size(count):
    return (N * SET_COLS, N * ((count + SET_COLS - 1) // SET_COLS))


# `random` sets: every tile is independent per-texel noise, hashed with its
# own index. No feature is larger than a texel, so any tile meets any other
# without a seam and nothing repeats at the block period.

def stone_set(seed):
    def px(t, x, y):
        v = h01(seed + t, x, y) * 34 + h01(seed + 50 + t, x, y) * 14 - 24
        if h01(seed + 101 + t, x, y) < 0.07:  # dark flecks
            v -= 22
        g = 104 + v
        return (g, g, g + 4)
    return px


def dirt_set(seed):
    def px(t, x, y):
        v = h01(seed + t, x, y) * 30 - 15
        r = h01(seed + 101 + t, x, y)
        if r < 0.06:    # dark pebbles / roots
            v -= 40
        elif r < 0.10:  # light grit
            v += 30
        return (120 + v, 84 + v * 0.8, 55 + v * 0.6)
    return px


def grass_set(seed):
    def px(t, x, y):
        v = h01(seed + t, x, y) * 40 - 20
        if h01(seed + 101 + t, x, y) < 0.12:  # light blade tips
            v += 35
        return (70 + v * 0.6, 135 + v, 45 + v * 0.3)
    return px


def stone_brick_set(seed):
    """One dressed stone per face inside a one-texel mortar border (two
    texels between blocks). The joint already shows the block, so the tiles
    differ only subtly: a small tint shift and a few chips near the edge."""
    def px(t, x, y):
        if x in (0, N - 1) or y in (0, N - 1):
            v = 80 + h01(seed + t, x, y) * 10
            return (v, v - 2, v - 6)
        tint = (h01(seed + 7, t, 0) - 0.5) * 10
        v = 132 + tint + h01(seed + t, x, y) * 16 - 8
        edge = min(x, y, N - 1 - x, N - 1 - y)
        if edge == 1 and h01(seed + 31 + t, x, y) < 0.18:  # chipped arris
            v -= 26
        return (v, v - 1, v - 4)
    return px


# `matched` sets: content near each labelled edge depends only on that
# edge's label and is one continuous function across the seam (seam
# coordinates: d = 0 on the seam, increasing down / right), so same-label
# edges meet seamlessly by construction. Unlabelled edges are free.

def column_noise(seed, cell):
    """Noise varying smoothly along y (lattice every `cell` texels) and
    independently per column x: grain."""
    def at(x, d):
        f = (d + 0.5) / cell
        j = math.floor(f)
        t = smooth(f - j)
        return h01(seed, x & 0xFFFF, j & 0xFFFF) * (1 - t) + h01(seed, x & 0xFFFF, (j + 1) & 0xFFFF) * t
    return at


def bark_set(seed):
    """Log bark (`matched`, ns): grain along the image's columns, i.e. along
    the log's axis, continuous across the N and S edges (log on log). A tile
    is N label | S label << 1. Columns are independent, so E/W edges (side
    by side logs) are breaks, as they should be, and darken slightly toward
    them."""
    band = [column_noise(seed + 10 + label, 4) for label in (0, 1)]
    interior = [column_noise(seed + 20 + t, 4) for t in range(4)]
    reach = 6.0  # texels from the edge over which the band fades out

    def weight(d):  # 1 on the two rows at the seam, then fading
        a = abs(d)
        return 1.0 if a <= 1.0 else max(0.0, 1.0 - (a - 1.0) / (reach - 1.0)) ** 2

    def px(t, x, y):
        north, south = t & 1, (t >> 1) & 1
        wn, ws = weight(y + 0.5), weight(y + 0.5 - N)
        wi = max(0.0, 1.0 - wn - ws)
        f = (wn * band[north](x, y) + ws * band[south](x, y - N) + wi * interior[t](x, y)) / (wn + ws + wi)
        v = 78 + f * 40 + h01(seed + t, x, y) * 12
        if f < 0.32:  # dark furrows, continuous wherever the field is
            v -= 26
        if h01(seed + 40 + t, x, y) < 0.012:  # knots
            v -= 30
        # Slightly darker toward the E/W edges, the log's long corners, so
        # side-by-side logs read as separate logs (an aesthetic choice; a
        # bark block can give uniform bark across a wall later).
        v -= (36, 22, 10)[min(x, N - 1 - x)] if min(x, N - 1 - x) < 3 else 0
        return (v, v * 0.72, v * 0.48)
    return px


def gravel_set(seed):
    """Gravel (`matched`, nsew): pebbles of 3–5 texels on fine grit. Pebbles
    on an edge are a function of that edge's (axis, label) alone, centred on
    the seam, so same-label edges continue them; they stay out of the
    corners, which only plain grit fills (a corner is shared by four tiles
    that each know two of its edges). Tile = N | E << 1 | S << 2 | W << 3."""
    def pebbles(key, count, lo, hi, rmin=1.4, rmax=2.4):
        out = []
        for k in range(count):
            u = lo + h01(key, k, 1) * (hi - lo)
            r = rmin + h01(key, k, 2) * (rmax - rmin)
            shade = h01(key, k, 3)
            out.append((u, r, shade))
        return out

    # Edge pebbles per (axis, label): axis 0 = N/S edges (u along x), 1 = E/W.
    edge = {(a, l): pebbles(seed + 100 + 2 * a + l, 2, 4.5, 11.5) for a in (0, 1) for l in (0, 1)}
    inner = [[(2.5 + h01(seed + 200 + t, k, 0) * 11.0, 2.5 + h01(seed + 200 + t, k, 1) * 11.0,
               1.3 + h01(seed + 200 + t, k, 2) * 1.0, h01(seed + 200 + t, k, 3)) for k in range(3)]
             for t in range(16)]

    def pebble_colour(shade, dx, dy, r):
        base = 112 + shade * 60
        lit = -(dx + dy) / (2 * r) * 18  # lighter toward the top left
        rim = -20 if (dx * dx + dy * dy) ** 0.5 > r - 0.8 else 0
        v = base + lit + rim
        return (v, v * 0.94, v * 0.86)

    def px(t, x, y):
        cx, cy = x + 0.5, y + 0.5
        hits = []
        labels = {"n": t & 1, "e": (t >> 1) & 1, "s": (t >> 2) & 1, "w": (t >> 3) & 1}
        for side, axis, centre in (("n", 0, lambda u: (u, 0.0)), ("s", 0, lambda u: (u, float(N))),
                                   ("w", 1, lambda u: (0.0, u)), ("e", 1, lambda u: (float(N), u))):
            for u, r, shade in edge[(axis, labels[side])]:
                px0, py0 = centre(u)
                hits.append((px0, py0, r, shade))
        for px0, py0, r, shade in inner[t]:
            hits.append((px0, py0, r, shade))
        for px0, py0, r, shade in hits:
            dx, dy = cx - px0, cy - py0
            if dx * dx + dy * dy < r * r:
                return pebble_colour(shade, dx, dy, r)
        v = 92 + h01(seed + t, x, y) * 26
        return (v, v * 0.95, v * 0.88)
    return px


# --- connected set (glass frames) ---------------------------------------------------

def glass_frames(seed):
    alpha = round(0.85 * 255)

    def px(X, Y):
        t, x, y = (Y // N) * SET_COLS + X // N, X % N, Y % N
        band = ((t & N_EDGE and y == 0) or (t & S_EDGE and y == N - 1) or
                (t & W_EDGE and x == 0) or (t & E_EDGE and x == N - 1))
        v = 208 + h01(seed, X, Y) * 8
        return (v, v + 4, v + 8, alpha if band else 0)
    return px


def grate(seed):
    # The cutout test block (v0.2.5): an iron lattice, bars two texels wide
    # at the edges and through the middle, square holes between them fully
    # transparent (alpha 0; the G-buffer's alpha test discards below 0.5).
    bars = {0, 1, 7, 8, 14, 15}

    def px(x, y):
        if x not in bars and y not in bars:
            return (0, 0, 0, 0)
        v = 70 + h01(seed, x, y) * 18 + (10 if (x in (1, 8, 15) or y in (1, 8, 15)) else 0)  # a lit bevel
        return (v, v + 3, v + 8, 255)
    return px


# --- negative-test fixtures ---------------------------------------------------------

def big(seed):
    """A 32x32 texture: block textures must be 16x16."""
    return lambda x, y: (100 + h01(seed, x, y) * 40, 100, 100)


def full_atlas(seed):
    """A 64x64 atlas (16 tiles) declared as matched ns, which needs 4 (64x16)."""
    return lambda x, y: (90 + h01(seed, x, y) * 40, 90, 90)


# --- output -------------------------------------------------------------------------

def write_png(path, px, size):
    w, h = size
    raw = bytearray()
    channels = len(px(0, 0))
    for y in range(h):
        raw.append(0)  # filter: none
        for x in range(w):
            raw.extend(clamp8(c) for c in px(x, y))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    color_type = 6 if channels == 4 else 2
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, color_type, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


SQ = (N, N)
TEXTURES = [  # (file name, generator, (width, height))
    ("brick", brick, SQ),
    ("stone_set", tile_set(stone_set, 8), atlas_size(8)), ("dirt_set", tile_set(dirt_set, 8), atlas_size(8)),
    ("grass_set", tile_set(grass_set, 8), atlas_size(8)),
    ("stone_brick_set", tile_set(stone_brick_set, 4), atlas_size(4)),
    ("gravel_set", tile_set(gravel_set, 16), atlas_size(16)),
    ("log_end", log_end, SQ), ("bark_set", tile_set(bark_set, 4), atlas_size(4)),
    ("furnace_front", furnace("F", front_extra), SQ), ("furnace_back", furnace("B", plain), SQ),
    ("furnace_right", furnace("R", plain), SQ), ("furnace_left", furnace("L", plain), SQ),
    ("furnace_top", furnace("U", top_extra), SQ), ("furnace_bottom", furnace("D", plain), SQ),
    ("lamp", lamp, SQ), ("polished", polished, SQ), ("gold", gold, SQ),
    ("glass_frames", glass_frames, atlas_size(16)), ("glow", glow, SQ),
    ("grate", grate, SQ),  # last: every texture's seed is its position here
]

FIXTURES = {
    "not16": [("big", big, (2 * N, 2 * N))],
    "wrong_tiles": [("full_atlas", full_atlas, atlas_size(16))],
}


def write_all(out_dir, textures):
    os.makedirs(out_dir, exist_ok=True)
    for seed, (name, make, size) in enumerate(textures, start=1):
        write_png(os.path.join(out_dir, name + ".png"), make(seed * 7919), size)


if __name__ == "__main__":
    args = sys.argv[1:]
    if args[:1] == ["--fixtures"]:  # --fixtures NAME DIR
        write_all(args[2], FIXTURES[args[1]])
    else:
        write_all(args[0] if args else os.path.join("assets", "blocks"), TEXTURES)
