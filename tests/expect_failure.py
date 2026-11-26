"""CTest helper: run a command that must FAIL (non-zero exit) and must say
why (the expected text appears in its output).

usage: expect_failure.py <expected-substring> <command> [args...]
"""
import subprocess
import sys


def main(expected, cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    out = r.stdout + r.stderr
    sys.stdout.write(out)
    if r.returncode == 0:
        print("FAIL: command succeeded but was expected to fail")
        return 1
    if expected not in out:
        print(f"FAIL: output does not mention {expected!r}")
        return 1
    print(f"OK: rejected (exit {r.returncode})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2:]))
