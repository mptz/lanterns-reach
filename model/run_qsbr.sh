#!/usr/bin/env bash
# Check QSBR configurations and print one summary line each.
#
# Usage:
#   TLA2TOOLS=/path/to/tla2tools.jar model/run_qsbr.sh [config-name ...]
# e.g.
#   TLA2TOOLS=$HOME/.kiro/crew/scratch/runtime-ab95d884/tla/tla2tools.jar model/run_qsbr.sh
#
# With no arguments it runs every QSBR*.cfg except the large ones in LARGE;
# QSBR_Probes.cfg is run once per probe (QSBR_Probe<Name>). Pass config names
# to run a subset; the large ones are run by name, with more heap:
#   TLC_HEAP=4g TLC_DIRECT=1g model/run_qsbr.sh QSBR_K2
#
# Environment:
#   TLC_HEAP    JVM heap (-Xmx), default 2g.
#   TLC_DIRECT  JVM direct memory (-XX:MaxDirectMemorySize), default unset
#               (the JVM's own default).
#   OUT         TLC output directory, default
#               ${KIROCREW_SCRATCH:-${TMPDIR:-/tmp}}/qsbr-tlc.
set -u
JAR=${TLA2TOOLS:?set TLA2TOOLS to the path of tla2tools.jar}
cd "$(dirname "$0")"
OUT=${OUT:-${KIROCREW_SCRATCH:-${TMPDIR:-/tmp}}/qsbr-tlc}
mkdir -p "$OUT"

JVM=(-Xmx"${TLC_HEAP:-2g}")
if [ -n "${TLC_DIRECT:-}" ]; then JVM+=(-XX:MaxDirectMemorySize="$TLC_DIRECT"); fi
JVM+=(-XX:+UseParallelGC)

PROBES=(ProbeWorkerHoldsRetired ProbeFrameHoldsRetired ProbeCasLost
        ProbeOfflineSkipped ProbeStaleStore ProbeFrameStaleStore)
LARGE=(QSBR_K2 QSBR_N3)

cfgs=("$@")
if [ ${#cfgs[@]} -eq 0 ]; then
  for f in QSBR*.cfg; do
    c=${f%.cfg}
    if [[ " ${LARGE[*]} " == *" $c "* ]]; then continue; fi
    if [ "$c" = QSBR_Probes ]; then
      for p in "${PROBES[@]}"; do cfgs+=("QSBR_$p"); done
    else
      cfgs+=("$c")
    fi
  done
fi

for c in "${cfgs[@]}"; do
  cfg="$c.cfg"
  if [[ $c == QSBR_Probe* ]]; then
    cfg="$OUT/$c.cfg"
    sed "s/^INVARIANTS PROBE$/INVARIANTS ${c#QSBR_}/" QSBR_Probes.cfg > "$cfg"
  fi
  start=$(date +%s)
  java "${JVM[@]}" -cp "$JAR" tlc2.TLC -workers 2 \
       -metadir "$OUT/meta-$c" -config "$cfg" QSBR.tla > "$OUT/$c.out" 2>&1
  secs=$(( $(date +%s) - start ))
  verdict=$(grep -m1 -oE "No error has been found|Invariant [A-Za-z0-9]+ is violated|Temporal properties were violated|Deadlock reached|Error:.*" "$OUT/$c.out")
  states=$(grep -oE "[0-9]+ states generated, [0-9]+ distinct states found" "$OUT/$c.out" | tail -1)
  depth=$(grep -oE "depth of the complete state graph search is [0-9]+" "$OUT/$c.out" | grep -oE "[0-9]+$")
  echo "$c | $verdict | $states | depth ${depth:--} | ${secs}s"
done
