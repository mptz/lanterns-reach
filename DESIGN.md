# Sandbox Game — Design

A Minecraft / Dwarf Fortress style 3D voxel sandbox. This repo captures design
decisions, tenets, and open questions as the discussion develops.

## How this is organized

Primary organization is **topical**, not a numbered decision log. Each decision
and tenet has a durable **`group/slug`** identifier that is used for all
cross-references (e.g. `coords/f64-authoritative`) so references stay stable
under insertion and reordering. One file per topic area lives in `design/`.

**Invariants** (properties that must always hold) are list items, each
named by a durable **`invariant/<entity>/<name>`** id: `<entity>` is the
owning doc's entity (`goal-source`, `vicinity`, `concurrency`, …) and
`<name>` says what holds. Ids are never renumbered or reused, the order of
a list carries no meaning, and cross-references use the id. Each item
states in a sentence or two what must be true, with its terms defined in
its doc or `GLOSSARY.md`, followed by:
- **Enforced:** one or more of **type** (compile time), **fatal check**
  (all builds), **debug audit**, **test**, **model** (naming the TLA+ spec
  and property), **telemetry or watchdog** and **review**, each with its
  specific mechanism, or "none specified yet";
- **Cost:** the release cost, only where it isn't zero;
- **Why:** what breaks if it fails, omitted when the statement makes that
  obvious.

A doc whose mechanisms need describing keeps them in a **Checks** section,
and its invariants refer to each mechanism by name.

**All tenets live in `TENETS.md`**, grouped by theme and
unnumbered. A tenet's slug keeps its original group prefix
(e.g. `core/closed-form-first`, `ethics/no-fomo`), so references are unchanged;
the prefix records origin, not the section that holds it.

A **tenet** (any entry in `TENETS.md`) captures a belief or principle, not a
goal; every other entry is a decision with its rationale and consequences.

**`GLOSSARY.md` defines the vocabulary:** each engine and architecture term
in a sentence or two, with the slug of the entry that specifies it, plus the
words that collide and the former names. Read it first; when a
term changes meaning or is renamed, the glossary changes in the same commit.

**Entries are final unless tagged.** A tag after the slug, in the entry's
heading and its index line, marks an entry whose decision itself may still
change:
- `(draft)`: being worked out;
- `(proposal)`: suggested, not agreed;
- `(trial)`: adopted to try, expected to be revisited;
- `(deferred)`: parked or uncommitted;
- `(superseded)`: kept only as a stub pointing to its replacement.

Details still to settle inside an otherwise final entry are listed in its
**Open** bullets.

The **point-release roadmap** (what we build, and in what order) lives in
`versions.md` — build targets, not design decisions. Superseded material (the
v0.1 release specifications and build notes) lives in `attic/`, as history,
not authority.

**Two kinds of design doc**:
- **Conceptual design docs** (most of `design/`) are organized by topic:
  principles, decisions and the connective tissue between parts, such as
  `design/concurrency.md`, which keeps the concurrency model's shared
  assumptions and invariants.
- **Entity design docs** specify one key class or entity of the engine
  each (the Cartographer first): its role, where it sits, its invariants
  and how they are checked, and its open issues.

**Style:** specifications and design entries are written as specs, not
devlogs: no per-item attribution and no running revision notes (git keeps
the history). Open issues stay conversational, and may say who raised what.

## Index: tenets and conceptual design docs

### `TENETS.md` — tenets (consolidated; unnumbered, referenced by slug)

*Authority, representation and reproducibility*
- `core/authority-derived-split` — authoritative state is conserved and immutable; everything fast is a derived, rebuildable view of it
- `net/source-blind-client` — the client can't tell simulation from replication, and doesn't need to: one read path and one edit path in single-player and multiplayer; only the writer of the state differs
- `core/f32-origin-relative` — f32 is origin-relative, never absolute
- `core/worldgen-integer-domain` — worldgen RNG stays integer and the float boundary is one-way; one recorded exception: SIMD noise may gate SOFT content, with rare cross-machine flips accepted; hard content stays integer
- `worldgen/metered-information` — worldgen meters authoritative Shannon information; no white-noise chunks; appearance variety is free via hashing
- `core/opaque-chunk-queries` — chunks answer semantic queries; the voxel representation is never introspected (no getVoxel); a public answer is O(1) or O(surface), never O(volume); every consumer gets results (mesh, raycast with the hit cell, emitters, edit plans, shape queries such as probeVolume); no single-cell probe
- `deps/selection-principles` — rules for taking on a dependency: no lib crosses an authority seam it doesn't belong in; reproducibility is a hard gate for anything feeding worldgen; thin replaceable seam over framework; permissive/maintained; boring beats clever

*Simulation and concurrency*
- `core/chunk-locality` — chunks simulate independently; cross-chunk is the controlled exception
- `core/soft-over-hard` — prefer soft simulation to hard, and local hard to global; hard (exact, conserved) only where value is high, e.g. ores and the ledger; hide soft seams from ordinary players
- `core/parallel-by-default` — subsystems are parallel-by-default; serial/shared-mutable sections are justified, named exceptions; every new subsystem must answer "how does this parallelize?"
- `core/isolation-first` — parallelism is easy to lose and hard to regain: explore isolated and pure designs thoroughly before relaxing them; relax narrowly, through the one soft mechanism, with the isolated version as fallback; make coupling explicit in signatures
- `core/fair-share` — no consumer of sim work starves another: scheduler-style allocation over subsystems and heavy entities, guaranteed minimums, work-conserving; slowdown per-entity and observable
- `core/closed-form-first` — use a closed form wherever one exists: anchor (state, t₀) and evaluate f(state, now − t₀), never step; randomness hashed on absolute time; relaxation for coupled fields
- `core/level-triggered` — prefer level-triggered, idempotent checks over queued notifications; soft simulation has no messages (published state read at checks; nudges may point, with a location hint, but carry no content); test: would the world still converge if this were lost or duplicated?
- `core/soft-state` — coordination state (markers, physics proxies, hints, caches) carries a TTL and expires lazily unless refreshed; blocks, ledger, inventories and authored content are never soft

*World, content and fiction*
- `core/soft-thresholds` — prefer probabilistic / sigmoid soft thresholds over hard cutoffs
- `core/earned-vastness` — vastness and proliferation must be justified by player-perceived complexity, not raw count; "same rules, bigger number" is padding
- `core/low-fantasy` — low fantasy enlarges the world, high fantasy shrinks it: ordinary people, deadly arbitrary threats, no destined hero; test: does this make the world revolve around the player?
- `core/realistic-in-kind` — pressures and processes are realistic in kind, forgiving in magnitude: real causes and remedies, scaled until they are pressure rather than tedium (heat → thirst, sleep, water, food, gamified weights)
- `core/diegetic-authoring` — content authoring is in-world, not a code/data boundary
- `core/diegetic-cost` — significant/expensive engine operations are projected into the world as diegetic actions

*Gameplay and engagement ethics*
- `ethics/wanting-coupled-to-liking` — the pull to keep playing must stay proportional to satisfaction; test = "do players endorse having played" not "do they keep playing"; self-directed sandbox is structurally the least exploitative genre
- `core/no-meta` — a clearly optimal choice is a defect: balance with situational trade-offs, diminishing returns and scaling costs; test: would a well-informed player always pick it?
- `ethics/no-variable-ratio-core` — no variable-ratio (slot-machine) reward schedules on core progression
- `ethics/no-appointment-mechanics` — no appointment / time-gate mechanics; the decay curve is the key risk — rewards presence, never punishes absence (restates `persist/presence-not-absence`)
- `persist/presence-not-absence` — decay rewards presence, never punishes absence; single-player pausable, multiplayer consensus pause
- `ethics/no-fomo` — no FOMO / limited-time scarcity pressuring play now
- `ethics/no-manufactured-friction` — no artificial friction inserted only to be sold/ground away
- `ethics/clean-stop` — the player can stop a session cleanly; don't engineer unfinished-business hooks

### `design/deps.md` — third-party library dependencies
- `deps/noise` — stock FastNoise2 (MIT), keyed by blending whole fields per key region, each evaluated with one call-wide seed in its key point's own frame, which also keeps f32 precision anywhere in the world (off the shelf: one seed per call, absolute float positions); a fork hashing each corner with its owner region's seed is the fallback; full SIMD, bulk-grid API, per-thread generators; output is not bit-identical across SIMD levels, the recorded exception to `core/worldgen-integer-domain`; pinned SIMD is the fallback
- `deps/windowing-input` — GLFW (Decided): window/keyboard/mouse/Vulkan-surface; keyboard+mouse primary, weak/no gamepad acceptable for Linux/desktop v1; behind the engine's own Input seam
- `deps/save-store` — SQLite is the only commit point (the manifest of immutable chunk blobs, the scarce table and world records), so the chunk-blob store's choice (LMDB, region files or plain files) is performance only; NOT LevelDB (Bedrock's, crash-fragile)
- `deps/math` — GLM for render/derived f32 math (clear winner); authoritative f64 position stays in typed WorldPos, not GLM
- `deps/renderer-support` — VMA + vk-bootstrap + Vulkan-Hpp + in-house render-graph (committed in `engine/vulkan-abstraction`)
- `deps/parallelism` — in-house, the Dispenser (`engine/dispenser`); TBB and Taskflow set aside; work-stealing rejected
- `deps/toolchain` — committed: Clang on every platform (`clang-cl` on Windows, Apple Clang for a Mac port), each platform's standard library, `-ffp-contract=off`, thread-safety analysis and `lifetimebound` on; `std::hash` in memory, but no unordered iteration order where determinism matters and no `std::hash` value leaving the process
- `deps/profiling` — committed: Tracy (BSD-3), CPU and Vulkan GPU zones, frame marks, lock contention, compiled out when off; adopted in v0.2.7
- `deps/tooling` — PLANNED, out of build: ThreadSanitizer for concurrency stress tests; include and build-target visibility checks enforcing the Cartographer and Dispenser header gates
- `deps/image-loading` — stb_image (PNG-only, pinned by commit) for block textures
- `deps/data-formats` — nlohmann/json for the temporary v0.1.x JSON block definitions, replaced by grammar text files once `data/grammar` exists
- `deps/testing` — no test framework: in-house `CHECK` macro + CTest + Python tool checks; Catch2 v3 only if the suite outgrows it
- `deps/verification` — PLANNED, out of build: TLA+/PlusCal + TLC (or Quint) for `sim/exact-core`, plus property-based conformance tests with content digests as oracle
- `deps/audio` (proposal) — miniaudio for the device layer (callback API, ring buffer); our own mixer and DSP; Steam Audio a later option for HRTF
- `deps/dev-ui` — Dear ImGui with the GLFW/Vulkan backends, developer UI only
- `deps/acquisition` — FetchContent pinned to full commit SHAs, never tags; vendoring under `third_party/` later; avoid adding FetchContent deps meanwhile

### `design/coords.md` — coordinates & precision
- `coords/f64-authoritative` — pure f64 authoritative coords; MC-Java determinism tradeoff
- `coords/typed-positions` — typed positions enforce the f32/f64 boundary
- `coords/axis-convention` — x,y horizontal, z-up; right-handed, +x+y = NE (DF-style, not y-up)
- `coords/typed-time` — `WorldTime` (real time minus pauses), `TickCount` and `WallTime` are distinct types; `CalendarTime` (48-min day) derives from WorldTime by one explicit ×30 conversion

### `design/world.md` — world structure
- `world/voxel-scale` — full 3D voxels, ~1 foot per unit
- `world/voxel-first` — keep things in voxel form, departing from blocks only when it really doesn't make sense; resonant crystals are cubical blocks; blocks are placed 1:1 with cells, never in piles
- `world/cubic-chunks` — cubic chunks 64³, no vertical limit; 64 = the 64-bit lane width bitmask meshing relies on
- `world/progressive-lod` — information LoD: distant chunks hold less information and refine on approach through many thresholds; terrain shape's levels are `world/octave-pyramid`'s; no surface assumed; shape by frequency band, materials by superclass, features by size; stages directly computable, monotonic; visibility hints and bounds drive refinement; errors transient and self-correcting
- `world/octave-pyramid` (draft) — terrain density as a Laplacian pyramid of octaves: level k is the partial sum down to its bin of octaves, one bin of scale per tree level (binned statically by drawing radius; the roots fold every octave drawn across the whole view; fine bins stay separate levels so terrain degrades gracefully, sharpening in rounds, when generation falls behind, only a Vicinity needing full detail; work batched, data not grouped), grids set by node size (64 per axis below the roots, about two samples per wavelength of a level's finest octave, with an apron for the kernel; roots per field), composed recursively (each node's composed field is its parent's interpolated plus its own bin, a fixed order that is the definition, so results are bit-identical everywhere) and stored cumulatively per field where its octaves reach, increments never stored; the same structure is the generation stage, stored terrain-node content, render level, replication unit and bounds; no separate mipmapping except for edited chunks; surfaces re-extracted per level; target look of blocks 1–2 px across at every distance: real cubes near, DDA ray-marching of the pyramid in the middle distance (an experiment), smooth isosurfaces with view- and light-dependent face-weighted shading (the block staircase as a six-direction microsurface, with masking and shadowing) and triplanar block textures far, skirts for cracks, one lighting model
- `world/brick` — see `design/brick.md` (entity doc)
- `world/chunk-representation` — virtualized chunk representation (multiple reps, one interface); the representations are a Brick's (uniform, palette), chosen by content not region
- `world/chunk-meshing` — three layers (visibility classes, potentially visible faces, camera occlusion later); per-rep binary greedy meshing, whole-chunk; meshing is isolated, one chunk's own cells only, no halos; seams conservative, designed with LoD; pop-in acceptable; one soft concurrency mechanism for rendering and simulation
- `world/chunk-scheduling` — generation is a dynamically-reprioritized frontier keyed on live viewpoint distance, frustum and motion, over terrain nodes and their levels and stages: the Surveyor's priority policy over goal actors in the World's `GoalSource`, ranked when work is taken, dependencies inheriting their dependents' priority; ordering constraints are dependencies, not queue order; withdrawal with soft hysteresis
- `world/block-orientation` — the 24-element rotation group, no mirrors; self-symmetry as one of 11 subgroup types with canonical storage; one placement rule per type; wrench turns 90° about the clicked face (≤3 clicks to any)
- `world/climbing-and-descent` — ropes are block columns with gravity (cut → below falls; material per foot; pulleys shift blocks); falls hurt by deceleration, survivable beyond real life; infrastructure decays; caves trend vertical in places; unaided climbing open

### `design/worldgen.md` — worldgen noise fields
- `worldgen/noise-fields` — every field is 3D; per-field sample-grid spacing + interpolation; one sample grid per terrain node, the cost knob a field's octave range, interpolation part of the definition (`world/octave-pyramid`); each field's octave range a design choice by purpose (preliminary table); view radius at most 2¹⁷ units (the architecture's ceiling; settings only lower it, with spherical haze scaled to keep the fade-out at the draw limit); non-aligned lattices (no common factor, per-octave offsets, a random rotation per key region and octave for isotropic noise; standard practice per Musgrave and Quilez); the finest octave where amplitude reaches a voxel; `surface` derived from `landmass`, `uplift` and `erosion` by curves (peaks to about 10,000 ft, rare drama, shelves and plains at coasts; preliminary scale: landmasses about 6–12 miles across, bulky mountains below about 45° and steep ones up to about 60–70°), stopping short of voxel scale; 10 primaries + 2 cave fields
- `worldgen/field-driven-features` — no official biomes: features respond to combinations of noise fields (sigmoid conditions, soft AND); rarity measured by histogram
- `worldgen/dimension-profiles` — one density-field form for every dimension (`density = bias + Σ noise`); a dimension's content is its bias profile, noise field set and response curves

### `design/regions.md` — regions (regional fields, provinces, poles)
- `worldgen/regional-fields` — the slow fields that set a place's character and dangers: `weirdness`, `alignment` (monsters), `yield`, `liminality` (shared across dimensions), `savagery` (wildlife), `habitation` (people now), `antiquity` (marks of the past)
- `worldgen/provinces` (draft) — province sites (thinned Poisson with gap-fillers) about 2¹⁹ ft apart, each hashed to one of a curated list of about 23 authored types; a type is a vector of biases and spreads for the regional fields only (no terrain or climate shifts), tagged with one of nine temperaments (`alignment` for monsters × `savagery` for wildlife, Idyllic to Dreadful) so types stand in for difficulty, and with one of nine settlements (`habitation` × `antiquity`, Trackless to Enduring) for the human mood; biases are blended at borders, ids never interpolated, so a world's mix changes at a scale that takes days to cross; world presets are spawn searches for the nearest province of a type or temperament, the same seed staying the same world; adds the `habitation` (people now) and `antiquity` (marks of the past) regional fields
- `worldgen/poles` (draft) — pole sites (thinned Poisson) about 2¹⁷–2¹⁸ ft apart (travelable, finer than provinces); latitude φ = d₁/(d₁+d₂), equator where two poles are equidistant; temperature a curve over φ plus regional noise; pole sites keyed on their own tier and released by distance, so clients see nearby poles but can't map the world; a compass reads the gradient of φ, from the server beyond the release radius, so it can be gated by progression
- `worldgen/site-sets` (draft) — sparse sites (poles, provinces) are thinned Poisson points (Matérn II over hashed lookup buckets), not a jittered lattice, so no grid shows; gap-fillers bound province gaps; lookup is local, integer-exact and cheap; noise-minimum sites rejected

### `design/worldgen-features.md` — worldgen features (trees, continuations, structures)
- `worldgen/tree-generation` (draft) — worldgen trees grow from a root block whose few states carry worldgen's field-derived knobs (size, vigour, form), and are pure functions of (species, root state, seed key, coordinates), so variety needs no metadata; each chunk stamps every tree rooted within its box plus a halo, writing only its own cells, with no continuation; the distant placeholder is the same function's coarse output; trees grown after worldgen also hash an epoch of world time at their growth time, still a closed form
- `worldgen/feature-continuations` — cross-chunk features continue from published records {RNG state, type, growth remaining}, for growth after worldgen, huge trees and other localized simulation (ordinary worldgen trees are stamped, `worldgen/tree-generation`); commutative, idempotent merge; never overwrites non-generated content; continuations are soft simulation in the receiving chunk's turn, reach capped at the edge, visible growth into existing chunks accepted
- `worldgen/structure-plans` — rare huge structures are integer-domain plans from the seed, cached per origin; each chunk materializes its own clipped pieces, usually as a sparse overlay on noise terrain

### `design/seed-keys.md` — seed keys
- `worldgen/seed-keys` — the seed is a UTF-8 string hashed to a 256-bit root, never used as RNG state; octaves grouped by frequency band into release tiers (near, mid, far), one key per key region per tier, sized to the tier's release radius, which is the drawing radius of its coarsest octave (about 125 λ; placeholders near 2¹³ ft, mid 2¹⁵ ft, far the 2¹⁷ ft view radius), two key grids per tier (cells for 3D fields, columns for 2D fields, released by horizontal distance); one FastNoise2 call per key, field and octave (seed = derived field seed + octave index, terrain warps inside, a derived random rotation per octave for isotropic noise, folded with frequency and offset into one affine transform), so each octave stays its own increment; weights a product of 1D plateau weights, keys found per box (far: evaluate all everywhere; near: filter positions per key); purposes derived from the key by label, never by slicing; each key point's field blended with smooth, variance-normalised weights evaluated at a public boundary-warped position (f = Σwᵢ(q)nᵢ/√Σwᵢ(q)², q = p + W(p)), so no seams and no fork; Terra's far-tier terrain shape 2D, its regional fields 3D; terrain shape never public, 32-bit field seeds crackable per region, revealing nothing outside that region, since a field shows only within it; keys derived from the root, not chained; release per tier by generation radius plus transition and warp; secrets on server-only branches

### `design/dimensions.md` — dimensions (the set, each dimension, cosmology)
- `world/dimensions` — many dimensions; container interiors are pockets of the Aether, not separate keyed spaces (`dimensions/aether-pockets`)
- `dimensions/pilot-set` — the committed set is Terra, Aether, Enigma; Discordia bookmarked; no hell, no sky dimension; Machina, Somnium and base extrapolation parked
- `worldgen/dimension-cosmology` — matter, medium, mystery; low magic draws on Aether (resonance, pouches, `liminality`); folk magic vs Enigma's inscrutable, not player-centric magic
- `dimensions/terra` — matter: Terra (Earth-like naturalistic surface); about exploring nature, roving the surface and descending deep into vast caves, more climbing/descent than Minecraft; terra incognita maps instead of coordinates
- `dimensions/aether` — medium: Aether (sky over a bottomless ocean — islands float/iceberg/submerge, no seabed; nexus for inter-dimensional travel + its own alien fields & feature curves + exaggerated night sky + entropic/impermanent-matter REGIONS — opens `world/dimensional-travel`)
- `dimensions/aether-pockets` — container interiors are real places in the Aether (box arrays at random locations; a 360-stack is 360 boxes) stabilized by their container; orphaned pockets decay; Enigma tech may find them
- `dimensions/enigma` — mystery: an endless ~70° cliff over an endgame dungeon delve; archive and cabinet of curiosities whose core mystery stays unsolved; reality bombs and finding aids as found Enigma artifacts
- `dimensions/discordia` (deferred) — most likely of the uncommitted: mayhem (competitive games, PvP, minigames); outside the fantasy and single-player model; sky islands, very rapid decay, no valuables, command blocks
- `dimensions/machina` (deferred) — second most likely: an endless mechanical world; deferred on simulation cost and unification with logic machines
- `dimensions/somnium` (deferred) — third most likely: a dream dimension authored from content bundles; downstream of the authoring system (Michael: iffy)
- `dimensions/base-extrapolation` (deferred) — a dimension that extrapolates a player's build indefinitely; needs style capture that doesn't exist yet
- `worldgen/dimension-warp` — dimensions are the same size on every axis; space between them warps locally through the Aether as hub: symmetric coupling layers with smoothed zigzags through jittered pins, fold-free and exactly invertible, smooth, bimodal with modes around 12:1 and 1:12, extremes near 50:1 (1:1 rare), vertical included; the best route alternates dimensions with the current's sign, and the Aether reaches Terra's depths

### `design/geology.md` — geology (rocks, suites, minerals)
- `geology/no-generic-stone` — no generic "stone" in the playable world; every rock cell is a specific rock; "stone" is only the coarse-LOD superclass
- `geology/rock-catalog` (draft) — reference: the common rocks by origin (igneous intrusive/extrusive, sedimentary, metamorphic with protoliths), approximate abundance (crust mostly igneous/metamorphic; sedimentary a thin veneer covering most land; shale ≫ sandstone > limestone), properties and uses
- `geology/rock-suites` — suites are a design tool, not an in-game construct: Earth assemblages author rocks and minerals, which in game respond to field combinations; host rock predicts ore
- `geology/ore-sites` — soft generation places ore sites; on reveal a valuable ore is minted in place; decay re-resolves sites, so ore is renewable on a 6+-month timescale
- `geology/essential-minerals` — no spawn conditions or guarantee rule: early-game minerals (e.g. quartz for the first lantern) are geologically ubiquitous and tuned so being out of luck is statistically very unlikely; an inhospitable spawn means starting a new world
- `geology/rock-blocks` — raw / cracked / polished / brick variants, no slabs or stairs; cracked is a sledge outcome scaled by rock strength; ~25 rocks, ~30 minerals, rare decorative stones
- `geology/ore-forms` — every ore form except placers is chunk-local noise and hashes (veins, seams, nodules, inclusions, pipes); grade is a field value; placers deferred to rivers
- `geology/soils-and-sediments` — ~10 readable soils from climate, organisms, relief, parent rock and time; sands colored by source; gravels and clays; bog iron and magnetite sand as early iron
- `geology/resonant-crystals` — quartz, calcite, selenite, agate, fluorite and celestine can each make the first lantern, made resonant in the magical hearth; small trade-offs between them
- `geology/generation-sketch` (draft) — rock from field combinations (likely a few slow geology fields, steep conditions); layers, folds, intrusions, veins; LOD stone → field-derived rock family → rock

### `design/trees.md` — trees (a large catalogue of real trees)
- `flora/trees` (draft) — about 1,250 real trees in a reference catalogue (`design/trees/catalog/`), so abundance discourages collecting them all; chosen for being well known, folklore, culture, striking features, flowers, fruit, medicinal or toxic properties, products or prehistory; one entry per tree a player can tell apart, cultivars and clones collapsed; recently extinct trees included as modern; each record holds habitat (range, climate bands, strata, shade tolerance), form, timber, products, lore, speculative game-fit notes and sources; gaps in timber numbers are estimated from similar trees for gameplay
### `design/sim.md` — simulation
- `sim/gpu-speculative-compute` — fluids are soft: Minecraft-style source blocks (shape, moved by edits) and stored, derived flowing water, level-triggered across seams; an infinite-source rule keeps large bodies' surfaces clean (rule open); no fluid conservation; GPU speculation cut (slug kept for stability)
- `sim/logic-machines` — in-world logic machines; mechanical souls (player-built, sandbox-confined behavior); sealing = compile, and sets clock speed (diegetic timing closure: unsealed = erratic, slow-clock only; sealed = deterministic, overclockable within fair-share); soft logic: no instant lines or recursion, unsealed contraptions run in ordinary physics, built from latches, debounce, samplers, delays, clocks, handshakes and interlocks
- `sim/seasons` — season-invariant worldgen + per-block seasonal specialization; two-plane (authoritative/perceptible) blocks
- `sim/player-embodiment` — the player is a soul bound to a body (third soul implementation); sensorium + intentions; possession falls out
- `sim/dreaming-designation` — sleep is a need; dreaming is a spectator soul-rebind that captures intent (DF-style designate), no Minecraft night-skip; elaborate build planning (schematic projection, region copy, layer view, material lists) lives in the dream
- `sim/cpu-parallelism` — a pool of workers pulling actor turns from sources, not thread-per-subsystem; no work-stealing; memory layout is the lever; authoritative worldgen decisions on the CPU; the threading model is `sim/concurrency-model`
- `sim/mob-navigation` — mob movement = approximate off-thread planning (intention, vs stale snapshot) + authoritative in-lockstep actuation (body, discrete steps); pathing deliberately heuristic not optimal, degrading with range; capability per-soul; avoidance lives on the actuation half
- `sim/queued-updates` — block updates use the snapshot discipline with bounded, coalescing queues and hashed conflict arbitration; under revision: queues are the fallback to closed forms (`sim/random-ticks`)
- `sim/speed-of-information` — bounded effects reach at most 1 chunk per tick (a ceiling; the effective speed depends on how a subsystem is simulated), so every effect lands within the 26 neighbours; over-limit effects are clamped; entities may spill sparingly
- `sim/block-metadata` (draft) — a cell is exactly its block state, with no per-block metadata: timing and variety are closed forms of (simulation key, state, coordinates, world time), events memoryless hashed streams so no cell records when it entered its state, outcomes keyed by the official event time, so no per-chunk seed; a chunk persists only its last-materialized time; hidden simulation state (schedules, scent) is allowed, private to the chunk's turns, never persisted, digested or replicated; rare rich content (a book's text) is a world-level record a state's reference property names; cross-chunk markers are wipeable projections of face-layer states; no inter-block references; face-layer checks boosted ~2×
- `sim/process-kernels` — soft-sim mechanisms are generic engine kernels declared per block in the grammar; no bespoke per-block sim code; invariant-critical values derived by the kernel
- `sim/random-ticks` — random ticks are checks, not drivers: a hashed per-cell Poisson process on absolute time that materializes closed-form state; counts over any interval in O(log Δ) by a hierarchy of time blocks, each with its own key derived from its parent's under a server-only simulation key, clients holding only current and next blocks' keys so they can't predict further; portable integer samplers; cost bounded by rate × volume, no backlog
- `sim/swept-collision` — entity collision is swept through the voxel grid, so no tunnelling at any speed; across a chunk seam the neighbour's thread-safe sweep API computes the hit
- `sim/mob-perception` — mobs act on timestamped, uncertain percepts, never ground truth: vision, sound events, and local geometry read directly for pathing; no persistent world belief
- `sim/mob-decision` — utility-based decisions: soft-weighted drives score intentions through response curves; commitment bias for hysteresis; interrupts emerge from the numbers; temperament per soul

### `design/sim-execution.md` — simulation execution model (top level)
- `sim/concurrency-model` (draft) — `design/concurrency.md`: shared objects follow seven ownership and borrowing rules (one owner by `unique_ptr`, immutable once published except live chunk storage, written in place by its owner and read through validated or tolerant reads, frees only after a grace period, borrows for one turn with keys across time, owners holding across their own turns, dependency pointers dereferenced only in their holder's turn), checked fatally in release by `Borrow<T>`'s turn and generation checks over type-stable memory; chunks are idle actors on a shared worker pool; reads are validated zero-copy semantic queries (one seqlock per palette-form chunk to start); messages are hard simulation only (soft effects are published state plus nudges); committed direction: Vicinities are disjoint, merge-and-cleave, single-threaded actors one level up; settled things are chunk-owned, movers Vicinity-owned; chunks see movers only through opt-in physics proxies; receivers resolve, never reject; persistence per Vicinity: plentiful content in immutable chunk blobs, scarce instances in a ULID-keyed scarce table with versioned rows written only if newer, one save path (chunk blobs, in-memory capture, one short SQLite transaction), frozen or optimistic against a transformation counter, Vicinities shrinking by cleaving off frozen Vicinities; hard messages live in Vicinity queues, chunks have no mailboxes; the Surveyor and the Dispenser, first sketched here, are specified in their entity docs (`world/surveyor`, `engine/dispenser`)
- `sim/execution-plan` (superseded) — the two-phase step/reconcile exchange over per-pair outboxes; replaced by `sim/concurrency-model`
- `sim/execution-invariants` (superseded) — what survives: a chunk changed only by its own turn, bounded reach, conservation and one location per entity
- `sim/cross-owner-commit` (superseded) — reserve-then-commit; replaced by Vicinity-owned movers, conserved messages and assemblies committed in the Vicinity's own turn
- `sim/residency-levels` — chunks are unloaded, held by the World as terrain nodes (no actor; rendered and replicated only), frozen (a Vicinity's member that doesn't run: readable, never written) or running (a Vicinity's member that runs); only viewpoints decide, so effects can't cascade loads; simulation is a budgeted service allocated across demand (every sense, observer artifacts) under operator clamps, not a fixed radius; no backlog on load
- `sim/subsystem-rate-degradation` — under load, subsystems run less often (never worldgen); debt retired by default; soft processes lower their check rate; sealed machines slow as a whole; conserved transfers are never skipped
- `sim/multi-rate` — processes step at different rates, divisors of a base tick aligned with the frame rate; closed-form motion integrated exactly per check interval (rates change how often, not how fast); escalation on interaction; a 20 Hz floor for predictable things, slower only for random closed forms; decorative particles client-side
- `sim/motion-segments` (draft) — prediction stays close to physics because motion is committed (impulses act over time) and control is limited (much less in the air), bounding divergence by about ½·a_control·L²; bodies are still stepped by one shared `stepBody`; segments describe motion to the renderer and the wire; passive movers may optionally skip stepping until impact; server wins, so total visible rubberbanding is the quality measure; inputs act through inertia, impulses are telegraphed, own inputs applied at their tagged tick within a window
- `sim/exact-core` — hard simulation is small, local and formally modelled before it is built (TLA+ or Quint): conserved transfers between chunks and their Vicinity (receivers resolve, never reject), assemblies, merge and split, per-Vicinity saves through SQLite, chunk lifecycle with custody, the mint; everything else is soft
- `sim/execution-use-cases` — U1–U20 scenarios any execution design is checked against (seams, collapses, explosions, overload, unload, contention, pistons, growth across a border); to be re-walked against Vicinities (Evaluation)

### `design/data.md` — data & persistence
- `data/immutability` — ULIDs + immutable objects (object granularity)
- `data/grammar` — declarative capability-access grammar (not JSON, not Turing-complete); also declares per-block simulation behaviour by binding the generic kernels of `sim/process-kernels`
- `data/entity-ledger` — the ledger is conservation accounting (mint/sink, exactly one location per entity), not a central table: settled entities are chunk-owned at runtime, movers Vicinity-owned; transfers are conserved messages; every entity persists in the world's scarce table (one row per ULID, versioned; chunk content only refers to entities; sinking moves a row to equity, never deletes it, and the books are balanced offline), so a crash can roll back but never duplicate or lose (Vicinities never persisted); a small global store for portal, dimension, world and player records; logged-out players open
- `data/dimension-gc` — pockets are reclaimed by ordinary decay once their container is gone, contents through the decay sink; the ownership DAG stays for no-nesting; collector rules remain for any non-Aether dimension
- `data/authored-content` — player-authored content: immutable, content-addressed (hash = identity)
- `data/content-bundles` — curated, versioned, content-addressed bundles; the trust & coherence unit
- `data/stable-identity` — ULIDs on every definition object (blocks, textures, attributes…) with grammar nicknames; no numeric index outside its scope; every persisted record schema-versioned; migrations are total, pure, tested functions; derived data never persisted
- `data/cheap-invariants` — always-on (release included) invariants that cost O(1) to maintain and check (conservation running totals, one location per entity (checked per Vicinity), canonical state); recomputing audits (e.g. a reachability sweep = a second GC) rejected
- `data/content-digest` — a per-chunk, per-subsystem linear digest of authoritative content for chunks in a Vicinity (coarse terrain nodes carry none), computed lazily then O(1) per edit, representation-independent; used for rep-swap, lockstep, save/load and desync checks

### `design/net.md` — networking
- `net/scale-premise` — global scale = content DISTRIBUTION infra (content-addressed bundle registry/CDN); live simulation concurrency is tens–hundreds, NOT planet-scale
- `net/client-server-seam` — client-server is always the architecture: one authoritative sim, a separate client with bounded prediction over resident state, intentions up and derived views down; predicted edits roll back if rejected; the client predicts in local f32 with tolerance reconciliation and replay, no f64 on the client; in single-player the client reads the authoritative chunks through the client-facing read API, with no duplicated world
- `net/replication` (proposal) — chunks replicate as deltas between states at ticks against the last acknowledged state; anchors not results; client generation of unedited content within released keys (open); linear digests for drift with sub-block drill-down

### `design/persist.md` — persistence
- `persist/diegetic-persistence` — investment-tiered persistence; unstabilized world decays toward its generation baseline; persisted form delegated to the chunk

### `design/light.md` — lighting
- `light/visibility-only` — lighting is presentation-only, no gameplay role

### `design/render.md` — rendering (desktop GPUs only; all presentation-layer)
- `render/deferred-pipeline` — deferred shading for opaque (many small dynamic lights; cheap SSAO/SSR); fixed pass structure G-buffer → lighting (HDR) → MBOIT translucency → water → TAA → tonemap; Forward+ rejected, visibility buffer a later upgrade path
- `render/gpu-driven` — resident pooled meshes, compute culling and indirect draws, so per-frame CPU cost is O(changes); no pre-render consolidation thread; a render-thread split only if profiling demands it
- `render/gbuffer-layout` — thin per-pixel G-buffer (~18 B/px, glTF metallic-roughness) + bindless per-material table keyed by material ID, so user-authored materials extend the table not the targets
- `render/transparency` — no sorting or peeling: cutout first, MBOIT for general translucency, a single water layer, no refraction; absorbance as an exact boundary integral over outward faces (verified v0.1.6)
- `render/shadows` — working direction: four light classes per block (emissive, baked glow, shadowed point lights, volume later); cascaded sun shadows; sky exposure from worldgen; point-shadow technique open; a fixed per-frame shadow budget spent by a priority scheduler over independent cube faces, plus a 7th FOV face for handheld lights
- `render/block-textures` — real 16×16 face textures, anti-aliased nearest up close, mipmaps far; random variants break up repetition, edge matching only where coherent features cross edges, distinctive features are block content; larger patterns from orientation; appearance ids: a process-wide 32-bit id per distinct face payload, separate from block states (many to many), six per state in the state table, the greedy merge key
- `render/anti-aliasing` — TAA with depth-based camera reprojection (velocity only from moving entities), reactive mask for translucency, f64 rebase of the previous-frame matrix; SMAA 1x fallback; MSAA rejected
- `render/dev-ui` — Dear ImGui is the permanent developer UI (overlays, tuning, inspectors), drawn after TAA/tonemap; player UI is a separate later system (MSDF + HarfBuzz + FreeType)

### `design/input.md` — input & controls (client-side mapping onto intentions)
- `input/interact-key` — E interacts with the targeted world object; mouse buttons only ever use held items, removing Minecraft's right-click overload
- `input/vertical-movement` — C = ascend (climb), V = descend, camera-relative in free flight; a general locomotion intention any medium consumes (flight, swimming, fluids), kept under WASD; Space/Shift keep jump/crouch; Space while swimming = kick (short speed burst)
- `input/per-hand-use` — LMB is the left hand, RMB the right; each item defines its own actions; handedness setting picks the main hand; bare hand punches; no two-handed items; F swaps hotbars (was R)
- `input/bindings` — the default key map: WASD, C/V up/down, Space jump/kick, Shift crouch, Ctrl sprint, E interact, F swap hotbars, H / Shift+H bare hand, Shift+click secondary action, Q preview, X orientation, G drop, Tab inventory, T chat, MMB pick block; no Alt, no reload
- `input/debug-keys` — debug views on F1–F10 (F11/F12 avoided): from v0.2.7 fixed roles (F1 stats dashboard, F2 buffer-view cycle, F3 overlay menu, F4 inspect under the crosshair, F5 launcher) and F6–F10 favourites, world overlays apart from panels, scenarios opening views by name; in creative mode in release builds
- `input/hotbars` — one hotbar per hand, drawn in that hand's lower corner, no empty slots, max 9; bare is a selection state (H / Shift+H); wheel (wraps) / 1–9 = main hand, crouch + wheel / Shift+1–9 = off-hand; 0 unbound (careful action; sprint rejected so weapons stay switchable mid-sprint); Alt avoided (Linux WMs)
- `input/preview-mode` — toggleable view, not a build mode: each tool previews its next action (ghosts, removal highlights, the shovel shows both); preview and commit are the same pure edit-set function, so the preview can't lie; placing still works with preview off

### `design/inventory.md` — inventory & encumbrance
- `inventory/slots-and-pack` — body slots (two 9-slot hotbars, 5 wearables, 3 jewelry, 3 generic) encumber; pouches weigh a little and their contents never do; chests heavier, hold more, never place; items used only from hands; no nesting
- `inventory/encumbrance` — sigmoid speed falloff on body-slot carry weight (light / transition / saturated), nonzero floor; abstract gamified weights so building blocks stay light and heavy armor is what slows you; weight scales with stack size; negative-weight items later
- `inventory/stack-size` (trial) — 360 is the maximum slot capacity, set per container (bulk pouch vs specimen bag); body slots have capacity 1
- `inventory/ground-storage` (draft) — small non-block items can be set down on surfaces, under reconsideration; anything else outside inventory is a transient pile that despawns on world time
- `inventory/box` — a mundane, low-sided wooden box holds any one item (blocks drawn slightly shrunk, content upright), mounts on any face with no support (pigeonholes, item frame); can hold pouches and chests
- `inventory/hand-pointers` — body slots hold one of anything and never stack; blocks encumber; a pouch in hand is the placing tool, with the container's placement mode; G sets a pouch down
- `inventory/pouch-progression` — pouches are folk craft with stitched or painted patterns; rawhide or woven fibre first, larger bags need stronger pigments; three crafting axes: capacity, slots vs stack size, placement mode

### `design/crafting.md` — crafting (how recipes name inputs)
- `crafting/material-classes` — recipes take material classes, not specific items, so one recipe works everywhere; members differ only in small trade-offs
- `crafting/per-world-recipes` — wiki-proofing: some recipes (e.g. potions) are randomized per world from common ingredients and must be found, typically in Enigma; justifies a high material count; proposed: secrets only, seed-deterministic, found not rolled

### `design/tools.md` — hand tools (catalog; mechanism lives in `input/*`)
- `tools/few-and-versatile` — few tools, each versatile; tools differ by trade-off (precision vs bulk, yield vs speed) on a material class, not by block whitelist; tiers deferred to the recipe graph
- `tools/catalog` — pick (one-block, one-handed), sledge (bulk rock, cracked drops), shovel (digs and fills soft ground), knife, axe, wrench; heavy tools act on a camera-directed plane, never previewed; no tool is two-handed
- `tools/breaking-as-action` — breaking unfolds over time (swings, cracks, particles, a short crumble); damage is soft state published by the swinging body, visible to others loosely synced; only the final swing is an edit; the mesh update lands within the crumble effect; breaking is a physics process with duration that input only continues or halts: the final swing is the point of no return, so the edit is sent at its start and the mesh swap is timed to the impact
- `tools/compasses` (draft) — key gameplay: compasses read the gradient of a field (poles, yield or water, habitation, liminality, alignment) or point at a place (a bound lodestone, the last death site, the nearest province of a type); advanced dials carry several needles; direction only, precision by quality, long-range readings from the server so they gate by progression, in-world interference
- `tools/candidates` (draft) — bucket (fluids) proposed; trowel deferred unless mortar becomes a mechanic; chisel replaced by a workstation; fire starter is an item

### `design/mobs.md` — mobs
- `mobs/spawning` — spawns in "unreality"; suppression is multi-lever, decoupled from lighting; `alignment` shifts pressure and tier
- `mobs/wildlife` (draft) — animals are ordinary ecology, untouched by stabilization; `savagery` scales aggression range, persistence, size and density

### `design/peoples.md` — peoples (inhabitants, factions, reputation, trade)
- `peoples/peoples` (draft) — sapient animal peoples (after Dwarf Fortress) in tribes and camps, not cities, taking the place of villagers and traders; hunter-gatherer builders, not the ruins' builders; no moral alignment but values of their own; a third threat, shaped by the player's standing
- `peoples/reputation` (draft) — standing computed on arrival from remembered deeds, weighted by a soft news-reach kernel over distance and time (lossy, diegetic); individual, group and human levels summed
- `peoples/factions` (draft) — a signed affinity matrix between peoples spreads reputation across factions
- `peoples/trade` (draft) — barter with per-people preferences; nothing gated; standing sets terms
- `peoples/sojourners` (draft) — peoples, like players, arrive from elsewhere and fade back; their own lanterns (distinct, not usable by players, a source of trade desires) stabilize camps, and the immune response reacts to them; player changes in camps outlast the wild but revert; occupancy static, a temporal schedule recorded as a possibility
- `peoples/territory` (draft) — which peoples live where; the discrete territory subsystem

### `design/ambiguities.md` — ambiguities (deliberately unanswered lore)
- `ambiguity/ruin-builders` — whether the animal peoples built Terra's ruins is never stated; evidence points both ways
- `ambiguity/enigma-makers` — no direct information about who made Enigma or why

### `design/engine.md` — engine & platform
- `engine/cpp-vulkan-linux` — C++ + Vulkan, Linux-only for now; Windows the likely next platform (native Vulkan), macOS a distant port (MoltenVK and ARM to check)
- `engine/reference-hardware` — old hardware from about 2016: Intel HD 530 as the minimum, GTX 1050 Mobile as the comfortable default; development and measurement on the owner's current machines (a Latitude 7480 with HD 620, and more), measurements naming their machine, the 1050M for occasional validation; expensive work is a budget tuned from measured frame time; mesh pool sized by video memory
- `engine/telemetry` (draft) — structured logging, a counter registry read by the developer UI and dumped headlessly, with log-linear fixed-size histograms (percentiles without samples, merged by adding counts, windowed by summing), fatal-check reports, Tracy profiling; the developer UI's layout (overlays apart from panels, fixed-role keys, a launcher, favourites)
- `engine/graphics-api` — raw Vulkan 1.3, not WebGPU: Linux-only, bindless many-chunk rendering, explicit sync, AI-authorship economics; GPU compute corroborates but doesn't decide
- `engine/vulkan-abstraction` — 1.3 dynamic rendering + VMA/vk-bootstrap/Vulkan-Hpp + a render-graph seam for barriers/sync

### `design/gameplay-loop.md` — gameplay loop (deliberately deferred)
- `loop/deferred` — loop detail deferred as a choice; engine is loop-generative; flags the loop-knob open questions
- `loop/opt-in-challenges` — player-controlled, game-enforced challenge and behavior settings (e.g. a pacifist playthrough); recipes need no-kill paths
- `loop/speedrunning` (draft) — support runs and races cheaply: game-enforced challenges as categories; a high-precision timer with in-game and real time and splits on authoritative objectives; the fastest movement is running forwards, looking ahead, without jumping (jumps and looking far aside cost speed); skill from routing and knowledge, not luck; pinned SIMD and generator version for set-seed races; verification by video, no built-in recording; no appointment seeds

### `design/first-session.md` — the first sustained engagement (kept small)
- `first-session/scope` — a timeline, not a sitting (decay starts after days, worlds pausable); priorities: survival tooling, protection from hostiles, light for night/underground, one magical pouch, a lantern; food not a gate; the lantern is the stopping point
- `first-session/inventory` (draft) — a list of ~12 items (cordage, knapped knife, digging tool, antler/flint pick, fire starter, campfire, torch, snare, first pouch, hearthstone, resonant quartz, lantern); pouch before walls; no weapon needed, so a pacifist run passes
- `first-session/campfire-protection` — early protection is folk consumables fed to the campfire: a circle that dwindles unless fed; walls come later
- `first-session/resolved` — the cutting tool is the knife; the axe isn't needed for initial survival
- `first-session/sleep-and-day` — a 48-minute day with a midsummer start; 3 in-game hours of sleep per 2 nights, or soft ill effects; first night needs no sleep
- `first-session/onboarding` — no text or scripted tutorial; per-world advancements with just enough text, a tree with no in-game effects, can be turned off, significant ones broadcast to all players
- `first-session/biome-stress-test` (draft) — a walk-through of nine start settings, which differ in difficulty on purpose; heat raises thirst rather than causing heatstroke; gaps closed by material classes
- `first-session/spawn-lantern` (proposal) — a world setting (not the default, like the Bonus Chest): one shared lantern at spawn that doesn't decay and can be broken; a Narnia-like hint of arrival; breaking it yields an ordinary lantern drop
- `first-session/open` (draft) — sleep details, drink/containers, lantern housing, food
- `first-session/budget` (draft) — a cap: ~15 crafted items, no station beyond campfire and hearthstone; playtest that new players reach the lantern

### `design/pressures.md` — survival pressures (the goal-spawning edges of the emergent loop)
- `pressure/basic-survival` — heat, cold, food, drink; shelter is bi-directional (cold AND sun/desiccation); food: days without food are survivable, invisible caloric balance with fat stores, minimal "well-nourished" display and cravings; water once per 2 days
- `pressure/mobs-night` — hostile mobs attack; night is the recurring attack cadence (not light-gated spawning)
- `pressure/hearth` — a base starts with a fed hearth; first-session chain campfire → hearthstone → lantern; consumable fires repel and stabilize briefly, lanterns stabilize indefinitely once a safe range is cleared
- `pressure/defensible-space` — physically wall off hostiles; enclosure is a connectivity query
- `pressure/light` — light needed; early-game light is consumable (resource pressure, not a gameplay light-level); torches burn on world time (offline included, deliberate); the lantern is the first permanent step

### `design/progression.md` — progression dimensions (design-check catalog, not a tech tree)
- catalog of ~40 capability axes (survival, spatial security, mobility, extraction, creation, knowledge/access, social/economy, temporal, character/internal growth) mapped to engine hook vs. gap; records scope boundaries (Minecraft-flavored not colony-sim; colony mechanics parked; no meta-progression); finds reality-stabilization is the hub axis and diegetic-cost its signature move; crop/tree growth is closed-form everywhere, so the continuous-sim resource buys only STEPPED production (actuators, transfers, mob farms, sealed machines); decay interacts only emergently

### `design/open-questions.md` — open & resolved tracking

## Index: entity design docs
One file per key class or entity. Their shared assumptions and connective
invariants stay in `design/concurrency.md`.

### `design/cartographer.md` — the Cartographer (chunk generation service)
- `worldgen/cartographer` — a stateless, player-independent generation service, conceptually pure, called only from goal actors' and function actors' turns (never a stateful actor's turn); refinement takes an ancestor's const node data as input, with no cache of composed fields; requests are (dimension, box, level), volumes of distant terrain first-class; `requiredKeys` predicts key regions, keys cached until restart, a missing declared key is a "needs keys" fact, an undeclared one fatal; refinement generates fresh, bounds classification skips certain solid and air; 13 invariants (never reads the world, never waits on our design, owned results, bounded declared reach, no minting, placeholders are real blocks, same-machine/binary/SIMD determinism, no cancellation); tree continuations are soft simulation in the receiving chunk, outside it

### `design/vicinity.md` — the Vicinity (simulation transaction domain)
- `sim/vicinity` — a disjoint set of chunks run as a single-threaded actor: running and frozen mean the same for chunks as for Vicinities; every chunk actor owned by exactly one Vicinity with a non-null pointer to it; a running chunk never touches a Vicinity's border (its neighbours are all members), and running chunks may be discontinuous (a telescope's target); a Vicinity is running or frozen and no two running ones touch (touching merges them; a cleave divides one into running parts separated by frozen parts that save and shut down; running waits for frozen), running chunks complete by construction; membership decided by the World's planning component (the Surveyor); owns conserved objects in motion or ready for a chunk, hard-message queues (chunks have no mailboxes) and assemblies; per-turn handles; never persisted (one save path, frozen or optimistic; Vicinities shrink by cleaving); 15 draft invariants; open: turn budget, publishing mover transforms

### `design/frame-thread.md` — the frame thread (input in, a rendered frame and audio out)
- `engine/frame-thread` (draft) — one of three threads outside the actor model (with the audio callback and the network thread), on the process's main thread: polls input into intentions and events in the player's input slot, applies mouse look as an instantaneous offset on the body's view frame (look is authoritative input; movement is intention), evaluates published trajectories (never predicts: input goes through a per-player triple-buffered slot the body pulls from; in multiplayer the client runs local physics in its Vicinity, merged continuously with the authority), publishes viewpoints, installs meshes from the descriptor vector, culls and writes `visibleUntil`, rebuilds the record list, submits, releases by timeline value; never waits behind work; writes no chunk, sends no hard message; owns all Vulkan; never runs a turn, in any mode (lockstep mode alternates it with one worker); trajectory segments are (t₀, p₀, v₀, q₀, ω), never slerped; trajectories arrive as per-turn batches of deltas into an entity render table evaluated on the GPU (attachments relative); effects and animation are closed forms, never reconciled; targeting is the body's, so it reads no chunk storage; pause quiesces the back end and shows a still, focus loss doesn't pause; open: the UI and inventory design pass, the extrapolation clamp, local physics' scope; 7 draft invariants

### `design/audio.md` — audio (analysis, voices, mixing)
- `engine/audio` (draft) — presentation only; acoustic analysis (reverb from listener rays, occlusion, ambience) and voice management as a client Listener actor's turns on workers; mixing and DSP on the OS audio callback thread (no locks, allocation or waiting; atomic snapshots plus one SPSC ring buffer); the frame thread publishes only the listener pose; viewpoints carry per-sense ranges (view, hearing), whose union decides what is perceptible, and which may ask for simulation within a budget; the Surveyor publishes a perception index for event routing

### `design/dispenser.md` — the Dispenser (worker pool and actor scheduler)
- `engine/dispenser` — required to run work with dependencies natively (replanning cheap; cleanup safe); one per process, explicit start/destroy, header-gated; has no ready queue: all work comes from pull sources (`pullBatch`, `returnRemainder`, `advertise`); every work item is an actor's turn, at most one per actor, each with a passkey only the Dispenser constructs; function actors exist for one turn, for work that neither has dependencies nor is one, inputs and results as members, results reached afterwards through any-thread methods (shared `result()` or one-shot `claimResult()`), any number at once; work with dependencies through `GoalSource` (`engine/goal-source`), one per World; replanning is registering and withdrawing goal actors, and withdrawing abandons every dependent not yet started, superseding dependency graphs published whole; reclamation by global QSBR (liburcu's QSBR flavour, written in house: every worker and the frame thread copy a grace-period counter at the end of each turn or frame and go offline to sleep; `retire(CleanupActor&)` runs once every online thread has passed a quiescent point; a lowest-ranked ticker source, woken by the one timed idle wait, keeps grace periods moving while cleanups are pending); one-shot conditions (a function actor's `done()`, fired only by the Dispenser with an outcome, Completed or Abandoned) and non-blocking `when` chaining through embedded waiters and generation handles; `abandon()` cooperative at yield points and rippling to dependents; short-lived function actors pooled (TerrainNodes not), `reset()` pool-only, on a terminal actor in no waiter list, with retiring one returning it to its pool; each Vicinity the source for its own and its chunks' turns; stores no work; two-level scheduling with one global priority scale and ageing; eventcount with wake-one and chain wake; workers block only at acquisition; compute versus I/O items by duty cycle; partial completion gives back; System is its single root actor (System → World → Vicinity and Registrar; parents set children's desired state and watch them for stable predicates); from outside only intentions to System (shut down, load, pause, resume a world) and its published state; observing is a continuation turn run once halted children are quiet; three halt levels (Vicinity for merges, cleaves and frozen saves, World, System); a halt is one call carrying a continuation (move-only token), applied bottom-up (a parent's halting turn halts its children in the order it needs), with a subtree count of batches in flight making "quiet" stable by construction; lockstep mode; at least one worker in play; 18 invariants

### `design/surveyor.md` — the Surveyor (the residency plan: sky, terrain nodes, the running world)
- `world/surveyor` (draft) — its work required to be parallelisable across threads and run natively by the Dispenser (as goals); the World's planning component (not an actor), hiding what of the world exists where and at what fidelity as the Cartographer hides the noise schedule; plays no part in simulation; each turn computes desired residency from viewpoints and issues the delta against current residency (level-triggered); three tiers: the sky (per-dimension survey profile, chosen from the viewpoint and coarse fields), terrain nodes (inert, a sparse octree of world-aligned boxes from 2¹⁷-unit roots halving strictly down to chunk leaves, each node storing its composed field (its parent's interpolated, plus its own bin of octaves); residency a mosaic of the tree (the nodes drawn) with their ancestors, refined by replacing a parent with all eight children, 2:1 balanced; strictly outside in (ancestors before descendants; joining a fixed cascade the player waits for; portals power up while the destination generates; fast movers stall at the edge); at any level up to full detail; unedited from the octave pyramid, edited downsampled from content; a full-detail terrain node builds a chunk actor and a dissolving actor populates one) and the running world (Vicinities and chunk actors); holds the records of full-detail chunks and alone writes the map; its work pulled from goals in the World's `GoalSource`; allocates the simulation budget under operator clamps; decides membership and carries out merges and cleaves in the World's halt continuation under halt tokens; publishes the perception index; 11 draft invariants

### `design/goal-source.md` — GoalSource (demand-driven work with dependencies)
- `engine/goal-source` (draft) — `design/goal-source.md`: the Dispenser source for work with dependencies, one per World; it sequences and doesn't own: clients allocate goal actors, link them into their own structures and register them with their dependencies (`registerGoal`), each dependent holding `DepPtr` dependency pointers dereferenceable only in its own turn; every goal actor has a turn, client data moving in as inputs; states Pending, Running, Completed, Abandoned plus a withdrawn flag, under one mutex; `done()` fires once for outside waiters; `withdraw(span)` is GoalSource's counterpart of an unlink, abandoning every dependent not yet started by cascade, and must precede the cleanup that frees a goal actor (asserted); no keys; 14 draft invariants

### `design/world-actor.md` — the World actor and its Registrar
- `engine/world` (draft) — one per loaded world, child of System, parent of its Vicinities and its Registrar; holds global records and plans through its Surveyor component; the Dispenser source for work outside every Vicinity; its halting turn halts the Vicinities, which save while frozen to the Registrar, then halts the Registrar; the Registrar is the world's persistence actor and its single committer (SQLite manifest, scarce table and world records, rows applied only if newer so commits need no order; chunk blobs are written in parallel by I/O items; commits, loads and offline GC); 1 draft invariant (rows applied only if newer)

### `design/network-thread.md` — the network thread (remote clients)
- `engine/network-thread` (draft) — a many-headed frame thread: one asynchronous event loop (epoll/io_uring, IOCP, kqueue; one UDP socket, batched sends) serves every remote client, each a plain Client record (input slot, acknowledged versions, interest, budget), not an actor; inbound writes input slots, viewpoints and System intentions; outbound walks each client's interest and sends versions newer than acknowledged from encode-once caches, plus per-region transient events; outside the actor model, never waits behind work

### `design/chunk.md` — the chunk (interface only, for now)
- `world/chunk` (draft) — the chunk's interface only, by contract: a chunk is an actor only while in a Vicinity, running or frozen, and World state (tables, not objects) outside every Vicinity, with its key and World record stable across tenures; the Cartographer and loading produce terrain-node content at a level of detail (a Brick at full detail), not chunks, possibly grouped into volumes; the World's data is never authoritative (a design choice), and three invariants keep the two paths one (one chunk-data type, changes of form only at synchronised moments, no content change outside a Vicinity on the authority); a dissolving actor's data populates a full-detail terrain node, pinned until its Vicinity's save commits, then expendable; any-thread semantic reads (raycast with the hit cell's state, sweep, probeVolume over shapes, planEdit, face connectivity, emitters, markers, digest and version, stage, encoded payloads) and signals (nudge, physics proxies, render hints); World-only lifecycle and membership; own-turn intents (generate, load, consume mail including edits with expected old state and effective time, check, full check, mesh, snapshot, sends through the Vicinity handle); guarantees; implementation deliberately left out

### `design/brick.md` — the Brick (the engine's one cell container)
- `world/brick` — a Brick is 64³ cells at a power-of-two cell size, the engine's one cell storage, owned by a chunk or a terrain node, which is the abstraction boundary; palettes hold block states at every level; a chunk's content is a full-detail Brick plus its last-materialized time, owned by the chunk actor or a terrain node with no chunk-data type between, with no per-block metadata; uniform or palette, at most 2¹⁸ = 64³ distinct live states per Brick (so the palette also covers the dense case), stored at aligned power-of-two widths (1–32 bits); never resized in place: widening, a full palette and compaction all build a new Brick that the owner swaps in and retires; edits only append; widening is scheduled at a high-water mark (15/16) with a fallback in the edit, and compaction (to a low-water mark, 7/8) happens at saves, bulk fills and, for churning Bricks, after max(F, α·L₀) appends; the reverse lookup is the owner's, by scan or a table sized to the palette's capacity; generation writes indices into fixed palettes; layout x fastest, z up; palette entries are 32-bit process-scoped state ids into a process-wide state table, interned from state hashes hashed from the definition ULID and its own canonical variant values (draft, to try; ULIDs, never ids, are persisted, with what reconstructs each state; interning past 2³² is fatal); coarse cells hold 8-bit material classes for now; 9 draft invariants

## Major components (working map)

1. World representation & storage — `world/*`, `persist/*`, `data/entity-ledger`
2. Procedural generation — `worldgen/*`, `dimensions/*`, `geology/*`, `core/worldgen-integer-domain`, `world/progressive-lod`
3. Simulation / game logic — `sim/*` (execution model: `design/sim-execution.md`), governed by `core/closed-form-first`, `core/level-triggered`, `core/soft-state`; `core/chunk-locality`, `mobs/*`
4. Pathfinding & spatial queries — `sim/mob-navigation`, `sim/mob-perception`, `core/opaque-chunk-queries` (raycast/semantic queries)
5. Rendering — `render/*`, `engine/cpp-vulkan-linux`, `engine/vulkan-abstraction`, `coords/*`, `light/visibility-only`
6. Player interaction & input — `input/*`, `inventory/*`, `tools/*`, `sim/player-embodiment`, `world/block-orientation`
7. Cross-cutting — `TENETS.md` (all principles and tenets: `core/*`, `ethics/*` and a few others), `data/*`, `deps/*`
