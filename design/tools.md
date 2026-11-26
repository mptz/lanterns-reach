# Tools

The catalog of hand tools: what each one does to the world, and the trade-off
it represents. The *mechanism* for tools lives elsewhere and is not repeated
here: mouse buttons map to hands and items define their own actions
(`input/per-hand-use`); every tool is one pure function
`(tool state, target, world state) → edit set` that drives both its preview
and its action (`input/preview-mode`); a tool may draw material from
inventory (`inventory/slots-and-pack`).

## tools/few-and-versatile — Few tools, each versatile; tools differ by trade-off, not by block whitelist

- Decision: keep the tool set **small**, and make each tool **versatile**. A
  tool is defined by how it acts on a *class* of material and by the
  trade-off it makes (precision vs bulk, yield vs speed), not by a list of
  blocks it is allowed to break.
- Rationale: fewer, simpler mechanics (a general preference); a
  situational trade-off between tools is what `core/no-meta` asks for, where a
  tier ladder of strictly better tools would not be.
- Consequences: tool tiers and materials (bronze, iron, ...) are still
  dependency-graph content, deferred with the recipe graph
  (`progression.md`, "Extraction capability").

## tools/catalog — The tools

| Tool | Acts on | Action | Trade-off |
|---|---|---|---|
| **Pick** | rock | precision break: pries out one block at a time, clean drop | slow, full yield, no cracking |
| **Sledge** | rock | bulk break, e.g. driving a tunnel through rock | fast, but lower yield and a rock-strength-dependent share of cracked drops (`geology/rock-blocks`) |
| **Shovel** | soft materials (dirt, sand, gravel, clay) | digs **and places**: breaks and fills, levelling ground | the one tool that both removes and adds; soft materials only |
| **Knife** | all organic material: plants, fibre, flesh, wood | the generic cutting tool: harvests plants, clears brush, butchers, carves, and can cut a tree | versatile, but slow on timber |
| **Axe** | woody and fibrous material mainly | slow swing, but much more effective per swing on woody/fibrous material, so it comes out well ahead on a whole tree; a slow grass cutter | **heavier** than the knife: an encumbrance trade-off (`inventory/encumbrance`), so it earns its weight when cutting lumber in volume |
| **Wrench** | any placed block | rotates 90° about the clicked face's axis; secondary = the other way (`world/block-orientation`) | shaping, not breaking |

- **No generic leveler**: the leveler once
  specified for v0.1.7 (a 5×5 level-to-height edit that both removes and
  fills) is the shovel's action, so v0.1.7 builds the shovel instead,
  restricted to soft materials.
- **Shovel footprint and reach** (v0.1.7 build choice; reasonable, but
  **needs UX testing**, probably once the player is
  embodied):
  - The level is the targeted cell's z, over 5×5 columns centred on the
    target.
  - Each column scans at most 8 cells above the level and 8 at or below it.
    Above, it cuts soft material and treats rock as blocked. Below, it fills
    down to the first solid cell.
  - "Soft" is decided by block name (dirt, grass, sand, gravel). A non-soft
    fill setting fills nothing.

  Open to revisit with a body in the world: footprint size, the ±8 reach,
  whether the level should come from the player's feet rather than the
  target, and how the shovel should feel to use over many swings.
- **Bulk placement belongs to block stacks**:
  a block stack gets a fill footprint under preview mode (`input/preview-mode`),
  so walls and floors aren't placed one block at a time. No trowel as a
  generic placer.
- **Palettes for randomized multi-block placement**: a
  placement or fill can draw from a **palette**, a weighted set of block
  types, so a wall comes out as a natural mix (say 60% granite brick, 30%
  cracked, 10% mossy) rather than a uniform face. "Palette" is a little
  fourth-wall, but it is the word builders already use. Refinements
  (proposed, open):
  - **Each cell's pick is a hash of (cell, container id, draw count)**: it
    looks random, but the draw count is tool state that advances with each
    placement, so the randomness is temporal and the
    preview shows exactly what will be placed, keeping `input/preview-mode`'s
    rule for precision tools.
  - **Materials come from inventory per type,** and the weights are the
    current counts, so they change with every block placed (drawing from a
    bag). A type at 0 drops out, and when all are out
    the fill stops, placing partially (v0.1.8, `inventory/hand-pointers`).
  - **A diegetic option** (the direction,
    `inventory/hand-pointers`): the palette *is* a mixed pouch held in the
    hand with the count-weighted container placement mode, its weights the pouch's own
    contents, which matches the alternative already on record in
    `inventory/slots-and-pack` (material tools as containers with slots).
- **Elaborate designation and build modes belong to dreaming**:
  Litematica-style tools (projecting a schematic as ghosts,
  region selection, copy, rotate and mirror, layer-by-layer views, material
  lists) fit the dream's free-moving spectator perspective and its
  designation-only intention set (`sim/dreaming-designation`). Awake, the
  player has the hand tools and preview mode; asleep, the planning tools.
  See `sim/dreaming-designation` for how that fits the dream.
- **No hoe**: tilling is a shovel placement (the
  shovel places tilled soil).
- **No tool is two-handed**: a UX
  simplification. Every tool is used from one hand, so the two hands and their
  hotbars stay independent, even for heavy tools like the sledge and the axe.
  Weight shows up as encumbrance and swing rate instead
  (`inventory/encumbrance`).
- **Named "pick", modelled on a geologist's pick** (first called "pickaxe").
  The point is simply that it is **the
  one-block breaking tool**:
  - **Scaled to our voxels:** a light pick suits 1-ft blocks better than a
    miner's pickaxe (`world/voxel-first`).
  - **Clearly one-handed.** A heavy two-handed pickaxe would be the wrong
    picture.

  Bulk rock breaking stays the sledge's job. The first pick is still the
  antler or knapped pick (step 4 of the first-session tool chain).
- **Named "knife"** (first called "blade"). The
  plain, everyday word suits `core/low-fantasy`; the first one is a knapped
  flint knife, and bigger tiers can still be knives (a woodsman's knife, a
  heavy chopping knife).
- **The knife, plus an axe for lumber**. The separate
  small knife and the axe were too similar to keep as peers, so the generic
  cutting tool is the **knife**, covering all organic material including
  wood. Real-world
  precedent (proposed, from memory): the **kukri** (Nepal) and **parang**
  (Malay world) are general-purpose knives used for chopping wood, clearing
  growth, butchering and carving; the **machete**, **bolo** and European
  **billhook** cover most of the same ground. None fells a large tree as fast
  as an axe, which is why the **axe** stays as a specialist: the knife can cut
  a tree, but a player cutting lumber adds an axe to the loadout. Rock has the
  same shape (pick for everything, sledge when breaking rock in volume).
- **The axe is faster on wood, not bulk**: trees can be
  several blocks thick at the base, and the axe cuts each wood block faster
  than the knife. No whole-tree felling. Its cost is weight, so the choice is
  speed versus carried load, the kind of trade-off `tools/few-and-versatile`
  wants. (Unlike the sledge, which trades yield and cracking for area, the
  axe loses nothing on output.) A sledge-like axe was considered and is still
  possible, but this felt more natural.
- **Heavy tools act on more than the targeted block**:
  - **Sledge:** breaks blocks out to about 1–3 blocks from the hit in the
    **vertical plane facing the player**, so swinging forward clears a rough
    tunnel of roughly **3×3 to 7×7** cross-section, one layer per swing.
  - **Axe:** damages blocks in the **horizontal plane, edge-on to the player**
    (a horizontal cut through the target), so cutting through a thick trunk
    goes faster.
  - **Shifted axe** (Shift, the secondary-action modifier of
    `input/per-hand-use`): the **vertical plane, edge-on to the player**, a
    vertical cut, e.g. splitting.
  - **Planes are camera-directed, snapped to the lattice** (algorithm to be
    decided). The look direction picks the
    plane's orientation and the lattice fixes it to an axis-aligned plane,
    in all three axes, so you can **dig down**: a sledge swung at the floor
    acts in a horizontal plane and sinks a pit, and one swung up at a
    ceiling raises it. Likely shape (proposed): snap the look direction to
    its dominant axis among all six directions, with a little hysteresis near
    45° so the plane doesn't flicker; the edge-on axe planes are then the
    planes containing that axis and, respectively, the camera's horizontal
    right vector or world up.
- **Both are one generic mechanism: impact propagation**. A swing's impact
  spreads outward from the hit through the
  material, within the tool's plane (above), attenuated by the material:
  - **Rock strength sets the sledge's reach:** brittle or weak rock shatters
    farther, strong rock less. That surfaces rock strength a second way,
    alongside the cracked-drop share (`geology/rock-blocks`).
  - **Tool effectiveness gates the axe's spread:** adjacent woody blocks take
    damage; grass, rock or a player's planks next to the trunk are spared
    unless they are woody too.
  - Partial damage on blocks that didn't break is soft state that fades if
    the work isn't finished (`core/soft-state`), like ordinary break progress.
  - It is part of the tool's edit at swing time, not a simulation process.
- **Rough tools are unpredictable on purpose**: heavy
  tools aren't about precision, so you take a whack and see what happens. The
  **area of effect is never previewed**; players learn it empirically. Preview
  shows only the **target block** (the usual crosshair and highlight; a
  breaking ghost only if it is very subtle, since ghosting is distracting for
  breaking) and possibly the **cut direction** (which plane the swing will
  act in, e.g. to show the shifted axe's vertical cut). That is a deliberate
  exception to `input/preview-mode`'s rule that preview and commit are the
  same pure edit set; precision tools (pick, knife, shovel, block stacks)
  keep exact previews. The roll comes from the authority, so every
  observer sees the same result, and it only ever moves or sinks ledgered
  content, never creates it.
- **Area edits can span chunk faces:** an edit that crosses chunk owners
  commits all-or-nothing as a Vicinity transaction
  (`sim/concurrency-model`), as fills do.
- **Cutting speed = swing rate × effectiveness on the material** (generalized
  as the model for every breaking
  tool): each tool has a **swing rate** and an **effectiveness per material
  class** (woody/fibrous, soft plant, flesh, rock, soft ground). Time to break
  a block is its resistance divided by effectiveness, paid in swings. The axe
  is slow-swinging and highly effective on woody/fibrous material: it wins on
  trees and loses on grass. The knife swings fast with moderate effectiveness
  on everything organic. The same two numbers describe pick and sledge on
  rock, so one model covers all breaking tools.
- Open: everything in `tools/candidates`; tool wear; how a sledge's bulk area
  is chosen beyond its plane (aim point); the plane-snapping algorithm and
  its hysteresis; how the cut direction is indicated;
  tool weights, swing rates and the effectiveness table.

## tools/breaking-as-action — Breaking unfolds over time; its animation absorbs the world update's latency
- **Open:** effect durations and the placement animation.

- Decision: breaking a block is **an animated action unfolding over time**,
  with visible damage, particles and sound, not an instantaneous event that
  the world must reflect within a frame. Some of it is purely
  client-side; only its outcome reaches the server.
- **Phases:**
  1. **Swinging** is the player body's state (tool, target cell, swing
     phase), predicted on the client and owned on the server by the body's
     Vicinity (`sim/concurrency-model`). Each swing's animation, sound,
     impact particles and growing crack overlay are client-side
     presentation, immediate.
  2. **Damage** (cracks) is soft state published by the swinging body
     (cell, progress, tool), with a TTL that lapses when the player stops
     (`core/soft-state`), not chunk data. **Other players see cracks**
     (arguably yes, as in Minecraft; a relatively low priority for
     multiplayer), and **they need not be tightly in sync** with the
     swinging for them, unlike for the player breaking the block: updates
     at a low rate, interpolated, are fine.
  3. **Completion is predictable:** resistance and swing rate are known, so
     the client knows which swing finishes the block. Only that swing
     becomes an edit message, checked by the server against the expected
     old state; small client-server timing differences are absorbed by
     tolerance, as in movement reconciliation.
  4. **The break is animated:** the final impact starts a short crumble or
     debris effect (about 100–200 ms, from the block's texture) and the drop
     appears. The block's mesh update lands anywhere inside that effect,
     under cover of the burst.
- **Breaking is a physics process with duration; input only continues or
  halts it**. Holding the button is a continuing intention;
  releasing it halts the process at the next swing boundary. The process
  itself (swing rate, resistance, which swing finishes the block) is
  predictable, so latency is gated on the edit's round trip through the
  chunk's turn and the remesh, **measured from the point of no return, not
  from the click**:
  - **the final swing is the point of no return:** once it starts it
    completes, as a real swing does, so the edit message can be sent at
    its start rather than at impact, and the chunk's turn and remesh
    overlap the swing itself;
  - **the swap is timed to the impact:** the new mesh's descriptor carries
    the impact time, and the frame thread shows it from then, so a fast
    pipeline doesn't make the block vanish before the tool lands; a slow
    one still lands inside the crumble;
  - **the remesh can be speculative, ahead of the impact:** the chunk
    receives the pending edit with its effective time (the impact), and
    its own turn meshes the post-edit state into a mesh tagged with that
    time, without changing its content yet; the chunk applies the edit at
    the effective time, and the mesh is already uploaded and waiting. If
    the edit doesn't take effect (the player halts it, or its expected old
    state no longer holds because someone else changed the cell), the
    speculative mesh is retired unused. This keeps meshing inside the
    chunk's own turn and on its own data plus one pending edit, so it is
    not the client-side meshing reader or prediction overlay set aside in
    `sim/concurrency-model`; the same mechanism works on a multiplayer
    client's replica chunk, fed by its local physics' final swing;
  - **everything else is client-only physics:** swing animation, impact
    particles, crack decals and the crumble's tumbling debris
    (`sim/multi-rate`'s tumbling note) run on the client from known start
    times, so they never wait on the authority.
- **Consequences:**
  - The world update's latency target is "within the action's effect", not
    the next frame, so edited chunks need high priority but no special
    path (`sim/concurrency-model`, "Meshes and the GPU").
  - The renderer needs a small **effects layer** for transient, client-only
    effects (crumbles, crack decals over a cell, debris), needing only a
    cell, the block's appearance and a start time, nothing from chunks.
  - **Placement** may have a short settle animation (a scale-up from about
    90%, or a puff) that covers its latency the same way; **creative-mode
    instant breaks** still get the short burst, as feedback and cover.
  - **Bulk edits** (sledge, axe propagation) already happen at swing time
    and are covered by that swing's impact.

## tools/candidates (draft) — Tools under consideration

- Assistant's proposals. Each either earns a slot by owning a distinct verb,
  or folds into an existing tool.
  - ~~Separate small knife~~: merged into the knife (`tools/catalog`), which also absorbs
    shears.
  - **Bucket** (lean: add, once fluids exist). Moves fluids; really a
    container tool (`inventory/slots-and-pack`).
  - **Trowel** (deferred): real trowels are mainly for mortar (laying brick
    and stone, plastering) and fine digging. Bulk placement went to block
    stacks; a trowel comes back only if mortar becomes a mechanic (e.g. lime
    mortar from limestone making masonry resist decay longer).
  - ~~Hoe~~: folded into the shovel (`tools/catalog`).
  - **Chisel** (lean: no). Dressing stone into polished or brick variants
    fits a workstation better than an in-world tool, and avoids a secondary
    action on the pick.
  - **Fire starter** (flint and steel): an item, not a tool.
- That gives seven tools: pick, sledge, shovel, knife, axe, bucket,
  wrench. Two of them (sledge, axe) are volume specialists the player adds to
  a loadout for a job. Weapons are a separate topic.

## tools/compasses (draft) — Compasses read the world's fields and places; they are how players navigate a world with no coordinates

- **Key gameplay, not a convenience.** With no player-facing coordinates
  (`input/no-player-coordinates`), no fast travel and a world whose mix
  changes at province scale (`worldgen/provinces`), instruments are how a
  player finds their way, and a diegetic way to meter information
  (`worldgen/metered-information`).
- **One mechanism, many instruments.** A compass reads the gradient of a
  field: the needle points where the field rises, and how firmly it
  settles shows how steep it is. Candidates:
  - **pole compass:** toward the nearest pole, or away for the equator
    (`worldgen/poles`);
  - **dowsing:** toward richer `yield`, or toward water;
  - **wayfinder:** toward rising `habitation` (roads, people), or
    away from it into empty land;
  - **antiquarian's needle:** toward rising `antiquity` (ruins, relics);
  - **liminal needle:** toward thin places, where portals and folk magic
    come easiest (`liminality`);
  - **moral needle:** toward good or evil `alignment`.
- **Some compasses point at places, not fields:**
  - **bound lodestone:** toward a place the player has tied it to (a
    home, a hearth); Minecraft's lodestone is the precedent;
  - **recovery compass:** toward the site of the player's last death, where
    their dropped items lie for real days to weeks
    (`inventory/slots-and-pack`); Minecraft's recovery compass is the precedent;
  - **province seeker:** toward the nearest province of a chosen type, the
    world-preset spawn search as an object; a late-game item.
- **Several needles on one dial.** Advanced compasses carry more than one
  needle, each tuned to a field or place, so a player reads several
  things at once (Outer Wilds' signalscope, tuned to frequencies; Breath of
  the Wild's sensor, set to a chosen target). A needle might be tuned by a
  material (a resonant crystal, `geology/resonant-crystals`; a sliver of a
  field-linked plant). Worn in a jewellery slot (`inventory/slots-and-pack`)
  a compass could stay on as a dial at the edge of the screen; held
  compasses are the cheaper form.
- **Guards, so compasses don't shrink the world:**
  - **direction only, never distance;** at most an agitated needle hints
    that the target is close;
  - **precision by quality:** early needles drift and wander, better ones
    settle (jitter on server-sent readings makes this cheap);
  - **long range is progression:** within the key release radius a client
    could compute a reading anyway, which is acceptable; beyond it the
    server sends the reading, only to a player who owns the instrument
    (`worldgen/seed-keys`);
  - **in-world interference:** a liminal place confuses a needle and strong
    `weirdness` spins it, so players read the world as well as the dial.
- **Three dimensions.** Most targets are roughly level, but some are not
  (a death site deep in a cave, a liminal pocket in Enigma's cliff, anything
  in a cave world). Three forms, layered:
  - **bearing and dip (the common form):** the dial shows horizontal
    bearing, and a small second indicator (an arm on a vertical arc, or a
    bubble that rises or sinks) shows elevation, like a geologist's dip
    needle; legible as a corner widget;
  - **gimballed sphere (advanced, held):** the needle floats free in a glass
    sphere, read in first person, pointing in world space; too detailed to
    shrink to a widget, which suits an advanced held instrument;
  - **aim-to-read (a feature of better instruments):** a hum rising in
    pitch, a glow or a steadying needle when the player faces the target
    (Outer Wilds' signalscope); full 3D with no projection problem.
  A non-diegetic arrow at the screen edge is out of keeping with the
  no-coordinates design and isn't used. The frame thread draws needles from
  the published reading and the body's view frame, reading no world state
  (`engine/frame-thread`).
- **Each instrument declares the gradient it reads:** horizontal (pole,
  wayfinder, province seeker) or full 3D (recovery compass, liminal needle,
  dowsing for ore or water below). A field that changes most steeply with
  height would otherwise send a horizontal instrument up the nearest
  mountain: temperature falls with altitude, which is why the pole compass
  reads latitude φ, not temperature.
- **Open:** which compasses exist and where each falls in progression;
  which fields are truly 3D, and so which instruments read in 3D;
  how needles are made and tuned; how many needles a dial can hold; whether
  a jewellery-slot compass is a HUD element or must be looked at; how a
  bound lodestone is bound and whether the binding survives travel to
  other dimensions.
