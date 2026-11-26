#!/usr/bin/env bash
# Check GoalSource configurations and print one summary line each.
#
# Usage:
#   TLA2TOOLS=/path/to/tla2tools.jar model/run_goalsource.sh [config-name ...]
#
# With no arguments it runs every GoalSource*.cfg except the ones in LARGE
# (run those by name); GoalSource_Probes are listed in PROBES. Configurations
# run one at a time (never two TLC JVMs at once), each under a watchdog.
#
# Environment:
#   TLC_HEAP     JVM heap (-Xmx), default 2g.
#   TLC_DIRECT   JVM direct memory (-XX:MaxDirectMemorySize), default unset.
#   OUT          TLC output directory, default
#                ${KIROCREW_SCRATCH:-${TMPDIR:-/tmp}}/goalsource-tlc.
#   MAX_RSS_KB   watchdog: kill TLC above this resident size, default 6291456.
#   MIN_AVAIL_MB watchdog: kill TLC when available memory falls below this,
#                default 1500.
#   MAX_SECS     watchdog: wall-clock cap per run, default 7200.
#   TLC_ARGS     extra TLC options, e.g. "-lncheck final" (check liveness
#                once, after the state space is explored), default unset.
# A run the watchdog kills prints "killed (<reason>)" and its last progress
# line; such a run proves nothing and is not retried with a larger heap.
set -u
JAR=${TLA2TOOLS:?set TLA2TOOLS to the path of tla2tools.jar}
cd "$(dirname "$0")"
OUT=${OUT:-${KIROCREW_SCRATCH:-${TMPDIR:-/tmp}}/goalsource-tlc}
mkdir -p "$OUT"
MAX_RSS_KB=${MAX_RSS_KB:-6291456}
MIN_AVAIL_MB=${MIN_AVAIL_MB:-1500}
MAX_SECS=${MAX_SECS:-7200}

JVM=(-Xmx"${TLC_HEAP:-2g}")
if [ -n "${TLC_DIRECT:-}" ]; then JVM+=(-XX:MaxDirectMemorySize="$TLC_DIRECT"); fi
JVM+=(-XX:+UseParallelGC)

LARGE=()

cfgs=("$@")
if [ ${#cfgs[@]} -eq 0 ]; then
  for f in GoalSource*.cfg; do
    c=${f%.cfg}
    if [[ " ${LARGE[*]:-} " == *" $c "* ]]; then continue; fi
    cfgs+=("$c")
  done
fi

for c in "${cfgs[@]}"; do
  if pgrep -f "^java.*tlc2.TLC" > /dev/null; then echo "$c | another TLC is running; skipped"; continue; fi
  rm -rf "$OUT/meta-$c"
  start=$(date +%s)
  # shellcheck disable=SC2086
  java "${JVM[@]}" -cp "$JAR" tlc2.TLC -workers 2 ${TLC_ARGS:-} \
       -metadir "$OUT/meta-$c" -config "$c.cfg" GoalSource.tla > "$OUT/$c.out" 2>&1 &
  pid=$!
  peak=0; why=""
  while kill -0 "$pid" 2>/dev/null; do
    sleep 2
    rss=$(awk '/^VmRSS/ {print $2}' "/proc/$pid/status" 2>/dev/null || echo 0)
    rss=${rss:-0}
    [ "$rss" -gt "$peak" ] && peak=$rss
    avail=$(free -m | awk '/^Mem:/ {print $7}')
    now=$(( $(date +%s) - start ))
    if   [ "$rss" -gt "$MAX_RSS_KB" ];   then why="RSS ${rss} KB"
    elif [ "$avail" -lt "$MIN_AVAIL_MB" ]; then why="available ${avail} MB"
    elif [ "$now" -gt "$MAX_SECS" ];     then why="${now}s cap"; fi
    if [ -n "$why" ]; then
      kill -TERM "$pid" 2>/dev/null; sleep 10; kill -KILL "$pid" 2>/dev/null; break
    fi
  done
  wait "$pid" 2>/dev/null
  secs=$(( $(date +%s) - start ))
  rm -rf "$OUT/meta-$c"
  if [ -n "$why" ]; then
    last=$(grep -E "states generated|Progress" "$OUT/$c.out" | tail -1)
    echo "$c | killed ($why) | not claimed | last: ${last:--} | ${secs}s | peak $((peak/1024)) MB"
    continue
  fi
  verdict=$(grep -m1 -oE "No error has been found|Invariant [A-Za-z0-9]+ is violated|Temporal properties were violated|Action property [A-Za-z0-9]+ is violated|Deadlock reached|Error:.*" "$OUT/$c.out")
  states=$(grep -oE "[0-9]+ states generated, [0-9]+ distinct states found" "$OUT/$c.out" | tail -1)
  depth=$(grep -oE "depth of the complete state graph search is [0-9]+" "$OUT/$c.out" | grep -oE "[0-9]+$")
  [ -z "$depth" ] && depth=$(grep -oE "The depth of the counterexample|State [0-9]+:" "$OUT/$c.out" | tail -1)
  echo "$c | $verdict | $states | depth ${depth:--} | ${secs}s | peak $((peak/1024)) MB"
done
