#!/usr/bin/env bash
# Headless smoke test on a software Vulkan driver (Mesa lavapipe) + Xvfb
# (a window still needs a display; images come from the engine's --capture).
# Runs every scenario under scenarios/headless/ (the buffer views, overlays,
# scripted interaction, translucency, previews, tools, textures, a resize),
# the order-invariance, determinism and merged-quad comparisons, the
# Beer–Lambert check, and (Debug) the
# missing-validation-layer check, screenshotting the window mid-run. Every
# run must exit 0 (no validation errors, incl. sync validation).
#
# usage: tools/headless_check.sh [build-dir] [out-dir]
set -euo pipefail
BIN="${1:-build-debug}/sandbox"
OUT="${2:-build-debug/headless}"
mkdir -p "$OUT"
export VK_DRIVER_FILES="${VK_DRIVER_FILES:-/usr/share/vulkan/icd.d/lvp_icd.json}"

DISP=:97
Xvfb "$DISP" -screen 0 1600x1000x24 >/dev/null 2>&1 &
XPID=$!
trap 'kill $XPID 2>/dev/null || true' EXIT
sleep 1
export DISPLAY=$DISP
# GLFW prefers Wayland whenever a compositor is reachable; force X11 on Xvfb.
export SANDBOX_GLFW_PLATFORM=x11

fail=0
SCN="$(dirname "$0")/../scenarios"

# One shot: a scenario (scenarios/headless/NAME.json) run until its last
# event settles (the engine's rule: 20 frames past the last event, 60 past a
# resize, at least 60; or the scenario's `frames`), writing its own final
# image (--capture, UI included). Validation covers every frame that runs.
shot() {  # name [capture-name]
    local name=$1 out=${2:-$1}
    if "$BIN" --scenario "$SCN/headless/$name.json" --capture "$OUT/$out.png" >"$OUT/$out.log" 2>&1; then echo "PASS $out"
    else echo "FAIL $out (see $OUT/$out.log)"; fail=1; fi
}
compare() {  # label, a, b, max
    if python3 -I "$(dirname "$0")/imgdiff.py" "$2" "$3" --max "$4" >"$OUT/diff.txt"; then echo "PASS $1: $(cat "$OUT/diff.txt")"
    else echo "FAIL $1: $(cat "$OUT/diff.txt")"; fail=1; fi
}

# The views, overlays, scripted interaction (v0.1.5), translucency (v0.1.6),
# previews and tools (v0.1.7), textures, glass and the container patch
# (v0.1.8): every scenario under scenarios/headless/ except the comparison
# sets below, in name order.
for f in "$SCN"/headless/*.json; do
    name=$(basename "$f" .json)
    case "$name" in order_*|ghost_order_*|variant_run|quad_*) continue ;; esac
    shot "$name"
done

# Variant choice is deterministic: two runs, identical. Selection is per
# fragment: one merged quad over 4×4 stone cells looks exactly like the 16
# faces it replaces.
shot variant_run variant_run_a
shot variant_run variant_run_b
for q in quad_cells_v1 quad_merged_v1 quad_cells_v7 quad_merged_v7; do shot "$q"; done
compare "variant runs a vs b" "$OUT/variant_run_a.png" "$OUT/variant_run_b.png" 0
compare "merged quad (variant view)" "$OUT/quad_cells_v7.png" "$OUT/quad_merged_v7.png" 0
compare "merged quad (lit)" "$OUT/quad_cells_v1.png" "$OUT/quad_merged_v1.png" 1

# Order invariance (v0.1.6): forward / reverse / three shuffles of the
# translucent faces agree to within 1/255 per channel, and with preview
# ghosts in view (v0.1.7).
for o in forward reverse shuffle_1 shuffle_2 shuffle_3; do shot "order_$o"; done
for o in forward reverse; do shot "ghost_order_$o"; done
compare "ghost order forward vs reverse" "$OUT/ghost_order_forward.png" "$OUT/ghost_order_reverse.png" 1
for o in reverse shuffle_1 shuffle_2 shuffle_3; do compare "order forward vs $o" "$OUT/order_forward.png" "$OUT/order_$o.png" 1; done

# Absorbance matches Beer–Lambert, read back from the GPU: the v0.1 scene's
# checks.absorbance cases.
if "$BIN" --scenario "$SCN/v0.1-scene.json" --check-absorbance >"$OUT/absorbance.log" 2>&1; then
    echo "PASS absorbance ($(grep -c 'PASS' "$OUT/absorbance.log") checks)"
else echo "FAIL absorbance (see $OUT/absorbance.log)"; fail=1; fi

# Debug builds must refuse to run without the validation layer.
if grep -q 'validation: on' "$OUT/lit.log"; then
    if VK_LOADER_LAYERS_DISABLE='~all~' "$BIN" --scenario "$SCN/one-frame.json" >"$OUT/novalidation.log" 2>&1; then
        echo "FAIL novalidation (exited 0 without the layer)"; fail=1
    elif grep -q 'validation: off' "$OUT/novalidation.log"; then echo "PASS novalidation"
    else echo "FAIL novalidation (no 'validation: off')"; fail=1; fi
fi
exit $fail
