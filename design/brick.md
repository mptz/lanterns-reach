# world/brick — The Brick: 64³ cells at a power-of-two cell size

A **Brick** is the engine's one cell storage: 64³ cells, each 2ᵏ units on
a side, in a palettized form whose palette entries are block states
(`world/block-states`), at every level of detail. A chunk's cells are a
full-detail (1-unit) Brick; distant terrain may hold its cells as coarser
Bricks.

**The Brick is storage; its owner is the abstraction boundary** (owner,
2026-11-27). A Brick's owner is a chunk, for simulated terrain up close
(`world/chunk`), or a terrain node, for distant terrain (`world/surveyor`),
which may also store its cells some other way. The owner's code works with
the Brick directly, indices, widths and palette included, and nothing
outside the owner sees any of them: callers ask the chunk or the terrain
node semantic questions (`core/opaque-chunk-queries`). The owner also
decides when a Brick grows, compacts or is read concurrently
(`world/chunk-representation`). The Brick itself keeps only its own class
invariants (below).

## Role
- **The one cell storage,** at any power-of-two cell size: 1 unit at full
  detail, coarser for distant terrain. Coarse Bricks hold block states too
  (`world/block-states`), so they persist, replicate and mesh as
  full-detail ones do.
- **The same shape as a TerrainNode's grid** (N = 64,
  `world/octave-pyramid`), so a node at spacing s can hold its cells as a
  Brick at cell size s.

## Representations
- **Uniform and palette.** Uniform: one entry fills the Brick (all air, all
  stone). Palette: bit-packed indices into a per-Brick palette of state ids.
  A Brick holds at most 2¹⁸ = 64³ distinct live states, so at 32 bits every
  cell can have its own entry, and the palette also covers what a separate
  dense form would. The full-width case (every cell distinct, only a
  pathological build) costs about twice a plain array of state ids.
- **Aligned, power-of-two widths:** indices are stored at 1, 2, 4, 8, 16 or
  32 bits, so an index never straddles a 64-bit word and an access is a
  shift and a mask.
- **Layout:** cell index = x + 64·y + 64²·z, so a row of 64 cells along x
  is 64 consecutive indices, and neighbours along y and z are 64 and 64²
  indices away. It is chosen for bitmask meshing, where a row along x
  becomes one 64-bit mask (`world/chunk-meshing`); a change comes only if
  profiling of other readers asks for it. Nothing persisted depends on it
  (`data/content-digest` is defined on coordinates).
- **The palette** is one array allocated at its capacity, never a growing
  vector. Its capacity is fixed when the Brick is built and is independent
  of the width, which only limits how many entries the indices can name. An
  entry is stored before any index that refers to it.

## Growth and compaction
Palettes keep no per-entry counts, so an edit can't tell which entries are
dead. Every change of width or capacity builds a new Brick, which the owner
swaps in, retiring the old one (`retire`, `engine/dispenser`): it stays
readable and is never written again (`sim/concurrency-model`,
representation swapping).
- **Growth:** an edit that needs a new entry appends it within the
  palette's capacity. A full palette is replaced by a Brick with twice the
  capacity, at the same width while the capacity is below what the width
  can name (2ʷ entries), else at the next width. A Brick widens at most
  five times (1 → 32 bits) and doubles its capacity at most 31 times, each
  one copy, so growth can't thrash.
- **Sizing:** a Brick is built with room for its entries: the next power of
  two at or above them, at least 16 (a placeholder), at most what its width
  can name, so the 1-, 2- and 4-bit widths hold 2, 4 and 16 entries.
- **Compaction** is the same copy, renumbering: it finds the live entries,
  renumbers them, and picks uniform if one state is live, else the narrowest
  width at which the live entries sit at or below a **low-water mark** of
  7/8 of capacity (a placeholder).
- **Edits only upgrade** (uniform to palette, narrower to wider); a Brick
  becomes uniform or narrower only through compaction, which no edit
  performs.

## Invariants (draft)
- **`invariant/brick/owner-only`** — Only a Brick's owner (a chunk or a
  terrain node) reads or writes it, and nothing outside the owner sees its
  cells, indices, widths or palette.
  - **Enforced:** type: the owner holds the Brick privately and its API
    answers semantic queries (`core/opaque-chunk-queries`).
- **`invariant/brick/representation-invisible`** — A Brick's
  representation and width are invisible in its owner's answers: the same
  content, uniform or as a palette at any width, answers every query,
  digest and mesh identically.
  - **Enforced:** test: the rep-swap tests (`world/chunk-meshing`).
- **`invariant/brick/distinct-palette`** — No two entries of one palette
  name the same state.
  - **Enforced:** fatal check on insertion.
- **`invariant/brick/index-in-palette`** — No index refers past the
  palette.
  - **Enforced:** fatal check on write; debug audit.
- **`invariant/brick/edits-never-compact`** — No edit compacts, and no
  edit narrows a Brick.
  - **Enforced:** fatal check in the edit path; test: the representation
    tests.
- **`invariant/brick/never-reallocated-in-place`** — A Brick is never
  reallocated in place: its width and palette capacity are fixed for its
  life, every change of either is a new Brick swapped in by its owner, and
  an old Brick is freed only after a grace period.
  - **Enforced:** type: no resizing operation exists on the class, and the
    swap path is the only constructor from another Brick.

## Open
- Where coarse Bricks are used (the coarse-to-fine pipeline).
