# sim/execution-alternatives — Approaches weighed for cross-chunk execution

Not plan of record. This table was `sim/execution-alternatives` in
`design/sim-execution.md`: the approaches weighed for the step/reconcile
plan (`sim/execution-plan`), which `sim/concurrency-model`
(`design/concurrency.md`) replaced. Kept for future mining: several rows
(symmetric face fluxes, hierarchical colouring, faces-only forwarding for
distributed servers) may suit a subsystem or a later design. The Status
column is as recorded when the table was current.

| Approach | Pros | Cons | Status |
|---|---|---|---|
| **Two-phase buffered exchange, 26 directed-pair outboxes** | Every chunk parallel in every phase; owner-only mutation fits `core/opaque-chunk-queries`; no locks; idle costs nothing; chunks free to choose internal strategy | Cross-chunk effects need a protocol (deltas, bounces); one step of latency for face-crossing losers; effects with discovered write sets are awkward | Superseded 2026-11-05 (`sim/concurrency-model`) |
| **Three-phase (with outcome round)** | Sender learns its outcome the same step; no items in transit | One more phase per step | Superseded with it |
| **Pure gather with symmetric face fluxes** (each side computes the flux across a shared face from the same halo data, `f(a,b) = −f(b,a)` in integers, and applies only its own half) | No write buffers at all for radius-1 quantities; conservation by construction | Only works where the flux depends on the boundary layer alone; flux computed twice; discrete moves still need outboxes; deeper reads would break `core/opaque-chunk-queries`'s size rule | Usable inside a subsystem if convenient |
| **8-colour 2×2×2-chunk executors** (hierarchy level 1) | Plain sequential code may mutate neighbours live; write sets can be discovered while running; no latency | 8 phases per step, 1/8 of executors per phase, per-phase load imbalance; requires a mutating external chunk API (breaks I1) and quiescent chunks | Fallback, e.g. inside `sim/regions` |
| **27-colour single chunks** | Simple unit (one chunk) | 27 phases, 1/27 parallelism per phase | Rejected (strictly worse than 2×2×2 executors) |
| **Hierarchical colouring** (executors of side 2, 4, 8…; spill S/2) | Handles spill beyond one chunk; small effects never escalate for straddling a grid line | Parallelism falls 8× per level; cliff to global | Candidate mechanism for `sim/regions` |
| **Face-only "plus" colouring** (7 colours, `(x+2y+3z) mod 7`) | Fewest colours for single chunks | Only valid if writes never cross edges/corners: forces 6-connectivity on every rule | Rejected as general; only if a subsystem is 6-connected anyway |
| **Faces-only buffers with x→y→z forwarding** | Only 6 messages per chunk | 3 sequential sweeps per reconcile; pays only when messages are expensive (networks) | Rejected for shared memory; revisit for distributed servers |
| **Per-chunk locks in a global order** | Familiar; deadlock-free by ordering | Contention at seams; order and timing leak into results; locks in the hot path | Rejected |
| **Optimistic (speculate, validate, retry)** | Cheap when conflicts are rare | Retry cost is unbounded and timing-dependent; complex to make deterministic | Rejected as default; the Vicinity store may use it (STM) for cross-chunk transactions |
| **Atomic shared accumulators** | Trivial for quantities | Contention on hot targets; doesn't handle discrete conflicts | Rejected |
| **Dynamic regions (Folia-style)** | Coarse, proven for "mostly local, a few hotspots" | Merge/split policy, load balance | **Adopted in spirit 2026-11-05:** Vicinities as merge-and-split transaction domains (`sim/concurrency-model`) |
| **Single-threaded sim** | Simplest | Exactly what we're avoiding (parallel gen, serial sim) | Rejected |
| **Thread per subsystem** | Simple ownership | Doesn't scale with cores; subsystems couple anyway | Rejected (`sim/cpu-parallelism`) |
