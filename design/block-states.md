# world/block-states — Block definitions, block states and the state table

A **block state** is one state a block can take: a block definition plus a
value for each of its variant properties. Cells hold block states, as the
palette entries of their Brick (`world/brick`), at every level of detail.
This file holds what a state is, how it is identified and persisted, and
the per-process **state table** that every Brick's palette indexes.

## Definitions
- **A block's definition** has one ULID and holds what is true of every
  instance: material properties, symmetries, collision, behaviour, and the
  variant properties it declares. Appearance is a separate binding, so
  texture packs remain possible. How definitions are loaded is a later,
  grammar-based arc (`data/grammar`); until then they stay JSON files.
- **Internal states are invisible to players:** a definition maps each of
  its states to what players meet (what breaking it drops, what picking it
  gives, the name shown), so states that exist for simulation (a reed's
  height classes, a tip at a chunk's edge) all present as the one block.
- **Variant properties** each have a small closed domain: orientation
  (canonicalised under the block's symmetries, `world/block-orientation`),
  lit or unlit, a growth stage.

## State hashes
- **A state hash** identifies each state a block can take, hashed from the
  definition ULID and the canonical values of its own properties: a block
  with 6 canonical orientations and lit or unlit has 12. This generalises
  Minecraft's block states, where each block enumerates its own states, and
  holds up for player-defined blocks, since a new block or property needs
  no global layout, and blocks without a property pay nothing for it.
- **The hash is cryptographic** (owner, 2026-11-27): BLAKE3 in its
  `derive_key` mode, with a versioned context string, truncated to 128
  bits (`deps/hashing`). States are interned often but not on a hot path,
  and a cryptographic hash removes accidental collisions and makes
  deliberate ones (about 2⁶⁴ work) impractical, even for player-defined
  blocks. A new encoding gets a new context string, so old and new hashes
  never collide.
- **What is hashed is a canonical text form** (owner, 2026-11-27), also the
  state's reconstruction and its form in logs:
  `<definition ULID>[name=value,...]`, the ULID upper case, properties
  sorted by name, the brackets left out when no property is written. Names
  and values are `[a-z0-9_]+`, and values are written by name, never by
  index, so reordering a domain changes no hash.
- **No value is a designated default** (owner, 2026-11-27); a property is
  one of two kinds:
  - **Optional** (the common case): its domain is *absent* plus its named
    values, and absence is written as nothing. A flag is an optional
    property with the one value `true` (absent means false). Adding an
    optional property to an existing definition leaves every existing
    state's hash unchanged, since every existing state has it absent.
  - **Mandatory** (a wool colour, a wood kind: every state has a value and
    none is special): always written. Adding one to an existing definition
    changes every state's identity, so it is a migration
    (`data/stable-identity`), as it should be.
- **Orientation** is effectively optional, absent meaning unrotated, and is
  written so it doesn't depend on how the 24 rotations are numbered
  (`world/block-orientation`).
- **A block's starting state is behaviour, not identity:** a definition
  names an initial value for each mandatory property, its optional ones
  start absent, and none of that enters a hash. The grammar
  (`data/grammar`) reads properties into the same values; this text is the
  identity, whatever authors write.

## Persisting states
- **Never persisted without its reconstruction:** a saved Brick or a
  network message may carry state hashes, or a palettized form of them, for
  speed, but always with what is needed to reconstruct each state from
  definitions (each palette entry's definition ULID and named property
  values, or the hashing scheme's key and version), so no information is
  lost when a block's properties change (`data/stable-identity`). The rule
  limits information loss, not the representation.

## The state table
One per process, shared by every Brick. It holds what systems need per
state: the state hash, the definition, decoded property values
(orientation as a column every block has, for generic rotation code),
visibility class, collision, and six **appearance ids**, one per world face
(`render/block-textures`), derived from the definition, the state and the
resource pack rather than part of the state's identity.
- **State ids:** a palette entry is a 32-bit **state id** (`StateId`, a
  distinct type, so it can't be mixed up with a palette index), a dense
  index into the table, assigned when the process first sees the state (a
  chunk blob load, definition loading, a generator registering its palette,
  a player creating a block). Ids are scoped to the process and never
  persisted; state hashes are what is persisted. Reading a state's data by
  id is one array access, read once per palette entry when a Brick is
  meshed or queried, not per cell.
- **Interning** (state hash to id) is the one hash, used only when a state
  is first seen, never per cell or per edit.
- **Retrieved ahead of time** (owner, 2026-11-27): code resolves the ids it
  will need before the loop that uses them (a tool its states when an
  action is planned, a generator its palette when it starts, a mesher one
  table row per palette entry), and anything per cell, per face or per
  frame reads the table by id only. Moving between states of one
  definition (another orientation, `lit` on) is an array lookup in the
  definition's dense index of its states, not an intern.
- **Shared across threads:** the table only grows by appending, in chunked
  storage that never moves, so reads by id take no lock; interning takes a
  short lock (sharded if measurement asks), and is rare.
- **Overflow is fatal:** ids are never reused within a process, so
  interning more than 2³² states is a fatal error, in every build, not a
  rollover. Player-created blocks make this reachable in principle on a
  large, long-running server; at worst that is a crash, and a restart
  starts the count again. A telemetry gauge counts interned states.

## Keeping states few
- **Each definition caps the states its domains multiply out to at 2¹⁰**
  (owner, 2026-11-27; checked when definitions load, and a definition over
  it is rejected). That bounds the product of orientations and every other
  property; unique objects (a written book) take another path. A value
  that is open-ended or timestamped is not part of a state: timing comes
  from closed forms keyed by the state, the coordinates and world time;
  rare rich content (a book's text) is a world-level record that a
  reference property names, the one exception to small closed domains; and
  simulation's own per-cell data is hidden, never persisted
  (`sim/block-metadata`). This, not the id width, is what keeps palettes
  and the state table small.

## At every level of detail
- **A block state, at every level** (owner, 2026-11-27): a coarse Brick's
  palette holds block states interned like a full-detail Brick's. What a
  coarse cell shows is a **superclass** block where the stage ladder hasn't
  specialised it yet (generic stone, a soil, a canopy,
  `world/progressive-lod`): superclasses are block definitions like any
  other, with ULIDs, and one that needs no orientation declares none. So
  coarse Bricks persist, replicate and mesh as full-detail ones do, and no
  second vocabulary or palette bound exists for them.

## Invariants (draft)
- **`invariant/block-state/canonical-states`** — Every state in the table is
  canonical under its block's symmetries: two orientations in one coset
  are one state.
  - **Enforced:** type: interning canonicalises the orientation, and
    states are reached only through the table.
- **`invariant/block-state/unique-state-hash`** — No two distinct states
  share a state hash.
  - **Enforced:** fatal check on interning, in every build.
- **`invariant/block-state/no-state-id-leaves-process`** — Every state hash
  written to a save or a message travels with what reconstructs its state
  from definitions (each palette entry's definition ULID and named property
  values, or the hashing scheme's key and version), and no state id is
  written to either.
  - **Enforced:** type: the save and message encoders take the state
    table; review.
- **`invariant/block-state/state-ids-never-overflow`** — State ids never
  overflow: interning past 2³² is fatal.
  - **Enforced:** fatal check on interning, in every build.
