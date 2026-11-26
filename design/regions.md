# Regions

The world's large-scale structure: the slow regional fields that set a
place's character and dangers, provinces that bias them over days of
travel, polar regions, and the sparse site sets both are built on. The
field engine they rest on is `design/worldgen.md`.

## worldgen/regional-fields — Slow fields that set a place's character and dangers

Regional fields from the primary set of `worldgen/noise-fields`: coarse
sample grids, and the fields provinces bias.

- `weirdness` — how far from baseline Earth-normal. Regional (coarse sample grid,
  varies slowly in all 3 axes). Perturbs terrain AND gates strange gameplay.
- `alignment` — good↔evil. REGIONAL — explicitly does not need 1-foot scale;
  coarse sample grid, interpolated. Tints terrain AND drives `mobs/spawning`.
- `yield` — generic extractable value (minerals, flora, fauna, fertility — not
  ore-specific). Regional-to-mid sample grid. Feeds `world/progressive-lod`
  refinement and the extraction mint into `data/entity-ledger`.
  Possibility (proposed, not chosen): a **contrast curve** (a soft
  threshold, `core/soft-thresholds`) making yield roughly bimodal, rich
  patches separated by long lean stretches, with threshold and steepness
  set per province type; lean stretches would double as travel friction
  (little food, fuel or timber to live off). Fields should keep distinct
  characters, so this is a candidate for yield, not a pattern for every
  field.
- `liminality` — how thin the
  boundary between worlds is at a place, i.e. how readily low magic works
  there (`worldgen/dimension-cosmology`). Regional. May demote to
  derived-from-weirdness if it proves non-independent.
  **Shared among dimensions**: one liminality field
  spans the dimensions rather than each having its own, and where it is
  high the dimensions show **perceptible similarity** to each other.
  Notes (proposed): evaluate it at corresponding points
  through the cross-dimension coordinate map (`worldgen/dimension-warp`),
  keyed by a seed sub-key shared across dimensions
  (`worldgen/seed-keys`). **Coupled to the warp**:
  liminality is defined once in the warp's hub space (the Aether /
  midpoint coordinates) and every dimension samples it through its warp,
  so it is shared by construction; and the warp's jittered **nodes** (where
  the dimensions coincide exactly) are **the points where the worlds
  touch**, so liminality is raised around them, **parameterized around
  the nodes rather than using them as-is** (to further hide the 3D lattice
  of nodes), i.e. blended with liminality's own noise in strength,
  reach and shape per node. Strong currents and high liminality therefore
  coincide, which is accepted (player interest will coincide with both).
  It stays a primary field, not collapsed into the warp. A liminal place
  in Terra then lines up with liminal places elsewhere, and in each it
  borrows traits from the others (materials, plants, light, sounds, shapes
  of terrain), strongest at the peaks. Similarity is the point, so it must
  be legible to players; it also gives portals and folk magic natural
  sites, since thin places are where the worlds touch. If liminality ever
  becomes derived from `weirdness`, this sharing is the reason it stays
  independent.
  **Per-dimension amplification.** The shared field is the base; a
  dimension may amplify it locally (a province bias,
  `worldgen/provinces`), multiplicatively: liminality in dimension d is
  the shared base × a gain ≥ 0 set by d's own province. Multiplying keeps
  the thin places where the shared field puts them, so every peak still
  has counterparts elsewhere. Turning up the gain in a province increases
  bleed-through there, which shapes that province's character. Whether
  the other side's wall thins too (making these places where the worlds
  touch in both directions) or the bleed stays one-sided is deferred
  until after Terra's worldbuilding; several mechanisms could implement
  either. An additive bias is avoided: it would make thin places with no
  counterpart.
- `savagery` — (after Dwarf Fortress) how wild
  and untamed a place is, independent of `alignment` (moral) and
  `weirdness` (strangeness). Regional, coarse sample grid. Gameplay: denser,
  larger and more aggressive wildlife (fits `core/low-fantasy`'s deadly
  animals) and a stronger input to `danger`. Dual-purpose (proposed): it
  also perturbs terrain and ground cover, with overgrowth,
  thicker undergrowth and larger trees where it is high. Open: how it
  interacts with the parked territory / civilization subsystem (DF's
  civilizations avoid savage regions), and whether stabilization or
  settlement can tame it.
- `habitation` — (proposed, `worldgen/provinces`) how much people live
  in a place now: living settlements, farms, maintained roads, inhabited
  outposts. Regional, coarse sample grid. Independent of `savagery`,
  though province types often move the two in opposite directions.
  Gameplay: what living structures and paths feature placement finds
  (`worldgen/field-driven-features`), and how readily a traveller finds
  shelter, trade and routes. Open: how it relates to the parked
  civilization subsystem, and whether players' own building raises it
  locally (as soft simulation) or it stays worldgen-only.
- `antiquity` — (proposed, `worldgen/provinces`) how much the past has
  marked a place: ruins, overgrown roads, waystones, barrows, buried
  relics. Regional, coarse sample grid. Independent of `habitation`; the
  two together give the nine **settlements** of `worldgen/provinces`
  (Trackless, Storied, Forgotten and the rest). Independent of `savagery`
  too: an overgrown, savage place can hold ancient ruins. Ruin style
  (fresh abandonment versus deep ruin) is not a field; it comes from the
  province type's membership weight. Open: whether `antiquity` rises with
  depth (buried strata), which is for the underground's own systems.

## worldgen/provinces (draft) — Provinces of a few authored types bias the regional fields at a scale that takes days to cross

- **The aim: a world that feels vast.** Minecraft repeats all its variety
  within a few thousand blocks, so every world feels the same and little
  travel is needed to see it all. Here the *mix* of regions changes too,
  at a scale that is theoretically traversable but a significant
  commitment: hours to cross a province, days to reach a rare kind. The
  regional fields (`alignment`, `savagery`, `weirdness`, `liminality`,
  `yield`, `habitation`, `antiquity`) vary at about 64k ft and finer; provinces add
  structure at about 2¹⁷–2²⁰ ft (25–200 miles), beyond the far view radius
  (2¹⁷ ft), so provinces are found by travel, not seen from a hilltop.
- **Province cells.** Province sites are a thinned Poisson point set
  (`worldgen/site-sets`) with a mean spacing of about 2¹⁹ ft (placeholder),
  with gap-filler sites so no province is unboundedly large; each site's
  cell is the region nearer to it than to any other site, with
  domain-warped borders so they don't read as Voronoi straight lines.
  Each site hashes, under its key region's seed key
  (`worldgen/seed-keys`), to one **province type** from an authored list
  with authored weights (the list is below).
- **A type is a setting, not a feature list.** It is a vector of biases and
  spreads for the regional fields: it moves each field's mean and widens or
  narrows its range, and the fields keep varying inside the province.
  "Solitude" is low `habitation` and `antiquity`; "Blight" is low
  `alignment` and low `yield`. Most features read only the fields
  (`worldgen/field-driven-features`), but features may also read a
  type's **membership weight** for flavour: a novelty species, an
  ambience, instrument behaviour. The weight is 1 inside the province and
  falls to 0 across the border band, so such features fade at the border
  the way the biases do; the type id itself is never interpolated.
  Types bias only the regional fields: no terrain fields (one terrain
  engine is hard enough to tune) and no climate shifts (settling nearer a
  pole or the equator already gives those, `worldgen/poles`).
- **Types stand in for difficulty.** Monsters track `alignment`
  (`mobs/spawning`) and wildlife tracks `savagery` (`mobs/wildlife`), so
  the two fields give a 3×3 grid of **temperaments**, and every type is
  tagged with one:

  | | Benign wildlife | Normal wildlife | Savage wildlife |
  |---|---|---|---|
  | **Good** | Idyllic | Fair | Jubilant |
  | **Neutral** | Tranquil | Familiar | Untamed |
  | **Evil** | Eerie | Forsaken | Dreadful |

  Normal wildlife is already a real threat; benign wildlife still fights
  back up close. A temperament fixes the two threat fields' biases.
- **Settlements give each type its human mood.** `habitation` (people now)
  and `antiquity` (marks of the past) give a second 3×3 grid, the
  **settlement**, and every type is tagged with one too:

  | | Low antiquity | Medium antiquity | High antiquity |
  |---|---|---|---|
  | **Low habitation** | Trackless | Abandoned | Forgotten |
  | **Medium habitation** | Frontier | Settled | Storied |
  | **High habitation** | Burgeoning | Thriving | Enduring |

  The low row is a timeline: no one ever, people left, people left long
  ago. A settlement fixes the two fields' biases at one of three levels
  each, which is all that needs to be distinguishable. The two tags are
  also how a mood is conveyed: a province can be named to the player by
  them ("Eerie, Forgotten"), for worldbuilding and on maps, and presets
  can ask for them ("Fair and Storied"). The type's other biases are its
  story.
- **The list is curated, one layer.** Each type is authored as a whole,
  with its lore and border blends, rather than composed from independent
  layers (a temperament layer crossed with a character layer was
  considered: it composes well, but provinces lose authored identity and,
  with separate cell grids, change partway across). Every temperament
  appears at least once, and nearly every settlement; `weirdness` and
  `liminality` are pulled in by few types, so
  strangeness stays special. Some types carry a novelty signature
  (prehistoric or strange species), never a resource found nowhere else:
  types set difficulty and feel, not access. Draft list (names and biases
  open to revision; weights unset):

  | Temperament | Settlement | Type | Other biases |
  |---|---|---|---|
  | Idyllic | Settled | Arcadia | `yield` ↑ |
  | Idyllic | Trackless | Lost Eden | `liminality` slightly ↑ |
  | Fair | Enduring | Hallowed lands | living shrines and pilgrim roads on ancient sites, `liminality` ↑ |
  | Fair | Forgotten | Fey reaches | `weirdness` ↑, `liminality` ↑, compass interference |
  | Jubilant | Frontier | Verdance | `yield` ↑↑, giant flora |
  | Jubilant | Trackless | Dawnlands | prehistoric flora and fauna, `weirdness` slightly ↑ |
  | Tranquil | Storied | Old kingdoms | layered ruins, `alignment` spread wide |
  | Tranquil | Abandoned | Barrens | `yield` ↓↓ (scarcity without threats; land given up) |
  | Tranquil | Burgeoning | the Moot | many peoples' camps converging on a great meeting ground under a standing truce: trade fairs and gatherings; rival factions side by side, so news (`peoples/reputation`) travels fast and standing moves quickly both ways |
  | Familiar | Thriving | Heartland | narrow spreads |
  | Familiar | Trackless | Solitude | (alone in the world) |
  | Familiar | Settled | Strange lands | `weirdness` ↑↑, wide spreads (folk living with the strange) |
  | Untamed | Forgotten | Wilderness | (ruins swallowed by the wild) |
  | Untamed | Frontier | Borderlands | living, palisaded outposts |
  | Untamed | Trackless | Primeval | ancient growth, megafauna |
  | Eerie | Storied | Haunted | living towns among layered ruins that are not at rest, wildlife scarce |
  | Eerie | Frontier | Mistlands | `liminality` ↑, `weirdness` ↑, compass interference; tribes newly drawn to an altered land by its strangeness, and facing its dangers (after *Roadside Picnic*) |
  | Forsaken | Abandoned | Blight | `yield` ↓↓ |
  | Forsaken | Forgotten | Fallen realm | deep ruins gone bad |
  | Forsaken | Thriving | Tainted | `yield` ↑↑ (rich but cursed: generations drawn to a lure) |
  | Dreadful | Trackless | the Maw | land that swallows travellers |
  | Dreadful | Forgotten | Nightmare | `weirdness` ↑↑, `liminality` ↑ |
  | Dreadful | Enduring | Besieged | ancient walled ruins, packed with tribes holding them against the dread |

  The settlements need not be balanced across types.

  Solitude (Familiar, Trackless) is the vanilla "alone in the world"
  preset; Wilderness (Untamed, Forgotten) is savage and holds ruins the
  wild has swallowed. Old kingdoms and Haunted are both Storied and
  differ in temperament: in Haunted the past does not stay buried.
  Mistlands and Tainted both draw newcomers to a strange or cursed lure;
  Mistlands is new (Frontier: fresh camps, no deep past), while Tainted
  carries a longer history of temptation and corruption. "Giant flora", "prehistoric" and
  "compass interference" are not fields; they read the type's membership
  weight directly, usually combined with field conditions (Dawnlands
  megafauna where its weight and `savagery` are both high, say).
- **Borders blend the settings, never the type.** Across a border band a
  few miles wide the two cells' bias vectors are interpolated; type ids are
  never interpolated. Band width is a knob per pair or per type: narrow
  reads as entering the Blight, wide as natural drift.
- **The blend can make empty stretches too.** A border need not be a
  straight interpolation: the blend between two provinces may pass
  through lean values across several fields at once (low `yield`, low
  `habitation`), so provinces are separated by marches and wastes that
  take time to cross. How each field blends across a border is a design
  choice per field and per type pair.
- **Why categorical rather than more slow octaves:** cost is not the reason
  (province-scale values are evaluated only at the coarsest levels).
  Independent slow fields rarely coincide, so a compound setting ("Forsaken and
  Trackless") would grow exponentially rarer with each condition; a type
  authors the combination. Types are also legible: they can be named,
  mapped (Terra's hand-drawn maps, `dimensions/terra`) and given lore.
- **The guard against repetition:** with a fixed list, every province is
  one of N, so a type sets ranges, not values (two Blight provinces can
  differ a great deal), and the regional fields add their own variation
  within it.
- **World presets are spawn searches.** A preset changes only where a world
  starts, never the world: the same seed is the same world, so shared and
  speedrun seeds stay consistent. A preset names a type or a temperament
  ("any Dreadful province" for a hardcore start, Idyllic for a peaceful
  one), and the spawn is the nearest match, found by enumerating cell
  hashes outward from the origin, which is exact, cheap and bounded: a
  type with share w of sites is expected within about √(1/w) province
  spacings (on average; Poisson sites vary more than a lattice would). The
  **default preset** looks for the nearest Heartland, so the extremes
  exist in every world but lie days away. A late-game province seeker compass makes the same search an
  in-world object (`tools/compasses`). Spawning far from the origin is already a requirement (v0.2.15
  in `versions.md`).
- **Travel is a restart.** Because provinces are settings, far apart, a
  player or group who wants a different game (harder, stranger, emptier)
  can have one without a new world by making a journey to a province of
  another kind: about three or four in-game days (2½–3 real hours) to
  reach a neighbouring province's heart. The journey is meant to be
  memorable, especially as a group, and its length marks the old
  province off clearly from the new. The world never needs resetting to
  change its difficulty or mood. At 2¹⁹ ft cells this target holds with
  a mount: a neighbour's heart is about 2¹⁸ ft away, roughly 2.3 real
  hours at a horse's 32 ft/s, or 5 hours on foot at 14 ft/s (both
  speeds placeholders, Minecraft's, kept until there is a world to tune
  them in). Seeking
  out a specific rare kind, about √(1/w) cells away, is late game, when
  faster travel exists: Aether routes, whose currents dilate distance
  around 12:1 (`worldgen/dimension-warp`), and portals that land far
  away but not at a place exactly of the player's choosing (a randomized
  portal model, `world/dimensional-travel`). The province seeker compass
  (`tools/compasses`) then says which way to head from where one lands.
- **Provinces are a surface system.** They are mainly a way to seek a
  character above ground: a province's biases apply near the surface and
  fade with depth. The world is too deep for one flavour to hold unvarying
  all the way down, so the underground may get corresponding systems of
  its own (deep layers or 3D cells keyed on the 3D key-cell grid,
  `worldgen/seed-keys`), designed separately. Bias-free dimensions (cave
  worlds, `worldgen/dimension-profiles`) are 3D at large scale anyway.
- **Open:**
  - which regional fields are truly 3D; how fast province biases fade
    with depth; and what the underground's corresponding systems are;
  - the type list's names and biases, each type's weight, and how a
    temperament's two threat biases are sized;
  - the cell size, and whether types cluster (neighbouring cells more
    alike) or are independent;
  - border bands per type pair;
  - per-dimension lists (Enigma and the Aether may want their own types or
    none); province biases on `liminality` are multiplicative gains on
    the shared field (`worldgen/regional-fields`);
  - travel at this scale: mounts, roads and rail, and the Aether as a
    shortcut if the cross-dimension warp compresses distance
    (`worldgen/dimension-warp`); without these, provinces days apart are
    never seen;
  - presets that combine conditions beyond one type, and what happens when
    none is found within a bound (settle for the best match);
  - candidate types deferred until yield mechanics are understood:
    Patchwork (every spread wide, a quilt of small extremes) and
    Goldfields (a steep `yield` contrast curve, riches in patches).

## worldgen/poles (draft) — Polar regions around sparse pole sites, an equator where two poles are equidistant, and a compass the server reads

- **Temperature's extremes are places, not noise.** Pole sites are a
  thinned Poisson point set in each dimension (`worldgen/site-sets`, no
  gap-fillers), keyed like province sites. Where no pole lies within the
  lookup radius, φ is taken as 0.5: a rare, wide tropical stretch, where
  the compass wanders. With
  d₁ the distance to the nearest pole and d₂ to the second nearest, the
  **latitude coordinate** φ = d₁ / (d₁ + d₂) runs from 0 at a pole to 0.5 on
  the **equator**, which is the set of points equidistant from two poles: a
  network of warm belts between polar regions rather than one line, hottest
  where three pole cells meet. Temperature is a curve over φ (a narrow,
  steep cold cap, a broad warm band) plus the regional noise and the
  vertical lapse rate; domain-warped distances and a smooth minimum keep
  caps from reading as circles and the equator from showing Voronoi kinks.
- **Travelable, so finer than provinces.** Poles are about 2¹⁷–2¹⁸ ft apart
  (25–50 miles; placeholders), so a pole is hours from the equator, not
  days. A province (`worldgen/provinces`, about 2¹⁹ ft) therefore spans
  several polar cells. Province types do not alter the caps
  (`worldgen/provinces` has no climate shifts).
- **Secrecy by distance, like every other field.** Pole sites are keyed on
  their own key tier (`worldgen/seed-keys`) and released by distance from
  the player, out to about two pole spacings, so a client can compute φ,
  temperature and the poles near it, and see a nearby pole directly. What
  no client can do is map the world at large scale independent of where its
  player is (a Chunkbase-style map of every pole): keys beyond the release
  radius are one-way derived on the server and never sent.
  - **The compass reads the gradient of φ**, the direction φ changes at the
    player's position. Within the release radius a client could compute it
    anyway, which is acceptable; beyond it (a pole further than the
    released keys reach) the reading comes from the server, sent only while
    the player holds a working compass and only as a direction, its
    precision perhaps depending on the compass's quality. So the compass can
    be gated by progression where gating means anything: at long range.
  - **Following temperature by hand or by eye is intended:** snow, ice and
    cold plants show a nearby pole, and a player can walk the gradient.
- **The needle behaves in-world:** it swings to the other pole on crossing
  the equator, spins at a pole and wavers at a triple junction.
- **Open:** spacing and the curve's shape; whether all dimensions have
  poles (the Aether and Enigma may have none); how φ feeds province weights
  and other fields (moisture, daylight, seasons); what the compass is and
  where it falls in progression (`tools/compasses`); the polar tier's
  release radius.

## worldgen/site-sets (draft) — Sparse sites (poles, provinces) are thinned Poisson points, not a lattice, so no grid shows

- **Why not a lattice.** A jittered lattice puts exactly one site per cell,
  which leaves a faint regular spacing; the lack of grid structure is
  wanted. A homogeneous Poisson process has no such signature: bucketed
  or not, it looks the same.
- **Generation.** The plane is divided into **lookup buckets**, an index
  only. Each bucket hashes (integer hashes, under the relevant seed key,
  `worldgen/seed-keys`) to a Poisson-distributed number of candidates (0,
  1, 2, …) at hashed positions. **Hard-core thinning** (Matérn type II):
  a candidate is dropped if a candidate with a higher hash lies within
  radius r, which keeps sites from clumping and sets a minimum spacing.
  Optionally a slow noise field modulates the intensity, so some reaches
  are dense with sites and others sparse.
- **Gap-fillers (provinces only).** Poisson gaps are unbounded. A sparse
  secondary set of candidates at about twice the mean spacing survives
  only where no ordinary site lies within a large radius, which bounds
  the gaps; the filler pattern appears only inside rare gaps. Poles need
  none: a gap reads as tropics.
- **Lookup is local and exact.** The nearest and second-nearest sites of
  a point come from scanning buckets out to a bounded radius (plus r for
  thinning), a few dozen integer hashes, bit-identical on every client and
  the server (unlike sites defined as noise minima, which depend on
  floating-point noise evaluation and cost thousands of samples).
- **Rejected:** a jittered lattice (grid signature); sites at deep minima
  of a band-limited noise field (costly to find, minimum spacing hard to
  control, not bit-exact across SIMD paths).
- **Open:** intensities, r, the filler spacing and survival radius, and
  the lookup radius; whether the intensity modulation is used.
