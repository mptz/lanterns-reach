"""CTest (v0.1.6 carry-over): nothing outside the grid itself, the world
setup and the F10 bounds overlay knows about the dense grid. Every other use
of the world goes through WorldQuery.

Allowed: src/grid_world.{h,cpp} (the implementation), src/app.cpp (creates
it and applies the scenario's scene through WorldQuery), src/dev_overlays.*
(F10 draws its bounds), and tests/. Within those, `DenseGrid` may appear
only on the include, the construction line and boundsOverlay's signature.

usage: check_grid_confined.py <repo-root>
"""
import os
import re
import sys


def main(root):
    bad = []
    src = os.path.join(root, "src")
    for name in sorted(os.listdir(src)):
        if name in ("grid_world.h", "grid_world.cpp"):
            continue
        text = open(os.path.join(src, name), encoding="utf-8").read()
        hits = [i + 1 for i, line in enumerate(text.splitlines())
                if re.search(r'\bDenseGrid\b|"grid_world\.h"', line)]
        if not hits:
            continue
        if name not in ("app.cpp", "dev_overlays.h", "dev_overlays.cpp"):
            bad.append(f"src/{name}: lines {hits}")
            continue
        lines = text.splitlines()
        for n in hits:
            line = lines[n - 1]
            allowed = ('#include "grid_world.h"' in line or "DenseGrid grid(" in line
                       or "void boundsOverlay(const DenseGrid&" in line)
            if not allowed:
                bad.append(f"src/{name}:{n}: {line.strip()}")
    if bad:
        print("DenseGrid used outside setup / F10:\n  " + "\n  ".join(bad))
        return 1
    print("OK: DenseGrid confined to grid_world.*, app.cpp setup and F10")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
