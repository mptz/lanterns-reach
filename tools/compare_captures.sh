#!/usr/bin/env bash
# Compares a directory of captures (tools/headless_check.sh output) with a
# reference directory, capture by capture (RELEASES.md v0.2.5).
#
# usage: tools/compare_captures.sh REF_DIR NEW_DIR
#
# Each capture is reported as identical, as differing invisibly (every
# differing pixel's colour is within a pixel in the other image, or the
# pixels left form no cluster of 4 or more: imgdiff.py --tolerant 1
# --cluster 4), or as VISIBLE, with its largest clusters. Exact pixel
# counts are always printed: identity is still the information a
# regression shows first. The dev panels with live text (F1's frame
# times, F7's input ages) are masked in the captures that show them.
# Exits 1 if any capture differs visibly or is missing.
set -uo pipefail
REF="$1"
NEW="$2"
DIFF="$(dirname "$0")/imgdiff.py"

masks_for() {  # capture name -> imgdiff mask arguments
    case "$1" in
        lit|lighting|overlays|resize) echo "--mask 0,0,440,280" ;;  # F1 panel, top left
        hotbar) echo "--mask 0,320,260,500" ;;                       # F7 panel, left
        *) echo "" ;;
    esac
}

one() {  # capture file name
    local n="${1%.png}" out
    if [ ! -f "$NEW/$1" ]; then echo "MISSING $n"; return 1; fi
    # shellcheck disable=SC2046
    if out=$(python3 -I "$DIFF" "$REF/$1" "$NEW/$1" $(masks_for "$n") --tolerant 1 --cluster 4); then
        case "$out" in
            "max per-channel difference 0/255"*) echo "identical $n" ;;
            *) echo "invisible $n: $(echo "$out" | head -1)" ;;
        esac
    else
        echo "VISIBLE $n: $(echo "$out" | sed 's/^  //' | paste -sd'|' | sed 's/|/ | /g')"
        return 1
    fi
}
export -f one masks_for
export REF NEW DIFF

fail=0
for f in "$REF"/*.png; do basename "$f"; done | xargs -P 4 -I{} bash -c 'one {}' | sort || fail=1
exit $fail
