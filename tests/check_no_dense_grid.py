"""CTest (v0.2.1): DenseGrid, v0.1's throwaway world, no longer exists. The
world is a chunk (src/chunk_world.*), and `world.grid` is rejected by the
scenario parser (tested in sandbox_tests).

Fails if `DenseGrid` or grid_world.* appears under src/ or tests/ (this
script excepted), or in CMakeLists.txt.

usage: check_no_dense_grid.py <repo-root>
"""
import os
import re
import sys

PATTERN = re.compile(r"\bDenseGrid\b|grid_world")


def main(root):
    bad = []
    files = [os.path.join(root, "CMakeLists.txt")]
    for sub in ("src", "tests"):
        for dirpath, _, names in os.walk(os.path.join(root, sub)):
            files += [os.path.join(dirpath, n) for n in names if n.endswith((".h", ".cpp", ".py", ".txt"))]
    for path in sorted(files):
        if os.path.abspath(path) == os.path.abspath(__file__):
            continue
        with open(path, encoding="utf-8") as f:
            for i, line in enumerate(f, 1):
                if PATTERN.search(line):
                    bad.append(f"{os.path.relpath(path, root)}:{i}: {line.strip()}")
    if bad:
        print("DenseGrid still referenced:\n  " + "\n  ".join(bad))
        return 1
    print("OK: no DenseGrid in src/, tests/ or CMakeLists.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
