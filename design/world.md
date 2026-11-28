# World structure

Voxel scale, chunking, dimensions, and the physical storage representation of
chunk voxel data.

## world/voxel-scale — Full 3D voxel world, ~1 foot per unit

- Decision: 3D voxels. The base unit 1.0 is ~1 foot (closer to a foot than a
  meter), i.e. roughly 3× Minecraft's linear resolution.
- Rationale: Finer resolution for detailed building and simulation.
- Consequences: ~27× voxel density by volume vs. Minecraft. This is the dominant
  cost multiplier on storage, meshing, and simulation, and is the reason
  `world/chunk-representation` and the coordinate decisions matter so much.

## world/voxel-first — Keep things in voxel form; depart from blocks only when it really doesn't make sense

- Decision: take advantage of our small voxels (`world/voxel-scale`) to keep
  things **in voxel form**. Materials that can sensibly be a block are a
  block: agate, selenite and the other resonant crystals
  (`geology/resonant-crystals`) are cubical blocks, so they are placeable
  under the ordinary block rules with no special case. We depart from blocks
  only where a block really doesn't make sense (plant fibre, nuggets, a
  knapped flint).
- **Blocks are placed 1:1 with cells:** a block in the world always occupies
  exactly one cell. There are no piles or heaps of blocks; non-block items
  use ground storage (`inventory/ground-storage`).
- Rationale: one placement model for most things, orientation and symmetry
  for free (`world/block-orientation`), and fewer item models to author.

## world/cubic-chunks — Cubic chunks, 64³, no vertical limit

- Decision: Cubic chunks, **64³**. Unlimited height/depth — chunks stack freely
  on the vertical axis.
- Rationale: Symmetric addressing suits a many-dimensions model; no artificial
  world ceiling/floor. **Chunk size is a work-granularity decision, not a storage
  one:** `world/chunk-representation` makes storage cost proportional to a
  chunk's *entropy*, not its voxel count — a sky chunk is ~1 palette entry (air),
  a solid-stone chunk ~1 entry (stone), and only a genuinely heterogeneous chunk
  approaches a dense 64³ array. Larger chunks then *help* by amortizing per-chunk
  overhead (coord key, rep header, DB row) over more mostly-free voxels.
- Consequences: 64³ = 262,144 voxels. The real cost of large chunks is
  work-granularity — re-meshing, loading, and cross-chunk sync operate at chunk
  resolution, so a single edit could dirty a whole-chunk remesh.
  Mitigation: **bitmask meshing makes a whole-chunk remesh cheap**
  (`world/chunk-meshing`), so meshing in sub-chunk blocks is a fallback,
  only if measurement asks for it. **64 is
  also the 64-bit lane width:** a row of cells along x is one `uint64`, which is
  what makes bitwise face culling and greedy merging work 64 cells per
  instruction — a second reason for the size beyond amortization. Chunk
  address is (dimension_id, chunk_coord); see `world/dimensions`.

## world/progressive-lod — Chunk generation is progressive, deterministic LOD refinement

- Decision: Chunks generate at coarse LOD first and **refine on approach** as
  lazy, deterministic, seed-derived detail. A distant chunk may hold only
  "stone" (a superclass) or a sky-facing "grass block" without foliage; closer
  in, it specializes — stone → a specific stone (limestone/marble/granite) from
  finer noise, grass → foliage/grass variants, etc.
- **Hard refinement-hierarchy invariant:** each finer LOD is a *constrained
  subdivision* of the coarser one, never an independent draw. A superclass can
  only resolve into its members ("stone" → an actual stone; never into a lake).
  Coarsening a fine result back up must still read as the coarse category, so the
  world the player saw from afar never contradicts what they find on approach.
- **Information LoD, not rendering LoD:** distant chunks literally **hold
  less information**, as `worldgen/metered-information` asks. Instead of one
  "chunk render distance" there are **many refinement thresholds**, and a
  chunk is revisited and refined as it is approached. For terrain shape,
  levels of detail are `world/octave-pyramid`'s: about one octave per level,
  each level on its node's sample grid, so coarse levels are not on the unit grid and the
  former "no geometric cracks between stages" no longer holds (seams are
  that entry's open question). Materials and features refine in place, as
  below. The notes and the ladder below are proposals.
- **There is no "surface"**: the world is fully 3D, and the camera
  may be deep underground in a cavern of mushroom trees. Nothing in the ladder
  may assume a terrain surface.
- **Shape refines by frequency band**: low-frequency
  density first (mountains, huge caverns and voids), finer fields and octaves
  later (small caves, overhangs, passages). Caves are not a separate stage;
  they are the high-frequency part of shape, and the cave fields
  (`worldgen/noise-fields`) join at the stage matching their scale.
  The remaining octaves' amplitude bounds which cells a later stage can still
  flip between solid and air, so large structures never change: the shape
  form of "stone never resolves into a lake". `render/shadows` uses the same
  octave-bounded intervals.
- **The ladder is ordered by what is visible from a distance:** shape octaves,
  coarse to fine; then materials by superclass (stone → rock family →
  specific rock, `geology/no-generic-stone`; the same for soils); then
  features by size: **placeholder large features** first (trees, mushroom
  trees: a trunk column plus a canopy superclass that may resolve to leaves,
  branches or air, since trees are visible further out), then real trees
  (stamped from their root blocks, `worldgen/tree-generation`), then small vegetation and
  decoration, with ore sites (`geology/ore-sites`) last. Stage boundaries are
  to tune.
- **Superclasses are blocks** (owner, 2026-11-27): "stone", a soil or a
  canopy placeholder is a block definition with a ULID, held in Brick
  palettes like any block state (`world/block-states`), and its members are the
  blocks it may resolve into.
  - **`invariant/world/superclasses-unobtainable`** — No superclass block
    is ever obtained: refinement replaces it before terrain is playable,
    and if one is mined anyway (refinement ran late), its drop table
    yields a member chosen at random (a specific stone for generic stone),
    never the superclass itself.
    - **Enforced:** type: a superclass definition has no item form, so no
      inventory can hold one; test: every superclass's drop table yields
      only members. Drop tables arrive with block behaviour; until then
      there are no superclass blocks.
- **Each stage is directly computable:** a pure function of (seed, box,
  stage), the box being a chunk near the player or a larger volume of
  distant terrain (`worldgen/cartographer`), built so its choices are members of the coarser stage's classes.
  The refinement invariant then holds by construction, not by data flow, so
  stages parallelize (`core/isolation-first`). A teleport could jump
  straight to full detail, but generation is strictly outside in
  (`world/surveyor`): ancestors are cheap, so a destination's coarser
  levels are built first, while a portal powers up. Refinement reads no
  neighbour chunk: trees are stamped by every chunk they reach, from roots
  within a halo (`worldgen/tree-generation`), and only huge trees continue
  across chunks (`worldgen/feature-continuations`).
- **Generation is monotonic**: a chunk only refines, or is
  discarded and regenerated later. Since refinement is constrained
  subdivision, an unedited chunk projected down to stage k equals stage-k
  generation, so dropping detail when moving away is free. **Edited chunks**
  are the exception: their coarse view must be projected from their content,
  the one place data flows up.
- **What decides refinement:** distance, **visibility hints** and **bound
  classification**:
  - **visibility:** each stage records the face-connectivity table of its
    chunk or box
    (`world/chunk-meshing` layer 3, `core/opaque-chunk-queries`), and a flood
    fill from the camera's chunk through connected faces gives the chunks the
    camera could see into. Only those refine further. The fill is allowed to
    lag. It reads only the published tables, through the same soft
    mechanism as everything else; each chunk may answer its table its own way
    (uniform O(1), flood fill, coarse-stage bounds, finer sub-tables);
  - **bounds:** when a stage's remaining octaves can't move any cell across
    the threshold, the chunk is certainly solid or certainly air at full
    detail and needs no fine evaluation.
- **Errors are transient and self-correcting, not forbidden**: players
  forgive pop-in and approximation; they don't forgive
  stuck voids or persistent terrain glitches. Answers are level-triggered, so
  a wrong one is corrected as the chunk refines, its table is recomputed or
  the camera moves. Each use picks its bias: **refinement may best-guess**
  (a wrong "closed" only leaves a chunk coarse a little longer); **drawing
  leans open** (a wrong "closed" would show a hole).
- **Geometric LoD for distant terrain is `world/octave-pyramid`'s:** the
  mesh at a level is extracted from that level's partial sum, not by
  decimating a fully generated chunk's mesh. Extracted geometry is
  preferred over impostors, which need a horizon far enough to be
  convincing.
- Rationale: finer noise is seeded deterministically from
  (seed, chunk, subcell, LOD level), refining *within* the coarse cell's
  already-decided category — enabled by `core/worldgen-integer-domain` at
  every level. Approach/retreat/re-approach regenerates identical detail (unless
  the player modified it, at which point `persist/diegetic-persistence` governs).
- **Specialization is a general mechanism with more than one axis.** Resolving a
  superclass into a member is keyed on a context that includes LOD *and* season
  (`sim/seasons` resolves generic-leaf → seasonal member) — and potentially other
  axes later. Same superclass→member discipline and refinement-hierarchy
  invariant regardless of which axis drives the resolution.
- Consequences:
  - **Representation follows the level:** at full detail a chunk is cells in
    a representation (`world/chunk-representation`); a coarse level of
    terrain shape is a box's composed fields, not cells (`world/octave-pyramid`).
    How materials and features are held at coarse levels is part of chunk
    data's open format (`world/chunk`).
  - **Defers the expensive fine-3D fields** — most notably the volumetric cave
    noise (`worldgen/noise-fields`): its fine octaves are evaluated only when a
    chunk refines to them, only for chunks the visibility hints reach, and not
    at all where bound classification says the chunk is certainly uniform.
    This is what makes per-voxel 3D cave noise affordable.
  - **No ledger interaction** (`data/entity-ledger`): LOD refinement is
    in-situ world shape/material only. Materializing an implied ore vein on
    approach mints nothing: refinement places only ore *sites*; a valuable
    ore is minted when its site is resolved on reveal (`geology/ore-sites`),
    a separate, low-volume step near players, and non-valuable material
    mints at extraction.
    This keeps the high-volume gen/LOD path fully decoupled from conservation.
- **LOD ↔ persistence share a principle, not a storage shape:** *don't store what
  generation can reproduce.* `world/progressive-lod` stores only the divergence
  from the seed baseline (coarse chunks refine toward seed-derived detail on
  demand); `persist/diegetic-persistence` decays an unstabilized chunk back toward
  its generation baseline over TIME and deletes it once it fully reverts (the chunk
  is reproducible again). Decay is a closed form from an anchor
  (`core/closed-form-first`), evaluated on demand — no simulation backlog
  on load (`sim/residency-levels`). Both lean on the same `core/worldgen-integer-domain` reproducibility — but persistence
  does NOT mandate a divergence-from-seed storage layout: it delegates the
  at-rest form to the chunk (`persist/diegetic-persistence`), which may store a
  diff, two concrete endpoints, or near-nothing. So the shared thing is the
  *principle* (reproducible ⇒ need not be stored), one resolved over spatial detail
  and one over time; the storage representation is each chunk's own choice.

## world/octave-pyramid (draft) — Terrain shape is a pyramid of octaves: one structure for generation, storage, rendering, replication and bounds

Terrain density is a Laplacian pyramid of noise octaves. Level k is the
partial sum of every octave from the coarsest down to octave k, and that one
structure is at once the generation stage, the stored terrain-node content, the level
of detail drawn, the unit replicated for distant terrain, and the source of
the bounds that skip fine evaluation. This entry owns terrain shape's levels
of detail; `world/progressive-lod` keeps the stage ladder for materials and
features, which are not additive.

- **Definition (normative), recursive, per field.** A node at tree
  level k holds the field's **composed field** `Pₖ` on its grid `Gₖ`:
  - `Pₖ = Iₖ(Pₖ₋₁) + Σⱼ∈Bₖ Sⱼ(Gₖ)` below a root: the parent's composed
    field interpolated onto the child's grid first, then the level's
    octaves in order of index, each evaluated directly at the grid's
    points; a dimension's density at level k is `bias + Pₖ`;
  - `Bₖ` is level k's bin of octaves (below), and `Sⱼ(Gₖ)` is octave j
    evaluated on `Gₖ`, with the octave's own lattice spacing and offset
    (`worldgen/noise-fields`: lattices are not aligned);
  - `Gₖ` is set by the node, not by the noise (next bullet); `Iₖ` is the
    one stated interpolation kernel, parent to child;
  - at a root, `P₀` is the root's folded octaves on their own grids (`The
    ends of the tree` below);
  - at a chunk, `Gₖ` is the voxel grid itself.
  The recursion, not a flat sum over octaves, is the definition: it fixes
  the order of every floating-point operation, so a node refined from its
  parent, one rebuilt from the root, the server and a client all compute
  the same bits. Interpolation is part of the definition, not an
  approximation of a continuous sum.
- **Grids are set by node size.** Below the roots, every node's grid is
  the same N samples per axis over its box (N = 64, plus one sample at the
  far edges), in the world's axes, so spacing halves with the node. A bin
  holding two octaves of a field leaves the grid unchanged: the coarser
  one is just sampled more finely than it needs, up to 2× per axis (up to
  8× its evaluations in 3D), accepted since bins rarely hold two and
  direct evaluation needs no second grid or kernel. N = 64 puts a level's
  finest octave at about two samples per wavelength (the Nyquist floor),
  so a node's edge is about 32 λ of its finest octave: chunks (64 units)
  sample about 2-unit octaves per voxel, 128-unit nodes 4-unit octaves, a 2¹⁵-unit
  node 1,024-unit octaves. A 2D field's grid is N² over the node's footprint.
- **An apron for the kernel:** each stored grid carries a margin of
  samples outside its box as wide as `Iₖ`'s reach (two per side for a
  cubic, one for linear), generated by the node's own turn, so a child is
  computed from its parent alone and no node waits on its neighbours.
  With N = 64, two samples per side cost about 13% in 2D and 20% in 3D.
- **One bin of scale per tree level:** each level adds the octaves of
  every field whose range reaches that scale (usually one per field,
  occasionally none or two, since lacunarity isn't exactly 2), so
  generation stages and render levels coincide. An octave is binned to the
  coarsest level that is refined by the time the viewer is within the
  octave's drawing radius (about 125 λ) and whose spacing samples it at
  least twice per wavelength, so its data is generated exactly when its
  detail becomes visible; binning uses each octave's maximum
  amplitude (`uplift` makes amplitude vary by place), so flat areas are
  slightly over-detailed, harmlessly. The mapping is static per dimension
  and generator version, and unaffected by resolution, field of view or
  the view-radius setting, which scale octave radii and node distances
  together. The tree holds sample grids only: lattices live inside the
  noise calls and are never stored, so lattice spacings can be irregular
  while every node, sample grid and box is a power of two.
- **The ends of the tree:** nodes halve strictly from the roots to the
  chunks, one bin per level.
  - **The chunk** (64 units, full detail) holds only its own, finest bin
    (octaves of about 2 units, sampled per voxel) plus the non-octave
    per-voxel work: hash noise for speckle and material variation, and the
    stage ladder's fine features.
  - **The roots** (2¹⁷ units) fold in every octave drawn across the whole
    view (about 1,000 units and coarser), the one exception to node-sized
    grids: those octaves span continental to kilometre scale, so each field
    gets its own grid over the root, sized by its finest root octave at
    two samples per wavelength (about 128² for the 2D terrain fields, about
    33³ for the coarse regional 3D fields), and `P₀` is each root octave
    interpolated onto it and summed in order of index.
  - **The fine bins stay separate levels** (about 4, 8, 16 units and up, in
    128, 256, 512-unit nodes): although every octave below about 16 units is
    visible only within the full-detail radius, keeping them as levels lets
    terrain degrade gracefully. When generation falls behind (fast flight,
    a weak CPU, a burst of new terrain), the mosaic draws the best level
    already generated and refines it level by level, so terrain sharpens
    in rounds, as somewhat larger blocks for a moment (the same look,
    `Rendering` above), rather than leaving gaps. Only a Vicinity's chunks
    need full detail, for the simulation; terrain beyond can lag visually.
  - The exact assignment of octaves to levels (with lacunarity not
    exactly 2) is to settle; levels just below the roots may hold none.
- **Batched work, not grouped data:** a request may generate a node and
  its descendants a few levels down at once (sharing key lookups and bulk
  calls), one composition turn may sum several levels, and joining
  generates several levels of a region per turn; the data stays one bin
  per level, keeping level-of-detail transitions smooth and pops small.
  With capacity, one turn generates a chunk with its missing fine
  ancestors, at no more latency than folding them would give; when
  behind, the planner generates one level at a time and the mosaic draws
  whatever is ready.
- **Refinement makes all eight children at once:** the unit of
  refinement is a node's eight children, since the mosaic only ever swaps a
  parent for all of them. One operation evaluates one contiguous grid at
  the children's spacing over the parent's box (2N per axis plus one far
  edge and one apron around the whole), interpolates the parent's composed
  field onto it in one pass, and runs one bulk call per key; each child's
  grid, apron included, is a block of it, so the aprons on internal faces
  are shared samples rather than separate evaluations (for N = 64, about
  11% fewer samples in 3D and 7% in 2D). A 2D field is evaluated once per
  footprint, shared by the upper and lower children above it, halving its
  work. The children are created, stored and released together, as one
  sibling set; certainly solid or air children carry a flag and no data.
- **What is stateless, and what is stored.** The noise function is
  stateless: evaluating an octave at a point is a pure function of position
  and seed, holding nothing between calls, and a lattice corner's value is
  cheaper to recompute than to store. The pipeline does store evaluated
  samples: a level's octaves are evaluated on its node's grid, and finer
  levels interpolate the result, as each node's composed field. Storing a
  grid of expensive evaluations pays, since each octave is evaluated only at
  its own level's spacing, never on the finest grid (for a far box with
  about five octaves per field, roughly 4× fewer evaluations; far more in
  3D and per voxel); storing cheap corner hashes doesn't.
  The stored samples are derived and deterministic, so any of them can be
  dropped and rebuilt with identical results, which is why terrain nodes
  are never authoritative.
- **No separate mipmapping:** coarser octaves are low-frequency by
  construction, so a partial sum is already the band-limited view for its
  distance: a node's composed field is its distant view, and interpolating
  it onto a child's grid is the upsampling refinement starts from.
- **Levels and distance:** an octave of amplitude A is worth drawing while
  its relief covers about a pixel. With a pixel near 6×10⁻⁴ rad (90° across
  2560 px) and a couple of pixels' tolerance, that is out to about
  A / 10⁻³. With `surface`-like ratios (A ≈ λ/8), about 125 λ: an 8-unit
  octave to about 1,000 units, 64 units to about 8,000 units, 512 units to about 12
  miles, 1024 units to about 24 miles. **The view radius is at most 2¹⁷ units**
  (about 25 miles at `world/voxel-scale`'s foot): a ceiling the terrain architecture is built on (root
  size, key spacings, tier radii), adjustable in settings only downwards.
  It sits at the top end of voxel games (Minecraft's
  maximum is about 512 blocks; Distant Horizons renders up to 4096 chunks
  across, about 32k blocks of radius) and well short of flight simulators.
  **Aerial perspective follows the setting:** spherical atmospheric haze
  around the viewer is scaled with the chosen view radius, so terrain fades
  out exactly where drawing stops and the limit always reads as atmosphere,
  never as an edge. So octaves coarser
  than about 1,000 units are drawn everywhere in view. Level selection follows
  this screen-space error, with hysteresis bands (`world/chunk-scheduling`),
  measured at the Euclidean distance from the viewer to the nearest point of
  each node's box, the same in every direction (`world/surveyor`).
- **Storage is cumulative, per field:** a box at level k holds the
  composed field `Pₖ` of each field whose octaves reach its level, on its
  node-sized grid with its apron; a terrain node's content at level k is exactly that
  (`world/chunk`). Increments are never stored: a level's octaves are
  generated fresh, added to the parent's composed field, and dropped. A
  field with no octave in the level's bin is not copied: the node's turns
  read it from the nearest ancestor holding it, interpolating as needed
  (for the coarse regional fields, a root, resident whenever in view).
  Meshing, `surface`, bounds, materials and replication all consume
  composed fields, never a single band, and a node needs only its own
  grids and those ancestors', so it can be drawn, sent or evicted on its
  own. **Node grids are const in the type system,** not by convention: the
  object is created const, its buffers propagate const (vectors, never
  plain pointers), and it has no `mutable` members or lazily filled fields;
  anything learnt about a node later lives in the planner's state. A node
  is an `TerrainNode`, held in a sibling set, a goal actor the World owns
  (`world/surveyor`, `engine/goal-source`). Below full
  detail these are sample grids, not voxels: voxels exist only when a
  chunk-sized box reaches full detail. Across the tree the sizes
  shrink geometrically, about 1 + 1/8 + 1/64 + … ≈ 1.14 times the finest
  level stored. Coarse volumes are world-aligned power-of-two boxes
  (`worldgen/seed-keys`).
- **Worked example: a far terrain node** (Terra; a 2¹⁵-unit box, about 6
  miles, about 20 miles away, at a level whose finest octave is about
  1024 units, a far-tier octave drawn out to about 24 miles):
  1. **keys:** `requiredKeys` gives the far-tier key columns and cells
     touching the box, widened by transitions and the boundary warp: a few,
     at most 3×3 columns and 3×3×3 cells, with each field's seed and
     rotation from the key cache;
  2. **increments:** for each field with an octave in this level's bin
     (here the 1024-unit octaves), the node's 64×64 grid over the footprint,
     plus one sample at the far edges and the apron; one bulk FastNoise2
     call per key with a nonzero weight in the box, at rotated local
     positions, then the warped, normalised blend, giving Sⱼ; the coarse
     regional 3D fields have no octave this fine and are read from the
     root;
  3. **compose:** interpolate the parent's composed field onto the box's
     grid, then add each Sⱼ, with amplitudes (the recursion above);
  4. **derive `surface`** from `landmass`, `uplift` and `erosion` by the
     coast profile and ridged uplift (`worldgen/noise-fields`); at this
     level Terra's terrain is that height field;
  5. **bounds:** with the unevaluated octaves' amplitude A, below
     `surface − A` is certainly solid and above `surface + A` certainly
     air, so most of the 6-mile-tall box is classified without evaluation;
  6. **coarse materials** from the stage ladder (water below sea level,
     rock or soil from coarse fields, `world/progressive-lod`);
  7. **output:** the composed fields, the `surface` grid, bounds and
     materials, meshed by a mesh goal actor as a smooth, face-weighted surface
     with skirts; refining later starts from these composed fields and adds
     the next level's octaves.
  At 20 miles the box spans about 0.3 rad, about 500 px, so 64 samples
  across is about 8 px per sample, enough because the next octave down
  (512 units, drawn only within about 12 miles) moves the surface by about a
  pixel there, at the level rule's threshold; it costs a few thousand
  samples per field.
- **Generation:** a Cartographer request (box, level) returns that box's
  composed fields (`worldgen/cartographer`). Refinement starts from the
  parent's composed field and adds the level's octaves, generated fresh
  from the keys; nothing reads or modifies stored data.
- **Bounds:** the amplitude left after level k is the sum of the finer
  octaves' amplitudes, so at level k each sample is known to within
  ± Σⱼ<ₖ Aⱼ. Where that interval doesn't straddle the threshold, the
  cell is certainly solid or certainly air at full detail and the finer
  octaves are never evaluated there (`world/progressive-lod`'s bound
  classification, `render/shadows`'s intervals).
- **Rendering:** the surface at level k comes from the partial sum on
  `Gₖ`. Density is additive; meshes are not, because terrain is a threshold
  of density, so each level's surface is extracted again rather than
  combined.
- **The target look is blocks about 1–2 px across at every distance**: near
  terrain is drawn as its real 1-unit blocks; further out the
  cube size grows by powers of two with distance, so blocks stay about a
  pixel or two on screen and distant terrain reads as continuous with near
  terrain. A 1-unit block covers one pixel at about 1,200 units (1920 px across
  90°), 1,600 units (2560 px) and 2,400 units (3840 px); within about 800 units it
  is clearly visible as a block. Haze matters only at miles, so it hides
  coarse levels, not the 1-unit cells.
- **Three ways of drawing it, by cube size on screen**:
  - **Above about 2 px** (near terrain, roughly the first 800–1,600 units):
    real cube meshes (`world/chunk-meshing`).
  - **About 1–4 px (middle distance): DDA ray-marching of the pyramid**
    (an experiment). Each box's partial sum is baked into a small 3D texture
    by a GPU compute pass whenever the box changes level; its bounding proxy
    is drawn, and the fragment shader steps through it cube by cube until
    the density crosses the threshold, writing depth and the G-buffer like
    any surface (`render/deferred-pipeline`). Real cubes with correct
    occlusion and silhouettes, no geometry beyond the proxy, and no cracks,
    since neighbouring boxes sample one continuous field. A per-cell
    minimum and maximum (the pyramid's own bounds) skip empty space; a fixed
    step budget per pixel falls back to the smooth surface when exhausted.
  - **Under about 1 px (far distance): smooth isosurfaces with face-weighted
    shading.** The surface is shaded as the staircase of blocks it stands
    for would look on average, treating that staircase as a microsurface
    whose facets point only along the six axis directions (the idea
    microfacet shading models are built on). A staircase approximating a
    surface of normal n has facet areas in proportion to |nₓ|, |n_y| and
    |n_z|; each facet direction's contribution is weighted by its area,
    times its projected area towards the viewer, times a masking term for
    facets hidden behind neighbouring steps, and it is lit with the same
    terms towards the sun (shadowing, risers shading treads at low sun).
    So the mix is view- and light-dependent, as real distant blocks are:
    mostly risers (side colour) from a low eye-line, mostly treads (top
    colour) from above. The block textures are mapped triplanar by the same
    weights. This gives the average appearance a well-antialiased block
    world shows, including the shift with eye-line and sun, without the
    sub-pixel sparkle that is aliasing in real block renders; a little
    sparkle can be added on top as controlled noise if wanted, an art
    choice. A smooth mesh needs perhaps a tenth of the triangles of
    pixel-sized cubes (which greedy meshing can't merge on slopes, leaving
    about one quad per 1–4 px).
- **Seams**: **skirts** (short flaps hanging from each smooth
  box's edges) hide the cracks between smooth levels, which suffices because
  the steps between levels are sub-pixel by the level rule. Cube meshes and
  ray-marched boxes don't crack. Transvoxel is not needed.
- **One lighting model:** every way of drawing feeds the same lighting,
  including sky exposure (`render/shadows`), so the transitions between
  them don't show.
- **Reconstruction:** trilinear for coarse levels; cubic for levels drawn
  near the player, where trilinear's creases and periodic variance banding
  would show. Each octave keeps its own lattice offset, so lattice points
  never line up across levels.
- **Octave tails:** Perlin octaves are not perfectly band-limited, so a
  partial sum carries a little energy above its level's Nyquist limit and
  may shimmer slightly at distance; pre-filtering an octave per level is
  the remedy if it shows.
- **What is not additive:**
  - **Materials and features** follow `world/progressive-lod`'s stage
    ladder, keyed on the same levels.
  - **Edited chunks** are not band-limited: their coarse view is a real
    downsampling (a mip filter) of their content, the one place mipmapping
    in the usual sense remains, and their detail comes from their chunk blob.
- **Replication:** for distant terrain the server sends either keys, so the
  client generates the composed fields itself, or the composed fields
  themselves (edited areas, or clients without the keys)
  (`worldgen/seed-keys`, `net/replication`).
- **Open:**
  - Whether DDA ray-marching earns its place in the middle distance, and its
    cost in fragment time (marched pixels write depth, which gives up early
    depth rejection for them); shadows for marched boxes (marched too, or
    from the smooth surface).
  - Where exactly each way of drawing hands over, with hysteresis.
  - **The kernel `Iₖ`:** linear needs a one-sample apron but leaves visible
    facets at about two samples per wavelength; an interpolating cubic,
    applied level by level on nested power-of-two grids, is the 4-point
    Dubuc–Deslauriers subdivision scheme, which converges to a smooth
    surface, needs a two-sample apron and no state beyond the samples. An
    approximating scheme (a cubic B-spline) would need control points
    distinct from the values, so it is avoided. Whether the coarse
    regional fields use the same kernel.
  - **Aliasing, deferred until it is seen:** sources are octave tails,
    domain warp compressing features, and above all nonlinear shaping
    (ridges, the `surface` splines, the solid/air threshold), which creates
    sharp edges; it shows as shimmer at edges and silhouettes and, more
    subtly, in shading, since each octave's slope is about the same however
    small it is. Remedies if needed: pre-filter an octave's grid at the
    level it is drawn, sample warped or ridged octaves more densely, and
    shade distant terrain with the omitted octaves' slope variance as
    roughness (Toksvig or LEAN style). Tune after the nonlinear terrain
    shaping is in place, since that dominates. The hysteresis bands between
    levels are also open.
  - **Reconstruction from shared, aligned grids, deferred until it is
    seen:** each octave is evaluated once, at about two samples per
    wavelength on its level's world-aligned grid, and interpolated down the
    tree, so octaves seen up close (about 8 to 64 units at full detail) carry
    interpolation error with a slight world-axis bias at power-of-two
    spacings. This is separate from Perlin noise's intrinsic lattice bias,
    which the per-octave rotations remove. Sampling noise on a coarse grid
    and interpolating is standard (Minecraft samples its density every few
    blocks and interpolates; quadtree planet renderers keep a fixed grid of
    samples per node), and its known artifact is square-shaped distortion,
    which mods such as KdotJPG's Simply Improved Terrain reduce. Refined
    only if perceptible, in order: evaluate 2D fields without interpolation
    at full detail (cheap); re-evaluate an octave exactly one level after it
    enters, replacing its interpolated contribution; sample at about four
    per wavelength.

## world/brick — A Brick is 64³ cells at a power-of-two cell size, in one palettized form
- **Specified in `design/brick.md`** (an entity doc): the engine's one cell
  storage; a chunk's cells are a full-detail Brick, owned by the chunk actor
  or a terrain node.

## world/block-states — Block definitions, block states and the state table
- **Specified in `design/block-states.md`** (an entity doc): a state is a
  definition plus its variant property values, identified by a BLAKE3
  state hash of a canonical text, and numbered in each process by a
  `StateId` into the shared state table, which every Brick's palette
  indexes.

## world/chunk-representation — Virtualized chunk representation (multiple reps, one interface)

- Decision: Chunks expose a common interface backed by swappable internal
  representations, selectable per chunk and changeable over a chunk's
  lifetime. Today these are a Brick's two (`world/brick`): uniform, and a
  palette at a width from 1 to 32 bits, which covers what a separate dense
  array would. Others may be added behind the same interface if
  measurement asks; run-length encoding and a sparse octree inside a chunk
  are not planned (uniform and narrow palettes cover the first, and
  sparsity above the chunk is the terrain octree's, `world/surveyor`).
- Rationale: The escape valve for `world/voxel-scale`'s ~27× density cost:
  a representation fitted to each chunk's content, compact where it is
  uniform or uses few states.
- Consequences: The interface is the contract; reps are an implementation
  detail. Enables profiling-driven tuning without touching callers. Chunks are
  also the unit of independent simulation (`core/chunk-locality`): a chunk is
  mutated only by its own turn (`sim/concurrency-model`), at a residency level
  chosen by viewpoints (`sim/residency-levels`).
- **Enforcement:** the "interface is the contract, reps are private" promise is
  made a hard invariant by `core/opaque-chunk-queries` — no public
  `getVoxel`, consumers ask semantic queries, type-enforced. That tenet is
  what keeps this decision from silently eroding.
- **The query summary** (moved from `core/opaque-chunk-queries`):
  answers come from a small derived cache beside the representation (the 6×6
  face graph, a solid/transparent bitmask, a material histogram, an optional
  per-column surface-Z cache), not an on-demand scan. It refreshes on edit, or
  lazily when its generation no longer matches the chunk's; a remesh push is
  only a hint (`core/level-triggered`). It is never digested, and is distinct
  from `data/content-digest` (it was once also called a "digest"). Interface
  finders such as `surfaceInterfaces(predicate)` serve
  `worldgen/surface-free-placement`.
- **The representations are a Brick's** (`world/brick`): **uniform** and
  **palette**, with up to 2¹⁸ = 64³ distinct live states per Brick, so the
  palette covers everything a separate dense array would. Comparisons across representations (uniform against palette, and a
  palette at different widths) replace a dense reference representation;
  the per-cell rule remains the meshing reference. Tests use the real chunk
  code, never a separate world implementation. **The representation follows the content,
  not the region:** a chunk is generated into a fixed generator palette,
  then stored in the most compact representation holding it; edits promote
  uniform → palette and only widen, and a chunk returns to uniform or
  narrows only through a bulk fill or a compaction (`world/brick`; when,
  below). A generator with known bounds may
  classify a chunk as uniform **without sampling it**; that shortcut must
  agree exactly with full generation (a test, not an assumption).
- **The owner's Brick policies** (moved from `world/brick`; the Brick
  provides the mechanics, its owner, a chunk or a terrain node, decides):
  - **Widening is proactive, at a high-water mark:** once a palette passes
    15/16 of its capacity (a placeholder), the owner schedules the widening
    for a later turn of its own, and edits keep appending into the headroom
    meanwhile, so the copy is off the edit's path. An edit that finds the
    palette full anyway (a paste, a fill, created blocks in one turn)
    widens in its own turn; correct, only slower. Telemetry counts those
    fallbacks, which tune the mark. The swap happens in the owner's own
    turn, outside any write window, which holds only the pointer store.
  - **Compaction is never automatic** (owner, 2026-11-27): neither the
    Brick nor its owner has the context to schedule it, so it runs only
    when a caller that does asks: **at a save**, when the save asks
    (encoding a chunk blob visits every cell anyway, and the owner swaps
    the compacted Brick in as its live one); **at a bulk fill**, which ends
    with one; and **on request**, an explicit operation (to the plain
    low-water mark, or leaving room for more entries) that nothing calls
    yet. The gap between the low-water and high-water marks is the
    hysteresis: a just-compacted Brick fills at least 1/16 of its capacity
    before it widens again. A possible policy for churning Bricks, if a caller
    ever needs one: compact once the palette has gained max(F, α·L₀)
    entries since the last compaction (L₀ the live count that compaction
    found; F = 4096 and α = 1/4, placeholders), with room for F more
    entries; the owner counts appends.
  - **The reverse lookup** (state id to palette index, which an edit needs
    to find whether its state is already an entry) is the owner's: only
    the owner's turn edits and no reader uses it, so it needs no
    synchronisation. A scalar scan of the palette up to 8 entries
    (measured; a placeholder), above that a flat linear-probing table of
    palette indices at load at most ½, sized to the palette's capacity
    when built, so it never regrows within one Brick's life; a swap drops
    it, and the next lookup that needs it rebuilds it in O(entries). It is
    built on the first edit that needs it and dropped when the owner stops
    editing (a chunk freezing or saving). An edit that finds a dead entry
    for its state reuses it; writers that already know an index (copying a
    cell, one state across many cells) write indices directly.
  - **Generation writes indices into fixed palettes:** each generator (a
    biome's material set, say) declares its palette up front and writes
    indices at a width fixed in advance, with no lookup per cell; bulk
    fills do the same. The dynamic path (append, grow, the reverse lookup)
    is for edits afterwards. Fixed-palette entries a Brick never uses are
    dead entries, dropped at the next compaction.
  - **Concurrent reads:** a retired Brick is never written after its swap,
    so a reader still holding one sees a consistent but stale snapshot;
    only the live Brick's in-place cell writes and appends can be read
    mid-change, which the version counter (below) detects. How much of that
    each reader tolerates or validates away is the owner's policy, behind
    its API (open: the policy per reader).
- **The owner's invariants over its Brick** (moved from `world/brick`,
  where the class can't check them):
  - **`invariant/chunk-representation/distinct-palette`** — No two entries
    of one palette name the same state.
    - **Enforced:** by construction: an edit appends only after the
      reverse lookup misses (reusing a dead entry when it finds one), and
      growth and compaction copy or renumber the entries; test: a
      duplicate-entry audit (`ChunkTestAccess::paletteDistinct`) after the
      randomized equivalence edits and in the dead-entry tests.
  - **`invariant/chunk-representation/edits-never-compact`** — An edit
    never swaps in a compacted, narrower or uniform Brick; only an explicit
    compaction (a save, a bulk fill, a request) does.
    - **Enforced:** structure: the edit path calls only growth; test: the
      representation tests and the randomized equivalence test (a forced
      width never shrinks under edits).
- **Version counters for concurrent reads** (`sim/concurrency-model`,
  "Reading chunks"): a palette-form chunk carries a
  seqlock-style version counter, one per chunk to start,
  so other threads read the live arrays zero-copy and validate;
  nothing is copied or published. Per-subchunk counters (say 16³) only if
  instrumented retries call for them; "subchunk" is defined per
  representation. Private to the representation.

## world/chunk-meshing — Meshing is a per-representation chunk query, isolated per chunk; seams belong to the LoD design
- **Open:** occlusion culling and AO (later layers); whole-chunk remesh cost
  and seam cost, to measure in v0.2.

- **Scope:** chunks at full detail (actors in a Vicinity, and full-detail
  terrain nodes the World holds). Distant terrain is drawn per
  `world/octave-pyramid`, from boxes, not chunk meshes.
- **Three layers, kept separate:**
  1. **Cell visibility classes** — camera-independent, per cell: opaque,
     transparent (air), cutout (leaves: solid but see-through), translucent with
     a group (glass, water). A property of the cell's contents.
  2. **Faces (the mesh)** — camera-independent *potentially visible* surfaces:
     a face is emitted where a cell borders one that doesn't cover it. A cave
     sealed in rock is still meshed.
  3. **Occlusion culling** — camera-DEPENDENT, per frame: which chunks the
     camera can actually see into, by flooding outward through `core/opaque-chunk-queries`'s 6×6
     `faceConnectivity` digest (the "advanced cave culling" technique), run
     by the frame thread over its render table (`engine/frame-thread`),
     from v0.2.16. It decides what is drawn; the visibility hints that
     decide what is refined (`world/progressive-lod`) are a separate,
     World-side fill that may reuse the same connectivity data, not yet
     scheduled. GPU depth-pyramid occlusion is a later step.
- **Face rule (who owns a boundary face):** a chunk emits faces only for **its
  own cells**, treating cells outside it as air (isolated meshing, below). A face between two cells is drawn on the side whose cell is not
  covered by the other: opaque next to non-opaque → the opaque side's face;
  cutout never hides its neighbour, an opaque neighbour hides a cutout face,
  and two cutout cells of one block type hide each other's faces (v0.2.5; a
  meshing rule only, to revisit once foliage exists); translucent next to translucent of the
  **same group** → nothing (a glass wall reads as one pane), different group
  or air → a face. Two opaque or two transparent cells → nothing.
  **Translucent cells emit their own boundary** (`render/transparency`,
  "Volumetric absorbance from boundaries"): a
  translucent cell emits a face, wound outward, wherever its neighbour is not
  the same group — **including against opaque cells** (absorbance only, never
  shaded) — so an interface between two groups is two coplanar faces, one per
  side. Closed per-group boundaries are what make absorbance exact.
- **Meshing is isolated: a pure function of one chunk.** `buildMesh() → mesh` reads only the chunk's own cells and
  treats everything outside it as air, so a chunk meshes correctly the moment
  it exists, hovering in the void, with no reference to any other chunk.
  - **At seams this is conservative:** every boundary face is emitted. An
    opaque face against a solid neighbour is never visible, only extra
    geometry. Glass across a seam shows an internal surface and frame lines
    at the chunk boundary (absorbance stays exact: the two coplanar faces
    cancel in the boundary integral). Accepted as a known limitation.
  - **Seams are part of the LoD co-design** (v0.2's LoD releases): what a chunk shows at a
    boundary it doesn't know, or doesn't trust, is one problem, whether the
    neighbour is the same level or another one, near the camera or far. No
    same-level seam mechanism is built first, and no halo query or neighbour
    interface is designed for meshing until then. A dropped design of
    neighbour halos (generation numbers as verification, push-style remesh
    hints) is in `attic/concurrency-history.md` for that work.
- **Principles for rendering**:
  1. **Visible pop-in is acceptable** to address seaming if it buys simplicity
     and concurrency, as LoD already accepts at distance.
  2. **One set of soft concurrency mechanisms for rendering and simulation**
     (`core/isolation-first`):
     if rendering ever reads across chunks, it uses the same carefully
     engineered mechanism the simulation uses, not a second one. Every
     cross-chunk refinement is asked whether it must be deterministic and
     whether it must be instantaneous.
- **Each representation answers the query its own way**
  (`core/opaque-chunk-queries`):
  - *uniform air* — emits nothing;
  - *uniform solid* — its six sides, each one greedy-merged quad;
  - *palette* — **bitmask ("binary") meshing**: build occupancy masks from
    the palette indices (a row of 64 cells along x = one `uint64`), cull
    with word operations (adjacent row words for y and z, shifts within a
    word for x: `row & ~(row >> 1)`), and **greedy-merge** coplanar faces
    with equal **appearance ids** (`render/block-textures`: equal face
    payload, so distinct states that look the same on a face merge, and
    differently turned faces don't) with shifts and masks. Only the ±x
    face planes need 64×64 bit transposes, so their slices run along x.
    The masks and planes are a **private working set** of the palette
    representation, built for meshing and cached or discarded — never part of
    the public digest;
    - **The greedy rule is a CPU–GPU trade-off kept simple for now:** one
      fixed, deterministic rule (along the row first, then across rows).
      It covers the same faces as any rule; only quad shapes differ. Rules
      favouring squarer quads cost more CPU per mesh but rasterise more
      efficiently (fewer thin triangles), make fewer T-junctions and give
      tighter per-quad bounds for GPU culling; exercised if measurement
      with GPU-driven draws asks.
    - **T-junctions:** first measured (pinhole cracks where a quad's corner
      lies on another's edge); if they show, opaque quads are enlarged
      slightly in the vertex shader, a cheap and reversible remedy.
      Translucent quads are never enlarged, which would blend twice and
      break exact absorbance. Measured in v0.2.3: isolated pixels on
      lavapipe, nothing visible on GPU hardware, so deferred (owner).
    - **Translucent faces merge too** (v0.2.5), with one constraint: a
      quad grows only while each of its sides stays uniformly framed or
      unframed (`render/block-textures`), so four side bits and four
      corner bits in the quad describe its connected frames exactly, and
      the mesh stays a function of the chunk alone. Their merge key is the
      appearance id plus whether the face shows the surface layer. Jagged
      outlines cost some extra quads; a per-chunk volume of glass groups
      read by the fragment shader was the alternative, rejected for its
      memory and its reads beyond the quad.
  - *the per-cell rule* — the reference meshing that the bitmask mesher
    must match, over any representation.
- **Whole-chunk remesh, no sub-blocks** (supersedes the sub-chunk mesh
  blocks once proposed under `world/cubic-chunks`): bitmask meshing is
  expected to remesh a 64³ chunk well under a millisecond (published claims,
  unverified here). v0.2 records remesh time per chunk; sub-blocks or
  incremental face patching come back only if measurement asks. If
  single-edit latency ever hitches, the standard hybrid is: patch the affected
  faces immediately without merging, then re-merge in the background. An edit
  remeshes only its own chunk.
- **Rep-swap invariance (the key test):** the same content stored uniform
  or as a palette at any width must yield an **identical mesh**. The meshing paths
  share no code, so this is a real check that the interface is the contract
  and the representation is private.
- Why: the seam bugs voxel engines suffer — holes or hidden walls from
  off-by-one or axis errors at boundaries, forgotten remeshes when a neighbour
  arrives, stale neighbour data — cannot happen when no chunk's mesh depends
  on another's. The price is extra seam geometry and visible glass seams until
  the LoD design addresses them.

## world/chunk-scheduling — Generation is a dynamically-reprioritized frontier keyed on live camera state

- Decision: when generation work is oversubscribed (always, near the
  camera), what runs next follows a **dynamic priority** recomputed as
  viewpoints move, not the order work was asked for. Places entering view
  or range rise; places leaving sink, or their work is withdrawn. This is
  the Surveyor's priority policy (`world/surveyor`) over the goal actors it
  registers with the World's `GoalSource` (`engine/goal-source`); the
  Dispenser runs them (`engine/dispenser`). The same viewpoints decide
  which chunks run in Vicinities and which are frozen around them
  (`sim/residency-levels`).
- **Priority is evaluated when work is taken, not baked in when it is
  asked for:** GoalSource ranks runnable goal actors as a worker pulls, and
  the Surveyor updates priorities in its planning turns as viewpoints move,
  so no stored order goes stale. A dependency inherits the highest priority
  of the goal actors waiting on it, so needed ancestors are never starved
  by their own urgency.
- **The priority function is view-aware:** distance (the Euclidean distance
  to the nearest point of the node's box, `world/surveyor`) sets both
  urgency and the level wanted; the view frustum and facing boost work
  ahead of the camera; motion prediction leads a moving viewpoint, so
  generation stays ahead rather than chasing it. During play, refinement
  is ranked by the screen-space error it removes per unit of work
  (`world/surveyor`).
- **The frontier is over terrain nodes and stages, not flat chunks:**
  terrain is a progression from coarse to fine (`world/progressive-lod`,
  `world/octave-pyramid`), so the frontier is "bring this terrain node to
  this level or stage", and priority arbitrates across both.
- **Ordering constraints are dependencies, not queue order:** refinement is
  outside in by construction (a node depends on its parent), and stages of
  a node depend on the stage before. GoalSource sequences those; among
  runnable goal actors the order is free, so reprioritizing never has to
  untangle anything. Generation reads no neighbouring node (trees are
  stamped by each chunk they reach, `worldgen/tree-generation`), and is
  never skipped under load: load may delay it, never alter it
  (`sim/subsystem-rate-degradation`).
- **Withdrawal with hysteresis:** work for a place that leaves range is
  withdrawn, which abandons it and, by cascade, everything waiting on it
  that hasn't started; a turn already running finishes (and publishes
  nothing if withdrawn). A soft band (`core/soft-thresholds`) rather than
  a hard radius keeps a viewpoint jittering near the edge from thrashing
  work in and out. Persisted chunks are loaded rather than regenerated
  (`persist/diegetic-persistence`), so this concerns generation work, not
  a stored chunk's fate.
- Open (see `open-questions`, `world/chunk-scheduling-tuning`): the exact
  priority weighting (distance, frustum, predicted motion), the hysteresis
  bands, and finding the best runnable goal actor cheaply.


## world/block-orientation — Block orientation is the cube rotation group (24); self-symmetry declared in the grammar

- Decision: a block's orientation is an element of the **cube's proper rotation
  group O ≅ S₄ (order 24)** — 6 facings × 4 spins, including the 120°
  corner-to-corner (body-diagonal) rotations. **Every block accepts every
  rotation**; there are no per-block placement rules. (Contrast Minecraft, which
  hardcodes per-block subsets — 4 horizontal facings for a furnace, 3 axes for
  logs, 6 facings for pistons — and can never express a furnace facing up or a
  body-diagonal rotation.)
- **No mirror symmetries.** Reflections are non-physical — you cannot rotate a
  real object into its mirror image — so they are excluded from the group. This
  keeps every orientation a physical rotation and simplifies equality/diffs.
  Consequence (accepted): a structure-mirroring operation cannot produce the
  mirror image of a **chiral** block; where a left/right pair is wanted, the
  author provides two distinct blocks. Also removes all handedness concerns
  from rendering (no winding reversal or bitangent-sign flips per orientation).
- **Self-symmetry is declared in the grammar (`data/grammar`).** O has **11
  conjugacy classes of subgroups** — the possible self-symmetry *types*. The
  grammar uses **intuitive names as keywords** (and in UI); the math names are
  the precise definitions. Each name is the geometric feature an orientation
  picks out, so the name also tells the author the orientation count:

  | Keyword | Group | Orientations | Orientation picks out | Example |
  |---|---|---|---|---|
  | `uniform` | O | 1 | nothing | stone |
  | `column` | D₄ | 3 | an unoriented face axis | log, pillar |
  | `pointed` | C₄ | 6 | a face (directed face axis) | grass (top ≠ bottom) |
  | `brick` | V₄ (3 face axes) | 6 | roles of 3 distinct unoriented axes | 3-way-distinct block |
  | `twisted-column` | V₄ (1 face axis + 2 edge axes) | 6 | an axis + a cross-section diagonal | spiral-patterned column; diagonal vein |
  | `pointed-brick` | C₂ (face axis) | 12 | a directed axis + a secondary axis | — |
  | `edge` | C₂ (edge axis) | 12 | an edge | corner trim; vein clipping an edge |
  | `corner` | C₃ | 8 | a vertex (directed body diagonal) | vein clipping a corner |
  | `diagonal` | S₃ | 4 | an unoriented body diagonal | hexagonal-section vein |
  | `tetrahedral` | A₄ | 2 | one of the two inscribed tetrahedra | crossing vein net |
  | `oriented` | trivial | 24 | a full frame (up + front) | furnace |

  The grammar defines one **standard-position representative** of each type in
  the block's local frame (concrete generators chosen for v0.1.3 in
  `versions.md`; to be promoted into the grammar). **Authoring frame convention:** local **+z is the
  attach direction** (points out of the surface the block is placed on) and
  local **−y is the front**; the standard positions put each type's feature
  there (column/pointed axis along local z, etc.). The author declares the type
  and models the block in this frame.
  - A block with self-symmetry H has exactly **24/|H| distinct orientations**;
    orientations in the same coset gH look identical.
  - **The grammar enforces that textures match the symmetry:** a block supplies
    exactly **one texture per orbit of H on the 6 faces** (O → 1 texture,
    log D₄ → 2: ends + sides, trivial → 6). Supplying a different count is a
    grammar error, so the declaration and the art cannot disagree structurally.
  - **A canonical transversal makes any texture well-defined.** A
    non-representative face in an orbit takes the
    representative's texture turned by one fixed h ∈ H that carries the
    representative onto it (the lowest-index such h). Other choices differ by
    the face's stabilizer, so without a fixed choice the result would be
    ambiguous for a texture that isn't stabilizer-invariant; with it, the
    result is deterministic. Combined with canonical coset storage, an
    asymmetric texture simply renders in one canonical orientation, and
    rotating the block about its own symmetry has no visible effect (the
    stored state doesn't change). H therefore means "orientations treated as
    the same", not necessarily the art's exact symmetry. Rationale: exact
    4-fold or 180° pixel symmetry is too restrictive for organic art (rings
    with a knot, grain direction), and forcing it would push authors to fake
    symmetry or declare a smaller H just to show an asymmetry.
  - **Texture properties are artist-declared assertions, hard errors when
    false.** Nothing is required for correctness; an author who wants a
    property guaranteed asserts it per texture, and validation fails the
    definition if the texture doesn't have it. Initial vocabulary:
    `invariant` (unchanged under the face's stabilizer in H; e.g. O: 4-fold,
    log end: 4-fold, log side: 180°), `rot2` / `rot4` (explicit 180° / 90°
    symmetry claims), `tileable` (wraps without a visible seam). Rotational
    assertions are pixel-exact, since 90°/180° rotation of a square raster
    is exact. `tileable` had no exact pixel test, only a seam statistic
    (since removed, below).
  - Supersedes the earlier always-on rule that every face texture must be
    invariant under its stabilizer.
  - **`invariant` is dropped**, to simplify code
    and testing. Rotational invariance comes from **canonical orientations**,
    not from properties of the textures. A rotationally invariant texture is
    arguably bad: rotation never shows on a block that is symmetric, and
    `invariant` is misleading for one that isn't. It also conflicts with
    variant sets (`render/block-textures`), whose tiles must differ. **`rot2` /
    `rot4` and the `--lint` "art more symmetric than H" warning are dropped
    too**, on the same reasoning. `tileable`
    was generalized to edge matching, then **removed too**: the engine
    checks structure only, and continuity at matched
    edges is the generator's job (`render/block-textures`). Specified for
    v0.1.8 (`attic/versions-v0.1.md`).
  - Declaring H *smaller* than the art's true symmetry is legal but wasteful
    (visually-identical orientations get distinct encodings); an optional lint
    can detect it by the same rotation comparison.
- **Two block kinds, one orientation model:**
  - **Solid blocks** — full 1-unit cubes with six face textures. Their symmetry
    is entirely a matter of the face textures (rules above). There are no
    partial-cube shapes: `world/voxel-scale`'s 1-ft unit is intended to make
    stairs/slabs-style sub-blocks unnecessary (detail is built from more blocks).
  - **Non-solid blocks** — essentially **3D models** occupying a cell. They use
    the same 24-element group and the same declared self-symmetry; validation
    can assert the model's geometry (and its textures) invariant under the
    declared H, analogous to the face-texture assertions; otherwise the
    canonical transversal applies here too.
- Consequences / obligations:
  - **Canonicalization:** an orientation is stored as the canonical
    representative of its coset gH (a fixed per-type table). Identical-looking
    states therefore have ONE encoding — what equality, structure diffs, palette
    dedupe and content-addressed identity (`data/authored-content`: hash =
    identity) require.
  - **Storage cost is ~nil; 4-vs-5 bits does not matter.** Orientation is part of
    the block STATE, which lives in a per-chunk palette
    (`world/chunk-representation`); per-voxel cost is the palette-index width,
    driven by how many distinct states a chunk uses, not by the raw orientation
    field (which would be 5 bits — 4 cannot hold 24). Canonicalization keeps the
    palette free of visually-identical duplicates.
  - **Everything face-indexed permutes via precomputed 24-entry tables** — face
    → texture/UV mapping, shape/collision rotation, and the face-connectivity
    digest (`core/opaque-chunk-queries`). Table lookups, no runtime math.
  - **Behavior is defined in the block's local frame and rotates with it.** A
    furnace facing up works; a non-solid model's geometry and collision simply
    rotate with it.
    "Up" is always lattice +z in every dimension (gravity is always −z —
    `world/dimension-gravity` resolved), so world-up-dependent rules (fluids
    falling, items dropping) never need orientation special cases.
  - **Greedy/merged meshing** may only merge faces with equal appearance ids
    (`render/block-textures`), which include the texture's turn, so rotated
    faces slightly reduce merge opportunities.
- **Placement UX is ONE general mechanism, not block-specific rules.**
  - **Initial orientation — one rule, three inputs, per-type mapping.** At
    placement the engine reads:
    - **n** — the normal of the clicked support face;
    - **f** — the direction toward the player, snapped to the lattice direction
      perpendicular to n;
    - **hit point** — where within the clicked face the ray hit (which corner /
      edge region).
    Each type maps these onto its feature (local frame per the convention above):
    - `column`, `pointed`: the axis / point goes along **n**.
    - `brick`, `pointed-brick`, `twisted-column`: primary axis along **n**; **f**
      selects the secondary axis / diagonal.
    - `oriented`: up along **n**, front facing the player (**f**) — Minecraft's
      furnace rule, generalized. **Unlike Minecraft, a wall placement lies the
      block on its back** (top out of the wall, front toward the player), since
      up follows n everywhere; confirmed as intended in the v0.1.5 build
     . An upright wall furnace is a wrench turn (or, later, a
      latched ghost orientation) away.
    - `corner`, `diagonal`, `tetrahedral`, `edge`: the vertex / edge nearest the
      **hit point** (the only types that need it — the same mechanism Minecraft
      special-cases for slabs/stairs, here general).
    - `uniform`: nothing to choose.
    The result is canonicalized by H, so lower-count types simply ignore inputs
    they don't need. Per-type behavior with no per-block code.
  - **Modifier key = wrench on a ghost.** Holding the modifier (**X**,
    `input/bindings`) while placing
    shows a **ghost preview**; the wrench interaction (90° about the axis of the
    face looked at) acts on the ghost before committing. One rotation
    vocabulary for placement and re-orientation; any orientation is ≤ 3
    wrench clicks away (three axes) or ≤ 4 wheel steps (two axes: 1/4/10/8/1
    orientations at 0–4 steps). While X is held the **wheel** turns the ghost about the normal of
    the **clicked face** — the already-placed block face it's placed
    against (up = clockwise, down = counterclockwise, as seen looking at that
    face), so the mouse buttons keep placing. **Shift+wheel tips** it about screen-right made
    perpendicular to that normal, then snapped to a lattice axis (up = top
    away, down = top toward you). **No behavior depends on the ghost's own
    faces**, only on placed blocks and the camera.
    One axis alone can't reach every orientation from a single viewpoint (a
    log toggles between two of its three); two perpendicular axes reach all
    of them from anywhere. This is still the wrench's 90° turn, **not scroll-cycling**
    through a list of orientations, which stays rejected: fine for
    `column`'s 3 states, miserable for `oriented`'s 24. With
    preview mode on (`input/preview-mode`) the ghost is always shown; the
    modifier is then only needed to rotate it.
  - **The chosen orientation lasts only while X is held** (a quasimode).
    Once the wheel
    has been used, whatever orientation it lands on is **locked**, even if
    that's the default inferred one, and every placement of that block type
    uses it, in **world frame**, until X is released. So a row of furnaces or
    a wall of sideways columns, where placement UX hurts most, is placed by
    choosing once and clicking along the row with X held. Releasing X
    returns to plain placement inference. No hidden state outlives the key,
    so there's nothing to clear in any UI. Considered and rejected: a
    persistent per-type latch cleared by a UI button or by rotating back,
    because a stateful latch is easy to forget and confusing to clear.
  - **Wrench** (diegetic tool, `core/diegetic-authoring`): clicking a face of a
    placed block rotates it **90° about the axis through that face** (clockwise
    as seen looking at the clicked face). Its **secondary action**
    (`input/per-hand-use`) rotates **counterclockwise** about the same axis, so
    every turn is undoable from the face already being looked at (the opposite
    face is often buried in a wall or floor). Clicking the opposite face also
    rotates the other way.
  - **Reachability:** quarter turns about the face axes generate the whole
    rotation group, so every one of the 24 orientations is reachable — and in
    at most **3 clicks** from any starting orientation (one click: the 6
    quarter turns; two: the 3 face-axis half turns and 8 corner-axis 120°
    turns; three: the 6 edge-axis half turns).
  - Clicks that land in the block's own symmetry H (e.g. turning a log about
    its own axis) leave the canonical state unchanged — a visible no-op, which
    is correct.
  - Because every block shares the same group (reduced by its own symmetry),
    one UX covers all blocks, including user-authored ones.
- Scope: only **lattice-preserving** rotations. Arbitrary angles (45° etc.) are
  not orientations — that would be a different (non-voxel) representation.
- Open: tuning only — hit-point region sizes for `corner`/`edge`-family
  selection, and whether the X-held orientation choice applies per block
  type or to every block placed while X is held.
- **Orientation is a property with an enumeration-free name** (v0.2.4):
  in a state's canonical text (`world/block-states`), `orientation` names its
  coset gH by the member that sorts first by (image of +x, image of +z),
  axes ordered +x, −x, +y, −y, +z, −z, written as the two images (`+y+z`).
  The identity's coset is the default and is left out. In-process code
  keeps a rotation index (the lowest in the coset); no index is persisted
  or hashed.

## world/climbing-and-descent — Ropes are block columns; falls hurt by deceleration; descent is a Terra progression

- **Unaided climbing is open**. If it exists, blocks are
  distinguished as **rough or smooth**, and only rough faces can be climbed.
  There **may be a stamina mechanic** for climbing and for ropes.
  - Note (proposed): rough/smooth is a block property declared in the
    content grammar, which suits geology well: weathered sandstone is rough,
    polished stone and ice are smooth.
- **Falls**:
  - **Dangerous, and they can kill,** but you survive longer falls than in
    real life (`core/realistic-in-kind`: real in kind, forgiving in
    magnitude).
  - **Deceleration is what harms you,** not the fall as such. Hitting deep
    water hurts at the surface, and hurts again at the bottom if the water is
    shallow enough that you're still moving fast when you reach it.
  - **Damage is a function of the speed lost and the distance it was lost
    over**. Each medium
    the body passes through takes off some speed and deals damage for it; the
    swept collision already crosses those cells in order
    (`sim/swept-collision`). Soft materials (sand, snow, leaves) and fluids
    are then cushions by the same rule, with no special cases.
  - **The body gives too**: the stopping distance is the
    medium's give plus the body's own (legs bending), and posture changes
    it (a crouch or roll lengthens it, stiff legs or heavy armour shorten
    it). The landing itself is a short, prescribed deceleration of the
    centre of mass, not an instantaneous stop (`sim/motion-segments`).
  - **Only what exceeds the body's absorption harms**:
    there is a lower threshold, so small falls and gentle stops do no damage
    at all, never an infinitesimal amount. Note (proposed): a body property
    (`sim/player-embodiment`), so different bodies and later gear (padding,
    boots) can raise it.
  - **A deep enough pool is the safe landing,** and diving into a shallow one
    is the classic mistake, which the rule produces on its own.
- **Ropes are columns of blocks with a gravity mechanic**:
  - **Cutting a rope lets the blocks below it fall.**
  - **Material per foot:** each rope block costs rope.
  - **Retrieving a rope from below** takes special mechanisms or magic.
  - **Ropes over pulleys** might be implemented by shifting blocks from one
    end to the other.
  - **Ropes may be slightly magical,** acting as very-short-range anchors
    (some or all ropes).
  - Notes:
    - **Support is level-triggered:** a rope block is held if the block above
      it is rope or an anchor (`core/level-triggered`). A cut is just a support
      check failing below it. Fallen rope lands as items or a transient pile,
      conserved (`inventory/ground-storage`).
    - **A pulley shift is a conserved move,** one block off one end and one
      onto the other in a single edit. Across a chunk seam it is an assembly
      committed by the Vicinity, like a piston line
      (`sim/concurrency-model`).
    - **The short-range magic** could be what lets a rope hang from the side
      of a block, or bridge a one-cell gap to an anchor, without a physical
      knot. A bit of folk magic in the everyday tool (`core/low-fantasy`).
- **Counterweight lifts**: pulleys meet encumbrance. A
  basket hangs on each end of a rope over a pulley. Carry a stone block into
  one so that you outweigh the other basket, and you descend. Throw your
  ballast overboard and you ascend. Notes (proposed, open):
  - **Weight becomes physical, not just a slowdown.** Encumbrance's weights
    (still TBD, `inventory/encumbrance`) are then the same numbers a pulley
    compares. They're gamified rather than realistic, but they have to be
    consistent, since players will balance baskets with them.
  - **Only mundane weight counts.** Extradimensional contents weigh nothing,
    so a pouch full of stone is no ballast. It has to be blocks in body slots
    or in the basket itself, a mundane container. That's a neat consequence of
    the existing rule, not a new one.
  - **Ballast thrown overboard falls.** Whether a falling block harms what it
    lands on is **not** covered by the deceleration rule above, which is about
    the player's own body. Impacts from objects would be a separate damage
    mechanic, undecided.
  - **Water as ballast** would make a repeatable lift: fill a tank at the top,
    drain it at the bottom. Real water-balance cliff railways (Lynton and
    Lynmouth, 1890) work this way, and so did old mine hoists.
  - **The motion can be a closed form** (`core/closed-form-first`): speed from
    the weight difference, capped, with the rope shift as one conserved move
    per cell travelled.
- **Fixed infrastructure** (ladders, pegs, rope bridges, lifts) **is all
  subject to decay** (`persist/diegetic-persistence`). A route left
  unstabilized eventually crumbles away.
- **Descent is a progression**: going deep takes **pouches**
  (enough inventory), **tools**, and **enough fibre to make ropes**, possibly
  as string → cord → rope (`crafting/material-classes`: fibre/cordage).
  - Note: rope per foot makes depth a resource budget. A 100-ft
    shaft needs 100 rope blocks, which is a real gathering and hauling cost
    early and trivial later. That's a natural depth gate with no explicit
    lock.
- **Cave generation**: caves should have **sheer drops and
  overhangs**, and **some cave generation should trend vertical**. That is
  for the cave fields (`worldgen/noise-fields`, volumetric cave fields). A
  vertical bias could be one more field in the combination (proposed).
- Open: unaided climbing and stamina; fall damage numbers; how retrieval from
  below works; which ropes are magical and how far their reach goes;
  whether string → cord → rope are separate items or quality grades;
  whether falling objects deal impact damage (a separate mechanic).
