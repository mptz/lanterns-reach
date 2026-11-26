# Dimensions

The dimensions of the world: what a dimension is, the committed set, what each
is for, and the cosmology their names suggest. How terrain is generated in any
dimension (the one density-field form, bias profiles, per-dimension fields) is
`worldgen/dimension-profiles` in `design/worldgen.md`. Uncommitted
dimensions (Discordia, Machina, Somnium, base extrapolation) are at the
bottom, with their open questions; travel between dimensions is
`world/dimensional-travel` in `design/open-questions.md`.

## world/dimensions — Many dimensions, pocket dimensions as first-class spaces

- Decision: Support many dimensions (not a fixed small set).
- **Container interiors are pockets of the Aether**
  (`dimensions/aether-pockets`). A pouch or chest interior is a real
  region of the Aether, a 3D array of boxes, held by its container. It is not
  a separate keyed dimension.
- Rationale: Unifies "alternate worlds," "the Nether," and "inside a container"
  under one mechanism. Chunk addressing becomes (dimension_id, chunk_coord);
  dimensions are first-class records in the DB.
- Consequences: Need a dimension registry and lifecycle subsystem; reclamation
  and the ownership graph are specified in `data/dimension-gc`. Worldgen differs
  per dimension — all dimensions share one unified thresholded density-field
  *form*, while a dimension's **content** — its bias profile plus its own noise
  field set and per-feature response curves (no official biomes,
  `worldgen/field-driven-features`) — is what gives it character (Earth-like
  heightmap, floating/sky-island downward dissolve, endless cliff, bias-free cave
  medium, bottomless cosmic ocean); see `worldgen/dimension-profiles`.
- **Pockets are a spectrum, not just containers.** The same first-class
  keyed-space mechanism spans from a chest/bag interior (small, maybe
  single-chunk) up through **portable workshops** to **private extradimensional
  spaces** a player owns and inhabits. They differ only in size, ownership, and
  access (a portal/container mouth), not in kind — so "a bag's inside" and "my
  private pocket realm" are the same construct at different scale. This is why no
  separate "player housing" or "workshop" dimension type is needed.

## dimensions/pilot-set — The committed set: Terra, Aether, Enigma

The pilot set is three full-scale dimensions: **Terra** (the Earth-like starting
dimension), **Aether** (the dimensional nexus, a cosmic ocean), and **Enigma**
(the mysterious cliff-like endgame delve). What the names suggest is recorded in
`worldgen/dimension-cosmology`. This initial dimension roster is decided, although
each dimension will be detailed as it's built.

Dimensions use distinct `bias` profiles and noise-field sets to yield distinct
visual appearances, topologies, and gameplay experiences over common underlying
random-generation and chunk/physics mechanics. Individual dimensions are vast;
dimensions proliferate because they offer different gameplay and progression,
not just to fit in more content.

**Deliberately NOT included (and why — no capability lost):**
- **No hell/underworld dimension.** Unconstrained depth below Terra's surface
  (the world is cubical, with 10⁹ units available below z=0) means Terra *contains*
  an underworld. Cave layers, a vast Underdark, even hollow-earth like scenarios
  are viable without dimension-hopping.
- **No separate sky dimension.** Redundant with both unbounded +z in Terra and
  with the sky/water-centric design for Aether, in which all land is floating
  (in the sky, in the water, or spanning the two).

## worldgen/dimension-cosmology — What the names say: matter, medium, mystery

- **One world, three roles ("matter, medium, mystery"):**
  - **Terra** is matter (ground, survival, the stable world, nature to explore);
  - **Aether** is the medium between things (the classical aether filled the space
    between the heavens, and the "luminiferous aether" was what light travelled
    through, which fits the nexus: the medium of travel);
  - **Enigma** is knowledge, and what can't quite be known.
  These lore roles correspond to gameplay dynamics: survival, diegetic fast travel,
  and ongoing endgame challenges.
- **Low magic draws on Aether**, giving it an in-world vocabulary without high
  fantasy:
  - **Resonance is attunement to Aether** (`geology/resonant-crystals`):
    hence celestine is stronger at altitude, closer to the sky, and selenite
    follows the moon.
  - **Pouches open onto small pockets of Aether** (`inventory/pouch-progression`):
    every extradimensional pouch quietly touches the nexus; portal items are
    the same idea grown up.
  - **The `liminality` field** is how thin the boundary to Aether is at a place
    (`worldgen/regional-fields`): magic works best where the world is liminal.
    **Liminality is cross-dimension, not per-dimension:** liminal places correspond
    across dimensions, and there the dimensions echo each other.
- **Two kinds of magic**:
  - **Folk magic** (Terra's): low, local, costly, sympathetic (like affects
    like), and everyday: patterns on pouches, resonant crystals, the fed
    protection circle.
  - **Enigma's magic** (late game, higher): **inexplicable, hard or impossible
    to replicate**, with mechanics that are the **opposite of folk or
    sympathetic magic**: baffling, non-intuitive, unexpected. **Not
    player-centric:** often of marginal or even negative utility. **Dangerous
    and unpredictable.** It fits `core/low-fantasy` because the world's
    strangest power doesn't exist for the player.
  - **Not "unreliable"**: Enigma's technology is better
    characterized as (1) **not necessarily aligned with the player's
    intention**, and (2) **ancient and possibly broken, but so inscrutable that
    it is hard to tell whether it is working or not**. What reads as failure
    may be the device doing exactly what it was made for.
  - **Constraints:**
    - **Unpredictable, not random:** each artifact follows a fixed hidden rule
      (e.g. hashed from its identity), so patient experiment can learn *what*
      it does, never *why*. That keeps Enigma's core mystery
      (`dimensions/enigma`) and rewards the curious and
      technical player.
    - **Outside core progression,** and rarely worth rolling for, so it can't
      become a variable-ratio loop (`ethics/no-variable-ratio-core`) or a
      meta (`core/no-meta`).
    - **Conservation still holds:** an effect may move, transform or destroy
      ledgered things (declared sinks), but never create them outside a
      declared, bounded source (`core/soft-over-hard`, `data/entity-ledger`).
    - **Inspiration:** the Strugatskys' *Roadside Picnic* was front of mind:
      the Zone's artifacts are useful, useless or lethal for reasons
      nobody understands, and the visitors who left them never cared about
      humans. Other precedents (from memory): S.T.A.L.K.E.R., *Annihilation*'s
      Area X, the objects of *Control*.
- **Power tiers telescope out of view** (inspired by Vernor Vinge's Zones of
  Thought, where the Powers of the Transcend are beyond comprehension and
  something greater still may have engineered the Zones themselves as
  galactic "nature preserves"). Folk magic is
  understood; Enigma's makers are not; and whatever lies beyond them is not
  even glimpsed. The player never sees the top of the ladder, which keeps the
  world large (`core/low-fantasy`).
- **Naming convention for future dimensions:** short Latin or Greek words for
  a dimension's essence: *Machina* (`dimensions/machina`), the dream-space
  *Somnium* (`dimensions/somnium`).
- Per-dimension consequences are in each dimension's entry: `dimensions/terra`,
  `dimensions/aether`, `dimensions/enigma`, and the uncommitted ones at the
  bottom.

## dimensions/terra — Terra: matter; the Earth-like starting dimension, about exploring nature

**Terra — Earth-like, the starting dimension.** A 2D surface with perforated
solid ground beneath (`bias = z − surface(x,y)`; the 2D-surface profile of
`worldgen/dimension-profiles`). Baseline gravity, baseline traversal; the
reference against which the others are "other."

**Terra is about exploring nature**: roaming the surface; venturing deep into
vast caves, descending by ropes, mechanisms, and even mounts. Because the world
is so vertical, it needs **more climbing and descending mechanics than Minecraft**
(see `input/vertical-movement`, `world/climbing-and-descent`). Its identity comes
from geology, soils, seasons and depth (`design/geology.md`).

**Terra evokes *terra incognita*:** maps the player draws, with blank,
unexplored edges, are a diegetic and educational answer to what replaces
coordinates (`input/no-player-coordinates`).

## dimensions/aether — Aether: medium; the nexus, sky over a bottomless ocean

**Aether — the nexus / connective dimension.** The "cosmic ocean", a place
outside the ordinary world yet reachable from it, featuring a space-like sky
and rainbow/oilslick-colorful ocean surface.

Aether has a **two-zone vertical structure: sky in the top half, ocean in the
bottom half, and NO solid bottom** — the fluid simply continues down without a
floor. The bias puts open air above the ocean surface and fluid below it. Solid
ground appears only as islands: **islands that float in the air, float submerged
in the water, or span the two like icebergs or pillars**, never a seabed. The bias
encodes air/water/solid by height around the ocean surface, not land/air around
a terrain surface, and might create distinct vertical layers of land masses.

**Aether's primary purpose is inter-dimensional travel with space dilation** —
it is the *nexus* through which other dimensions are reached and the diegetic
fast-travel mechanism allowing huge distances to be traversed. Aether's space
dilation vs other dimensions is described in `worldgen/dimension-warp`: local
3D currents with modes around 12:1 and 1:12 and extremes near 50:1 mean the
shortest routes alternate dimensions following the current's sign. Navigating
these dimensional currents reinforces the "ocean" element of the setting.

Beyond worldgen, Aether will require diverse navigation/topology mechanics,
notably including **multiple portal models** (e.g. randomized, affine-transform,
movable), potentially as independent progression elements; the travel
and portal models are the open question `world/dimensional-travel`.
Portal proximity
may be restricted, and certain types of portals might be tightly limited in
number. The Aether isn't just how you reach *new* dimensions; **the Aether is
how you reach Terra's extreme depths and far regions** as part of gameplay
progression or simply to access otherwise nigh-unreachable terrain.

**Secondary purpose: a genuinely alien place.** Per the per-dimension
model (`worldgen/dimension-profiles`), Aether has its own noise field set and
its own feature response curves. We'll use this independent worldgen machinery
to create a world which feels categorically other, not just a reparameterization
of the ordinary. Aether will have its own biomes and ecology, similarly adhering
to the principle that **the rules themselves are different here**. Per
`core/earned-vastness`, a completely new dimension requires no less.

**Exaggerated night sky.** The sky half is an opportunity for a
fiction/game-style **colorful night sky — nebulae, auroras, multiple or
strange celestial bodies, otherworldly phenomena** — not Terra's
naturalistic sky. Presentation-layer (skybox/ambience, `light/visibility-only`
is already presentation-only), so it's cheap and carries much of the "you are
somewhere else" signal for free. Reinforces the alien-biome otherness above.

**Tertiary: entropic / impermanent-matter regions.** The "matter won't hold"
idea (blocks decay/drift, you can't durably *build*, only pass through or
stabilize at cost) is folded in here as a **regional property of Aether**,
not a separate dimension — it suits the otherworldly vibe and gives the nexus
a reason to feel dangerous and alien rather than a mere lobby. Mechanically
it's the local decay curve cranked toward the floor (reuses the stabilization
/decay machinery, `persist/diegetic-persistence`), scoped to regions of Aether.
Drift/decay is a closed form evaluated on demand (`core/closed-form-first`),
declared as a process kernel (`sim/process-kernels`) rather than bespoke Aether
code — never stepped per tick. Dimension-wide status was considered and
rejected: impermanence is a region flavor, not something needing a whole vast world.

## dimensions/aether-pockets — Container interiors are real places in the Aether, held by their container
- **Open:** the consequences marked as proposals.

- Decision:
  - **The "pocket dimension" a container uses is a real place in the
    Aether:** a 3D array of boxes (`inventory/box`). This is part of why
    mundane boxes exist.
  - **Pockets are in random locations,** so you're vanishingly unlikely to
    come across one by chance. **Enigma technology may create
    possibilities.**
  - **A stack is an array of boxes**: a container slot
    that stacks to 360 is an array of 360 boxes in the Aether, each holding one
    item (`inventory/box`), though the UX hides this completely. Mundane storage
    and extradimensional storage obey the same one-item-per-box rule; a stack is
    just many boxes. Notes (proposed, open):
    - **A pocket's size is its capacity:** slots × slot capacity boxes. A
      9-slot pouch with 360-capacity slots is 3,240 boxes, about a 15-ft cube,
      so "bigger bag = bigger pocket" is literal. 360 factors neatly into arrays
      (for example 6×6×10 or 8×9×5).
    - **Decay can be gradual:** an orphaned pocket can lose boxes one at a time,
      so a lost stack thins out over time rather than vanishing all at once.
  - **The container stabilizes its pocket of Aether.** If the container is
    destroyed, ordinary decay applies (`persist/diegetic-persistence`).
- This makes `inventory/pouch-progression`'s "pouches open onto small pockets
  of Aether" literal rather than figurative.
- Consequences (proposed, open):
  - **Garbage collection becomes decay** (now recorded in
    `data/dimension-gc`): an orphaned pocket is an unstabilized region of
    Aether that decays like any other, with its contents through the ledger's
    decay sink (`data/entity-ledger`). One mechanism fewer, conservation
    intact. In Aether's entropic regions (`dimensions/aether`) an orphaned
    pocket decays fast.
  - **Lost pockets persist for a while, and Enigma can find them.** Between
    losing a pouch and decay (real months) the pocket is still out there, so
    finding aids from Enigma (`dimensions/enigma`) could locate a lost pocket,
    or somebody else's. That's recovery in single-player and theft or
    archaeology in multiplayer. Old decayed pockets could also be a
    worldgen-adjacent find: ruins of other people's storage.
  - **Placement:** a pocket's location is a seed-deterministic hash of the
    container's id, in a region of Aether vast enough that collisions are
    negligible. A cheap allocation check rejects the rare overlap.
  - **Open idea (Michael, 2026-11-02): extradimensional storage doesn't work
    in the Aether.** The Aether *is* the extra dimension, so a pouch there has
    nowhere to fold into. This matches no-nesting. Michael wants to think about
    the gameplay implications. Notes (assistant):
    - **It matches a rule already decided:** traversable pocket dimensions
      hold no containers (`inventory/slots-and-pack`). If those are Aether
      pockets, this is the same rule.
    - **Aether trips become expeditions on body slots.** You carry what your
      body holds, encumbrance bites (`inventory/encumbrance`), and loot coming
      home is haul-limited. That suits a dangerous, alien nexus.
    - **Bulk building in the Aether becomes impractical.** That matches its
      entropic regions (`dimensions/aether`), but it costs builders an
      interesting place.
    - **Sleds don't help either**, since they are extradimensional too
      (`inventory/pouch-progression`). Mundane hauling (carts, mounts, a
      mundane pack) would gain a real role.
    - **The mildest form is "inert, not lost":** pouches and chests still
      come along and still weigh what they weigh, but don't open there.
      Nothing is lost, only access. Spilling or losing contents would be far
      harsher.
    - **The risk:** the Aether is the travel nexus, so players can't simply
      avoid it. That is tolerable if crossings are short (space dilation,
      `world/dimensional-travel`). It hurts if players are meant to spend
      long stretches there.
  - **Traversable pocket dimensions** (portal items, `inventory/slots-and-pack`)
    would naturally be larger Aether pockets of voxel space, held by the
    portal item the same way.
  - **The no-nesting rule needs a new rationale.** It was justified by the
    shallow ownership DAG for GC (`data/dimension-gc`). With decay replacing
    GC, nesting is a design choice rather than an engineering need: it still
    stops the "infinite bag of bags".
  - **The data model shifts:** storage is no longer `(dimension_id, …)` per
    container but a region of the one Aether dimension. Placed containers
    still record a reference edge to their pocket.

## dimensions/enigma — Enigma: mystery; the endgame delve, archive and cabinet of curiosities

**Enigma — endgame dungeon-delve domain, and a place of mysteries.**
**Broadened with the name**: not just treasure and
lore but **mysteries**, including **specimens: creatures preserved in
stasis, some of which have escaped**. Assistant's notes: the escaped
specimens can be Enigma's own creatures; the collection fits the archive
and past-world replica rooms below, as a collection whose keepers are long
gone; and it stays low fantasy (`core/low-fantasy`) because nothing in it
is about the player.
- **A cabinet of curiosities**: specimens are things to
  collect, identify and understand; bringing finds back and gradually
  learning what they are is a knowledge progression (the specimen bag,
  `inventory/stack-size`, is its natural container).
- **An occasional escaped specimen turns up in Terra**: a creature nobody
  can explain, foreshadowing Enigma long before a player reaches it.
- **Enigma's central mystery stays unsolved**: no grand
  revelation of who built it or why. Players can learn a great deal, but
  the core stays a mystery, because answering it would shrink the world
  (`core/low-fantasy`; Tolkien's unvisited island).

The **endless-cliff** bias
(solid where a horizontal axis < 0, open where > 0, sigmoid transition) — same
scale as Terra, but the surface is an endless ~70° cliff.
- **The tilt is a worldgen (bias) phenomenon; gravity and blocks are
  unchanged.** Gravity points true-down (lattice −z) and blocks stay
  axis/gravity-aligned exactly as in Terra; only the bias profile
  slopes the density field along a horizontal axis, so the emergent surface
  normal sits ~70° off vertical. NOT a new engine axis (`world/dimension-gravity`
  resolved) — voxel grid, physics and fluids are untouched; waterfalls fall
  *down*, vines hang *down*; the otherworldliness is the **orientation of the
  terrain**, not the look or the physics. The face has biomes, waterfalls,
  clinging vines.
- **Deliberate traversal challenge; the ~70° is an AVERAGE, not uniform.** The
  cliff has real terrain, just tilted: the bias sets a ~70° mean slope, the
  noise carves local variation into it — **shallower sections (standable
  rests), sheer sections, even overhangs.** So it's barely-standable *on
  average* with a mix of easier ledges and harder sheer/overhang stretches,
  not a monotone ramp — which is what makes it a traversal *puzzle* rather than
  one uniform grind. Enigma **requires and rewards movement/traversal
  capabilities beyond Terra's** (climbing, clinging, ledge-work; the
  vines are the floor, not decoration). This is intended: the endgame skill
  gate. The 70° mean is a tunable gradient steepness, not forced. (This
  supersedes an earlier "barely-standable everywhere" reading — the variation
  is the point.)
- **Surface structures.** The face carries built structures too — **balconies,
  broken bridges reaching out into the sky** — placed via the local-interface
  structure primitive, dressing the cliff with ruined grandeur (pairs with the
  surface boss below).
- **Surface boss as a traversal gate (one-time per region).** A boss-type
  monster lives on the OUTER face and **harasses free traversal/exploration**
  of its cliff region. It's a **soft** gate, not a hard lock: you *can* flee
  into the dungeons, but the harassment taxes surface movement until you tire
  of it and take the boss on. **Killing it is one-time per cliff REGION** and
  permanently frees that region's surface. Diegetic opposition, not manufactured
  friction (`ethics/no-manufactured-friction` — the boss is a real creature
  gating a real reward, not an artificial wall). Needs per-region defeated
  state (dimension/region record in the entity ledger).
  - **Player chooses the arena (emergent from terrain variation).** Because the
    face is varied — shallow ledges, balconies, sky-bridges, not a uniform
    slope ([2]/[3]) — the player can **pick where to fight the boss**: lure it
    to a spot where the traversal obstacles aren't too punishing (a wide
    balcony, a shallow rest) rather than fighting on a sheer stretch. This
    falls out of the design for free (variation + a mobile boss) and is good
    sandbox texture — mastering the terrain *is* part of beating the gate, and
    the choice is the player's, not a scripted arena. Reinforces why the cliff
    wants real terrain variation, not a clean ramp.
- **Interior = endless dungeons.** The solid half (behind the face) is a
  dimension-sized rock volume; dungeons are seeded there via the existing
  local-interface structure primitive (no surface-height assumption), with
  cave/challenge terrain between them. The face is pockmarked with **doors**
  (portal/structure features on the density interface). Tagline: *"endless
  dungeons which hold the plunder of a million worlds."* Like Aether,
  Enigma uses **its own noise fields and feature response curves** (per
  `worldgen/dimension-profiles`) — its worldgen objective is challenge-carving
  and dungeon-density, not naturalistic surface, so reusing Terra's
  field set would be the wrong tool.
- **The plunder is knowledge as well as loot (absorbs the knowledge/archive
  axis).** "The plunder of a million worlds" is read literally: the dungeons
  hold not just valuable/useful items but **knowledge and lore** — Enigma
  interior is **a vast library/archive as much as a storehouse**, watched over
  by the same guardians. This is where the **knowledge/lore progression axis**
  (`progression` lore/discovery + `progression/knowledge-model`) gets its
  spatial home — *absorbed into Enigma rather than spent as a separate
  "archive dimension"* (`core/earned-vastness`: deepen an existing dimension on
  a new axis instead of adding a whole vast one). Concretely it includes
  **strata / models of past worlds** — huge dungeon rooms containing **replicas
  of cities, preserved artifacts, reconstructed structures** from vanished
  worlds — so delving is both a combat/loot run and a *discovery/decipherment*
  run. (The "real decodable structure, not flavor text" bar is
  `core/earned-vastness` again; the authored-content half leans on `data/authored-content`
  / `data/content-bundles` as those mature, but the generated strata/replica
  rooms are buildable now.)
- **Varied architectural styles.** Dungeon sections come in distinct
  architectural styles — a discrete selection made from Enigma's fields
  (through the integer-domain gate, `worldgen/field-driven-features`). (Foreshadows the base-extrapolation idea
  in `dimensions/base-extrapolation`: styles are the vocabulary such a feature
  would later extrapolate.)
- **Varied interior regimes break up the built dungeons:**
  - **Sponge / Nether-style terrain** — `bias ≈ 0` noise-carved rock (the
    no-sky regime) used as connective *obstacle* terrain between built
    sections; pure noise, the cheap/independent case.
  - **Platform-through-void sections** — a high-threshold regime (little solid)
    → platforms and catwalks through large open volume, not just halls and
    rooms. Lethal falls under Enigma's true-down gravity make these a
    traversal test.
- **Depth gradient = difficulty × loot (the progression spine).** "Deeper" is
  **orthogonal to the cliff surface** — distance *into the face* (along the
  inward surface normal), NOT world-z. Since the face averages ~70° off
  vertical, "into the cliff" is mostly horizontal; the depth axis is then given
  a **slight ~20° descent bias** so delving still carries a gentle
  descent-into-the-depths / rise-toward-the-surface feel without being a
  vertical shaft. So the progression axis ≈ "horizontally into the rock, tilted
  ~20° downward." This continuous depth field drives **both rising danger and
  rising loot tier** — deeper is more dangerous and better rewarded; feeds mob
  danger (`mobs/spawning`) and loot tables. It's Enigma's reward curve.
- **Noise-based generation, not structure-spreading (deliberate, note [8]).**
  Even though the content is rectilinear (rooms/halls/platforms), generate it
  from **noise so small areas generate independently** — every voxel a pure
  function of its own coords, preserving `core/chunk-locality` /
  `core/parallel-by-default` / reproducibility. Structure-spreading (place a
  seed, grow corridors across chunks) is explicitly avoided because it breaks
  chunk-independence (a corridor from chunk A must agree with chunk B without B
  seeing A). **Mechanism:** rectilinearity comes from **quantized-domain
  cellular/value noise** (snap sample coords to a grid → boxy cells,
  axis-aligned walls, room partitions straight from noise — FastNoise2 Cellular
  supports this). **The hard part is connectivity**, handled LOCALLY not
  globally: whether a doorway exists between two adjacent cells is a
  deterministic function of the hash of their shared face, so both chunks
  compute the same door without communicating (a pure shared-face hash,
  not a halo exchange: `core/chunk-locality`, bounded by
  `sim/speed-of-information`). Honest caveat: pure noise gives independence but NOT guaranteed
  global connectivity/traversability — the local per-face door rule is what
  buys a connected layout while staying independent, and getting it to
  reliably produce traversable (not dead-locked) dungeons is the open design
  work — see `worldgen/dungeon-connectivity`.
- **Principle — rules CORRECT the noise, they don't replace it.** Rules are
  unavoidable (pure noise won't make navigable structure), but the design
  stance is: noise generates first, and rules are **local corrections to the
  noise's deficiencies** (punch a door where two rooms would otherwise be
  sealed, ensure a cell has at least one exit, repair an isolated pocket) —
  NOT large random structures spanning many chunks that are *core* to
  generation. The correction rules stay as local as possible (ideally a cell
  and its immediate neighbors / shared faces), so generation remains
  chunk-independent and parallel. The test for any proposed rule: *does it act
  within a bounded local neighborhood, correcting noise output, or does it
  require authoring a multi-chunk structure as a first-class generation step?*
  The former is allowed; the latter is what we're avoiding.
- **Reality bombs are Enigma effects**: anti-lantern
  effects that induce **prompt decay** (`persist/diegetic-persistence`'s
  reality bombs). They are powerful, with real in-game uses, but optional:
  late- or endgame artifacts on no progression critical path, found, not
  crafted, and as unreplicable and dangerous as the rest of Enigma's
  magic. Note (proposed): prompt decay returns ore sites to
  unresolved (`geology/ore-sites`), so a reality bomb could re-roll ore early;
  being rare, unreplicable and dangerous is what keeps that from becoming a
  farming loop.
- **Finding aids: Enigma's diegetic search engine**.
  Information turns "roll the dice until it appears" into directed effort
  (`ethics/no-variable-ratio-core`): you learn roughly where something is,
  then go and get it. Serendipity stays for wanderers.
  - **Forms:** catalogues in the archive (what a wing holds, roughly where);
    specimen registers (where each specimen was kept); odd devices that answer
    a question when given a sample of what you seek. Real archives call these
    finding aids.
  - **Ancient and partly wrong:** "Specimen 41, east gallery" may be an empty
    case because it escaped. Right but incomplete keeps the mystery and gives
    escaped specimens a trail.
  - **Constraints:** approximate answers (a direction, region or depth band,
    never coordinates, `input/no-player-coordinates`); a cost per query;
    answers drawn only from deterministic sources (the generator, as for sky
    exposure, `render/shadows`, and the structure-plan cache,
    `worldgen/structure-plans`), so they point to sites and plans without
    resolving them and the anti-x-ray property holds (`geology/ore-sites`);
    and **aligned with what the player asks**, unlike most of Enigma's
    technology, though as ancient as the rest: when a finding aid seems wrong,
    it may be out of date, or broken, or right in a way nobody understands.
  - **Non-instantaneous**: speculative generation while hunting for
    something has a real, variable cost, so it is **projected into the game as
    the artifact working** (a needle settling, pages turning) rather than as a
    lag spike (`core/diegetic-cost`). The search runs as a low-priority
    background work under fair share (`core/fair-share`), the answer arrives
    when it is done, and a search that exhausts its budget answers "nothing
    within reach".
  - Terra's hand-drawn maps (`dimensions/terra`) are the folk-level counterpart.

---

## worldgen/dimension-warp — Space between dimensions warps locally: smooth, symmetric, fold-free 3D currents through the Aether

- **Goals**:
  - **Every dimension is the same size,** on every axis: a billion blocks
    is a billion blocks, vertically as horizontally (the ±10⁹ edge of
    `coords/f64-authoritative`).
  - **Local warping, evaluated without integrating** from an edge.
  - **Compression and dilation balance locally,** on a scale players can
    explore, so they can **chart the currents** and plan routes.
  - **Strong ratios: modes on the order of 12:1 and 1:12, with about 50:1
    as a reasonable extreme** some 2–3 standard deviations from a mode
   , well beyond the Nether's 8:1.
  - **Smooth:** no kinks, boundaries or visible grid.
  - **Bimodal:** mostly in a compression or a dilation lobe; 1:1 rare, a
    brief crossing.
  - **3D, vertical included:** the Aether is how you reach Terra's extreme
    depths.
- **The construction: symmetric coupling layers with smoothed zigzags.**
  - **A shared midpoint space.** Each layer relates two spaces A and B
    through a midpoint coordinate s along the layer's axis u:
    `a = s − e(s)`, `b = s + e(s)`. Neither side is privileged; swapping
    them negates e. The local ratio along u is `(1 + e′)/(1 − e′)`, so
    `|e′| < 1` covers every ratio from 0 to ∞, symmetrically (e′ = ±0.96 is
    about 49:1 either way).
  - **Leg slopes are drawn in log-ratio space** (proposed): each leg's
    ratio is ℓ = ln r, drawn from its lobe, a normal distribution centred
    on ±ln 12 (≈ ±2.48). For 50:1 (ln 50 ≈ 3.91) to sit 2–3 standard
    deviations out, σ ≈ 0.5–0.7 in ln r, roughly ×1.6–2.0. The slope is
    then e′ = tanh(ℓ/2), which **is below 1 in magnitude for any ℓ**, so no
    draw from the tails can fold the map; a soft clamp on ℓ (around 99:1,
    e′ = 0.98) bounds the extremes. The 12:1 modes sit at e′ ≈ ±0.85.
  - **e is a smoothed zigzag through jittered pins.** Pins are e's zero
    crossings, with hashed, irregular spacing; between consecutive pins e
    rises to a peak or falls to a trough, alternating, so consecutive legs
    meet at a pin with slopes of the same sign and the ratio never crosses
    1:1 there. **Legs are the currents:** near-constant slope, so
    near-constant ratio; **1:1 occurs only at the rounded extrema**, a
    fraction of about w/L of the ground (rounding radius w, leg length L).
    Corners are smoothed in closed form (log-sum-exp or softplus), so the
    ratio is continuous everywhere: the bimodal distribution by
    construction.
  - **Local balance by construction:** a leg's height is slope × length
    and is capped by the maximum displacement D; consecutive legs (one
    rising, one falling) reach the same height, so the stronger of the two
    is the shorter. Pins hold the extent exactly, so every dimension keeps
    its size.
  - **Fold-free by construction:** bump shapes have analytically known
    maximum slope, and slopes come from e′ = tanh(ℓ/2) with ℓ softly
    clamped, so `|e′| ≤ 0.98`.
  - **Coupling layers make it 3D:** a layer along u maps (u, v, w) to
    (u ∓ e(u; v, w), v, w); every parameter (pin positions, slopes,
    rounding) is smooth noise of the perpendicular coordinates. Its Jacobian
    is triangular with positive diagonal, so it can't fold whatever that
    noise does, provided pins keep their order (wobble less than half the
    minimum spacing). One layer per axis at least; layers along hashed
    directions keep currents from all running along x, y and z. The
    composite chains layers with −e on one side and +e on the other, so it
    stays symmetric.
  - **No explicit cells.** Each layer's pins form curved sheets; the curved,
    irregular cells between three families of sheets are never built.
    Where sheets of every layer cross, the dimensions coincide exactly:
    a jittered 3D lattice of **nodes**, natural portal sites and **the
    points where the worlds touch**: `liminality` is raised around them,
    parameterized per node rather than used as-is so the lattice stays
    hidden (`worldgen/noise-fields`). Explicit
    per-hexahedron warps were rejected: identity on cell faces shows the
    cells, ratio continuity across faces is hard, folds need per-cell
    Jacobian checks, and the inverse needs a 3D solve.
- **Through the Aether as hub:** each dimension has one warp to Aether
  space, so Terra → Enigma is the Aether map composed with Enigma's inverse
  and every portal agrees on correspondence. Ratios multiply when corridors
  line up across legs, so caps are set per leg. Rotation between
  dimensions (Terra and Enigma) composes later as a rigid transform; not
  needed for Terra and Aether, which are similarly oriented.
- **Cost and precision:** each layer's inverse is a monotone 1D solve
  (bracketed Newton within one leg, a few steps), the same cost in both
  directions. Exact evaluation is needed only where a landing cell matters
  (portal creation; portal links store both endpoints). Anything only read
  through the map (shared `liminality`, a currents overlay, cartography) is
  evaluated at coarse grid points and interpolated. Each layer's
  parameters are 2D noise, cacheable per chunk row or column. f64 at 10⁹
  resolves about 10⁻⁷ blocks, ample even at the strongest ratios. Pure,
  seed-keyed and bounded in reach: Cartographer-side
  (`worldgen/cartographer`'s invariants).
- **Noise libraries** (FastNoise2's domain warp) can't be the map (no fold
  guarantee, no inverse) but supply every perpendicular parameter, and may
  drive purely presentational distortion.
- **Gameplay**:
  - **The optimal route alternates dimensions, following the sign of the
    current:** travel in whichever dimension is compressed for the stretch
    ahead, switch through a portal where the sign flips. Horizontally and
    vertically alike.
  - **This motivates reaching the Aether:** you could dig miles down in
    Terra, but that's the hard way; strong vertical currents in the Aether's
    bottomless ocean reach Terra's depths far faster. A leg's height is
    capped by D, so extreme depth means chaining many corridors, a
    deliberate journey.
  - Learnable, not a meta: the best route depends on the local currents,
    so the skill is charting them (`core/no-meta`); fixed by the seed, so not
    a variable-ratio reward (`ethics/no-variable-ratio-core`).
  - **Ratios depend on direction:** a layer's lobes are bimodal along its
    axis, and diagonal travel mixes layers (likely to create interesting outcomes).
- **Open:** pin spacing, D, the lobe width (σ in 0.5–0.7) and rounding w;
  layer count and directions; per-leg caps through the hub; arrival safety
  for portals at depth or in solid rock; whether bimodality stays legible
  once layers compose (the 3D harness); vertical correspondence of surfaces
  (a Terra surface portal lands where in the Aether's sky-over-ocean).

# Uncommitted dimensions

Candidates that are recorded but not part of the pilot set, ordered by how
likely they are to be adopted: **Discordia**, then
**Machina**, then the dream dimension **Somnium**, then base extrapolation.
Each must still earn its place (`core/earned-vastness`). Their open questions
live in their entries here rather than in `design/open-questions.md`.

## dimensions/discordia (deferred) — Discordia: mayhem; a bookmarked dimension for competitive play

- **A fourth, bookmarked: Discordia, "mayhem"** (deferred, not committed). A
  dimension for **competitive games, PvP,
  minigames and custom rules**: materially different from the other three in
  emphasis (contest rather than survival, travel or discovery), probably with
  a bit of "murder". It follows the naming convention: Discordia is the Roman
  goddess of strife.
  - **Outside the fantasy and the single-player engagement model**:
    a deliberate exception, not an instance of them. Precedent: Minecraft's
    thriving competitive ecosystem (PvP contests, Bedwars, MCC and others),
    built largely on custom servers and mods; a built-in dimension for it
    reduces the need for mods.
  - **Sketch**: a **sky-island world** with **very rapid decay**, **no
    valuables**, and some equivalent of **command blocks**. Notes (proposed):
    rapid decay resets arenas for free through the existing decay mechanism
    (`persist/diegetic-persistence`); no valuables means no hard content
    (`core/soft-over-hard`), so nothing in Discordia can be duped or carried
    back as wealth; command blocks fit the logic-machine and grammar-binding
    machinery (`sim/logic-machines`, `core/diegetic-authoring`). The earlier
    rejection of a sky dimension (`dimensions/pilot-set`) was of a redundant *survival*
    dimension; Discordia's sky islands serve a different purpose.
- Open: whether and how it fits the progression; whether it is one shared
  dimension or per-match pockets (`world/dimensions`); how custom rules relate
  to `loop/opt-in-challenges` (game-enforced settings); what crosses the
  boundary (do items and deaths there affect the rest of the world, given
  conservation in `data/entity-ledger`); and how it sits with the engagement
  ethics, since competitive loops are where FOMO and compulsion tend to creep
  in.

## dimensions/machina (deferred) — Machina: an endless mechanical world

- **An endless mechanical world:** a dimension whose substrate is machinery
  rather than terrain (gears, conduits, moving platforms, powered mechanism
  as the landscape itself). Recorded as the most interesting dimension idea
  at the time.
- **Why it isn't absorbed into an existing dimension:** unlike the knowledge
  and archive axis that folded into Enigma, it isn't loot (Enigma), travel
  (Aether) or survival (Terra). The world itself is **active, moving
  mechanism** rather than static terrain you act upon. Its closest kin among
  the progression axes is logic machines and automation (`progression`
  Creation and Industry families), but as an environment rather than
  something the player builds.
- **Why it's deferred:**
  - **Simulation cost.** A dimension-scale *moving* mechanical world is a far
    bigger commitment than static terrain plus local fluid and CA. It must be
    checked against `sim/cpu-parallelism` and `core/chunk-locality`: is
    dimension-scale mechanism still chunk-local and parallel, or does moving
    machinery couple chunks pervasively?
  - **Relationship to player-built logic machines** (`sim/logic-machines`):
    is Machina the *same* mechanism substrate at world scale, or a distinct
    one? The same would be a powerful unification; a distinct one risks a
    second mechanism model.
- Name: *Machina* follows the Latin/Greek naming convention
  (`worldgen/dimension-cosmology`).
- Open: the simulation-cost and logic-machine-unification questions above,
  which must be answered before it could be first-class.

## dimensions/somnium (deferred) — Somnium: a dream dimension authored from content

- **A dream or mind-authored space,** composed from a soul's authored
  content, memory or intent bundles (yours or an NPC's). In practice it's a
  social and sharing feature (visiting spaces built from others' content
  bundles) wearing a dimension costume.
- **Downstream of the content-authoring system:** it sits on the
  authorship-source axis (content authored by an entity or player, not a
  seed), so it depends on authoring and sharing maturing first
  (`core/diegetic-authoring`, `data/authored-content`, `data/content-bundles`,
  `net/scale-premise`). The bar it must clear: it has to *fundamentally enable
  content authorship*, not merely reskin seed-authored generation. Its
  substance is the bundle system, not a worldgen profile.
- Related: `sim/dreaming-designation` (planning tools while asleep).

## dimensions/base-extrapolation (deferred) — A dimension that extrapolates a player's build

- **You build a base in a given architectural style,** then spawn a dimension
  that **extrapolates that base indefinitely**: the generator's input *is*
  your build.
- **It passes the authorship bar cleanly** (the build is the authored
  content, not a palette on noise), like Somnium downstream of the
  content-authoring system, but it's not readily implementable: it needs a
  style-capture and coherent, indefinite extrapolation mechanism over
  player-authored structure, which doesn't exist yet.
- Enigma's varied architectural styles are the vocabulary such a feature
  would later extrapolate (`dimensions/enigma`). Strong future idea; parked.
