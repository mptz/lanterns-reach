# Dependencies — third-party libraries we rely on

What external libraries the engine depends on, and why each is the chosen one.
This is a **living registry**, not a one-shot decision: entries move from
*candidate* to *committed* as they're integrated. The stack is fixed —
C++ (`engine/cpp-vulkan-linux`), raw Vulkan 1.3 (`engine/graphics-api`),
Linux first, Windows likely next — so every choice below assumes a native
C++ Linux target that ports to Windows, built with Clang
(`deps/toolchain`).

*(Tenet `deps/selection-principles` lives in `TENETS.md`.)*

---

## The slots

### deps/renderer-support — Vulkan helper libs (COMMITTED)
Already decided in `engine/vulkan-abstraction`; listed here for the full picture:
- **Vulkan Memory Allocator (VMA)** — GPU allocation. The highest-leverage dep.
- **vk-bootstrap** — instance/device/swapchain setup. **Pin it to the version
  matching the Vulkan headers in use** (vk-bootstrap tracks header versions; v0.1
  pins v1.3.275 for Ubuntu 24.04's headers — newer releases don't compile
  against them). Bump the two together, or build against the LunarG SDK.
- **Vulkan-Hpp** — RAII C++ handles (ships with the SDK).
- **render-graph seam** — built in-house (minimal), not a third-party frame-graph.
- **Shader toolchain** — GLSL → SPIR-V via **`glslc`** (or `glslangValidator`)
  from the **LunarG Vulkan SDK**, compiled **offline at build time** (`.spv` baked
  into the build); no runtime shader-compile dependency (`shaderc`) unless/until
  runtime shader generation is needed. The Vulkan SDK is the umbrella dep
  (loader, validation layers, Vulkan-Hpp headers, the shader compilers).
Clear winners, no contest; VMA and Vulkan-Hpp are effectively the standard.

### deps/noise — procedural noise generation

**Pick: [FastNoise2](https://github.com/Auburn/FastNoise2) (Auburn), unmodified,
keyed by blending fields** (below; a fork is the fallback) — modular node-graph noise, SIMD-fused across the whole graph (not
per-pass arrays), modern C++17, permissive (MIT). The older **FastNoise** (single-header) and **libnoise**
(the historical default, now largely unmaintained) are the alternatives;
FastNoise2 supersedes both. **Priority driving this choice: fast,
SIMD-accelerated noise generation is a baseline capability we want available —
it is NOT a differentiating part of the engine, so we take the best off-the-shelf
accelerated library and do not over-invest in a bespoke one.** Short- or even
long-term compromises in exchange for keeping that acceleration are acceptable.

**Run FastNoise2 at full runtime SIMD dispatch across the ENTIRE noise
workload.** The reproducibility requirement (`core/worldgen-integer-domain`) does
*not* cost us acceleration, because the conflict is far smaller than it first
looks — it is confined to one narrow moment, and is fixed at the point the noise
is *consumed*, not where it is *generated*:

- **The non-reproducibility is only last-float-bit.** FastNoise2's value is
  runtime SIMD dispatch (SSE2 / AVX2 / AVX-512 / NEON / scalar, chosen per CPU),
  so the same binary on two machines can take different SIMD paths. But its
  gradient/hash step is **integer-based** (hashes integer lattice coords), so the
  divergence is not in *which* gradients — it's only in the **float accumulation**
  (FBm octave sums, `Fade`/interp curves), where differing SIMD width / FMA
  contraction can disagree in the last bit(s).
- **That bit-difference is invisible everywhere except one place.** For all
  visual / continuous / candidate noise — density fields for meshing, detail,
  decoration, anything fed to rendering or later re-validated — a last-bit wobble
  changes nothing observable. **Full SIMD, no caveat.** This is the overwhelming
  majority of noise work.
- **The only hazard** is the instant a noise float is compared against a
  threshold to make a *discrete, reproducible* worldgen decision (block identity,
  structure placement, a feature's threshold): there a last-bit wobble could flip the
  outcome and two machines would diverge from the same seed.

**Resolution — quantize at the gate (keeps full SIMD everywhere).** Generate the
**Resolution — ignore the non-reproducible low bits at the gate (keeps full SIMD
everywhere).** The general principle: *a discrete worldgen decision must not
depend on bits the SIMD path can disagree on.* SIMD divergence corrupts only the
low mantissa bits, so make the threshold insensitive to them. Two
implementations, cheapest/most-precise first:
- **Mask/round the low mantissa bits** — zero (or round-to-nearest at) the low
  *k* mantissa bits of the f32 before comparing. Targets *exactly* the
  non-reproducible bits and keeps the most resolution; masking ~4–8 low bits
  swallows the few-ULP divergence with margin.
- **Quantize to a coarse integer grid** — `q = round(noise × N)` as an int, let
  the integer `q` make the decision. Simpler to reason about, but coarser (loses
  more resolution) and imposes a grid rather than respecting the float's own
  structure; it's really the blunt member of the same "ignore low bits" family.

Either way, reproducibility becomes a property of *how the gating value is
consumed* (the mirror of `core/worldgen-integer-domain`'s one-way integer→float boundary), not of the noise
library, and FastNoise2 runs full SIMD across the whole field — the only
concession is this low-bit-insensitive read in the handful of places noise makes
a world-defining decision. **Honest caveat:** neither mask nor quantize *fully
eliminates* the boundary — both shrink and relocate the measure-near-zero set of
coordinates sitting exactly on the new edge, rather than removing it. For a
non-lockstep game that chose consensus/arbitration over bit-exactness
(`coords/f64-authoritative`), a vanishingly rare worldgen seam is tolerable; only
the forced-only pinned-SIMD / owned-kernel path (below) makes divergence strictly
*impossible* rather than merely negligible. `core/soft-thresholds` already steers
us off hard cutoffs, shrinking the ambiguous zone further; the residual tie risk
is boundable and testable.

**Correction: masking does not reduce the flip probability.** The
analysis above overstated it. Comparing a masked (or quantized) value against a
threshold is equivalent to comparing the unmasked value against a slightly
shifted threshold, so two machines whose values differ by a few ULP still land
on opposite sides with probability ≈ divergence × value density at the
threshold — the same as without masking. Masking only moves the boundary. No
transform of the value alone can fix this: any map from a continuous value to a
discrete outcome has a boundary, and only a bit-identical input makes the
decision strictly reproducible. Rough scale for the dense case (solid/air at the
density threshold): ~10⁻⁷–10⁻⁶ flips per voxel, i.e. occasionally one voxel per
chunk, and only when a world is generated on machines with different SIMD
levels. The mitigation choice is open; candidates are recorded in
`core/worldgen-integer-domain`'s exception (`TENETS.md`) and the fallbacks below.

**Fallbacks, forced-only (NOT the plan):** if quantization ties ever prove fiddly
for a specific gating field, narrow the fix to *that field* without touching the
fast path — either **pin it to a single SIMD level** everywhere (FastNoise2
supports forcing a level; costs SIMD acceleration on that one coarse field only),
or **own a small in-house kernel for just the gating noise** (value/Perlin with
integer-hashed gradients, a few hundred lines on the worldgen integer PRNG). In
every case **the bulk visual noise stays full-SIMD FastNoise2** — we never trade
away acceleration on the main workload; worst case we pin one narrow path. The
exact gating mechanism is recorded for `worldgen/noise-fields` when worldgen is
implemented.

**Keyed noise on stock FastNoise2, by blending fields** (`worldgen/seed-keys`).
FastNoise2's API can't key noise per region directly:
- **Every generation call takes one `int seed`** (`GenUniformGrid2D/3D/4D`,
  `GenTileable2D`, `GenPositionArray2D/3D/4D` in `Generator.h`), and the
  nodes hash each lattice corner inline as `seed ^ (primed coordinates)`,
  times a constant (`GetValueCoord`, `HashPrimes` in `Utils.inl`). Every
  corner of one evaluation uses that one seed; `SeedOffset` only adds a
  constant. There is no way to pass in lattice values or a key per corner,
  so a key that followed the evaluation point would hash the same lattice
  point differently on either side of a region boundary, and the field
  would crease there.
- **And positions are absolute `float`s,** so evaluated in world
  coordinates it can't generate a ±10⁹ world seamlessly: the f32 step is
  about 0.06 units at 10⁶, 1 unit at 2²⁴ ≈ 1.7×10⁷ and 64 units at 10⁹, so
  interpolation steps and then terrain falls apart (Minecraft's Far Lands).

Instead of changing how corners are hashed, we **blend whole fields**: each
key point has its own field, evaluated by stock FastNoise2 with one
call-wide seed (derived from that key), in that key point's own frame, and
the fields are blended with smooth, warped, variance-normalised weights
(`worldgen/seed-keys`). That solves both problems with no patch:
- **Seams:** every field is one ordinary FastNoise2 field, so it is smooth
  everywhere; the blend is smooth too.
- **Precision:** each field's coordinates are relative to its key point,
  within about two key spacings of zero, so precision is the same at 10⁹ as
  at the origin. The bound is in lattice cells, and it is loose:
  voxel-centre positions (an integer plus 0.5) are exact in f32 up to about
  2²²; error enters only where a node scales a position by its frequency
  (exact for powers of two, otherwise about 2⁻²⁴ relative); even 2¹⁶ cells
  leave the fraction within a cell good to about 2⁻⁷, far finer than
  per-voxel sampling shows.
- **One call per key, field and octave,** with a 32-bit field seed derived
  from the key and offset by the octave's index (as FastNoise2's fractal
  node does internally), so each octave stays its own increment
  (`world/octave-pyramid`). Each octave's positions are transformed by us
  before the call, by one affine transform folding a rotation derived from
  the key (per field and octave), the octave's frequency and its offset.
- **32-bit seeds are enough:** a field seed in a region is assumed
  crackable from terrain observed there, and the requirement is only that
  this reveals nothing outside that region; seeds are one-way derivations
  from independent tier keys, and a field shows only within its region,
  so it holds.
- **Cost:** one evaluation per octave in a region's plateau, 2 to 8 in
  transition zones; about 1.2× on average for the finest octaves, and
  negligible for coarse ones.
- **Licence:** MIT (Copyright 2020 Jordan Peck), so the fallback below is
  open to us: forking, modifying and shipping are allowed, provided the
  copyright and permission notice stay with every copy or substantial
  portion.
- **Fallback: a fork** (`worldgen/seed-keys`): if blended transitions show,
  or a node type can't be blended (cellular noise), a fork changes two
  things in the nodes we use: each corner takes the 32-bit seed of the
  lattice region that owns it, by a SIMD gather from a 3×3×3 table, and its
  coordinate is its lattice index local to that region. That gives one true
  field at 1× cost, at the price of a patch rebased on upstream changes,
  possibly reduced to a build hook if new nodes can be built outside
  FastNoise2's tree.
- **Open:** how transitions look in practice (a v0.3 prototype settles
  it); how cellular noise is keyed.

**Field notes — how real projects use FastNoise2, and the confirmed sharp edges
(we are committing to FastNoise2 for now).** The earlier
reproducibility reasoning is confirmed by primary sources, not just inferred:

1. **Cross-SIMD output is NOT bit-identical — straight from the maintainer.**
   FastNoise2 issue [#147](https://github.com/Auburn/FastNoise2/issues/147)
   ("Is noise output identical across platforms?"): the author (Jordan Peck /
   "Auburn") answered *"It's not bit for bit identical, there will be some minute
   floating point error level differences. Although I'm working towards having an
   optional mode that is bit identical with some performance sacrifice."* So: (a)
   our concern is real and acknowledged upstream; (b) the differences are
   confirmed to be *minute float-error level* (low-bit), which is exactly what
   the ignore-the-low-bits gate defends against; (c) a bit-identical mode is a
   stated future goal but **does not exist yet** — do not design assuming it.
2. **SIMD-level divergence has shipped as real bugs**, so "minute" is not
   "never": e.g. issue #189 *"Fix 4D Simplex domain warp output differing between
   SIMD feature sets"* (merged Oct 2026), plus active hardening of the SIMD-level
   contract (#205 reject sources with a different SIMD feature set, #209 document
   null return for unsupported levels). Implication: pinning a single SIMD level
   gives one code path but is **not** a guarantee of cross-*compiler* identity on
   its own — it must still be tested. This is why the gate (ignore low bits), not
   the generator, is where we put the reproducibility guarantee.
3. **The bulk-grid API is the performance contract — use it, not per-sample
   calls.** Across wrappers (godot_voxel, the FastNoiseSIMD lineage) the
   consistent finding: single-point lookups (`GenSingle3D`) are ~3× slower than
   requesting a block (`GenUniformGrid3D` / a tile of noise at once). The SIMD
   win *comes from* generating a whole chunk's field in one call. This fits us
   perfectly — we generate per-chunk anyway (`world/chunk-scheduling`), so sample
   a chunk's grid in one `GenUniformGrid` call, never per-voxel. A corollary
   sharp edge: generating the SAME coordinate via a single-sample call vs. as
   part of a grid can differ at the low-bit level (different code path / tail
   handling), so **pick one access pattern for the gating field and stick to
   it** — another reason gating reads go through one disciplined path.
4. **x86-first portability.** godot_voxel's build notes FastNoise2 support
   *"only works on x86"* in their integration; ARM/NEON backends exist but are
   less exercised. Fine for our Linux/desktop-native x86-64 target
   (`engine/cpp-vulkan-linux`); flag it only if ARM Linux ever becomes a goal.
5. **Thread safety needs care.** Issues #100/#136 report crashes generating from
   background threads / shared generator state. Since our noise runs inside the
   parallel worker pool (`sim/cpu-parallelism`, `core/parallel-by-default`),
   treat a FastNoise2 generator as **not freely shareable across threads**: give
   each worker its own generator instance (they're cheap to hold) or confirm the
   specific call is re-entrant. A known, avoidable edge — not a blocker.
6. **Prior art exists at exactly our use case.** godot_voxel (the most serious
   open-source voxel engine) uses FastNoise2 for terrain, and the *FastestNoise*
   NeoForge mod drops FastNoise2 under Minecraft's worldgen for a 5.5–12× speedup
   — so "FastNoise2 driving voxel terrain" is well-trodden, including under a
   determinism-sensitive game (the mod accepts that worlds won't match vanilla,
   which is the same tradeoff shape we're managing deliberately).

**Net:** committing to FastNoise2 is sound and well-precedented. The one thing to
engineer around — confirmed by the maintainer, not just theorized — is that its
output carries low-bit cross-SIMD/compiler variation, so the reproducibility
guarantee lives at the **gate** (ignore the non-reproducible low bits when a
noise value makes a discrete worldgen decision), the bulk-grid API is the fast
path, and a generator is per-thread. None of these is a surprise waiting to bite;
they're the four edges to code to from day one.

### deps/windowing-input — window + input + HAL
- **Open:** gamepad support.

The "hardware abstraction layer for input" you named is really the
**windowing + input-events + Vulkan-surface** layer. Two real contenders:

- **GLFW** — the conventional winner for a Vulkan renderer: tiny, does exactly
  windowing + keyboard/mouse + Vulkan surface creation (`glfwCreateWindowSurface`)
  + monitor/gamma, Zlib license, rock-solid on Linux (X11 **and** Wayland).
  Deliberately *not* a framework — no audio, no image loading, no main-loop
  ownership. Fits principle 3 exactly.
- **SDL3** — broader: windowing + input + **gamepad/controller DB** + audio +
  more. Heavier and more opinionated, but the gamepad handling (hot-plug,
  button-mapping database) is genuinely better than rolling your own.

**Winner: GLFW.** It's the Vulkan-ecosystem default and the
minimal, replaceable choice for window/keyboard/mouse/surface. The gamepad
question is **resolved in GLFW's favor**: keyboard+mouse is the primary input for
a Linux/desktop-native target, so **weak/no controller support is acceptable for
v1** and does not justify the heavier, more opinionated SDL3 or a second input
library. GLFW *does* expose a basic joystick/gamepad API (with an SDL-derived
mapping DB) if we want minimal controller support later — enough for "a gamepad
mostly works" without taking on SDL. If first-class controller support (rich
hot-plug, rumble, full mapping UX) ever becomes a goal, that is a *future*
decision to add a standalone gamepad lib behind the seam — **not** a reason to
switch windowing libraries now. Either way the input layer sits behind the
engine's own `Input` abstraction, so gameplay never sees GLFW types (same seam
discipline as Vulkan) and the backend stays swappable.

### deps/save-store — persistent local save (KV + atomic SQL)

You named two things, and they are genuinely two slots, matching the two shapes
of save data in `persist/diegetic-persistence` + `data.md`:

- **Bulk chunk/voxel blobs** (big, write-heavy, keyed by chunk coord, no queries) →
  a bulk store. **First, what Minecraft actually uses, since it's the obvious
  prior art**:
  - **Minecraft Java** uses the **Anvil region-file format** — *not a database at
    all*. Groups of 32×32 chunks are packed into one `.mca` region file with a
    header table of sector offsets; a chunk is a compressed NBT blob at an offset.
    It's a hand-rolled, append/rewrite-in-place file format.
  - **Minecraft Bedrock** uses **LevelDB** (Google's LSM-tree KV store).
  - **Neither uses LMDB.** LMDB (Symas, memory-mapped B+tree) and LevelDB
    (Google, LSM-tree) are *different, unrelated libraries* — I conflated them.
    So "is LMDB the one Minecraft uses?" — no, Bedrock uses **LevelDB**.
  - **Your crash-resistance unease is well-founded, and it's about the right
    library.** LevelDB (and Bedrock saves built on it) has a real reputation for
    corruption on unclean shutdown — Bedrock world corruption is a known, common
    complaint. LevelDB's recovery story (log replay + manifest) is weaker than a
    B+tree with a durable commit, and it was not built as a crash-hardened store.
    So if the memory you're matching is "LevelDB/Bedrock felt fragile," that's an
    argument *against* the LevelDB family, not against LMDB.
  - **The candidates, ranked for THIS project's priorities (crash-safety first,
    one authoritative writer, read-heavy near-player working set):**
    - **LMDB** — memory-mapped B+tree, fully ACID with a real durable commit,
      single-writer/many-reader, *copy-on-write so a crash leaves the last
      committed transaction intact by design* (no log replay needed). This is
      exactly the crash story LevelDB lacks, and the single-writer model fits one
      authoritative sim writing commits. The usual LMDB caveat (the whole DB is
      memory-mapped into the address space) is a non-issue on 64-bit. OpenLDAP
      license.
    - **RocksDB** — Facebook's hardened LSM fork of LevelDB; much better crash
      behavior than stock LevelDB, excellent write throughput and compression,
      but a *heavy* dependency (large build, many tuning knobs). The pick only if
      write/compaction volume is the bottleneck.
    - **Our own region-file format** (the Minecraft-Java path) — no DB dependency
      at all, full control over the on-disk layout and the durability protocol
      (write-new-then-atomic-rename, fsync discipline). More code, but it's the
      approach `persist/diegetic-persistence`'s "storage cost scales with
      investment" model may want anyway, and it sidesteps the "someone else's
      crash semantics" problem entirely.
  - **Lean: LMDB** for a KV approach (it's the crash-safe answer to the exact
    concern you raised), with **a hand-rolled region format as the serious
    alternative** if we'd rather own the durability protocol than depend on one.
    **Explicitly NOT LevelDB** — it's the one with the crash-resistance
    reputation you remembered.
- **Structured, queryable, transactional state** (the world store: the
  chunk manifest, the scarce table of conserved instances,
  `data/entity-ledger`, the portal table, dimension records, world metadata
  and player data) → **SQLite**. This is the
  unambiguous winner and needs no deliberation: it *is* "a robust atomic local
  SQL store" — the most-deployed database on earth, serializable transactions,
  single-file, crash-safe (WAL mode), public domain. Nothing else is close for an
  embedded transactional SQL store.

**So: SQLite is a clear, uncontested winner for the SQL slot; for the bulk-blob
slot the lean is LMDB (crash-safe by design, unlike the LevelDB family Bedrock
uses) or a hand-rolled region format (own the durability protocol), with RocksDB
only if write-bound — and NOT LevelDB.** They coexist — SQLite for anything you
query/transact, a KV store (or region files) for the voxel bulk you only get/put
by key. One subtlety worth recording: SQLite alone *can* hold chunk blobs
too, and
SQLite is itself genuinely crash-safe (WAL + atomic commit), so a simpler v1
could be **SQLite-only** (chunk blobs in a table) and add a dedicated bulk
store only
if chunk blob throughput demands it — fewer dependencies to start, and it
inherits
SQLite's durability rather than introducing a second crash-semantics surface.
Decide at implementation (`persist/diegetic-persistence`, `persist/edit-atomicity`
— the open deep dive on storage and cross-store atomicity).

- **Cross-store atomicity, resolved:**
  SQLite is the only commit point. Chunk blobs are immutable and
  write-once, keyed by (chunk, version), holding plentiful content only;
  one short SQLite transaction per Vicinity save commits the chunk
  manifest (current version and digest per chunk), the scarce table (one
  row per UUID, versioned, applied only if newer) and the world records
  (portal rows included). The chunk-blob store needs no transactions, so
  LMDB, region
  files or plain files are equally correct, and the choice is purely
  performance; chunk blobs in SQLite remains an option. Unreferenced chunk
  blobs are
  removed by an offline collector at first (`sim/concurrency-model`).

### deps/parallelism — job scheduler (decided: in-house)
- **Specified in `design/dispenser.md`** (`engine/dispenser`).
**In-house: no library.** The worker pool is the Dispenser of
`sim/concurrency-model` (an actor scheduler whose work all comes from pull sources, and
pull sources, eventcount wake-ups, process-global with explicit
start/destroy). Its pull model with advertised priorities, actor turns and
cooperative resizing don't map onto a DAG executor, so **Intel TBB**
(mature, heavy) and **Taskflow** (header-only DAG executor, MIT), the
candidates, are set aside. Work-stealing is rejected.
C++20 `std::atomic::wait`/`notify_one` is the only platform facility it
needs.

### deps/toolchain — Clang on every platform (COMMITTED)
- **Clang is the one compiler,** on Linux now and on the platforms that may
  follow: Windows (`clang-cl`, Clang with MSVC's ABI) and, as a distant
  port, macOS (Apple Clang, the only practical compiler there). One compiler
  family gives one set of warnings and analyses everywhere.
- **Why Clang over GCC:** its analyses fit the design at no cost:
  thread-safety analysis (`-Wthread-safety`) checks the actor convention
  (`ActorOwned`, passkeys and mutexes as capabilities), and
  `[[clang::lifetimebound]]` flags borrows that escape their scope (the
  pointer rules, `sim/concurrency-model`); the planned clang-tidy and clangd
  share its frontend, so tools and compiler parse the same code alike;
  diagnostics are clearer and builds generally faster. Generated code and
  sanitizers are a wash, and FastNoise2 supports both.
- **Each platform's standard library:** libstdc++ on Linux, Microsoft's
  under `clang-cl`, libc++ on macOS; code is written against the standard
  only.
- **`-ffp-contract=off` everywhere,** so floating-point results don't
  depend on a compiler's default for fusing multiply-adds.
- **Unordered containers never decide anything deterministic:** hashing
  uses `std::hash`, tuned for each platform, but nothing deterministic,
  persisted or replicated may depend on an unordered container's iteration
  order (iterate a sorted copy, or keep an ordered index, where order
  matters), and no `std::hash` value leaves the process (stored, sent, or
  compared across builds), since values differ between standard libraries;
  digests and other persisted values use their own defined functions
  (`data/content-digest`).
- **Switching** is part of v0.2.1: `-ffp-contract=off` first, still on GCC,
  with captures unchanged; then Clang, with captures compared within a
  tolerance, inspected, and taken as the new reference (`versions.md`).

### deps/tooling — static checks and sanitizers (PLANNED, out of build)
- **ThreadSanitizer** (compiler-provided) for the concurrency stress
  scenarios.
- **Khronos synchronization validation** (part of
  `VK_LAYER_KHRONOS_validation`, from the Vulkan SDK) in debug runs, for
  missing barriers and write-after-read hazards in the upload and
  deferred-release paths (`sim/concurrency-model`, "Meshes and the GPU").
- **Include and build-target visibility checks** (clang-tidy or a small
  script over the include graph) to enforce the header gates in
  `sim/concurrency-model`: the Cartographer's build target can't see world
  headers (`invariant/cartographer/never-reads-world`), and turn code can't
  see the Dispenser's lifecycle or source headers.

### deps/profiling — Tracy (COMMITTED, adopted in v0.2.7)
- **Tracy** (BSD-3-Clause): a real-time, nanosecond-resolution frame
  profiler, the common choice in games and engines, with CPU and Vulkan GPU
  zones, memory, and locks and their contention between threads. Its client
  is compiled into the engine behind macros that compile out entirely when
  disabled; its viewer runs separately. Pinned like the other FetchContent
  dependencies.

### deps/math — linear algebra / vectors / matrices

**GLM** — header-only, GLSL-mirroring vector/matrix/quaternion math, MIT, the
de-facto standard for Vulkan/OpenGL C++. Clear winner. One caveat tied to
`coords/f64-authoritative`: GLM is used for the **render/derived** math (f32,
origin-relative); authoritative f64 world position lives in our own typed
`WorldPos` (`coords/typed-positions`), so GLM is a derived-side dependency, not
the authoritative coordinate type.

---

### deps/image-loading — texture decode (COMMITTED, added in v0.1)
- **stb_image** (single header, public domain), PNG-only build, pinned by
  commit. Added by the v0.1 build agent to load block textures; clear winner
  for a small header-only decoder.

### deps/data-formats — temporary block definitions (COMMITTED for v0.1.x)
- **nlohmann/json** via `FetchContent` (pinned to a commit SHA, see `deps/acquisition`) to read the v0.1.3 JSON
  block definitions. **Temporary:** JSON is replaced by grammar text files
  (`data/grammar`) — more checkable and expressive — once the grammar exists.
  Validation tooling is Python standard library only (`tools/validate_blocks.py`).

### deps/testing — deliberately minimal (COMMITTED for v0.1.x)
- **No test framework.** A single test executable with a tiny in-house
  `CHECK(expr)` macro, registered with **CTest**, plus Python tool checks
  registered as CTest tests. Test pure logic only. Revisit (Catch2 v3 is the
  likely pick) only if the suite outgrows this.

### deps/verification — formal model and conformance tooling (PLANNED, out of build)
- For `sim/exact-core`: **TLA+/PlusCal with the TLC
  model checker** (or **Quint**) for the hard simulation's specs, and
  property-based conformance tests (random interleavings against lockstep
  mode, content digests as oracle, `data/content-digest`). The spec tooling
  sits outside the game build; the property-based tests run in CTest and may
  justify revisiting `deps/testing`'s "no framework" stance. Library choice
  open.

### deps/audio (proposal) — audio device layer
- **miniaudio** for the device layer only: a single file, public domain or
  MIT-0, a callback API on every platform we target, and a ready
  single-producer, single-consumer ring buffer for the callback's command
  queue. Mixing and DSP stay ours, so the dependency is a thin, replaceable
  seam (`deps/selection-principles`, `engine/audio`).
- **Later option:** Steam Audio (Apache 2.0) for HRTF and propagation;
  ray-based estimation over our own voxel geometry may make its propagation
  unnecessary.
- Considered: OpenAL Soft (LGPL, and a 3D API that would own the mixing we
  want to keep ours).

### deps/dev-ui — developer UI (COMMITTED)
- **Dear ImGui**, via CMake FetchContent (pinned to a release's commit SHA), with the official
  `imgui_impl_glfw` and `imgui_impl_vulkan` backends. Developer UI only — see
  `render/dev-ui`; the player UI is a separate later dependency set (msdfgen /
  msdf-atlas-gen, HarfBuzz, FreeType).
Clear winner; the de facto standard for in-engine tool UI.

### deps/acquisition — how third-party code enters the build (FUTURE: vendor)
- **Today (v0.1.x):** every third-party library except Vulkan and Python is
  fetched from GitHub by CMake `FetchContent` at configure time, **pinned to a
  full commit SHA** (the human-readable tag kept as a comment). Convenient, but a first configure needs the network, and a
  build depends on upstream hosting staying put.
- **Pin rule: never a tag or branch, always a full commit SHA.** Tags are
  mutable: anyone with push access (or an attacker holding a maintainer's
  account) can re-point one, and the next fresh configure would silently build
  different code. A SHA is effectively immutable; you get exactly that commit
  or the build fails. (`GIT_SHALLOW` only works with branch/tag names, so it's
  not used.) Annotated tags pin the commit they point to (`<tag>^{}`), not the
  tag object.
- **Security rationale (supply chain):** third-party code runs at configure
  time (a dependency's CMake scripts can run arbitrary commands) and in the
  game. SHA pins make what runs deterministic; vendoring (below) adds the
  missing piece, a reviewable import commit where an upgrade's diff can be
  read before it's trusted. Distro packages (apt) are signed and
  maintainer-mediated, a real supply-chain advantage for libraries they
  package well.
- **Planned (not yet scheduled):** **vendor** third-party
  sources into the repo (e.g. `third_party/<lib>/`) for **repeatable builds**:
  no network at build time, byte-identical inputs on every machine, the exact
  version recorded in git.
- **Controlled merges from upstream:** updating a vendored library is a
  deliberate, reviewable commit (an import of a new pinned upstream version,
  with the upstream tag/commit and any local patches recorded), never a silent
  change at build time. Local patches stay separate from the import so the
  next upgrade can reapply them.
- **System packages (apt) were considered and not preferred** as the general
  mechanism: vk-bootstrap isn't packaged, ImGui's packaging is uneven, and
  distro versions vary by release (GLM 0.9.9 vs 1.0, GLFW 3.3 vs 3.5), which
  defeats pinning. Individual well-packaged libraries may still use them.
- Until the switch: **don't add new `FetchContent` dependencies casually**;
  each new one is one more thing to vendor.

## Summary — where there are clear winners

| Slot | Clear winner? | Pick | Notes |
|---|---|---|---|
| GPU allocation | **Yes** | VMA | industry standard; already committed |
| Vulkan C++ handles | **Yes** | Vulkan-Hpp | ships with SDK |
| Vulkan setup | **Yes** | vk-bootstrap | |
| Math (f32/render) | **Yes** | GLM | derived-side only |
| Developer UI | **Yes** | Dear ImGui | dev/debug only, permanent; never player UI |
| Image decode | **Yes** | stb_image | PNG-only, header-only |
| Block definitions (temporary) | — | nlohmann/json | until the grammar replaces JSON |
| Testing | — | none (CTest + in-house `CHECK`) | deliberately light for v0.1.x |
| SQL store | **Yes** | SQLite | uncontested for embedded transactional SQL |
| Windowing/input | **Yes** | GLFW | keyboard+mouse primary; weak/no gamepad fine for Linux/desktop v1 |
| Bulk store | **Lean** | LMDB *or* own region format | crash-safe; NOT LevelDB (Bedrock's, the fragile one); or SQLite-only to start |
| Noise | **Yes** | FastNoise2 (full SIMD, whole field) | same-machine determinism is enough; fastest SIMD path |
| Job scheduler | **Yes** | in-house (the Dispenser) | `sim/concurrency-model`; TBB / Taskflow set aside |
| Compiler | **Yes** | Clang (`clang-cl` on Windows) | analyses fit the actor convention; one compiler on every platform |
| Profiler | **Yes** | Tracy | the common choice in games; CPU and Vulkan GPU zones, locks; compiled out when off |
| Static checks | **Planned** | ThreadSanitizer; include-visibility checks | `deps/tooling` |

The non-obvious result: the slots you'd expect to "just pick the popular library"
mostly *do* resolve to the popular library — including **noise**, where
FastNoise2 wins outright at full SIMD and this project's reproducibility
constraint turns out to cost no acceleration (it only disciplines the rare
*gating* read, via quantize-at-the-gate, not the generation). **Save store** is
the one place a project constraint genuinely redirects the choice: the obvious
prior-art pick (Bedrock's LevelDB) is rejected on crash-safety, steering us to a
crash-safe B+tree (LMDB) or an owned region format. Everywhere else the
conventional choice is correct.
