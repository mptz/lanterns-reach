"""CTest: gen_block_texture.py creates its (nested, absent) output directory
and reproduces the committed assets byte for byte.

usage: check_gen_block_texture.py <gen_block_texture.py> <assets-dir> <scratch-dir>
"""
import filecmp
import os
import shutil
import subprocess
import sys


def main(tool, assets, scratch):
    shutil.rmtree(scratch, ignore_errors=True)
    out = os.path.join(scratch, "nested", "out")  # does not exist yet
    subprocess.run([sys.executable, "-I", tool, out], check=True)
    made = sorted(n for n in os.listdir(out) if n.endswith(".png"))
    committed = sorted(n for n in os.listdir(assets) if n.endswith(".png"))
    if made != committed:
        print("generated set", made, "differs from committed set", committed)
        return 1
    bad = [n for n in made if not filecmp.cmp(os.path.join(out, n), os.path.join(assets, n), shallow=False)]
    if bad:
        print("differs from committed assets:", ", ".join(bad))
        return 1
    print("gen_block_texture: output directory created; all textures match the committed assets")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:4]))
