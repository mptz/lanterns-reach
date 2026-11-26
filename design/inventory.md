# Inventory & encumbrance

What a player carries, where it lives, and what carrying it costs. Not needed
for the v0.1 line (the harness has hard-coded hotbars and no body); recorded
because it shapes hotbars (`input/hotbars`), the body (`sim/player-embodiment`)
and carry-capacity progression (`progression.md`, Mobility & logistics).

Context: Minecraft's inventory pressure is almost entirely *slot count* — one
mechanic, mostly an annoyance. The decisions below split carrying into **two
mechanics with different costs** — body slots that weigh, extradimensional
storage that doesn't — so the player makes a real trade: e.g. accept moving
slowly in heavy armor, or stay light for exploring.

## inventory/slots-and-pack — Body slots (encumbering) vs. extradimensional storage (not)

- **Body slots** (the paperdoll) — what is physically on the body. All of them
  count for encumbrance (`inventory/encumbrance`):
  - **the two hotbars** (`input/hotbars`; at most 9 slots each, no empty slots),
  - **wearables:** head, chest, back-or-outerwear, legs, feet,
  - **three jewelry slots:** rings, amulets,
    charms and similar small wearables, which can be part of the magic system
    (e.g. resonant-crystal charms, `geology/resonant-crystals`; folk patterns,
    `inventory/pouch-progression`). Like the other wearable slots, pickups
    never route into them automatically; the player puts items there.
    Rationale: with encumbrance and no stacking on the body, the
    paperdoll can afford to be a little generous.
  - **three generic slots** — usable for anything; their intended use is
    holding inventory items (pouches) once the player has them.
- **Extradimensional inventory items** (pouch, later larger bags): an item whose
  storage is a pocket dimension (`world/dimensions`). The **item itself has a
  (small) weight** and encumbers wherever it sits on the body; **its
  extradimensional contents never do.** The rule is "items on the body weigh;
  things in another dimension don't."
- **Chests (single-block extradimensional containers) work the same way**
  (not "boxes": a box is the mundane one-item container, `inventory/box`).
  A chest is
  1×1×1 externally. It can be broken and carried while keeping the link to its
  contents, like a Minecraft shulker box. Its differences from a pouch:
  1. **The chest itself is much heavier** than a pouch.
  2. **Chests typically hold more at the same tech level.**
  3. **No container placement modes:** using a held chest just places the
     chest. It is never a placing tool (`inventory/hand-pointers`).

  So pouches carry and place, chests stay put and hold more. Multi-block
  containers are deferred to the general open issue of multi-block items placed
  in the world.
- **Items are only usable from a hand**. Nothing is used
  directly out of a pouch or wearable slot. This is the friction that keeps the
  two mechanics from collapsing into "keep everything in the weightless pouch":
  to use something it must be on a hotbar, and hotbars are body slots.
  - **A tool in hand may draw material from inventory** (e.g. a shovel pulling
    blocks from pouches). That isn't a contradiction: the tool is what's used, and
    it's in a hand. It's also deliberately building-friendly. Alternative kept on
    record: make material tools containers with their own slots.
- **No nesting:** an extradimensional item can never be inside another
  extradimensional container — not in a pouch, not in a chest (a chest interior
  is itself an Aether pocket). It *can* be placed in the world, or in a mundane
  box (`inventory/box`). This stops the "infinite bag of bags" and keeps the
  ownership graph shallow (`data/dimension-gc`). Pockets decay rather than
  being collected, so a shallow graph isn't an engineering need;
  no-nesting is a design rule.
- **Pouches are early and placeable:**
  - The **3×3 (9-slot) pouch** is a very early item, so three pouches in the
    generic slots give **3×3×3 = 27 extradimensional slots** quickly.
  - A pouch can be **placed in the world and broken/grabbed back with its
    contents** (like a Minecraft shulker box), so a pouch is a portable
    **9-item loadout** — far less inventory management than Minecraft's grid.
  - **No loadout swap.** Exchanging a hotbar's contents with a pouch's is
    unnecessary given the pouch-in-hand placing tool and capacity-1 body
    slots (`inventory/hand-pointers`): a pouch on the hotbar already is a
    loadout.
- **Pickup routing**: items picked up from the world
  (walking over loose item entities) go **only into the three generic slots**,
  filled **left to right**. If a generic slot holds a container (pouch), that
  container's slots are filled **before** moving on to the next generic slot.
  Picked-up items **never** go into hands/hotbars or wearable slots.
  - Consequences: a pouch in generic slot 1 is the player's "catch-all" bag;
    pickups into a pouch are weightless, pickups into a bare generic slot weigh.
  - An extradimensional item can't enter a container (no nesting), so a
    picked-up pouch only ever lands in an empty generic slot itself.
  - When nothing fits, the item stays on the ground.
  - **Top up first**: a pickup first merges into any
    matching partial stack anywhere along the route (generic slots and the
    containers in them), and only then takes the first slot with room, left to
    right. (Proposed, `inventory/stack-size`: a container holds at
    most one stack per item, so when it is full for that item the route moves
    on to the next container.) Hotbars are never topped up either, so a hotbar loadout keeps exactly
    the counts the player put there.
- **Broken blocks go straight to inventory**: an
  anti-frustration feature. When a tool breaks a block, its drop goes directly
  into inventory **if there is room**, skipping the drop-and-pickup step.
  - **Same route as pickups** (proposed): top up matching stacks first, then
    generic slots left to right, containers before the next slot, never hands,
    hotbars or wearables. One rule, so players can predict where things land.
    Body slots hold one item each (`inventory/hand-pointers`), so a broken
    block can also land in an empty generic slot, where it weighs;
    containers are filled first along the route.
  - **When nothing fits**, the drop falls in the world as an ordinary item
    entity, as before.
  - **Opting out is diegetic:** a late-game player clearing a large area who
    doesn't want junk blocks sets their pouches aside or leaves no room, and
    the blocks simply drop. No settings toggle.
  - **Feedback:** an animation (a shard arcing into the player) and a pickup
    sound keep the feel of collecting the drop; **a subtle shift in that
    animation and sound signals that inventory is full** and the block
    dropped instead, so a valuable drop isn't lost unnoticed.
  - **Consequences:** before the first pouch, a broken block lands in an empty
    generic slot if there is one (one block per slot), otherwise it drops. Bulk breaks (sledge area, axe propagation,
    `tools/catalog`) route every resulting block, cracked variants included.
    In multiplayer it removes contention over drops at the dig face. For a
    valuable ore minted in place (`geology/ore-sites`) the transfer is a
    single ledger move from world to inventory, with no intermediate item
    entity.
- **Progression:** mid/late game adds **lighter items with more slots** (5×5,
  7×7, 9×9) and **encumbrance-reducing items** (negative weight).
- **Late game: portal items.** Any body slot (wearable, hotbar or generic) may
  hold a **portal item** that opens not an inventory but a **traversable pocket
  dimension** (`world/dimensions`' "private extradimensional space"). Portal
  items obey the same rules: no nesting, not placeable in containers — which
  naturally limits how many a player can carry.
  - **Traversable pocket dimensions hold no containers and no portals**
    (deliberately constrained starting point): only workstations and
    other placed blocks. No chests, pouches or portal items may be placed inside
    one. Possibly too limiting; revisit once the mechanic is playable.
- **Death:** **all body-slot items drop** — wearables, hotbars and generic slots
  — pouches included, with their contents still inside them.
- **Despawn of player-dropped items:** player death drops last **real
  days to weeks** of `WorldTime` (see `inventory/ground-storage` for the
  other classes), counted on the world's clock **regardless of whether the chunk is
  loaded**; the drop
  timestamp (world time) is stored on the ledger entry (`data/entity-ledger`), so
  despawn is a closed-form expiry evaluated lazily when the entry is read or
  checked (`core/closed-form-first`), never a per-tick simulation or a
  sweeper; the despawn sink still records the conserved removal in the
  ledger. Minecraft's
  "5 minutes while loaded" both exposes chunk loading (a fourth-wall break) and
  is too tight for recovering your own items. The world's clock pauses when the
  world does, so a closed or paused single-player world never despawns your
  drops (`persist/presence-not-absence`). On a multiplayer server the world
  clock runs while you're offline; days to weeks of world time is the
  balance there (counting only while the owner is online was rejected as
  a fourth-wall break; the consensus pause still applies). Revisit with play.
- **Despawn and decay are two separate steps** (`dimensions/aether-pockets`):
  1. **Despawn removes the pouch item only.** The ledger records the pouch's
     removal (`data/entity-ledger`); nothing inside it is touched.
  2. The pouch was what **stabilized its pocket** of the Aether. The pocket is
     now ordinary unstabilized Aether and **decays by the usual mechanics**
     (`persist/diegetic-persistence`, `data/dimension-gc`). Decayed contents go
     through the ledger's decay sink, never silently.
  3. Until it decays (real months), the pocket and its contents are still out
     there, and Enigma's finding aids might locate it.
  (An Ender-Chest-style shared inventory is a permanently stabilized pocket, and
  chests are only access points to it.)
- **Inventory belongs to the body**, not the soul. A soul
  that leaves a body leaves its items on it. Soul-linked storage (e.g. an
  Ender-Chest-like block reaching a per-soul persistent dimension) is a
  world-block question, not an inventory one: it must be placed to be used.
- Rationale:
  - Two mechanics with different costs create choices; one slot-count mechanic
    only creates chores. Here the costs are: encumbrance for what's on the body,
    and "only hands can use it" for what's stored.
  - "Extradimensional" is literal: under `world/dimensions` a bag interior *is* a
    pocket dimension, so weightless contents and bigger-bag = bigger-dimension
    both fall out of the existing model.
  - Quick loadout switching stays bounded without extra rules: a loadout is a
    pouch, it must be in a body slot to use, and body slots are few and weigh
    something.
- Open:
  - Higher pack tiers' names and costs.

## inventory/hand-pointers — Body slots hold one of anything and never stack; a pouch in hand is the placing tool

- **Body slots hold anything, but never stack**. Every body slot
  (hotbars, generic slots, wearables, jewelry) holds **one item**, a block
  included: the body is a capacity-1 container in the sense of
  `inventory/stack-size`. Stacking happens only in extradimensional
  containers.
  - Non-stackable tools need no special case, and containers work as before.
  - One block in hand is far less immersion-breaking than hundreds, which keeps
    the "too heavy to carry in bulk" fiction: carrying blocks in bulk is why
    even a first-day pouch is magical.
  - A player without a pouch can still carry a few blocks, one per slot.
- **Blocks count for encumbrance**: weights are not realistic
  (`inventory/encumbrance`), but carrying several heavy blocks unstacked on
  the body **should slow you down**.
- **To place from a pouch, move the pouch to your hand**. A pouch in
  hand is a placing tool that draws from its contents: the "degenerate placing
  tool", like the shovel placing its fill (`tools/catalog`).
  - **The placement mode is a property of the container** ("container
    placement modes", since a pouch is just a light container that's
    convenient to carry). Four modes:
    1. **in order**;
    2. **random slot**;
    3. **random weighted by block count**: a count-weighted container *is* a
       palette, its weights being its own contents;
    4. **match target**: place the same block type as
       the targeted block if the container holds one, otherwise place nothing.
    **M** cycles a held container's mode, for now (`input/bindings`).
    **The randomness is temporal**: one click places
    one block, and successive placements walk the modes. Picks hash (cell,
    container id, the container's draw count); the draw count advances with
    each placement that changes the world and is also the in-order cursor.
    It is tool state, so the preview stays exact (`input/preview-mode`), and
    the client predicts it from replicated inputs (server wins on
    disagreement). Multi-cell placement (an N×N patch) is a separate
    capability orthogonal to the modes (v0.1.8). **Placing consumes:** each
    block placed decrements its count, and count-weighted picks weight by the
    current counts, so the weights shift with every placement, like drawing
    from a bag. A patch uses up counts in scan order
    and places partially, the preview marking what it can't afford.
    Placement modes are minor early secrets (`inventory/pouch-progression`).
  - A pouch on the hotbar is already a loadout, which replaces the loadout
    swap (`input/hotbars`).
- **Placing the pouch itself is G**: click places
  *from* a pouch in hand; **G** (set down, `input/bindings`) places the pouch
  itself in the targeted cell, as a placed container, or at your feet with no
  target. No modifier and no secondary action spent. Dropping is setting down
  (`inventory/ground-storage`); **Shift+G** is reserved for a later "drop as a
  floating entity" if it's ever wanted. Shift+click was considered and not
  used, keeping the pouch's secondary action free.
- **Alternatives considered:**
  1. **Ordinary stacks in body slots:** block
     stacks on the hotbar like Minecraft, with gamified weights. Rejected:
     hundreds of blocks on the body break the fiction, and pouches lose their
     point.
  2. **Blocks never in body slots**: too heavy to carry at all, so
     before the first pouch broken blocks simply drop. Rejected as too strict:
     it forced placing to go through pouches indirectly, and made the first
     pouch a hard gate for building.
  3. **Hand pointers**: a hotbar slot as a handle on a stack that
     stays in a pouch. Dropped for the simpler pouch-in-hand: no dangling
     pointers when a pouch is set down, no question of which pouch a pointer
     targets.
  4. **Small stack capacities on the body** (e.g. 6 blocks per slot).
     Rejected for capacity 1, which is simpler and reads more clearly.
- **Which item a pouch in hand places is answered by its placement mode**:
  in order, random slot, count-weighted or match target. The
  latter covers pick-block from the world. A "fixed slot" mode, placing one
  chosen type, may still be wanted (proposed, open).
- Open: encumbrance details, including weights per block class (TBD,
  Michael, 2026-11-03; `inventory/encumbrance`).

## inventory/pouch-progression — Folk-craft pouches: hide, patterns and mineral pigments

- **Pouches open onto small pockets of Aether** (`worldgen/dimension-cosmology`).
- **Extradimensional pouches are folk craft, not arcane power**
  (`core/low-fantasy`): ordinary, local, costly magic that villagers use too,
  so the pouch is part of how the world works rather than the player's
  destiny.
- **Folk patterns, not runes**: pouches carry stitched or painted
  folk patterns, which are less culture-bound than runes. Runes may appear
  later, for example engraved into weapons.
- **Draft chain:**
  1. **First pouch (day one):** rawhide from small game (cut with the knife,
     sewn with sinew or plant fibre; snares from fibre). Animals are deadly,
     so the first hide is small game, not a bear. **No-kill alternative**
     (Michael likes it): a woven bark or fibre pouch, slightly worse. Patterns
     painted in **ochre** (iron-oxide pigment, ubiquitous, already in
     `geology/essential-minerals`), or burned in with a stone heated in the
     campfire.
  2. **Tanned leather:** rawhide is stiff and slowly degrades; tanning uses bark
     tannin, smoke, or brain tanning. Different hides differ in look and small
     trade-offs (weight vs durability), never in a single best hide
     (`core/no-meta`).
  3. **Bigger bags** (5×5 → 9×9) need **stronger pattern pigments**, real
     historical mineral pigments, so geology feeds the craft: ochre (red,
     yellow), charcoal and soot (black), malachite (green), azurite and lapis
     lazuli (blue; ultramarine), cinnabar (vermilion, toxic), orpiment
     (yellow, toxic). Resonant quartz dust as a late binder.
  4. **Portal items** stay late game (above).
- **Hauling stays extradimensional**: blocks are too heavy for
  mundane carrying, so bulk hauling is extradimensional too. A **sled** (a
  dragged container) could host more storage than a pouch, trading mobility
  for capacity. (Naming: the breaking tool is the "sledge"; the hauling one
  should be called a sled to avoid a collision.)
- **Pouches vary along three axes, all crafting inputs**:
  1. **total capacity** (tiers: 3×3 → 9×9);
  2. **more slots vs larger stacks** (slot capacity, `inventory/stack-size`:
     bulk pouch vs specimen bag);
  3. **container placement mode** (in order, random slot, random weighted by
     count, match target; `inventory/hand-pointers`).
  Which crafting input sets which axis is open (the earlier draft mapped
  material to grid and pattern to capacity).
- **Container placement modes and their variants are minor, early secrets**:
  learned in Terra as new magical **patterns**. They are quality
  of life, not essential to progression, and need not be rare. Note
  (proposed): this makes them the gentle, early end of the secrets spectrum,
  with per-world recipes found in Enigma (`crafting/per-world-recipes`) at the
  far end; how a pattern is learned (seen on a found object, taught,
  discovered by experiment) is open.
- Open: hide types and their trade-offs; pigment tiers per bag size; whether
  patterns are a crafting choice with visual variety; the sled's size, speed
  and how it is drawn.

## inventory/ground-storage (draft) — Small non-block items can be set down on surfaces

- Decision: **small, non-blocky items** (plant fibre, nuggets, a
  knapped flint) can be put down on surfaces in the world, so containers are
  a convenience rather than a necessity early on, when resource counts are
  small. Blocks are not ground-stored: they stay 1:1 with cells
  (`world/voxel-first`). Big stacks still really want an inventory slot.
- Precedent: Vintage Story's ground storage (from memory); Minecraft needs
  item frames for the same job.
- **Proposed mechanism (proposed, open):**
  - **A set-down item is a ledger entity with a world location**
    (`data/entity-ledger`), on a cell face (e.g. one of four quadrants of a
    top face for small items). No physics, no ticking, **no timed despawn**;
    if its chunk decays to baseline months later, the item is sunk through
    the ledger like other ledgered contents (`persist/diegetic-persistence`).
  - **Rendered as a small pile** that grows in a few visual steps with the
    stack size.
  - **Dropping becomes setting down:** a dropped item lands on the surface you
    look at, or at your feet, rather than being thrown as a loose entity. No
    new verb. (Display and wall storage are the box's job, `inventory/box`.) Anything that would otherwise be a
    loose drop (a non-block item from a mob, say) settles into a surface
    placement once it lands.
  - **Pickup rules split:** spilled items are still collected by walking over
    them (`inventory/slots-and-pack` pickup routing); items deliberately set
    down need an explicit pickup with the interact key (`input/interact-key`).
- **Two kinds of ground storage**:
  - **Permanent:** small non-block items deliberately set down (above).
  - **Transient, with despawn:** anything that ends up outside inventory
    without being deliberately set down, **blocks included** (a broken block
    with no room in inventory, a mob's drop). It sits on the ground as a
    temporary pile and eventually despawns; it is never permanent storage.
    This answers where an overflow block goes.
  - **Despawn timing**: much longer than Minecraft's
    five minutes, **somewhat randomized**, and on **`WorldTime`** (real time
    minus pauses, `coords/typed-time`), not game days. **Valuable items and
    player death drops last much longer**: real days to weeks, or until chunk
    decay (real months) takes them (`persist/diegetic-persistence`).
  - Notes (proposed): the randomization can be a hash of the item's id, so
    each pile has its own fixed lifetime, evaluated as a closed form
    (`core/closed-form-first`) with no ticking. In ledger terms despawn is a
    declared sink (`data/entity-ledger`).
- Open (proposed): **if permanent ground storage is dropped,**
  where small items (fibre, nuggets, flint) live before pouches. Body slots
  hold one item each, so early gathering could feel cramped. Candidates: the
  first pouch in the first session, or boxes earlier than planned
  (`inventory/box`).
- Open: what happens when the supporting block is removed (the pile falls to
  the next surface); whether animals or thieves can take set-down items;
  ownership in multiplayer; the exact despawn lifetimes per class.

## inventory/box — A mundane wooden box holds one item; pigeonholes and item frames
- **Open:** the details marked as proposals.

- Decision:
  - **A basic box is not extradimensional, so it holds exactly one item.**
    Probably made of wood.
  - **It can be placed against vertical surfaces** to build **pigeonholes**,
    and it needs no support.
  - **It does the job of Minecraft's item frame:** a visible, placed, single
    item.
  - Motivation: partly an alternative to ground storage, which Michael is iffy
    about (`inventory/ground-storage`).
- **The same rule as body slots** (proposed): mundane storage holds one of
  anything and only pocket dimensions stack (`inventory/hand-pointers`,
  `inventory/stack-size`). A box is a body slot fixed in the world.
- **Low-sided**: the box is shallow, so its sides don't
  hide the model of the item it holds. It occupies one cell and is oriented
  with its open side outward (proposed), so a pigeonhole wall is a grid of
  boxes.
- **A box holds any item, full blocks included**. It follows from
  `dimensions/aether-pockets`: pockets are arrays of
  boxes, and pockets store blocks. A block in a box is drawn **slightly
  shrunk**, as Minecraft's item frames do. Shrinking it visually is the forgiving magnitude in
  `core/realistic-in-kind`: a box holding a block is fine in kind.
- **Boxes in the world can hold pouches and chests**.
  A box isn't extradimensional, so no-nesting doesn't apply. The boxes making
  up a pocket in the Aether still refuse extradimensional items: that's the
  no-nesting rule (`inventory/slots-and-pack`).
- **Pocket interiors are arrays of boxes in the Aether**
  (`dimensions/aether-pockets`), which is part of why the mundane box exists.
- **Content is right side up**: the held item stays
  upright even when the box is on a vertical surface, though maybe upside down
  when the box is on a ceiling. A good default rule can be found for each
  pair of box orientation and content symmetry group. Notes (proposed, open):
  - **The default is a rotation from the box's own placement:** "up" is world
    +z for floor and wall boxes and −z for ceiling boxes. The content's front
    faces out of the opening, where its symmetry gives it a front.
  - **A block's content rotation is canonicalized** like any placed block's
    (`world/block-orientation`). So "right side up" is defined per symmetry
    type: a log stands on end, and a furnace's front faces out of the box.
- **The wrench turns whatever the ray hits first**. Content sits proud of the low sides, so
  aiming at it turns the content in place and overrides the default. The box
  itself rarely needs turning. When it does, aim at the box's rim,
  or empty it first.
- Proposed (proposed, open):
  - **Its content is on display.** That makes it an item frame and a
    diegetic label (an ore sample in a box above the chest holding the
    ore).
  - **Interaction:** the interact key (`input/interact-key`) puts the held item
    in or takes it out. Breaking a box yields the box, and its content goes to
    inventory by the pickup route, or to a transient ground pile if there's no
    room. A full box can't be carried: that would be two items in one body
    slot.
  - **Progression:** it needs boards, so it comes after the knife or axe:
    early, but not first-session.

## inventory/encumbrance — Soft, sigmoid slowdown on body load, with a nonzero floor

- Decision: total **carry weight of items in body slots** (hotbars, wearables,
  jewelry, generic slots, including the weight of pouches themselves but never
  their contents; **blocks count**, and several heavy blocks carried unstacked
  should slow you down) reduces walking speed along a **sigmoid** (`core/soft-thresholds`):
  - a **light region** where speed is essentially full,
  - a **transition region** where speed falls off,
  - a **saturated region** where speed is essentially the minimum.
  - Form: `speed = v_min + (v_max − v_min) · (1 − σ((load − L₀) / w))`, with
    `v_min > 0`: **you can always move**, however encumbered, just slowly. There
    is no hard carry cap.
  - Negative-weight items subtract from load (late-game progression).
  - **A slot's weight scales with its stack size**: a stack
    of 40 weighs 40× one item. Per-item weights for building blocks are set low
    enough that a partial hotbar of stacks stays light (below).
- **Weights are abstract and gamified, not physical.** Real density is unusable
  at 1-ft voxels (a stone block is ~75 kg). Each item/material carries an
  authored carry weight, tuned so that **at least a partial hotbar of building
  blocks stays in the light region**. The main thing encumbrance is *for* is
  equipment like **heavy armor slowing you down** — a mobility-vs-protection
  trade — not punishing building.
- Rationale: resolves the carry-capacity gap in `progression.md` (hard cap vs.
  soft slowdown; it leaned soft). A soft curve has no "one block too many and
  you're frozen" cliff, and a nonzero floor never strands the player.
- Open:
  - Which axes encumbrance affects besides walking speed (sprint, jump, swim
    kick, climb with C/V).
  - Tuning `L₀` and `w`, and whether they grow with character progression
    (strength), mounts or vehicles.

## inventory/stack-size (trial) — Voxel-sized items stack to 360; each container sets its slot capacity

- **Slot capacity is a property of the container, not of the item**
 . 360 is the **maximum**; a container may have a
  smaller capacity. **Each container has a single slot capacity** for all its
  slots (cognitively easier than mixing). Natural capacities are
  divisors of 360: 12, 30, 60, 90, 120, 180, 360.
  - **The trade-off is volume against variety:** a **bulk pouch** (3×3 × 360 =
    3,240 items of up to 9 kinds) suits digging; a **specimen bag** (e.g.
    9×9 × 12 = 972 items of up to 81 kinds) suits gathering rare
    supplies (seeds, herbs, pigments, crystals, nuggets). Neither is better
    overall (`core/no-meta`).
  - **Fill rule** (proposed, open): when a container's
    slot for an item is full, routing moves on to the **next container**
    rather than opening a second slot of the same item in the same container,
    so a specimen bag holds "12 of each" instead of filling with flint.
  - **Crafting mapping** (proposed, open): the sheet material and
    the bag's size set the slot grid; the folk pattern and its pigment set the
    slot capacity (`inventory/pouch-progression`).
  - **Body slots have capacity 1**
    (`inventory/hand-pointers`); small body capacities such as 6 were
    considered and rejected.
- The original rule, stated per item before slot capacity was added:

- **Basic blocks (voxels) and every item of that size class or smaller stack
  to 360** (now: in a container of full capacity), in particular world-placeable items that fit within
  one voxel. Larger items stack less (or not at all); tools are mostly single
  anyway.
- Rationale:
  - **Divisibility:** 360 divides evenly by every number from 1 to 10 except
    7, so stacks split cleanly between people, recipes and containers.
  - **Not a programmer number** (unlike 16 or 64), and **atmospheric**: the
    old circle and calendar number.
  - **Our blocks are small** (1 ft, `world/voxel-scale`), so a large count is
    natural; and although there are many types, there are few variants per
    type, and a player digging in one locality gets only some of them at
    once, so slots fill by type more slowly than the type count suggests.
- Consequences:
  - **Stacks live only in extradimensional storage**
    (`inventory/hand-pointers`: body slots hold one item each), so a 360-stack
    never weighs on the body. In
    pouches (weightless,
    `inventory/slots-and-pack`) full stacks are the normal case: three 3×3
    pouches hold 27 × 360 = 9,720 blocks.
  - So stack size is mainly a lever on **extradimensional capacity**.
- Considered: a per-item size class with stack size = slot volume / item
  volume (sub-voxel items would then stack higher than 360). Not adopted for
  now; the uniform 360 cap is simpler.
- Open: stack sizes for items larger than a voxel; how wear interacts with
  stacking (non-stacking tools, or condition tiers that keep like items
  mergeable); whether 360 holds up in play.
