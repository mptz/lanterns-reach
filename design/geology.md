# Geology

Rock, minerals and how they are arranged underground. Self-contained world
building for a digging game: what the player finds when they mine, and the
rules a player can learn from it. Inspired in part by Dwarf Fortress, whose
regional layer stacks and host-rock-specific minerals make mining feel like
reading a place rather than sampling a loot table.

Earth facts below are from memory of standard geology references, not
re-checked; numbers are approximate and only meant to set relative weights.

## geology/no-generic-stone — The playable world has specific rocks; "stone" is only an LOD placeholder

- Decision: there is **no generic "stone" block** in the playable world. Every
  solid rock cell is a specific rock (granite, shale, basalt, ...). "Stone"
  exists only as the **coarse-LOD superclass** that `world/progressive-lod`
  already describes ("stone → a specific stone ... from finer noise"); it
  never survives refinement to a playable LOD, and is never obtainable
  (`invariant/world/superclasses-unobtainable`).
- Rationale: in a digging game the rock is most of what the player looks at
  and handles. A plausible, varied geology gives mining texture, and makes
  rock a readable signal of what lies nearby (see `geology/rock-suites`).
- Consequence: refinement resolves **stone → a specific rock**, possibly via
  intermediate superclasses derived from the fields (e.g. a dark/pale or
  layered/massive family), never via suites, which are not in-game objects
  (`geology/rock-suites`). The refinement invariant (a superclass only resolves
  into its members) applies at each level.

## geology/rock-catalog (draft) — The common rocks of Earth's crust (reference)

- **Three origins:**
  - **Igneous:** cooled from melt. *Intrusive* (cooled slowly underground,
    coarse crystals): granite, granodiorite, diorite, gabbro, peridotite.
    *Extrusive* (erupted, fine-grained or glassy): basalt, andesite, rhyolite,
    tuff, pumice, obsidian.
  - **Sedimentary:** deposited and lithified at the surface: shale/mudstone,
    siltstone, sandstone, conglomerate, limestone, dolostone, chalk, chert/flint,
    evaporites (rock salt, gypsum), coal.
  - **Metamorphic:** existing rock recrystallized by heat and pressure. Each has a
    parent ("protolith"): shale → slate → phyllite → schist → gneiss (rising
    grade); limestone → marble; sandstone → quartzite; basalt → greenschist /
    amphibolite; peridotite → serpentinite.
- **Abundance (approximate):**
  - **By volume, the crust is overwhelmingly igneous and metamorphic.** Oceanic
    crust is basalt over gabbro. Continental crust is granitic rocks and gneiss
    at depth, averaging about granodiorite in composition.
  - **Sedimentary rock is a thin veneer** (roughly 5% of crustal volume) that
    covers most of the land surface (often quoted as about three quarters).
  - **Within sedimentary rock:** shale/mudstone is the large majority (roughly
    two thirds), then sandstone, then limestone/dolostone; the rest is minor.
  - **Minerals:** feldspars make up about half the crust, quartz about an
    eighth; then pyroxenes, amphiboles, micas, olivine, clays and calcite.
- **Implication for a game:** what a surface player sees is mostly sedimentary;
  digging deep on a continent reaches granite and gneiss; under the sea floor,
  basalt. The "common rock" changes with depth and setting, which is the
  structure worth reproducing.

| Rock | Origin | Feel / properties | Typical game uses |
|---|---|---|---|
| Granite | intrusive | hard, speckled, coarse crystals | strong building stone |
| Basalt | extrusive | dark, dense; columnar jointing | building, paving |
| Gabbro | intrusive | dark, coarse | building |
| Andesite, rhyolite | extrusive | grey to pale, fine | building |
| Obsidian | extrusive glass | black, conchoidal fracture | blades, tools |
| Pumice, tuff | extrusive | light, porous, soft | light blocks, abrasive |
| Shale / mudstone | sedimentary | soft, layered, crumbly | weak; source of clay |
| Sandstone | sedimentary | soft-ish, colored bands | easy building stone |
| Limestone | sedimentary | pale, soluble (karst, caves) | lime, mortar, flux |
| Chalk | sedimentary | very soft, white | lime; flint nodules inside |
| Chert / flint | sedimentary | hard, sharp edges | early tools, fire starting |
| Rock salt, gypsum | evaporite | soft, soluble | salt (food), plaster |
| Coal | sedimentary | black, layered seams | fuel |
| Slate | metamorphic | splits into flat sheets | roofing, tiles |
| Schist | metamorphic | glittery mica, flaky | weak; garnet host |
| Gneiss | metamorphic | banded, hard | building |
| Marble | metamorphic | pale, carvable | decorative stone, sculpture |
| Quartzite | metamorphic | very hard | tough stone, abrasive |
| Serpentinite | metamorphic | green, slippery feel | decorative; host of chromite, asbestos |
| Peridotite | intrusive (mantle) | green, olivine-rich | the deep; host of chromite, platinum, diamonds (via kimberlite) |

## geology/rock-suites — Rocks come in geologically plausible combinations
- **Open:** the suite list.

- Decision: **suites are a design tool, not an in-game construct**,
  the geological counterpart of the artistic prototypes in
  `worldgen/field-driven-features`. A suite is an assemblage that occurs
  together on Earth because one geological setting made it: which rocks, how
  stacked, which minerals they host. It is used to **author** rock and mineral
  conditions and to **evaluate** the result ("do our sedimentary basins read as
  basins?"). Nothing in the engine knows the word "basin".
- **In game, rocks and minerals are parameterized on field combinations**,
  exactly like other features: each rock has soft conditions on the fields
  (and depth), and coherence comes from shared inputs. Rock contacts are
  mostly sharp on Earth, so rock conditions tend toward the steep end of the
  steepness dial. A suite is then a region of field space where a set of rocks
  co-occurs, which can be checked by sampling, not a category the world stores.
- Players still learn the suites, implicitly: the host rock predicts the
  minerals nearby, a real learnable rule (`core/earned-vastness`).
- **Draft suites (Earth settings):**

| Suite | Setting | Rocks (top → down / main) | Characteristic minerals |
|---|---|---|---|
| **Sedimentary basin** | lowlands, platforms | soil → sandstone, siltstone, shale, limestone, coal seams | coal, clay, iron (bog/sedimentary ores), salt and gypsum in places, oil shale |
| **Carbonate platform / karst** | warm shallow seas, uplifted | limestone, dolostone, chalk (with flint), marble at depth | flint, lead–zinc (galena, sphalerite), caves by dissolution |
| **Evaporite basin** | arid, enclosed basins | red sandstone, mudstone, gypsum, rock salt, anhydrite | salt, gypsum, potash |
| **Shield / craton** | old continental core | gneiss, granite, schist, amphibolite, quartzite; greenstone belts | gold (greenstone), banded iron, nickel, uranium; pegmatites (mica, feldspar, quartz crystal, beryl, tourmaline) |
| **Mountain belt (orogen)** | colliding plates | slate → phyllite → schist → gneiss with depth, marble, quartzite, granite intrusions | garnet, kyanite, hydrothermal veins near intrusions (copper, lead, zinc, silver, tin, tungsten) |
| **Volcanic arc** | above subduction zones | andesite, dacite, rhyolite, tuff, pumice, obsidian, porphyry (shallow intrusions); diorite/granodiorite below | porphyry copper (around porphyry intrusions), gold, silver, sulfur, obsidian |
| **Basalt province** | hotspots, flood basalts, sea floor | basalt (columnar), gabbro below | copper, zeolites and agate in gas pockets |
| **Ophiolite** | sea floor thrust onto land | pillow basalt, sheeted dikes, gabbro, peridotite → serpentinite; chert caps | chromite, nickel, platinum, copper (massive sulfides), jade, asbestos |
| **Alkaline / rift** | continental rifts | basalt, trachyte, phonolite, syenite, carbonatite | rare earths, niobium (carbonatite); kimberlite pipes in old cratons (diamonds) |
| **Surface deposits** | everywhere, on top | soil, clay, sand, gravel, till, loess | clay, sand (glass), gravel; placer gold in rivers |

- **Typical vertical structure on a continent:** surface deposits and soil →
  sedimentary cover (variable thickness, folded and tilted in mountain belts) →
  crystalline basement (granite, gneiss) → at great depth, mafic and
  ultramafic rock (gabbro, peridotite). In the game the deep can go further
  than Earth's reachable crust, which suits "depth suffices" for the deep
  dimension-like strata (`dimensions/pilot-set`: no hell dimension).
- **Mineral forms worth distinguishing** (Dwarf Fortress does this): **veins**
  (thin sheets cutting rock, near intrusions), **layers and seams** (coal,
  salt, banded iron), **clusters/nodules** (flint in chalk), **scattered
  inclusions** (gems in pegmatites, garnets in schist), **pipes** (kimberlite),
  and **placers** (heavy grains in river gravels).
- **Dwarf Fortress reference** (from memory): each region gets a layer stack
  drawn from soil, sedimentary, igneous extrusive, igneous intrusive and
  metamorphic layer classes; minerals declare the host-rock classes and forms
  they occur in. That is the model these suites refine: suites are coherent
  stacks rather than independent random layers.

## geology/ore-sites — Soft generation places ore sites; an ore is resolved authoritatively when revealed

- Decision: **soft generation identifies ore *sites***: cells that *can*
  specialize to an ore, placed by the host-rock conditions
  (`geology/rock-suites`) and free to use float noise. A site is not yet an
  ore. **When a site is revealed** (the player interacts with it or comes close
  enough), the authority **resolves** it: ore or plain host rock, and which
  ore. This removes the tension between soft host rock and hard valuable ore:
  rock and site placement stay soft, and only the resolution is hard.
- **The mint is at resolution (specialization), not extraction**.
  A resolved valuable ore becomes an individually tracked ledger entity
  **in place** (`data/entity-ledger`, `sim/valuable-blocks`). That is what
  lets pistons push and players otherwise manipulate ore blocks as conserved
  objects; mining is then a move out of the world, not a mint. Resolution is
  decided once by the authority, so a site that exists in only one machine's
  soft generation is harmless. The outcome can also be a hash of (seed, cell)
  in the integer domain, so it is reproducible. Non-valuable content (stone,
  wheat, wood) still mints at harvest (`core/soft-over-hard`).
- **Decay regrowth is a feature, not a farming bug**. When an
  unstabilized chunk reverts to baseline (`persist/diegetic-persistence`), a
  resolved ore still in place is sunk through the ledger (a declared sink),
  its site returns to soft and unresolved, and a later reveal resolves it
  afresh (a declared source). Ore is therefore **renewable**, on a timescale
  long enough (on the order of **6+ real months** of `WorldTime`, not game
  months) that it doesn't alter game
  balance. **Ethics rationale:** theoretically renewable resources discourage
  the hoarding compulsion that non-renewables create (`ethics/no-fomo`'s "no
  artificial scarcity"; `ethics/wanting-coupled-to-liking`). Extracted ore is
  unaffected: it already left the chunk.
- **Anti-x-ray by construction**, provided resolution happens late enough: an
  unrevealed site carries no ore, so a modified client reading chunk data
  learns only where sites *could* be, which the host rock already tells an
  informed player.
- Open: what counts as "revealed" (exposed face, line of sight, distance at
  fine LOD), chosen late enough to keep the anti-x-ray property; the
  resolution probability per site and how it relates to `yield`; whether
  common ores also go through resolution or are plain soft content; the exact
  decay timescale for ore (`persist/decay-curve`).

## geology/essential-minerals — Early-game materials are reachable from any spawn

- Requirement: a few minerals are needed early, for example quartz
  for the first lantern (`pressure/hearth`). A player must never be locked
  out of them by where they spawned. This is also what keeps the first-session
  lantern reachable, which the burn-down exception in
  `ethics/no-appointment-mechanics` depends on.
- Decision: **no spawn conditions and no guarantee rule.** Spawn
  ignores terrain and resources. Instead, early-game minerals are made
  **statistically very unlikely to be missing**: choose materials that are
  geologically ubiquitous (quartz in veins in almost any rock and as pebbles in
  sand and gravel; clay; flint or chert; iron in some common form such as bog
  iron or ochre) and tune their site density so that being out of luck is
  rare. Rarer minerals (copper, tin, gold) stay setting-dependent and become
  reasons to travel.
- **Release valve:** a player who spawns somewhere inhospitable starts a new
  world, as with biomes in other games. Simpler than any guarantee mechanism.
- **Test:** sample many spawns and measure the probability that an essential
  mineral is beyond a first-session reach budget (walking distance and
  depth); keep it very small.
- Open: the essential list (driven by the early recipes); the reach budget
  and target probability; whether "reachable" includes depth (a quartz vein
  60 ft down may be too deep for a first session).

## geology/rock-blocks — The rock block set and its variants; cracked drops surface rock strength

- **Variants per rock: raw, cracked, polished, and brick/ashlar** (cut
  masonry). **No slabs, stairs or walls:** at our 1-ft block scale
  (`world/voxel-scale`) full blocks already give that resolution.
- **Cracked is a mining outcome, not a crafting step**. There is no
  structural strength-of-materials simulation, so rock strength is surfaced
  through mining instead:
  - A **precision tool (pick)** removes one block cleanly.
  - A **non-precision tool (sledge)** bulk-breaks blocks, faster, and a
    **share of the drops come out cracked**. That share depends on the rock's
    strength and brittleness: weak or brittle rock cracks more, strong rock
    less. Players learn which rocks are sound building stone from what their
    sledge gives back.
  - **Cracked blocks are worse for building recipes** (they can't become
    polished or brick, and look rough) and **make little difference to
    non-building recipes** (crushing, burning lime, smelting flux).
  - **Glass-like materials have no cracked variant**: quartz,
    obsidian, flint and chert fracture conchoidally, so a sledge reduces
    their **yield** (shards, loss) instead.
  - The pick-vs-sledge choice is a situational trade-off (speed vs
    quality), the kind `core/no-meta` asks for. Cracking can be a hash of
    (seed, cell) rather than a live roll; it concerns low-value stone either
    way (`ethics/no-variable-ratio-core` is about core progression). For a
    valuable ore minted in place (`geology/ore-sites`), any yield loss is a
    declared ledger sink.
- **Draft rock list (25 common):**

| Group | Rocks |
|---|---|
| Igneous, intrusive | granite, diorite, gabbro, peridotite |
| Igneous, extrusive | basalt, andesite, rhyolite, tuff, pumice, obsidian |
| Sedimentary | shale, siltstone, sandstone, conglomerate, limestone, dolostone, chalk, chert |
| Metamorphic | slate, phyllite, schist, gneiss, marble, quartzite, serpentinite |

  Optional setting signatures: syenite, trachyte, kimberlite, carbonatite,
  amphibolite, granodiorite.
- **Which variants each rock gets (draft):**
  - **All four** (sound building stones): granite, diorite, gabbro, basalt,
    andesite, rhyolite, sandstone, limestone, dolostone, slate, gneiss,
    marble, quartzite, serpentinite, and peridotite as a deep reward.
  - **Raw and cracked only** (weak or crumbly): shale, siltstone, chalk, tuff,
    pumice, conglomerate, phyllite, schist. Usable, but poor building
    material, so the choice of rock matters.
  - **No cracked variant** (glass-like): obsidian (raw, polished, brick),
    chert (raw; mostly a tool material).
  - Roughly 15 × 4 + 8 × 2 + 4 ≈ **80 rock blocks**.
  - **Deliberately broad for now**: block volume is being
    explored freely and will be refined in playtesting. Having no stairs,
    slabs, walls or fences keeps the count per material small, and per-world
    recipes (`crafting/per-world-recipes`) give a high material count a
    second purpose.
- **Rare decorative variants**: a few rare, striking
  stones, each tied to one setting, so finding one says something about the
  place (`worldgen/metered-information`). Each is a rock in its own right with
  its own variants, so they cost blocks: keep the set small (about 6-10).
  **Decorative blocks are soft content in general**, exceptions to
  be found case by case; they are decorative rewards, not core progression
  (`ethics/no-variable-ratio-core`). Draft candidates:

| Base rock | Decorative variants | Setting |
|---|---|---|
| Marble | statuary white, grey-veined, black, pink | mountain belts |
| Granite | rapakivi (round, rimmed feldspars), orbicular (concentric balls; very rare) | shields |
| Syenite | larvikite (blue iridescent flashes) | alkaline/rift |
| Andesite, rhyolite | porphyry, including a purple "imperial" kind | volcanic arcs |
| Limestone | travertine, banded "onyx" marble | hot springs, caves |
| Serpentinite | verde antico (green breccia) | ophiolites |
| Any | breccia (broken fragments re-cemented) | faults |
| Marble, gneiss | lapis lazuli (blue) | very rare |

- **Porphyry** is a *texture*, not a composition: large crystals
  (phenocrysts) in a fine matrix, from two-stage cooling (slow at depth, then
  quick near the surface). It occurs as granite, rhyolite or andesite
  porphyry, usually in shallow intrusions (dikes, sills) and lavas. Rome's
  purple imperial porphyry is the origin of "born in the purple". **It is also
  an ore signal:** porphyry copper deposits (with gold and molybdenum) form
  around porphyritic intrusions in volcanic arcs, so porphyry is both
  decorative and a prospecting clue.
- **Minerals players learn** (about 30, mostly ore cells and items rather than
  building blocks): ubiquitous/early (quartz, flint, clay, common iron, coal,
  salt); common metals (copper, tin, lead, zinc, iron ores); precious/deep
  (silver, gold, platinum, chromite, nickel); industrial (sulfur, gypsum,
  mica, feldspar, graphite); gems (garnet, beryl, tourmaline, corundum,
  diamond, jade, agate). Which are hard content (resolved and minted per
  `geology/ore-sites`) is open: likely the precious metals and gems, perhaps
  the common metals; the ubiquitous tier stays soft.
- **Oriented rocks** (`render/block-textures`): bedded
  sedimentary rocks, foliated and cleaved metamorphic rocks and columnar
  basalt have an orientation (bedding or foliation plane), set by worldgen
  from a slow dip field; massive rocks are `uniform`.
- Open: the strength/brittleness value per rock and the cracked-share curve;
  sledge area and speed; whether bulk-bodied minerals (coal, salt, gypsum,
  clay) are rock blocks or ore cells.

## geology/ore-forms — Ore bodies are pure-noise constructions; placers wait for rivers

- Decision: every ore form except placers is generated from **noise and
  hashes alone**, chunk-locally. Continuous fields make ore bodies span chunk
  boundaries with no communication between chunks, which is better than
  placing blobs that write into neighbours. Judged good enough.

| Form | Construction |
|---|---|
| **Veins** (thin sheets) | thin shell around a field's zero level (\|n\| < ε), domain-warped; a slow second field varies ε so veins pinch and swell |
| **Seams / layers** (coal, salt, banded iron) | thin band in the warped stratum depth coordinate that defines layer boundaries |
| **Nodules / clusters** (flint in chalk) | hashed, jittered points on an integer lattice, each a blob with noisy radius |
| **Scattered inclusions** (garnet in schist) | per-cell hash against a probability set by host conditions |
| **Pipes** (kimberlite) | hashed 2D points with a depth-varying radius: vertical carrots from the deep |
| **Placers** (gold in river gravel) | **deferred to rivers** (hydrology), the one form that needs "downstream" |

- **Local fixes for pure noise's shortfalls** (all still pure functions of
  position):
  - **Geological coherence:** allow hydrothermal veins only in a band around
    intrusion contacts (the intrusion field's value near its threshold acts as
    a rough signed distance), and align vein orientation with a slow regional
    direction field (anisotropic noise or a directional warp), so veins in a
    region share a strike a player can learn.
  - **Grade zoning:** the field value is the grade: rich in the core of a
    shell, poor at its edge.
  - **Rarity and density:** tuned by sampling field histograms
    (`worldgen/field-driven-features`).
  - **Coherent resolution:** veins define soft *sites* (`geology/ore-sites`);
    each site resolves with probability f(grade), near 1 in the core, so a
    resolved vein reads as continuous ore, not speckle. Still a per-cell
    integer hash, deterministic whenever revealed.
- **Cost:** veins need fine 3D noise like caves (`worldgen/noise-fields`), so
  they are evaluated only where host conditions allow (rock, intrusion
  proximity, depth) and only at interior-detail LOD (`world/progressive-lod`).
  At coarse LOD ore is absent, consistent with the refinement invariant if
  ore-bearing rock counts as a member of the rock superclass.
- **Plans only for landmarks:** a rare legendary deposit (a motherlode) could
  use `worldgen/structure-plans`; nothing else needs one.
- Open: placers with rivers; vein and seam parameters per mineral; whether
  vein noise can share evaluation with the cave fields.

## geology/soils-and-sediments — Soils, sands, gravels and clays as readable products of climate and rock

- Decision: loose surface material comes in many specific kinds, not one
  "dirt" and one "sand", and each kind is **a readable signal** of where it
  formed, as host rock is for ore (`geology/rock-suites`).
- **Soil is a product of five factors** (the standard soil-science framing):
  climate, organisms, relief (slope, drainage), parent material (the rock
  beneath) and time. That maps onto `worldgen/field-driven-features`: each
  soil is a soft combination of conditions on `temperature`, `moisture`,
  slope and the underlying rock. A player who learns the soils learns
  something true about the land.
- **Soils (draft, ~10):**

| Soil | Where | Look | Use or hook |
|---|---|---|---|
| Loam | temperate default | brown | baseline topsoil |
| Chernozem ("black earth") | grassland, steppe | near-black | most fertile |
| Podzol | cold conifer forest | ash-grey layer over rusty subsoil | poor for farming; striking profile when dug |
| Laterite | humid tropics | brick red | cut blocks harden in air (Angkor); bauxite source |
| Terra rossa | over limestone, dry summers | bright red clay | reveals limestone beneath |
| Peat | bogs, cold and wet | dark, fibrous | fuel; preserves what is buried in it; bog iron nearby |
| Gley | waterlogged ground | blue-grey with rust mottles | signals poor drainage |
| Loess | windblown silt downwind of deserts or old glaciers | pale yellow | very fertile; stands in vertical cliffs |
| Volcanic ash soil | near volcanoes | dark, light | fertile |
| Caliche (hardpan) | deserts | pale, cemented | hard to dig; blocks roots |

- **Soil profiles are visible at our scale.** A real profile is a few feet
  deep, so at 1-ft blocks (`world/voxel-scale`) digging down shows it: litter
  and humus, topsoil, sometimes a bleached layer (as in podzol), subsoil,
  weathered parent rock, then bedrock. The bottom of the profile reveals the
  rock beneath. Educational without any text.
- **Sands take their color from their source:**
  - common: **quartz sand** (tan to white; the glass sand), **carbonate sand**
    (white, from crushed coral and shell on tropical beaches; makes lime, not
    glass), **red desert sand** (quartz coated with iron oxide), **black
    volcanic sand** (eroded basalt);
  - rare and decorative: **magnetite sand** (black, heavy; an iron source),
    **green olivine sand** (very rare, near basalt), **garnet sand** (pink to
    purple; abrasive), **gypsum sand** (dazzling white, as at White Sands;
    plaster).
- **Gravels:** river gravel (rounded; placers, with rivers, `geology/ore-forms`),
  scree (angular, at cliff feet), shingle (beaches), glacial till (unsorted,
  with out-of-place boulders), flint gravel (near chalk).
- **Clays** (pottery): common red clay, white kaolin (china clay), ball clay.
- **Count and variants:** about 10 soils, 8 sands, 4 gravels, 3 clays (~25
  blocks). Mostly raw only, plus a packed/path variant where trampling
  matters; none is polished. Decorative and rare kinds are soft content, as
  decorative rock is (`geology/rock-blocks`).
- **Hooks into existing systems:** fertility per soil (crops); material
  chains (silica sand → glass, clay → pottery and brick, peat → fuel,
  laterite → brick, gypsum → plaster); **two more early iron sources**, bog
  iron in wetlands and magnetite sand, which help `geology/essential-minerals`;
  soil color reflecting the rock below.
- Open: the fertility model; which soils trample to paths; how profiles are
  generated (depth bands from surface, thinned on slopes); per-dimension soils.

## geology/resonant-crystals — Six crystals can become resonant for the first lantern; striking minerals carry low magic

- **Quartz, calcite, selenite, agate, fluorite and celestine are all viable
  for the first lantern**, as members of the resonant-crystal class
  (`crafting/material-classes`). That gives every kind of ground at least one
  source, which suits `geology/essential-minerals`.
- **Heating in the magical hearth makes them resonant**, the
  hearthstone step of `pressure/hearth`. Real chemistry supports the need for
  a *magical* hearth: in an ordinary fire selenite turns to plaster and
  calcite to quicklime, and only the hearth makes them resonant instead. Some
  fluorite really does glow when warmed (thermoluminescence).
- **Cool-looking minerals have low magic potential**: visual appeal
  signals magic, as a learnable rule of the world. **Resonance is attunement
  to Aether** (`worldgen/dimension-cosmology`). These crystals may have
  other low-magic uses besides the lantern.

| Crystal | How common | Where it forms |
|---|---|---|
| Quartz | everywhere | veins in most rock; crystal-lined geodes in limestone and shale |
| Calcite | very common | limestone: veins, cave crystals, clear "Iceland spar" |
| Selenite (clear gypsum) | common in sedimentary areas | clay and shale beds, evaporites, desert roses, caves |
| Agate (banded chalcedony) | moderately common | gas cavities in volcanic rock; pebbles in river and beach gravels |
| Fluorite | moderately common | veins, especially in limestone with lead and zinc ores |
| Celestine | uncommon | pale blue crystals in limestone and evaporites |

- **Geodes** (plain nodules in limestone and shale that open onto a
  crystal-lined cavity) are a natural, rewarding sedimentary source:
  cracking one open is a discovery moment.
- **Small trade-offs between lanterns**, never
  a best one (`core/no-meta`). **Quartz is the baseline**, being the most
  common. Draft per crystal, each echoing a real property (selenite's chosen,
  the rest proposed):

| Crystal | Trade-off | Real property it echoes |
|---|---|---|
| Quartz | baseline field; clear light | the common reference |
| Selenite | **weakly repels monsters at night, more strongly near full moon**; but dissolves if left exposed to rain, so it wants a roof | named for the moon; gypsum is soluble |
| Calcite | slightly wider field with a softer edge | double refraction spreads what passes through it |
| Agate | tough: keeps its resonance when the lantern is picked up and moved; slightly smaller field | chalcedony is tough and stable |
| Fluorite | stronger near heat (a fire or hearth), weaker in the cold; colored light | some fluorite glows when warmed |
| Celestine | stronger at altitude, weaker deep underground; pale blue light | "celestial" name; uncommon, so a reward |

- **These read only authoritative state** (moon phase and weather from world
  time, nearby heat sources, altitude), never light, which has no gameplay
  role (`light/visibility-only`). Selenite's weak repellence is a small,
  deliberate exception to "lanterns don't repel" (`pressure/hearth`), and it
  helps clear the lantern's activation range at night.
- Open: the six crystals' other low-magic uses; how geodes are generated
  (`geology/ore-forms`: nodules); magnitudes of each trade-off.

## geology/generation-sketch (draft) — How rock and minerals map onto worldgen (open)

- **Rock from field combinations** (`geology/rock-suites`): likely needs a
  few geology-specific slow fields (e.g. a crustal-setting or crustal-age
  field) besides `surface`, `moisture` (evaporites) and `temperature`
  (carbonate platforms). Contacts mostly sharp, so steep conditions.
- **Ore bodies:** `geology/ore-forms`.
- **Structure:** layer boundaries as depth bands warped by low
  frequency noise (folds, tilt and dip; steeper in mountain belts);
  intrusions as blobs (cellular noise) cutting the layers; veins as thin
  shells near intrusion boundaries; seams as thin bands within a layer.
- **Hard vs soft:** rock and ore sites are soft; valuable ores become hard
  only at resolution (`geology/ore-sites`).
- **LOD hierarchy:** stone → (field-derived rock family) → rock. A distant
  chunk can show its dominant rock's color, so a mountain range of marble or
  red sandstone cliffs reads correctly from afar (`world/progressive-lod`'s
  invariant: coarse never contradicts fine).
- **Gameplay hooks to design later:** hardness and tool tiers per rock;
  processing chains (limestone → lime/mortar, clay → brick, sand → glass, flint
  → early tools, coal → fuel, salt → food preservation); prospecting by reading
  rock (fits low fantasy and no player coordinates, `core/low-fantasy`,
  `input/no-player-coordinates`); caves by dissolution in karst suites
  (couples to the cave fields in `worldgen/noise-fields`).
- Open: the suite list per dimension (Enigma and Aether want
  non-Earth geology); how many rock types to ship (block-type budget vs
  variety); whether suites interact with `yield`.
