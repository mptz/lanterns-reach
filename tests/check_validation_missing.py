"""CTest (Debug builds): with every Vulkan layer disabled, the binary must
print `validation: off` plus an error and exit non-zero. This happens before
any window is created, so no display is needed.

usage: check_validation_missing.py <sandbox-binary> <scenario>
(the scenario runs one frame, so the check can never hang)
"""
import os
import subprocess
import sys


def main(binary, scenario):
    env = dict(os.environ, VK_LOADER_LAYERS_DISABLE="~all~")
    r = subprocess.run([binary, "--scenario", scenario], env=env, capture_output=True, text=True, timeout=60)
    out = r.stdout + r.stderr
    sys.stdout.write(out)
    if r.returncode == 0:
        print("FAIL: exited 0 without the validation layer")
        return 1
    if "[sandbox] validation: off" not in out:
        print("FAIL: did not print '[sandbox] validation: off'")
        return 1
    print(f"OK: exit status {r.returncode} with validation unavailable")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
