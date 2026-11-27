#!/usr/bin/env python3
"""Mint ULIDs (data/stable-identity): a 48-bit millisecond timestamp from the
real clock, then 80 random bits from the system, written as 26 characters of
Crockford base32. Within one millisecond the random part is incremented, so
ULIDs from one run sort in the order they were minted. Authoring tools use the
real clock; simulation never mints this way. Standard library only.

usage:
  mint_ulid.py [N]            print N new ULIDs (default 1), one per line
  mint_ulid.py --add FILE...  give each JSON block definition with no "ulid"
                              one, inserted as the first field; every other
                              byte of the file is left as it was, and a file
                              that already has a "ulid" is not touched
"""
import os
import re
import sys
import time

ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"  # Crockford base32: no I, L, O, U


def encode(value):
    """A 128-bit integer as 26 characters, most significant first."""
    return "".join(ALPHABET[(value >> (5 * (25 - i))) & 31] for i in range(26))


class Minter:
    def __init__(self, clock=None, randbits=None):
        self.clock = clock or (lambda: int(time.time() * 1000))
        self.randbits = randbits or (lambda: int.from_bytes(os.urandom(10), "big"))
        self.last_ms = -1
        self.last_random = 0

    def mint(self):
        ms = self.clock() & ((1 << 48) - 1)
        if ms == self.last_ms:
            self.last_random += 1
            if self.last_random >> 80:  # 2^80 in one millisecond: wait for the next
                while self.clock() == ms:
                    pass
                return self.mint()
        else:
            self.last_ms, self.last_random = ms, self.randbits()
        return encode((ms << 80) | self.last_random)


HAS_ULID = re.compile(r'"ulid"\s*:')
OPENING = re.compile(r"\{")


def add(path, minter):
    """Insert "ulid" as the first field; returns whether the file changed."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if HAS_ULID.search(text):
        return False
    m = OPENING.search(text)
    if not m:
        raise ValueError(f"{path}: no JSON object")
    text = text[: m.end()] + f' "ulid": "{minter.mint()}",' + text[m.end():]
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return True


def main(argv):
    minter = Minter()
    if argv and argv[0] == "--add":
        for path in argv[1:]:
            if add(path, minter):
                print(f"added a ULID to {path}")
        return 0
    n = int(argv[0]) if argv else 1
    for _ in range(n):
        print(minter.mint())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
