#!/usr/bin/env bash
# A poor man's profiler for a slow run, with nothing but gdb: starts the
# command, takes N stack samples of it a second or so apart, and prints which
# functions the samples were in (the innermost frame, and the innermost frame
# that is our own code), most frequent first. Build with debug info (Debug).
#
# usage: tools/sample_stacks.sh [-n samples] -- command args...
#   tools/sample_stacks.sh -n 30 -- build-debug/sandbox_tests --only-equivalence
set -euo pipefail
N=20
if [ "${1:-}" = "-n" ]; then N=$2; shift 2; fi
[ "${1:-}" = "--" ] && shift
"$@" >/dev/null 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null || true' EXIT
inner=$(mktemp); ours=$(mktemp)
for ((i = 0; i < N; i++)); do
    kill -0 $PID 2>/dev/null || break
    bt=$(gdb -p $PID -batch -ex "bt 40" 2>/dev/null | grep -E '^#' || true)
    [ -z "$bt" ] && continue
    echo "$bt" | head -1 | sed -E 's/^#[0-9]+ +(0x[0-9a-f]+ in )?//; s/ \(.*//' >>"$inner"
    echo "$bt" | grep -E '/home/' | head -1 | sed -E 's/^#[0-9]+ +(0x[0-9a-f]+ in )?//; s/ \(.*//' >>"$ours"
    sleep 0.5
done
echo "innermost frame:"; sort "$inner" | uniq -c | sort -rn | head -12
echo; echo "innermost frame in our code:"; sort "$ours" | uniq -c | sort -rn | head -12
rm -f "$inner" "$ours"
