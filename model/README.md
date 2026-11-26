# model/

TLA+ models of engine protocols, checked with TLC. Each model has a spec
(`<Name>.tla`), configurations (`<Name>.cfg`, `<Name>_<Variant>.cfg`), a run
script and a results file. The design docs are authoritative; a model checks
them and records what it found.

| Model | What it checks | Results |
|---|---|---|
| `QSBR.tla` | Global quiescent-state-based reclamation as specified in `design/dispenser.md` (Reclamation; `invariant/dispenser/ownership-and-borrowing`), with `design/concurrency.md`'s pointer rules other than `invariant/concurrency/dependency-pointers`, and `invariant/frame-thread/nothing-outlives-frame`. Workers and the frame thread are participants, each with one counter word (offline or a copy of the grace-period counter G) updated at its quiescent point; `Dispenser::nextBatch()` makes the opportunistic attempt (CAS on G, then a scan), `DispenserSource::pullBatch(Budget)` hands out runnable cleanups, the ticker source's turn or ordinary work, and `retire(CleanupActor&)` tags a cleanup t = G+1 after the unlink. Checks: no participant holds a freed object; every registered cleanup eventually runs; no permanent stall when every worker sleeps; variants that should fail do. | `QSBR-RESULTS.md` |
| `GoalSource.tla` | GoalSource as specified in `design/goal-source.md` and `proposals/goal-source-scenarios.md`: goal-actor states and transitions under one mutex, the withdrawn flag, the cascade, no batching (claim and turn start are separate steps), edges kept until terminal, `withdraw(span)` then cleanup, `done()` fired after the mutex, reclamation abstracted (a cleanup waits for the turns in progress at its registration). Checks A1-A7, B1-B5, C1-C6 on chain, siblings, diamond and cross-client shapes with 2 workers, F1-F4 on small shapes; variants that should fail do (no cascade for a Completed actor, no withdrawn check at registration, edges removed at claim with a returned batch, no withdrawn check at claim, publishing despite withdrawn, cleanup before withdrawal), and probes show the interesting states are reachable. | `GOALSOURCE-RESULTS.md` |

## Running

Needs Java (21 was used) and `tla2tools.jar` (1.7.4, TLC 2.19, was used).

```sh
TLA2TOOLS=/path/to/tla2tools.jar model/run_qsbr.sh            # default sweep
TLA2TOOLS=/path/to/tla2tools.jar model/run_qsbr.sh QSBR_TagG  # one config
TLA2TOOLS=/path/to/tla2tools.jar TLC_HEAP=4g model/run_qsbr.sh QSBR_K2  # a large config
```

For example, `TLA2TOOLS=$HOME/.kiro/crew/scratch/runtime-ab95d884/tla/tla2tools.jar`.
Runs are one at a time with two TLC workers. The heap defaults to `-Xmx2g`
(`TLC_HEAP`) and direct memory is left at the JVM default unless
`TLC_DIRECT` is set (for example `TLC_DIRECT=1g`). TLC output goes to `$OUT`
(default `${KIROCREW_SCRATCH:-${TMPDIR:-/tmp}}/qsbr-tlc`). The script prints
one line per configuration: verdict, states, depth and time. Each
configuration's first comment says what it is; `QSBR-RESULTS.md` gives the
expected verdicts. `QSBR_Probes.cfg` is a template, run once per non-vacuity
probe as `QSBR_Probe<Name>`.

The default sweep (no arguments) runs every `QSBR*.cfg` except the large
configurations `QSBR_K2` (K = 2) and `QSBR_N3` (N = 3), which are run by name
with `TLC_HEAP=4g` (and `TLC_DIRECT=1g`) on a host with the memory to spare.
GoalSource: `TLA2TOOLS=/path/to/tla2tools.jar model/run_goalsource.sh [config ...]`
runs the configurations one at a time under a watchdog (kills TLC above
`MAX_RSS_KB`, default 6 GB, when available memory falls below `MIN_AVAIL_MB`,
default 1500, or after `MAX_SECS`, default 7200; a killed run is reported as
not claimed). Raw output of the recorded runs is in `model/goalsource-out/`.
Use `TLC_HEAP=4g TLC_DIRECT=1g` for the large configurations, and for the
single-property liveness runs on cross and chain-with-mesh pass
`TLC_ARGS='-lncheck final'` (extra TLC arguments).
