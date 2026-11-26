# Worldgen — noise fields

The sampled fields that drive terrain and gameplay, and the discipline that keeps
their cost bounded. See `core/worldgen-integer-domain` (discrete decisions stay
integer) and `world/progressive-lod` (fields are sampled at the needed LOD).
Generation is never skipped under load and stays repeatable
(`sim/subsystem-rate-degradation`): load may delay it, never alter it.

**The model in one line (defined fully in `worldgen/dimension-profiles`; stated
here so the fields below make sense):** terrain everywhere is one thresholded 3D
field, `density(x,y,z) = bias(x,y,z) + noise`, solid where it clears a threshold.
This **form** is universal; its **content is per-dimension** — a dimension is its
*(bias profile + noise field set + feature response curves)*, since different dimensions have
different worldgen objectives. Within a dimension the noise carries no built-in
notion of up or "ground"; the **bias** is a smooth low-frequency term supplying
large-scale structure. An **Earth-like** world is simply a **2D parameterization
of the bias** — `bias = z − surface(x,y)`, where `surface(x,y)` is a 2D height
field — combined with that dimension's 3D noise: the bias supplies the large-scale
"there is ground around here," the noise supplies all the actual rock, caves, and
overhangs. Zero the bias and you get caves everywhere; make it a function of a
horizontal axis and you get an endless cliff.
"Elevation" is therefore *emergent* (wherever the summed field crosses threshold),
not an input the model reads.

*(Tenet `worldgen/metered-information` lives in `TENETS.md`.)*

The large-scale structure (regional fields, provinces, poles, site sets)
is `design/regions.md`; cross-chunk features (trees, continuations,
structures) are `design/worldgen-features.md`; the seed key hierarchy is
`design/seed-keys.md`; the dimension warp is in `design/dimensions.md`.

## worldgen/noise-fields — Every field is 3D; cost is tuned by per-field sample-grid spacing + interpolation
- **Open:** territory (parked; `design/open-questions.md`).

- **Sample grid versus noise lattice:** a field's **sample grid** is where
  it is evaluated before interpolation (the cost knob below); each noise
  octave's **lattice** is where its random values live, the one that is
  keyed (`worldgen/seed-keys`, `deps/noise`). The two are independent: a
  sample grid is chosen for cost and smoothness, a lattice for the
  octave's scale.

- **Fields are 3D; the cost knob is sample-grid spacing, not dimensionality.**
  Every field is conceptually a 3D function f(x,y,z). It is evaluated on a coarse
  3D sample grid and **interpolated** to the per-voxel value. Spacing is a per-field
  (and potentially per-axis) tuning knob. This is why cubical chunks earn their
  keep: fields CAN vary vertically (Nether-style 3D biomes), you just don't pay
  1-foot vertical resolution for a field that varies regionally. "2.5D/columnar"
  is merely the special case "fine in x,z, absent in y" — not a separate model.

- **Cost model:** worldgen cost per field ≈ (sample count) + (cheap
  interpolation per voxel). Sample count scales as 1/spacing³, so a 64-spaced
  sample grid costs ~1/262,144 the samples of a per-voxel field. Cost is a continuous
  dial (spacing), not a 2D-vs-3D binary.

- **One sample grid per node** (`world/octave-pyramid` for terrain
  shape): each level's octaves are evaluated on its terrain node's grid (64
  per axis below the roots, so spacing halves with the node) and added to
  the parent's composed field, interpolated; the finest octaves are
  evaluated per voxel. A field's spacing at a level is the node's, so the
  per-field cost knob is the field's octave range: a field that varies
  only regionally has only coarse octaves, stored high in the tree (the
  roots, on their own per-field grids), and read from there below.
  **Interpolation is part of the field's definition:**
  the field *is* "each level evaluated on its grid and added to its
  parent's, interpolated with a stated kernel", so progressive refinement, a jump to full detail, the
  server and a client all compute the same values, and a coarse stage's
  stored grid is the exact input to the next (`world/progressive-lod`).
  Prior art: Minecraft evaluates density at coarse cell corners and
  interpolates, choosing granularity per component of its density graph;
  one grid per level of octaves is a Laplacian pyramid.
  - **Reconstruction:** trilinear at about two samples per cell for coarse
    octaves (two samples per cell is the Nyquist floor; gradient noise is
    zero at its own lattice points, so sampling on the lattice shows
    nothing); cubic, or about four samples per cell, for octaves rendered
    near the player, where trilinear's creases, slight aliasing and periodic
    variance banding would show. The kernel is now one parent-to-child
    step per level, and its choice is open (`world/octave-pyramid`: an
    interpolating cubic is the 4-point Dubuc–Deslauriers scheme).
  - **Sample grids are powers of two,** for handling and nesting: they nest
    with chunks and volumes, independent of the lattices below. The owned
    samples of a block stay a power of two per axis, plus one sample at the
    far edge and an apron as wide as the kernel's reach, both generated by
    the block's own turn, so no block reads its neighbours.
- **Lattices are not aligned**: each octave's lattice has its own
  spacing, roughly halving per octave but with spacings that share no
  common factor, plus its own offset that is not a simple fraction of a
  cell, so lattice points of different octaves never coincide (aligned
  power-of-two lattices stack their lattice points into a faint grid at the
  coarse spacing, a classic fBm artifact) and frequency ratios aren't
  exactly 2. This is standard practice: Musgrave's guidance for fBm is a
  lacunarity near 2 but not exactly 2, and per-octave rotations and offsets
  are as common. In frequency terms, aligned power-of-two lattices put
  spikes at harmonics of the base lattice frequency (gradient noise is
  zero at lattice points, so coincident lattice points stack their zeros);
  a non-integer lacunarity only shifts each octave's band slightly, leaving
  a smooth spectrum. Keys don't depend on lattices at all
  (`worldgen/seed-keys` blends whole fields per key region), so nothing
  needs lattices to nest; sample grids, chunks and volumes stay powers of
  two, each level's grid set by its node (`world/octave-pyramid`). **A random
  rotation per key region and octave, isotropic noise only:** Perlin
  noise's axis bias is invisible at block scale, where blocks are
  axis-aligned anyway; at map scale, each octave in each key region is
  rotated by a rotation derived from the key, so the bias points a
  different way in each region and octave and no mountain ranges line up
  across the world (`worldgen/seed-keys`).
- **The finest octave is where amplitude reaches a voxel:** with gain 0.5
  and spacing roughly halving, an octave's amplitude is about A₀·λ/λ₀, and
  octaves below about one voxel of amplitude flip almost no cells. Fine
  detail beyond that comes from fields with their own amplitudes (cave
  fields, `erosion`, detail noise, the per-voxel hash), not from the tail of
  one fBm.
- **Each field's octave range is a design choice,** not a given: its
  coarsest octave comes from its purpose (the scale of the largest feature
  it describes), its finest from where it stops mattering (amplitude below
  a voxel, or another field taking over), and its octave count is about
  log₂ of the two spacings' ratio. Preliminary ranges (lattice spacings):

  | Field | Coarsest | Finest | Octaves | Why it stops there |
  |---|---|---|---|---|
  | `landmass` | ~64k ft | ~8k ft | ~3 | only continents and coasts |
  | `uplift` | ~32k ft | ~1k ft | ~5 | where mountains rise; finer shape is `surface`'s |
  | `surface` | ~32k ft | ~64–256 ft | ~7–9 | the 3D noise takes over below |
  | `erosion` | ~16k ft | ~256 ft | ~6 | smooth versus rugged at mid scales |
  | regional fields | ~64k ft | ~4–8k ft | 2–4 | climate varies over miles |
  | cave fields | ~256 ft | ~2–4 ft | ~6–7 | down to voxel scale |

- **Interpolation rule (preserves `core/worldgen-integer-domain`):** the one hard
  ban is **never linearly blend a categorical's IDs** — averaging category indices
  is meaningless (`(desert=3 + forest=7)/2 = tundra=5` is garbage; the IDs aren't
  a metric space). That is the *only* thing forbidden. It is NOT a ban on coarse
  categorical fields:
  - **Continuous fields** (temperature, moisture, alignment=0.3, …) interpolate
    normally between grid samples. Default **trilinear**; upgrade a specific
    field to smoothstep/cubic only if visible grid-aligned creasing shows
    (likely for regional fields with sharpish transitions, e.g. an evil region's
    edge).
  - **Categorical fields may be evaluated at ANY granularity**, just like
    continuous ones — this is the same "different granularities for different
    fields" knob, applied to a derived categorical. A coarse categorical (classify
    once per grid cell / chunk corner, voxels take the category by
    nearest-grid-point / region assignment) is legitimate and cheap; it never
    invents a category absent from the corners, so no garbage. Classifying
    per-voxel (interpolate the continuous *inputs* to each voxel, then classify)
    is just the max-smoothness, max-cost end of the SAME knob, not a requirement.
  - **Smoothness across a categorical boundary comes from blending CONTINUOUS
    values derived from the category, never the category itself.** Sample the
    categorical coarse, then derive continuous per-category *membership weights*
    (or material/quantity values) and blend THOSE — e.g. a coarse "cactus"
    vs "forest" selection on the sample grid, then smoothly blend a continuous
    cactus-density→tree-density value across the seam. (Most
    features skip the categorical entirely and read the fields directly,
    `worldgen/field-driven-features`.) The discrete category is
    chosen at the end (or the blended continuous material is used directly). This
    honors the ID-ban (we blend quantities, not indices) while giving smooth
    transitions at coarse classification cost.
  - **The gate still applies:** any discrete classification is a
    `core/worldgen-integer-domain` decision. The requirement is
    same-machine, same-binary, same-SIMD-level determinism, with the
    fastest SIMD path (`invariant/cartographer/same-machine-determinism`).
    Masking low bits of the inputs would not make classification identical
    across machines: it only moves the boundary (`deps/noise`). Continuous blending
    is derived-side and needs no gate.

  Short form: **blend continuous values (incl. category-derived weights) at any
  granularity; never blend category IDs; the discrete classification is the gate.**

- **Tiers (cost-control):** a conceptual field lives in the cheapest tier that
  works.
  1. **Primary fields** — independent multi-octave noise on their own sample grid.
     The real cost; keep the count small and the spacing as coarse as each field
     tolerates.
  2. **Derived fields** — cheap math over primaries (combine, threshold,
     gradient). No new noise.
  3. **Discrete / integer fields** — integer-domain hashing, not noise (structure
     seeds, territory). Reproducible by construction.
  - **Tier discipline:** a new field is derived or discrete UNLESS it demonstrably
    needs independent noise.
  - **Dual-purpose mandate:** gameplay fields must also perturb terrain — one
    evaluation serves both. No gameplay-only noise fields if avoidable.

- **Primary fields (9 sampled + the per-dimension bias), with default sample-grid spacing intent:**
  - `surface` — a 2D height field `surface(x,y)`. Its only job is to parameterize
    the Earth-like dimension's bias as `bias = z − surface(x,y)` (the model in the
    header; details in `worldgen/dimension-profiles`): it sets *where the ground
    tends to be*, while the uniform 3D noise supplies the actual terrain. 2D by
    nature. Not used in bias-free (no-sky) dimensions. It is not called
    `elevation` because it parameterizes a bias; the elevation itself is
    emergent. **Derived, not sampled:**
    `surface = S(landmass, uplift, erosion)`, shaped by curves rather than
    being one self-similar fBm, which would give the same kind of hills
    everywhere.
    - **Scale** (preliminary, to be tuned by feel): mountains up to about
      10,000 ft above sea level (rarely reached) and oceans of
      corresponding depth. **Landmasses about 6–12 miles across**
      (`landmass` lattice spacings of about 32–64k ft, a feature being
      roughly half a cell to one cell across), so the middle of a landmass
      is a few miles from the sea, about 20–40 minutes' run, with an
      occasional larger one from the noise's tail; a fantasy world, not
      Earth. **Two kinds of mountain:** bulky ones, with slopes mostly
      below about 45° (a 10,000 ft peak on a base 20,000 ft or more
      across, mountain octaves of about 16–32k ft), and really steep ones,
      up to about 60–70° at the steepest (a base near 1:1 with the height,
      octaves of about 8–16k ft), where cliffs and overhangs from the 3D
      noise come naturally. Which kind rises where is set by the fields
      (for example `erosion` low for sharp peaks, high for bulky ones;
      open). All of these octaves are coarser than about 1,000 ft, so they
      are drawn everywhere in view (`world/octave-pyramid`), and from any
      high point a view usually includes coastline and sea.
    - **Coast profile:** a height curve over `landmass` gives deep ocean, a
      steep continental slope, a shallow and flat continental shelf, the
      coast, and lowland plains rising inland.
    - **Rare drama**: `uplift` is mostly zero and occasionally high;
      where it rises, a ridged term scaled up to about 10,000 ft raises
      peaks, and the same below sea level gives rare trenches. `erosion`
      still sets smooth versus rugged at mid scales.
    - **Stops short of voxel scale:** `surface`'s octaves end around
      64–256 ft, where the 3D noise takes over, so it lives entirely in
      coarse levels and is never evaluated per voxel. With `bias = z − surface`
      the bias changes by one density unit per foot, so the 3D noise's
      amplitude near the ground, in density units, is how far in feet it can
      move the ground, and it must cover all relief below where `surface`
      stops.
    - Prior art: Minecraft (1.18 onwards) shapes terrain from continentalness,
      erosion and peaks-and-valleys fields through splines.
  - `landmass` — continent scale, 2D: land versus ocean,
    and how far inland or offshore; the input to the coast profile. Coarsest
    levels only.
  - `uplift` — (added) a rare-mountain and trench mask, 2D: a low-frequency
    field through a high threshold, so mostly zero, with its own ridged term
    where it rises. Coarse to mid levels.
  - `temperature` — regional horizontally, with an optional slow VERTICAL
    gradient (colder up high) a coarse 3D sample grid captures for free. Crops, mobs,
    survival pressure. With poles (proposed, `worldgen/poles`), it is a
    curve over the latitude coordinate φ plus this regional noise.
  - `moisture` — regional; with temperature, the main input to vegetation and
    ground-cover response curves (`worldgen/field-driven-features`). Coarse 3D.
  - `erosion` — fine-ish horizontal; flatness vs. ruggedness independent of the
    `surface` bias (so terrain isn't boringly height-correlated).
  - The regional gameplay fields (`weirdness`, `alignment`, `yield`,
    `liminality`, `savagery`, `habitation`, `antiquity`) are
    `worldgen/regional-fields` (`design/regions.md`).
  (Note: the former `openness` field is just the zero-bias case of the unified
  density model (`worldgen/dimension-profiles`), not a separate field.)

- **Volumetric cave fields (2) — the fine-3D cost outlier:**
  - Caves need FINE resolution in ALL THREE axes (near per-voxel) — the only
    fields that do, and by far the most expensive. We commit to **noise-only
    caves** (no path-based carvers).
  - **Two primary 3D noises** (`cave_primary`, `cave_secondary`); cave
    *morphologies* are DERIVED by thresholding/combining them, not separate
    primaries:
    - *spaghetti/noodle* — carve a thin shell near an iso-value (optionally both
      noises near-zero AND-ed → thin tunnels through 3D space);
    - *cheese* — carve where a single noise exceeds a high threshold → large
      caverns.
  - **Cost is controlled by LOD + masking:**
    - `world/progressive-lod` defers caves entirely — distant terrain is a
      coarse level that never samples cave noise; caves are evaluated only when
      a box refines to interior-detail LOD. This is what makes 3D caves affordable.
    - Cheap regional fields MASK the expensive sampling: don't evaluate cave
      noise above the surface (from `surface`) or where a depth/`weirdness`
      gate says "no caves," so average cost ≪ worst case.
  - Pairs with fluids (`sim/gpu-speculative-compute`: flooded caves/aquifers)
    and
    `yield` (exposed ore in cave walls → extraction → `data/entity-ledger`).

- **Derived fields (examples, not noise):** there is no `biome`
  classification: features read the fields directly
  (`worldgen/field-driven-features`), and a soft classification such as
  "jungleness" is derived on demand only where gameplay needs it. `slope` (gradient of the surface/density field), `danger`/`threat` (from alignment, savagery, weirdness,
  remoteness, stabilization field — what `mobs/spawning` reads, NOT its own
  noise).

- **Discrete / territorial (NOT noise):** civilization influence / territory is
  emergent discrete state (seeded capitals spreading influence, neighbor-
  dependent borders) — structurally like the stabilization-field spread, not like
  a sampled continuum. Parked as an open discrete subsystem (see
  `open-questions`); do not force it into the noise-field list.

- **V2 bookmark:** a VECTOR field (`flow`/`ley`/`current`) — direction, not a
  scalar — would unlock wind, water current, ley lines, migration/propagation
  bias. Genuinely interesting but a V2 concept; noted, not committed.

## worldgen/field-driven-features — No official biomes: features are parameterized directly on combinations of noise fields, through soft conditions

- Decision: there is **no authoritative biome classification**. Terrain,
  vegetation, ambient effects and decorations are each **parameterized
  directly on COMBINATIONS of the raw noise fields**: e.g. cactus on
  hot + dry + low, bromeliads on cold + dry + high, an ambient effect on
  humid + warm. Each condition in a combination is soft (a sigmoid on one
  field, `core/soft-thresholds`), and the combination is a **soft AND** — the
  product of its conditions' sigmoids (each in [0, 1]) — so a feature's
  suitability is high only where all of its conditions hold and falls off
  smoothly as any one fails. (A soft OR, where one is wanted, is
  1 − Π(1 − cᵢ).) The suitability then drives the feature's density or
  probability. Quantizing to official biomes is
  hard categorization with its own problems (biome blending) and loses
  information; reading the fields directly incurs neither.
- **Mush is controlled by steepness, not by categories.** A condition's steepness
  sets how wide a transition is: shallow gives gradual ecotones, steep gives
  crisp, memorable regions, and a hard boundary is just the limit. Crispness is
  therefore tunable per feature (and per feature pair) without a categorical
  step — `core/soft-thresholds` applied consistently. This is how `core/earned-vastness` is met:
  distinct, perceptible places come from steep shoulders where they're wanted.
- **Coherence comes from shared inputs.** Flora, ground cover and ambience
  that respond to the same moisture and temperature fields stay correlated
  because they read the same values; no category is needed to bundle them.
  Features may be **clustered ad hoc, or not**, as design wants (e.g. a group of
  "hot + dry" features tuned together).
- **Two separate things replace "biome":**
  1. **Artistic prototypes** — design-time evaluation targets, not system
     objects. "Do our deserts feel like deserts?" is a review and playtest
     question judged against reference intent; nothing in the engine knows the
     word "desert". Fixing a wrong-feeling combination is a curve adjustment.
  2. **Quantitative classification on demand** — "jungleness" as a soft
     membership computed from the fields, added **only when gameplay calls for
     it** (e.g. a player wants a map to a jungle), derived, never stored, never
     fed back into generation (`core/authority-derived-split`).
- **Rarity and distribution** are properties of the field distributions and
  transfer functions, measured directly by sampling field histograms over a
  large area ("what share of land has moisture > x at temperature > y") and
  tuned by adjusting curves.
- **Discrete outcomes still go through the gate:** where a curve output decides
  something discrete and reproducible (place a cactus or not, which
  architectural style), it is read through `core/worldgen-integer-domain`; the
  interpolation rule in `worldgen/noise-fields` (never blend category IDs) still
  applies to any discrete selection a feature makes.
- **Fits the machinery:** response curves are declarative per feature in the
  content grammar (`data/grammar`), like `sim/process-kernels`; evaluation is
  per voxel and purely local, with no classification pass.
- Open: the curve families and their parameters; the grammar syntax for
  declaring a feature's response; tooling for field-histogram measurement.
- **Idea bank: strange places from field combinations** (proposed; not
  committed). Each is a combination of
  fields plus at most one mechanism, not a biome category:
  - **Acid rainforests:** realistic in kind (volcanic regions produce acidic
    rain); a rainforest (high moisture and temperature) downwind of volcanic
    geology, amplified by `weirdness`. Acid dissolves carbonate rock, so
    limestone becomes karst (sinkholes, etched pinnacles, caves,
    `geology/*`), with acidic pools and waxy or glassy plants. Mechanism:
    acidity as a geology-driven input to weathering, so exposed metal
    corrodes faster (`sim/random-ticks`' weathering closed forms); a slow,
    forgiving pressure (`core/realistic-in-kind`).
  - **Entropical islands:** tropical islands where order decays faster.
    Mechanism: a regional **decay-rate multiplier** read by
    `persist/diegetic-persistence`, with costlier stabilization: beautiful
    places that are hard to hold. The disorder shows in the appearance
    channel (shimmering, subtly scrambled textures from the hash), not in
    stored noise, so `worldgen/metered-information` holds. Islands come from
    the ocean profile plus `surface`; the entropy could derive from
    `weirdness` and `liminality`, needing no new field. Ethics: faster decay
    in a chosen, legible place is consistent with
    `persist/presence-not-absence` while basic stabilization stays
    affordable there.

## worldgen/dimension-profiles — One unified density-field FORM; per-dimension content (bias + noise fields + feature response curves) is the character

- Decision: Terrain is defined by ONE model *form* in every dimension: a
  thresholded **signed density field**
  `density(x,y,z) = bias(x,y,z) + Σ amplitude·noise(...)`, solid where
  density > threshold, open otherwise. What is **universal is the FORM** (this
  equation, thresholded; one unified evaluation; no separate 2D-heightmap code
  path — the Earth-like heightmap is just one choice of bias). What is
  **per-dimension is the CONTENT**: a dimension is defined by its
  **(bias profile + noise field set + per-feature response curves)** — all
  three, not bias alone. Different dimensions have different worldgen *objectives* (the
  Terra wants naturalistic surface; Enigma/dungeon wants challenge-carving;
  Aether wants alien otherness), so they legitimately use different
  fields and different feature response curves, not one shared field set
  reparameterized.
- **Sky and directional light are part of the profile** (`render/shadows`):
  whether a dimension has a sun/moon, a lit sky, neither,
  or something else. The generator also answers sun/sky exposure for terrain
  that isn't loaded, via octave-bounded evaluation.
- Why this model (over a separate 2D heightmap path):
  - **Embarrassingly parallel / GPU-native — independent of whether fields are
    shared.** Every term is a pure function of the voxel's own coords; no voxel
    reads another. This holds whether dimensions share a field set or each has
    its own — per-dimension fields do NOT touch parallelism (the parallelism comes
    from per-voxel purity, not from noise being identical across dimensions). Maps
    to a compute shader with zero inter-thread communication (ties to
    `sim/gpu-speculative-compute`).
  - **It is where the field has moved** (Minecraft 1.18, No Man's Sky, Enshrouded,
    etc.): gradient + warped noise, thresholded.
  - **It unifies surface, caves, overhangs, floating masses, and voids** into one
    evaluation — no "stitch caves onto a heightmap" seam.
- **Shared infrastructure, per-dimension content.** What's common across
  dimensions is the *machinery* — the density-field evaluator, the per-field
  sample-grid/interpolation cost model (`worldgen/noise-fields`), the integer-domain
  gate for discrete decisions (`core/worldgen-integer-domain`), the local-
  interface structure-placement primitive. What's per-dimension is the *content
  plugged into it*: which fields, their sample grids, the bias, the feature
  response curves. The
  Terra's 7-field set is "Terra's fields," not "the fields." A new
  dimension defining its own generator is expected, not an exception — and
  `core/earned-vastness` says this is exactly where a dimension earns its vastness
  (a distinct generator, not a reskin). (This supersedes an earlier "noise is
  uniform across dimensions; only the bias changes" framing, which over-claimed:
  it tied the parallelism win to field-uniformity it doesn't actually need, and
  was wrong for dimensions with different worldgen objectives.)
- Cost discipline (consistent with `worldgen/noise-fields`): the `bias`/gradient
  must be **analytic or coarse-sampled low-frequency** — never another fine noise
  — so the composition stays in the established per-field sample-grid cost model.
- **The gradient/bias is the dimension parameter.** Example profiles:
  - **Earth-like (Terra):** a **2D parameterization of the bias**,
    `bias = z − surface(x,y)` with `surface(x,y)` a 2D height field. This tilts
    the field solid-below / open-above with the crossover tracking `surface`;
    combined with the uniform noise it yields a rolling surface near
    `z = surface(x,y)` that the noise still carves into (caves below, overhangs
    through it). The realized surface is the threshold crossing of `bias + noise`,
    NOT `surface(x,y)` read back — the 2D field sets the trend, the combined field
    is the truth.
  - **Floating / sky-island ("upside-down") dimension:** a **one-sided downward
    dissolve** — terrain up top that frays into islands and then into nothing as
    z descends (bias strongly positive/open at low z, solid band up high, with
    the noise breaking the underside into islands). World above, void below. This
    is the chosen flavor for the floating dimension.
  - **Endless cliff:** bias depends on a HORIZONTAL axis (solid where x<0, open
    where x>0, sigmoid transition) → a wall extending forever in y and z. Shows
    the "gradient" is any smooth low-frequency bias, not strictly vertical.
  - **No-sky / solid medium (cave world, underdark):** `bias ≈ 0` everywhere, so
    the noise alone decides solid/open → caverns and masses throughout. (This is
    the former `openness` field — now just the zero-gradient case of the unified
    model, not a separate field.)
- **Goal: no system should REQUIRE the concept of "surface."** In a unified
  density-field world where some dimensions have no surface (floating, no-sky,
  deeply 3D), any surface-dependent system is a special case that breaks dimension
  uniformity. So "ground Z at (x,y)" is NOT an input systems consume — it is at
  most a local optimization a system computes for itself. Target designs:
  - **Structure gen** attaches to **density features found by LOCAL query**, not
    to a surface height: a structure seeds at a location and discovers its own
    anchor by sampling the local density field (nearest solid/air interface, a
    flat-enough patch, a cavern floor, a cliff face). Surface, cave, and
    floating-island structures then share ONE placement primitive — "find a
    qualifying local interface" — differing only in which interface they request.
    Works identically in every dimension.
  - **Pathing is fully 3D** (walkable = solid-with-air-above, discovered locally),
    which the world already demands (caves, overhangs, floating terrain;
    `core/chunk-locality`). A pather needing "the surface height" is implicitly
    assuming a 2.5D world; we avoid that. Surface-Z may serve only as a *seeding
    hint* to start a search near the top, never as a requirement.
  - Unifying principle: **systems query the density field LOCALLY rather than
    consuming a precomputed global summary of it.** "Surface height" is a 2.5D
    summary; a 3D-uniform design prefers local interface queries.
- **Surface-Z cache is demoted to an optional self-managed optimization.** Not
  required, not blessed — a system MAY memoize its own local queries if profiling
  warrants, but nothing is *designed to need* it, and a dimension where "surface"
  is ill-defined simply never has one. No system branches on "does this dimension
  have a surface."
- **Why the gameplay demand is gone (unlike Minecraft):** Minecraft needs cheap
  authoritative surface/sky height because spawning, crops, and light all read
  it. Here those are decoupled — spawning keys off the stabilization field
  (`mobs/spawning`), and lighting is presentation-only (`light/visibility-only`).
  So "sky visibility" and "surface height" also decouple: sky visibility is a
  presentation/ambience query (open air above, for weather/skybox/AO), surface
  height a placement convenience — neither authoritative, neither the same lookup.
- **Acknowledged cost (this is a real constraint, not a free win):** 3D
  navigation is more expensive than 2.5D surface-following (larger search space),
  and local-interface structure placement is more complex than stamping at
  `height[x][y]`. We accept that cost because dimension uniformity is worth it,
  and we actively prefer surface-free designs over reaching for surface-Z as the
  easy default.
- **Far Lands note:** a bias defining a bounded solid region can structurally
  reach a void edge far out; the world is already bounded at ±10⁹
  (`coords/f64-authoritative`), and per-dimension bias choices decide whether an
  edge is reachable/meaningful. Not a bug to design around — a property of the
  chosen bias.

*(The dimensions themselves (the set, their identities and cosmology) live in
`design/dimensions.md`.)*
