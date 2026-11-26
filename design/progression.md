# Progression dimensions

Abstract axes along which a player grows more capable over a playthrough. This
file is a **catalog for design-checking**, not a tech tree and not tuning: the
point is to test the engine against the *shape* of progression a sandbox/sim
game is expected to support, and to see where existing machinery already serves
an axis vs. where there is a gap.

Explicitly NOT here: the concrete recipe/tool/transformation dependency graph
(that is `loop/dependency-graph`, deliberately deferred — `loop/deferred`). An
axis here says "players get better at X"; the graph says "via these specific
recipes." Keep them separate.

For each axis: **Hook** = engine machinery already serving it; **Gap** = what is
undesigned (often deferred into the dependency graph, sometimes a genuine hole).
Axes are named, not numbered — reference them by name so inserting one never
renumbers the rest (same reason the decisions use `group/slug`, not D#/T#).

## Scope boundaries (what this game is NOT)

The genre target is **Minecraft-flavored** (single-player and small multiplayer,
a player embodied in the world) — NOT a colony/management sim. Two boundaries set
this, so colony-sim axes surfaced while surveying the genre are
deliberately excluded or parked, not overlooked:

- **Colony / population mechanics are a PARKED possible-future direction, not
  core.** The soul model (`sim/player-embodiment`: a body with a canned soul) plus
  DF-style designation (`sim/dreaming-designation`) happen to supply the exact
  substrate a RimWorld/ONI/Frostpunk-style commandable-NPC-colony would need — so
  the possibility is real and worth noting — but commanding a population of
  semi-autonomous agents is NOT a core gameplay element. The player is an actor in
  the world, not a colony overseer. Colony-dependent axes (population command,
  workforce specialization, collective morale/hope, intra-group social sim,
  governance/law) are therefore excluded from the catalog below; they live here
  only as "what souls would make possible later," a direction to revisit, never a
  commitment.
- **Meta-progression across runs is a NO.** There is no permanent mechanical
  unlock that carries between playthroughs (no Against-the-Storm-style tech
  carryover). The ONLY things that persist across a player's games are **player
  skill and knowledge** — diegetic, in-the-player's-head progression, not an
  account-level unlock tree. Everything mechanical is earned within a world.

## Survival & homeostasis — staying alive

- **Warmth / heat sourcing** — cold → fire → better/sustained heat.
  Hook: `pressure/hearth`, `pressure/basic-survival` (cold), `sim/seasons`
  (winter spike). Gap: heat-source tiers (dependency graph).
- **Cooling / shade / hydration** — the hot pole: overheating + desiccation in
  hot biomes/dimensions. Hook: `pressure/basic-survival` (heat/drink). Gap: same.
- **Food security** — forage → hunt → farm → surplus/storage → trade.
  Hook: `pressure/basic-survival` (food), `sim/seasons` (plant/harvest windows).
  Gap: the food dependency chain (dependency graph).
- **Light** — consumable torches/fuel → eventually permanent light.
  Hook: `pressure/light`. Gap: light economy + permanent-light unlock
  (`pressure/light-economy`).
- **Health / injury / recovery** — damage, healing, possibly disease/status.
  Hook: NONE YET — genuine gap (`progression/health-model`). Most survival-sims
  have an injury/recovery axis; the docs currently don't.
- **Rest quality** — sleep as a need, and better rest → better dream yield.
  Hook: `sim/dreaming-designation` (sleep-as-need, dream-designation payoff).
  Gap: what "quality" means and how it scales the dream (`sim/dream-leash`).
- **Cooking / food processing** — raw → prepared → preserved → specialized
  dishes (buffs, spoilage resistance). Distinct from food *security* (having food)
  and food *abundance* (surplus): this is food *transformation/quality*. Hook:
  `sim/seasons` (preservation beats winter scarcity; spoilage is a wall-clock
  process, kin to decay), `data/entity-ledger` (cooked items are entities).
  Gap: the recipe chain (dependency graph).
- **Medicine / herbalism** — treat injury/illness; gather/cultivate reagents →
  remedies. Fills the *recovery* half of the health/injury gap above. Hook:
  `worldgen/regional-fields` (`yield`, `alignment`, `weirdness` gate rare/potent
  reagents), `progression/health-model` (the thing it acts on). Gap: the illness/
  remedy model (tied to the open health question).

## Spatial security — holding ground

- **Reality stabilization scale/potency** — bigger / longer / permanent
  anchored area. Hook: `persist/diegetic-persistence`. **This is the hub axis** —
  most other axes are denominated in "how much reality can you hold" (see
  Findings). **First rungs, decided (`pressure/hearth`):**
  campfire (no precursor; small area, limited time, burns on world time) →
  hearthstone (a stone heated in the campfire; fuelable, a bit stronger,
  still burns) → lantern (quartz mined underground, heated in the hearth into
  resonant quartz; stabilizes a small area indefinitely — the non-consumable
  short-range stabilizer at roughly Minecraft smelted-iron depth, a
  first-session goal). Offline burn-down of the first two is deliberate
  (first-day urgency); the lantern is the release valve
  (`ethics/no-appointment-mechanics`). **Mid-game rungs:** a
  bonfire (larger time-limited stabilization + repellence, optionally
  banishing mobs that can't flee) clears the safe range a **grand lantern**
  (permanent, stabilization only) needs to activate — the same "clear, then
  anchor" verb at a larger scale (`pressure/hearth`).
- **Mob repelling + consumable fuel** — suppression strength and its fuel
  economy. Hook: `mobs/spawning`, `mobs/suppression-systems`. Coupling: a potent
  stabilizer PROVOKES proportional mobs (`mobs/spawning` immune-response), so
  stabilization and suppression necessarily scale together — progressing one
  forces progressing the other.
- **Physical fortification / defensible space** — walls, enclosure, traps.
  Hook: `pressure/defensible-space`, `core/opaque-chunk-queries` (enclosure as a
  connectivity query). Gap: fortification tiers + trap mechanics.
- **Base automation (logic/work)** — contraptions that perform WORK/LOGIC for
  the player (doors, sorters, defenses, elevators). Hook: `sim/logic-machines`
  (powered circuits, mechanical souls). Distinct from resource-*generation*
  automation (auto-farming, below). Gap: `sim/logic-eval` (budget, I/O catalog).
- **Continuous simulation (keep reality TICKING)** — a sibling to reality
  stabilization: stabilization holds reality's *shape* (anti-decay storage); this
  keeps a region *running* (resident + ticking) while the player is elsewhere.
  Progression = how much unattended world you can afford to keep live. Hook:
  `progression/continuous-sim-resource` — an in-game resource the player spends,
  NOT free background sim (no absent-world simulation, no catch-up replay).
  `core/diegetic-cost` projecting the real engine cost of a resident ticking chunk
  into the fiction, exactly as stabilization does for storage. This is what makes
  away-from-keyboard farms possible without violating the present-player/
  loaded-only invariant: you don't simulate absence, you pay to make the chunk
  non-absent. Gap: the resource's form, cost curve, and how it composes with
  stabilization (same resource? adjacent tier?). Crop and tree growth is
  closed-form in all farms, everywhere,
  including unloaded chunks (`core/closed-form-first`, `sim/random-ticks`) —
  it needs no ticking and is not gated. Decay interacts emergently: it
  overwrites blocks toward baseline and an overwritten crop is simply
  gone, its stage having been its block state (`sim/block-metadata`), so players who care about a farm stabilize
  it. Residency/ticking buys only STEPPED production: actuators, item
  transfers, mob/entity farms, sealed machines (`sim/residency-levels`).
- **Combat capability (offense)** — weapons / offensive power vs. mobs, and
  PvP in multiplayer. Distinct from fortification (defense) and suppression
  (avoidance): this is *engaging* the threat, not walling or repelling it.
  Pairs with the stabilization⟷suppression immune-response loop — a potent base
  provokes potent mobs you must be able to FIGHT, not just out-wall. Hook:
  `mobs/spawning` (the threat), `sim/player-embodiment` (a body's combat
  affordances). Gap: no weapon/combat entry yet — a real hole, since the
  provocation coupling assumes you can fight back.

## Mobility & logistics — overcoming distance

- **Personal transportation** — walk → mount/vehicle → flight →
  portals / fixed teleport points. NOT AAA-style arbitrary fast-travel: movement
  stays diegetic — you travel under power (a mount, a vehicle, flight) or through
  a built/fixed link (a portal, a teleport pad), never an instant menu-warp to
  anywhere. Hook: `sim/player-embodiment` (pilot a vehicle / fly a body = bind a
  body with that affordance), `world/dimensions` (portals = reference edges,
  `data/dimension-gc`). Gap: flight model; the fixed-teleport network (what links
  to what, and its cost).
- **Item / resource transportation** — carry → container → bulk haul →
  networked logistics → cross-dimension transfer. Hook: `data/entity-ledger`
  (moves are accounted transfers), `world/dimensions` (pocket-dimension storage).
  Gap: logistics-network mechanics.
- **Information / perception range** — map knowledge, scouting, dream-scry.
  Hook: `sim/dreaming-designation` + `sim/dream-leash` (dream the reality you
  anchored). Usually an implicit axis; the dream mechanic makes it explicit and
  ties it to reality stabilization.
- **Carry capacity / encumbrance** — how much you can hold/move at once: two
  axes now (`inventory/*`) — **body load** (encumbrance; eased by lighter gear,
  negative-weight items, later mounts/vehicles) and **extradimensional storage**
  (3×3 pouch early → 5×5 / 7×7 / 9×9, weightless contents). A classic
  inventory-pressure axis that generates logistics goals (it is WHY transport
  matters). Hook: `data/entity-ledger` (what you carry is ledger entities in a
  container), `world/dimensions` (a bag interior is a pocket dimension — bigger
  bags = bigger pocket dimensions, so capacity progression IS dimension-size
  progression). Encumbrance model: soft sigmoid slowdown on body slots, with the
  extradimensional pack not encumbering (`inventory/encumbrance`,
  `inventory/slots-and-pack`). **Concrete chain:** folk-craft
  pouches from rawhide or woven fibre with ochre patterns, then tanned
  leather, then larger bags needing stronger mineral pigments, and a sled for
  bulk hauling (`inventory/pouch-progression`); blocks live only in
  extradimensional storage (`inventory/hand-pointers`).

## Material & extraction — the resource tree

- **Exploration for rare resources** — surface → caves → deep/dangerous →
  other dimensions. Hook: `worldgen/regional-fields` (`yield`, `alignment`,
  `weirdness`, `liminality` gate rarity), `world/progressive-lod`, `data/entity-ledger`
  (extraction mint). Gap: rarity/reward curve (tuning).
- **Extraction capability** — tools dig faster / deeper / harder materials.
  Hook: NONE SPECIFIC — tool tiers are dependency-graph content. Gap: deferred.
- **Material processing / refinement** — ore → ingot → alloy → component.
  Hook: NONE SPECIFIC — the crafting depth. Gap: `loop/dependency-graph`.
- **Energy / power** — fuel → sustainable power; powered circuits.
  Hook: `worldgen/regional-fields` (`liminality`), `sim/logic-machines` (powered). Gap:
  the power economy.
- **Food abundance (surplus beyond survival)** — from not-starving to reliable
  surplus that frees the player to pursue non-survival goals. The
  survival→thriving transition. Hook: `sim/seasons`, food security. Gap:
  dependency graph.
- **Resource-generation automation (auto-farming)** — PASSIVE resource
  PRODUCTION: auto-farms, mob/creature farms, self-replenishing yield that runs
  while the player does other things. Distinct from base automation (which
  automates work/logic, not production). Hook: `sim/logic-machines` (the
  actuators), `data/entity-ledger` (produced items mint through the ledger), and
  crucially `progression/continuous-sim-resource` — a farm's STEPPED parts
  (harvest actuators, item transfers, mob farms) run only in a chunk that is
  actually TICKING, and keeping a chunk resident-and-ticking while
  the player is elsewhere is bought with an in-game resource (see continuous
  simulation, above). Crop and tree growth itself is closed-form and advances
  everywhere, unloaded chunks included (`core/closed-form-first`).
  **Direction:** no absent-world
  simulation
  and no catch-up replay; instead, continuous simulation of an unattended chunk
  is a diegetic COST the player pays for. This is `core/diegetic-cost` projecting
  the real engine cost (a resident ticking chunk is expensive) into the fiction,
  exactly as reality stabilization does for storage.
- **Breeding / selective cultivation** — improve crops and animals over
  generations (yield, traits, hardiness). Hook: `data/immutability` (lineage as
  versioned immutable objects — the identity model for a genealogy already
  exists), `sim/seasons` (breeding cycles on the clock), `data/entity-ledger`
  (offspring mint). Pairs with cooking (better ingredients) and pets. Gap: the
  trait/heredity model.
- **Animal husbandry / ranching** — the scaled-up tamed-creature axis: herds,
  ranches, animal-derived resources. Creature-based auto-production (distinct from
  crop auto-farming but the same continuous-sim economics). Hook:
  `progression/continuous-sim-resource` (a ranch's entity physics is
  stepped; animal needs and growth are closed-form, `sim/random-ticks`
  catalog),
  `sim/player-embodiment` (herd animals are bodies with canned souls). Gap: same
  continuous-sim-resource open thread.
- **Water management / hydraulics** — irrigation, drainage, flood control,
  water-powered machines: dig channels, build aqueducts, power mills. Built
  on Minecraft-style sources and flowing water (`sim/gpu-speculative-compute`), so it is about routing and placing sources, not DF's
  volume and pressure. Hook: `sim/logic-machines` (water-powered
  contraptions). Gap: the hydraulics mechanics on top of flowing water.
- **Weather / environmental mastery** — read → predict → shelter from →
  eventually influence weather. Hook: the bookmarked `flow`/`current` vector field
  (V2) in `worldgen/noise-fields` (wind), `sim/seasons`. Progression from
  *surviving* weather to *exploiting* it (wind for flight/transport, rain for
  farming). Gap: whether weather is authoritative/gameplay or presentation — the
  `light/visibility-only` field-overloading caution applies.
- **Underground / verticality mastery** — mining infrastructure, deep-delving,
  surviving depth hazards (the inverse of flight). Hook: `worldgen/dimension-profiles`
  (no-sky/underdark bias), the cave fields (`worldgen/noise-fields`), fully-3D
  pathing (`sim/3d-pathing`). Progression as *how deep you can go and work*. Gap:
  depth-hazard model.
- **Closed-loop / cycle mastery** — engineer sustainable resource CYCLES rather
  than one-shot extraction (water→power→waste→fertilizer→crops, the Oxygen Not
  Included signature). **Lands squarely on `data/entity-ledger` conservation** —
  the ledger's no-faucet/no-sink accounting is exactly what makes a real closed
  loop possible instead of faked; most games cannot do this honestly. A
  Minecraft-flavored game need not center it, but the substrate makes it a
  first-class *option* for players who want it. Hook: `data/entity-ledger`
  (conservation), `sim/logic-machines` (the automation), `sim/gpu-speculative-compute`
  (fluid/gas cycles). Gap: which cycles are expressible; tuning so loops are
  satisfying, not fiddly.
- **Atmosphere / gas / temperature management** — breathable air, heat
  dissipation, pressure, ventilation (ONI's other signature). Gas is the same
  solver class as fluids, so it is a near-free extension of the existing sim, and
  temperature already exists as an authoritative field (`worldgen/noise-fields`
  temperature + `sim/seasons` offset). Relevant mainly in sealed/underground/
  pocket-dimension spaces where air is not free. Hook: the fluid solver
  (`sim/gpu-speculative-compute`, CPU only)
  (gas as a fluid), `worldgen/noise-fields` (temperature), `world/dimensions`
  (sealed spaces). Gap: whether gas is modeled at all or abstracted — a scope
  decision (full ONI-depth gas sim is a lot; a coarse "is this sealed space
  breathable" check is cheap via `core/opaque-chunk-queries` enclosure).

## Creation & mastery — sandbox self-expression (the differentiator)

- **Authoring vocabulary** — what a player can create: blocks, souls, bindings.
  Hook: `core/diegetic-authoring`, `data/authoring-vocabulary`,
  `data/authored-content`. Gap: the vocabulary scope dial (perpetual).
- **Logic-machine sophistication** — simple gates → complex automata → sealed
  mechanical souls. Hook: `sim/logic-machines`. Gap: `sim/logic-eval`.
- **Soul mastery** — distilling, binding, possession range/potency; which
  bodies a soul can drive. Hook: `sim/player-embodiment`, `data/soul-body-alignment`,
  `sim/logic-machines`. Gap: `sim/embodiment-boundaries`.
- **Pets / companions / tamed creatures** — tame → raise → bond → working
  animals/mounts. Not a new system but a USE of the soul model: a tamed creature
  is a body running a canned soul you influence; a mount is a pet bound more
  tightly; `data/soul-body-alignment` governs what you can tame/bind. Hook:
  `sim/player-embodiment`, `data/soul-body-alignment`, `sim/logic-machines` (a
  companion's behavior is a canned-soul binding). Overlaps husbandry (scaled-up)
  and personal transportation (a mount). Gap: the taming/bonding progression.
- **Content sharing / reputation** — publishing bundles, curation standing.
  Hook: `data/content-bundles`, `data/authored-content`. MULTIPLAYER-ONLY social
  axis. Gap: `data/content-moderation` (reputation model).
- **Dimension authoring / ownership** — not just REACHING dimensions (see
  dimensional access, below) but MAKING and owning them: pocket dimensions as
  containers, mechanical-soul brains, private spaces, eventually larger authored
  realms. "How much dimension can you create and hold" is a creation axis distinct
  from authoring objects. Hook: `world/dimensions` (first-class, pocket
  dimensions), `data/dimension-gc` (ownership DAG), `sim/logic-machines` (a soul's
  brain IS an owned dimension). Gap: the authoring/ownership tiers; how a created
  dimension's worldgen bias is chosen (`worldgen/dimension-profiles`).
- **Reality manipulation (the substrate meta-axis)** — the player's growing
  power over the SUBSTRATE itself: stabilize, seal, reality-bomb (prompt
  reversion: a powerful Enigma artifact, found rather than crafted, an
  optional late- or endgame item on no critical path,
  `worldgen/dimension-cosmology`), continuous-sim purchase, major-version convergence. The top of the
  creation ladder — power over how reality persists/ticks/reverts, not just what
  is in it. Hook: `persist/diegetic-persistence` (stabilize, reality-bombs),
  `sim/logic-machines` (sealing), continuous simulation
  (`progression/continuous-sim-resource`), `core/diegetic-cost` (all of these are
  diegetic-cost acts). This is the family no incumbent has — the engine's own cost
  model exposed as a progression ladder. Gap: whether these read as ONE coherent
  "reality-magic" skill tree or stay separate mechanics.
- **Ritual / magic practice** — the `liminality` field (low magic draws on
  Aether, `worldgen/dimension-cosmology`) made
  into a player capability: channel energy, cast, enchant. Hook:
  `worldgen/regional-fields` (`liminality`,
  `alignment`, `weirdness`), `sim/logic-machines` (a spell could be an authored
  binding), `core/diegetic-authoring`. **Two kinds**
  (`worldgen/dimension-cosmology`): folk magic in Terra, and Enigma's
  late-game
  magic, inexplicable, unreplicable, often of marginal or negative use, and
  dangerous. Progression from *finding* liminal regions to
  *wielding* them. Gap: whether magic is its own system or a skin over
  logic-machines + grammar bindings (lean: a skin, to avoid a parallel system).
- **Aesthetic / architectural mastery** — decoration, monumental building, the
  "make it beautiful" axis sandbox players chase intrinsically. Hook:
  `core/diegetic-authoring` (author blocks / N³ surfaces), `data/authored-content`.
  The pure-expression rung — non-combat, non-survival. Gap: none structural; this
  is content + tools, not a mechanic hole.

## Knowledge & access — gating by understanding / reach

- **Recipe / tech knowledge** — what you know how to make (discovery vs.
  explicit unlock). Hook: NONE YET — Gap `progression/knowledge-model`: is
  progression gated by KNOWING (learn/discover recipes) or by HAVING
  (tools/materials)? A foundational genre choice (Minecraft = mostly having;
  many sims = knowing) not yet made.
- **Dimensional access** — which dimensions you can reach and survive (the
  Nether-portal archetype): progression as WHERE you can go. Hook:
  `world/dimensions`, `worldgen/dimension-profiles` (per-dimension hostility via
  bias/fields). Coupled to personal transportation — see
  `progression/travel-dimension-coupling`.
- **Lore / discovery / world knowledge** — decipher the world's history, read
  ruins, uncover what happened here — knowledge gated by *understanding the
  world*, distinct from recipe/tech knowledge (how to make things). Hook:
  `data/grammar` + `data/authored-content` (lore as content-addressed objects),
  `worldgen` (ruins/structures via `worldgen/surface-free-placement`). Gap: the
  discovery mechanic; whether lore is authored content or generated.
- **Alignment / faction / diplomacy / territory standing** — good↔evil,
  civilization influence, and DIPLOMATIC standing with NPC groups/civs (reputation
  unlocking trade/access/alliance — the social face of territory, vs. its spatial
  face). Hook: `worldgen` `alignment` field, parked `worldgen/territory`. Partly
  multiplayer/social. Gap: the whole territory subsystem is parked, and the
  diplomacy/reputation model with it.

## Social & economy — multiplayer only

- **Trade / economy / specialization** — barter → markets → division of labor;
  players specialize and exchange rather than each doing everything. Emerges once
  content sharing + logistics exist. Hook: `data/entity-ledger` (conservation
  makes a real economy possible — no dupe faucet), `data/content-bundles` (traded
  authored content). MULTIPLAYER-ONLY. Gap: no economy entry; whether the engine
  provides market *mechanism* or just the conservation substrate and lets players
  build economies (lean: substrate only, per the mechanism-not-enforcement
  stance).
- **Cooperative scale** — what a GROUP can hold/build/defend beyond a solo
  player: shared stabilization, joint bases, collective continuous-sim budget.
  Hook: reality stabilization + continuous simulation (shared reality-holding),
  `persist/pause-semantics` (consensus pause already assumes group coordination).
  MULTIPLAYER-ONLY. Gap: how shared ownership/budgets work (`data/dimension-gc`
  ownership edges may extend here).

## Temporal mastery — control over WHEN

- **Time / scheduling mastery** — control over timing: scheduled automation,
  season prediction/preparation, timed machine behavior, and the pause levers.
  A thin but real axis the season clock + logic machines + pause already imply.
  Hook: `sim/seasons` (the shared clock to predict/exploit), `sim/logic-machines`
  (timed/triggered behavior), `persist/presence-not-absence` (pause). Gap:
  whether this is a deliberate axis or just emergent from the others — flagged so
  it is not mistaken for a hole if left emergent.

## Character / internal growth — what the body & mind BECOME

(The catalog is otherwise almost all *external* progression — what you own,
reach, or build. This family is *internal*: growth of the character itself, which
most survival-RPGs lean on heavily and this design is currently thin on.)

- **Resilience / acclimatization** — the body adapts: cold/heat tolerance,
  disease immunity, carry-conditioning, altitude/depth hardiness. Soft-threshold
  growth against the survival pressures (`core/soft-thresholds`) — exposure
  shifts the falloff curves rather than flipping a flag. Hook:
  `pressure/basic-survival` (the pressures it adapts to), `progression/health-model`
  (shares the body-state model). Gap: whether adaptation is a deliberate stat axis
  or emergent; interaction with medicine/illness.

## Cross-cutting couplings (axes that are NOT independent)

- **Stabilization ⟷ suppression** — stabilization provokes proportional mobs;
  holding more reality forces stronger suppression. Reward and antagonist scale
  on one variable.
- **Stabilization ⟷ continuous simulation** — holding reality's SHAPE and keeping
  it TICKING are sibling diegetic-cost axes; together they define "how much live
  world is yours." Away-from-keyboard farms' STEPPED parts (actuators,
  transfers, mob farms) sit on continuous simulation; crop growth is
  closed-form and needs neither.
- **Stabilization ⟷ perception range** — dream-scry range is bounded by anchored
  reality (`sim/dream-leash`): perception range is a *function of* holding ground.
- **Transportation ⟷ dimensional access** (`progression/travel-dimension-coupling`):
  dimensional access is not just "can you open the portal" but "how far does a
  dimension let you travel per unit of Terra distance." A dimension can be a
  TRANSPORT tier: a compressed dimension (Minecraft's 8:1 Nether) turns
  cross-dimension travel into fast Terra travel. So dimensional access and
  personal transportation are the same progression lever viewed two ways —
  unlocking a dimension can BE a transport upgrade.
  - **Design idea (open, `worldgen/distance-ratio-field`):** rather than (or in
    addition to) a FIXED per-dimension ratio like Minecraft's 8:1, make the
    Terra⟷dimension distance ratio a **noise field** so the ratio is LOCAL —
    some regions compress travel more than others. Fits `worldgen/noise-fields`
    cleanly (a regional scalar field, coarse sample grid, interpolated — a
    `distance-ratio`/`compression` field), and `core/soft-thresholds` (smooth
    spatial variation, no hard ratio cliffs). Turns "which dimension" AND "where
    in it" into a navigation puzzle and a discoverable transport resource (find
    the high-compression corridors), rather than a flat constant. Hard questions
    it raises: how a continuously-varying ratio maps coordinates between
    dimensions coherently (a fixed ratio is a clean linear map; a field is a
    position-dependent one — round trips must land you back where you started),
    and whether the mapping stays reversible/conservative.
- **Continuous simulation ⟷ auto-farming** — a farm's stepped production
  (actuators, transfers, mob/entity farms) runs only where reality is
  kept TICKING, which is bought as a diegetic resource
  (`progression/continuous-sim-resource`), NOT free background sim.
  Away-from-keyboard farming is "pay to keep the chunk live," never "simulate the
  absent world." Crop/tree growth is the exception by construction: a closed
  form on absolute time, so it advances everywhere at no cost and is not
  simulated (`core/closed-form-first`).

## Findings (what the catalog surfaces as a design check)

1. **Reality stabilization is the hub, not one axis among equals.** Transport
   reaches *out from* held reality; mobs scale *with* it; dreaming scries
   *within* it; warmth/light/fortification *defend* it; and keeping reality
   *ticking* is its sibling cost. Progression is hub-and-spoke around
   reality-holding, not ~30 independent meters. This is a strong, falsifiable
   structural claim — if it holds, it is the backbone the dependency graph should
   be hung on.
2. **Genuine engine holes (no entry, not deferral):** the health/injury body-state
   model (`progression/health-model`) — though medicine/herbalism now names its
   *recovery* half and acclimatization its *adaptation* half, so the hole is
   "define the body-state these act on," not "invent recovery"; combat/offense (a
   real hole, since the stabilization⟷suppression provocation coupling ASSUMES you
   can fight back); knowledge-vs-having gating. New threads this enumeration
   created: the continuous-sim resource (direction set), the distance-ratio field,
   dimension-authoring tiers.
3. **Deliberate deferrals (inside `loop/dependency-graph`):** extraction-capability
   tiers, material refinement, the survival dependency chains, the power economy.
   These are *content*, correctly not-yet-designed — the catalog confirms they are
   deferred, not forgotten.
4. **Multiplayer-only axes:** content sharing/reputation, trade/economy,
   cooperative scale, most of territory standing. They do not exist single-player
   and must not be assumed universal — worth knowing when scoping a single-player
   milestone.
5. **The creation family is the differentiator.** Most survival-sims stop at
   gather→craft→build. Authoring blocks/souls/machines, authoring dimensions,
   manipulating the substrate, and sharing bundles are where this design has
   progression ladders the genre's incumbents lack — reality manipulation
   especially has no incumbent analog. It is the family most dependent on the
   diegetic-authoring tenets holding up.
6. **Presence-gating is a recurring consequence — and now has a unified answer.**
   The present-player / loaded-only invariant (`core/chunk-locality`, the mob
   model, lazy decay) constrains perception range (scry only held reality),
   auto-farming (no free absent farming), and suppression (no absence sieges). The
   design does NOT fight it with absent-world sim or catch-up replay; instead the
   continuous-sim resource (`progression/continuous-sim-resource`) turns "keep a
   chunk live" into a diegetic purchase. The rule stands — *the game simulates for
   a present player* — and the genre's away-from-keyboard expectation is met by
   paying to make a chunk non-absent, not by simulating absence.
   Closed-form processes (crop and tree growth, decay, burn-down) are
   evaluated from anchors on absolute time, which is not simulating
   absence; the purchase covers only stepped production
   (`core/closed-form-first`).
7. **Diegetic-cost is doing more load-bearing work than any single tenet.**
   Stabilization, continuous simulation, the whole reality-manipulation family,
   and the dream-designation payoff are all `core/diegetic-cost` instances. The
   pattern: wherever a real engine cost (storage, a live tick, compilation) would
   normally be hidden, this design turns it into a resource the player spends —
   which is simultaneously the performance model AND a progression axis. That dual
   role is the engine's signature move.
8. **Several axes are existing engine subsystems promoted to progression, not new
   systems.** Water/hydraulics is the fluid solver (`sim/gpu-speculative-compute`,
   CPU only)
   as an axis; weather is the bookmarked vector field; pets/husbandry are the soul
   model (`sim/player-embodiment`); ritual/magic is the `liminality` field +
   logic-machine bindings. The healthy kind of axis — the engine already has to
   build the substrate, so exposing it as progression is nearly free. A cull
   should favor keeping these over axes that need a whole new system.
9. **Internal (character) growth is the thinnest family.** Almost everything here
   is *external* progression (own/reach/build); only acclimatization and the
   health model are *internal* (what the body/mind becomes). Most survival-RPGs
   lean heavily on internal growth (skills, stats, mutations). Deliberate choice
   or gap? — flagged, not resolved. The engine has no character-stat substrate
   yet, so adding internal depth is the one direction that would need genuinely
   new machinery rather than exposing existing subsystems.
10. **The colony-sim survey (RimWorld, ONI, Timberborn, Frostpunk, et al.)
    confirmed the scope boundary more than it added axes.** Those games are all
    population-command sims; the soul model could support that, but it is parked,
    not core (see Scope boundaries). What DID transfer are the axes that work
    single-player and ride existing systems: closed-loop/cycle mastery (the
    `data/entity-ledger` conservation substrate — arguably the thing the engine
    does *better* than the games that are famous for it) and atmosphere/gas (same
    solver as fluids). Structural integrity (Going Medieval/Timberborn) was
    considered and CUT: it is a build *constraint* with no progression ladder
    unless coupled to a materials/engineering tree, which would compete with
    reality-stabilization (the actual answer to "how big can you build") and
    implies collapse physics the Minecraft-flavored scope does not want —
    Minecraft itself has none. The other non-fit axes (mood/hope, workforce,
    governance, meta-progression) were excluded by the same explicit genre
    decision, not missed.
