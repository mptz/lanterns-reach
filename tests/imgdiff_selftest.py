#!/usr/bin/env python3
"""Self-test of tools/imgdiff.py's visible-difference gate (--tolerant 1
--cluster 4, RELEASES.md v0.2.5) on synthetic images: a texel seam moved by
a pixel and isolated pinholes pass; a wrong block, a crack line and a wrong
tint fail; a mask hides what it covers. Standard library only."""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "imgdiff.py")
W, H = 96, 64


def write_png(path, px):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(v for x in range(W) for v in px[y][x]) for y in range(H))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def texture(seam=0):
    """Texels 4 pixels wide in two colours, every seam shifted by `seam` pixels."""
    return [[(200, 40, 40) if ((x + seam) // 4 + y // 4) % 2 else (40, 40, 200) for x in range(W)] for y in range(H)]


def run(a, b, *flags):
    return subprocess.run([sys.executable, "-I", TOOL, a, b, "--tolerant", "1", "--cluster", "4", *flags],
                          capture_output=True, text=True).returncode


def main():
    failures = 0
    with tempfile.TemporaryDirectory() as d:
        ref = os.path.join(d, "ref.png")
        write_png(ref, texture())

        def case(name, px, want, *flags):
            nonlocal failures
            path = os.path.join(d, name + ".png")
            write_png(path, px)
            got = run(ref, path, *flags)
            if got != want:
                print(f"FAIL {name}: exit {got}, want {want}")
                failures += 1
            else:
                print(f"PASS {name}")

        case("identical", texture(), 0)
        case("seam moved by a pixel", texture(1), 0)
        pin = texture()
        for x, y in ((10, 10), (50, 30), (80, 50)):
            pin[y][x] = (0, 0, 0)
        case("isolated pinholes", pin, 0)
        block = texture()
        for y in range(20, 28):
            for x in range(40, 48):
                block[y][x] = (40, 200, 40)
        case("wrong block", block, 1)
        crack = texture()
        for x in range(20, 60):
            crack[33][x] = (10, 10, 10)
        case("crack line", crack, 1)
        tint = [[(r, g + 8, b) for r, g, b in row] for row in texture()]
        case("wrong tint", tint, 1)
        case("masked block", block, 0, "--mask", "36,16,52,32")
    print("imgdiff selftest: " + ("all passed" if failures == 0 else f"{failures} failed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
