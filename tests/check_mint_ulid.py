"""CTest (v0.2.4): the ULID minting tool. ULIDs are well formed, distinct and
monotonic, even within one millisecond; --add gives a ULID only to
definitions without one, is idempotent, and leaves every other byte of the
file as it was.

usage: check_mint_ulid.py <tools/mint_ulid.py> <scratch dir>
"""
import importlib.util
import os
import re
import shutil
import sys

ULID = re.compile(r"^[0-7][0-9A-HJKMNP-TV-Z]{25}$")


def load(path):
    spec = importlib.util.spec_from_file_location("mint_ulid", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main(tool, scratch):
    m = load(tool)
    failures = []

    def check(cond, what):
        if not cond:
            failures.append(what)

    # Well formed, distinct, monotonic: a real clock, and a frozen one (all in one millisecond).
    for clock in (None, lambda: 1_700_000_000_000):
        minter = m.Minter(clock=clock)
        ids = [minter.mint() for _ in range(5000)]
        check(all(ULID.match(i) for i in ids), "malformed ULID")
        check(len(set(ids)) == len(ids), "duplicate ULID")
        check(ids == sorted(ids), "ULIDs not monotonic")
    # The timestamp is in the top 48 bits.
    frozen = m.Minter(clock=lambda: 1, randbits=lambda: 0).mint()
    check(frozen == m.encode(1 << 80), f"timestamp placement: {frozen}")
    check(m.encode(0) == "0" * 26 and m.encode((1 << 128) - 1) == "7" + "Z" * 25, "encoding edges")

    # --add on copies of definitions: one without a ULID, one with.
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch)
    without = os.path.join(scratch, "without.json")
    with_one = os.path.join(scratch, "with.json")
    original = '{ "name": "x", "symmetry": "uniform",\n  "textures": { "+z": "x.png" } }\n'
    existing = '{ "ulid": "01JDQ0000000000000000000AB", "name": "y" }\n'
    with open(without, "w") as f:
        f.write(original)
    with open(with_one, "w") as f:
        f.write(existing)
    minter = m.Minter()
    check(m.add(without, minter) and not m.add(with_one, minter), "--add chose the wrong files")
    text = open(without).read()
    found = re.search(r'"ulid": "([^"]+)",', text)
    check(found is not None and ULID.match(found.group(1)) is not None, "--add wrote no well-formed ULID")
    check(text.replace(f' "ulid": "{found.group(1)}",' if found else "", "", 1) == original, "--add changed other bytes")
    check(open(with_one).read() == existing, "--add touched a file that had a ULID")
    check(not m.add(without, minter) and open(without).read() == text, "--add is not idempotent")

    if failures:
        print("FAIL:\n  " + "\n  ".join(failures))
        return 1
    print("OK: ULIDs well formed, distinct and monotonic; --add inserts only, once")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
