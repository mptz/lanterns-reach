# Engine & platform

Language, rendering backend, and target platform.

## engine/cpp-vulkan-linux — C++ engine, Vulkan renderer, Linux-only (for now)

- Decision: C++ engine with a Vulkan rendering backend, targeting Linux only
  for now. **Windows is the likely next platform,** within about a year;
  macOS is a distant port.
- Rationale: Avoid cross-platform abstraction tax before it's needed.
- **Porting:** Vulkan is native on Windows, so the Windows port is mostly
  the platform layer (windowing, input, files, `clang-cl`,
  `deps/toolchain`). macOS would need MoltenVK, whose Vulkan subset must
  be checked against what `engine/graphics-api` relies on (dynamic
  rendering, timeline semaphores, indirect-count draws), and Apple Silicon
  is ARM, where FastNoise2's speed (it is x86-first) is to be checked; a
  Mac's terrain may legitimately differ bitwise, as the determinism rule
  allows.
- Consequences: Keep a thin render-backend seam so "Linux-only" does not become
  "Vulkan welded into gameplay." Gameplay/sim must not depend on Vulkan types.

## engine/reference-hardware — Two owner machines span the target range; expensive work is a budget tuned from measured frame time

- Decision: the target range is **old hardware from about 2016**, spanning
  "out-of-date integrated graphics" to "out-of-date gaming laptop". Both
  ends are machines the owner runs (the owner keeps computers for 10+
  years, and isn't the only one):
  - **Minimum: Intel HD 530** (Skylake GT2, 2015; ~0.4 TFLOPS, ~34 GB/s
    shared with the CPU, system RAM as video memory). Everything at its
    lowest settings must run here. The reference GPU of the v0.1.x
    measurements.
  - **Comfortable default: NVIDIA GeForce GTX 1050 Mobile** (Pascal GP107,
    2017; ~1.7–1.9 TFLOPS, ~112 GB/s dedicated GDDR5, 2 or 4 GB). About 4×
    the compute, 3× the bandwidth and 5× the fill rate (approximate, from
    published specs, unmeasured).
  - Assistant's notes: today's integrated graphics (Intel Iris Xe, AMD
    Radeon 680M/780M) fall near or above the 1050M, so the range covers
    current thin-and-light laptops too. The Steam Deck (RDNA2, ~1.6 TFLOPS,
    Linux and Vulkan) would be an easy optional fixed-spec check.
- **Don't optimize for either card; make expensive work scale.** Shadow
  faces per frame (`render/shadows`), cascade count and resolution, lights
  per cluster, render scale and LoD distances are budgets **tuned from
  measured frame time**, not per-GPU tables.
- **Memory capacity reverses between them:** a 2 GB 1050M can hold less
  than an HD 530 drawing on system RAM, so the resident mesh pool of
  `render/gpu-driven` is sized from available video memory, with distant
  LoD and eviction of the furthest meshes to fit.
- **Feature level is about the same:** both run Vulkan 1.3 on Linux and
  lack mesh shaders, so GPU-driven drawing uses compute culling and
  indirect-count draws (to verify on the Intel driver). A 1050M usually
  renders through Optimus / PRIME offload, which adds a frame copy to the
  integrated GPU; worth knowing when measuring.

## engine/graphics-api — Raw Vulkan 1.3 (not WebGPU), justified by Linux-only, bindless rendering and explicit sync (not GPU compute)

- Decision: Target **raw Vulkan 1.3** directly, rather than a mid-level
  abstraction such as WebGPU (Dawn / wgpu-native). Vulkan's explicitness is an
  **asset, not a liability**, for this game.
- **The justification is workload-specific, and it splits down the engine's own
  `core/authority-derived-split` line:**
  - *Derived/render half* (chunk meshes, frustum, depth) is conventional "boring
    triangles" — WebGPU would render it fine and cost nothing. No feature here
    argues for Vulkan.
  - *Compute half* — raw Vulkan's compute flexibility (below) is a workload
    **corroboration**, not the deciding reason: the Vulkan choice predates and does
    not depend on any GPU-compute ambition. A *future* GPU fluid/CA accelerator
    (`sim/gpu-speculative-compute`, parked) is one workload that would benefit from
    Vulkan's compute edge over WebGPU, but the decision stands on the render-side
    bindless/many-chunks workload, explicit sync, and the Linux-only point below
    even if the GPU never runs sim at all.
- **The specific Vulkan controls this workload needs that WebGPU limits**
  (ordered by how much they bite):
  1. **Compute flexibility.** WebGPU's compute is a constrained subset: low
     workgroup shared-memory floor (~16 KB vs Vulkan's commonly 32–48 KB+), and
     **no subgroup/wave intrinsics** (ballot/shuffle/reductions). This bites a
     *future* GPU fluid/CA accelerator (`sim/gpu-speculative-compute`, parked); it
     is a corroboration, not the deciding reason.
  2. **Explicit synchronization.** WebGPU auto-inserts conservative barriers (a
     win for ordinary rendering) but this engine wants to **schedule its own
     syncs** — the render pipeline's latch/double-buffer/producer-consumer ordering
     is *general concurrency* (`core/parallel-by-default`, `sim/cpu-parallelism`,
     `sim/concurrency-model`),
     not a GPU-compute special case: overlap producer and consumer stages, let a
     staleness window stretch under load. The thing WebGPU most helpfully abstracts
     is the thing this engine most wants to own — and that is true of the CPU
     parallel sim's own sync discipline, independent of whether the GPU ever runs
     sim.
  3. **Timeline semaphores** for pipelined producer/consumer ordering across
     frames and stages (a general concurrency primitive, useful to rendering and to
     any future GPU-accelerated pipeline alike) — a direct fit WebGPU's async
     buffer-mapping model does not give cleanly.
  4. **Bindless / descriptor indexing** for the many-resident-chunks workload
     (`world/chunk-representation`) — mature in Vulkan, only recently and
     partially in WebGPU.
- **What is NOT lost by declining WebGPU:** its reason to exist is one API across
  browser + native + all backends; we are **Linux-only by decision**
  (`engine/cpp-vulkan-linux`), so WebGPU's portability is a tax paid for
  portability already declined. This — plus the bindless/many-chunks render
  workload and the explicit-sync control above — is the decision's standing
  foundation, independent of any GPU-compute plan.
- **This decision doesn't depend on GPU compute:** raw Vulkan was chosen
  on its own grounds (Linux-only, bindless, explicit sync, AI-authorship
  economics below), and would not lose to WebGPU if GPU compute were
  descoped. GPU compute is a *corroborating* workload, not the load-bearing
  reason. So the fluid revision (GPU fluid parked, `sim/gpu-speculative-compute`)
  does **not** weaken this decision: it stands on the render-side and
  general-concurrency grounds regardless of whether the GPU ever runs sim.
- **Why raw Vulkan is a strength here, not a hazard (AI authorship + the
  implementer skill profile this decision assumes):** the historical case
  *against* raw Vulkan was cost, not difficulty — thousands of lines of correct,
  tedious, non-differentiating ceremony. AI authorship shifts the balance toward
  raw Vulkan on BOTH halves of the API, for opposite reasons:
  - *Boilerplate (the ~90%)* — tedium-substitution: AI produces verifiable
    artifacts (device init, swapchain, VMA wiring, pipeline/descriptor structs)
    that compile, pass validation, and are checkable. Weeks → an afternoon of
    review.
  - *Hard synchronization (the ~10%: barriers, layouts, the CPU sim's
    concurrency model `sim/concurrency-model`, and a
    future GPU fluid accelerator's pipeline `sim/gpu-speculative-compute`)* — fluency-amplification, NOT
    substitution. Vulkan's sync model is ordinary concurrency (memory visibility,
    happens-before, producer/consumer, cross-domain cache coherence) in GPU
    vocabulary — a pipeline barrier is a happens-before edge with explicit
    visibility; a layout transition is a forced cache-state change; a timeline
    semaphore is a monotonic wait-for-value counter. AI translates a *stated
    invariant* into the correct barrier/stage-mask/semaphore encoding; the human
    supplies and checks the invariant.
- **This safety rests on a stated precondition about the implementer, recorded
  as an assumption of the decision (not a claim about any one person):** the
  strategy assumes an **invariant-first** implementer with **low-level
  synchronization experience** — the profile that has reasoned about cross-domain
  visibility where coherence is NOT automatic (e.g. CPU-kernel sync primitives,
  an instruction-cache-flush-on-code-load coherence failure). Such an implementer
  *states the visibility/ordering property before writing the barrier*, so
  "is this correct?" becomes "does this satisfy the written invariant?" — which is
  the only robust defense against GPU sync's dangerous failure mode: the SILENT
  bug (validation passes, renders fine, corrupts under load). The same
  explicitness that is a strength for an invariant-first engineer is a minefield
  for a vibe-first one; raw Vulkan is the right call *given* this profile, and
  would be reckless without it. "Invariant-first" here means expressible-for-code-
  reasoning, not necessarily machine-checked (no Coq/TLA+ obligation) — for the
  Vulkan barrier code. (The hard simulation, by contrast, is TLA+-modelled,
  `sim/exact-core`.)
- **Residual risk and its backstop:** what a concurrency model does NOT pre-load
  is Vulkan-specific *representational* quirks (which stage/access flags are legal
  in a given transition) — a smaller, more checkable surface than "sync is hard,"
  backstopped by validation layers, GPU-assisted validation, and load testing.
- **A few high-value formal targets (scalpel, not methodology):**
  - **the hard simulation** (`sim/exact-core`): conserved transfers and
    messages, Vicinity merges and partitions, per-Vicinity saves, chunk
    lifecycle with custody, and canonical digests. It is small,
    self-contained, and the most correctness-critical spot ("never lossy
    about how much; net quantity delta only via an explicit
    `data/entity-ledger` mint/sink");
  - **novel concurrency protocols small enough to model:** QSBR
    reclamation and GoalSource are TLA+-modelled and checked with TLC
    (`model/`, `engine/dispenser`, `engine/goal-source`); the Dispenser's
    eventcount and its halts are next, both modelled before v0.2.19,
    where they first run on several workers.

  Do NOT formalize the whole engine; that trades a game for a
  verification project.

## engine/vulkan-abstraction — Abstraction strategy: Vulkan 1.3 features + helper libs + a render-graph seam

- Framing: the Vulkan boilerplate is a **one-time, AI-authorable cost**, not a
  reason to fear the API. Effort goes into the characteristics distinct to this
  game; the ceremony is generated and wrapped. The strategy below is what keeps
  "explicit" from becoming "unmaintainable."
- **Lean on Vulkan 1.3 features that delete whole categories of boilerplate:**
  - **Dynamic rendering** (`VK_KHR_dynamic_rendering`, core in 1.3) — removes
    render-pass and framebuffer objects entirely; declare attachments at draw
    time. The single biggest reduction vs. pre-1.3 Vulkan.
  - **Descriptor indexing / bindless** — one large descriptor array indexed
    per-draw instead of per-draw descriptor-set churn; also the right fit for
    many resident chunks.
- **Standard helper libraries (use all three; they are composable and
  conventional):**
  - **Vulkan Memory Allocator (VMA)** — highest-leverage dependency; replaces
    hand-rolled heap/memory-type/sub-allocation. Nobody writes raw
    `vkAllocateMemory` for real anymore.
  - **vk-bootstrap** — collapses instance / physical-device selection / logical
    device / swapchain creation into a few lines, keeping full control.
  - **Vulkan-Hpp** (ships with the SDK) — RAII C++ handles (no manual
    `vkDestroy*`), scoped enums, defaulted create-info structs.
- **The architectural principle (matters more than any library):** do NOT build a
  thin 1:1 OO wrapper around Vulkan handles (a `Buffer` class, a `Pipeline` class
  each mirroring one object) — that reproduces the pain at the wrong granularity.
  Wrap at the level of **operations and resources the engine actually thinks in**,
  and let Vulkan objects be implementation detail beneath that.
- **Render-graph / frame-graph seam (even a minimal one):** declare *passes* and
  the *resources* they read/write; the graph inserts the **barriers, image-layout
  transitions, and synchronization** — the single most error-prone part of
  Vulkan. Describe intent ("this pass writes depth, that pass reads it"); the
  graph emits the correct `vkCmdPipelineBarrier` + transitions. Not a AAA
  frame-graph on day one, but design the seam so one can grow there. (Note: the
  render-graph's automatic sync is for the RENDER half; the CPU sim sync
  (`sim/concurrency-model`) and any future GPU accelerator deliberately schedule
  their own sync per `engine/graphics-api` — keep the two sync regimes
  distinct.)
- **Winding/handedness footnote (ties to `coords/axis-convention`):** the z-up-RH
  → Vulkan-clip basis change folds into the projection matrix as ±1 diagonal
  entries (free at runtime), but a net odd number of axis flips reverses triangle
  winding — so the pipeline's `frontFace` / cull state must be set to match, or
  geometry culls inside-out. A pipeline-state convention, not a transform.
- Keep all of this behind the thin render-backend seam from
  `engine/cpp-vulkan-linux`: gameplay/sim never sees Vulkan (or Vulkan-Hpp) types.

## engine/telemetry (draft) — Logging, counters, profiling and the developer UI's layout

- **Structured logging:** levels and per-subsystem channels, rate-limited,
  written to an in-memory ring (shown in the developer UI) and a file;
  logging stays buffered and never blocks a turn for long
  (`engine/dispenser`'s compute items).
- **A counter registry:** named counters, gauges and histograms that every
  subsystem registers (the scattered "F1 overlay" counters of the
  simulation and concurrency docs, `data/cheap-invariants`'s totals,
  `engine/goal-source`'s counts of goal actors abandoned by cascade), accumulated per
  thread and merged once a frame, readable by the developer UI and dumped
  by headless runs. Measurements recorded in `BUILD-NOTES.md` come from it.
- **Histograms are log-linear and fixed-size** (the HdrHistogram scheme),
  so percentiles (p50, p90, p99 and beyond) come without keeping samples:
  - **Buckets:** each power of two is split into S linear sub-buckets, so
    every bucket is at most about 1/S of its value wide; a percentile read
    from the counts is accurate to that relative error at every
    percentile, the far tail included.
  - **Fixed memory per metric,** set by its range and precision, never by
    sample count: one nanosecond to about 17 minutes at S = 64 is 2,560
    counters, about 10 KB per histogram per thread.
  - **Recording** is a count-leading-zeros, a shift and an increment: no
    allocation, no comparison, a few nanoseconds.
  - **Integer inputs** (nanoseconds, counts; floating-point values scaled
    first), so bucket indices are deterministic and a scenario's histograms
    of anything but timings are identical across machines.
  - **Merging is adding counts,** exact, which makes the per-thread merge
    once a frame and every window cheap: per-frame histograms for the
    dashboard, and rolling windows (the last second, the last minute) summed
    from short ones, so p50, p90 and p99 don't jump about. Headless runs
    dump the counts, so reports compute exact-to-the-bucket percentiles
    offline.
  - **Precision per metric:** S = 64 (about 1.6%) for frame and turn times;
    coarser (S = 8 or 16, a fraction of the memory) for queue depths,
    palette sizes and the like. S, the ranges and the window lengths are
    placeholders until v0.2.7 measures them.
  - **Rejected as the default:** t-digest (order-dependent, allocates,
    merges less simply), P² and similar single-quantile estimators (one
    instance per percentile, not mergeable), reservoir sampling (noisy
    tails). DDSketch is the same idea with another bucket layout, worth it
    only for an unbounded range.
- **Fatal-check reports:** a failed invariant logs the check, the actor and
  the turn before terminating (`sim/concurrency-model`'s pointer rules).
- **Profiling with Tracy** (`deps/profiling`): zones and frame marks on the
  CPU and the GPU, lock contention, compiled out when disabled.
- **The developer UI's layout** (`render/dev-ui`, `input/debug-keys`): world
  overlays kept apart from panels; function keys with fixed roles for the
  most frequent actions (stats, buffer views, the overlay menu, inspection
  under the crosshair, the launcher) and the rest as favourites; every view
  reachable from the launcher; panels grouped by subsystem, with tabs;
  scenarios opening views by name. Refined continually as views arrive.
