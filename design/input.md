# Input & controls

How the human's control surfaces map onto the game. Per
`sim/player-embodiment`, controls produce **intentions**, not raw actions: the
body resolves intentions into actuation. Everything here is the client-side
mapping from keys/buttons to intentions, so bindings are rebindable and may
differ per surface without touching the sim.

Context: Minecraft's main control pain is the **overloaded right mouse button**,
which means both "use the held item" (place, eat, draw a bow, raise a shield)
and "interact with the targeted block" (open a chest, flip a lever). Collisions
are resolved by per-case rules (sneak to place against a chest, item-vs-block
priority, off-hand priority), and Java and Bedrock resolve some of them
differently. The decisions below remove the overload structurally instead.

## input/interact-key — World interaction is its own key (E); mouse buttons never open things

- Decision: **E interacts with the targeted world object**: open a container,
  operate a door/lever/machine, use a bed or workstation. Mouse buttons only
  ever use held items (`input/per-hand-use`); they never trigger world
  interaction.
- Rationale: this is the convention in most modern survival/building games
  (Valheim, Rust, Satisfactory, 7 Days to Die, Space Engineers). It removes the
  "use held item vs. interact with target" collision entirely, so no
  sneak-to-place or priority rules are needed.
- Consequence: v0.1/v0.1.1's free-fly camera bound vertical movement to E/Q,
  which collides with this key; it moves to C/V (`input/vertical-movement`).

## input/vertical-movement — C = up (climb), V = down; a general locomotion intention, not a creative-only binding

- Decision: **C** produces an "ascend" intention and **V** a "descend"
  intention (mnemonics: C = *climb*, V = a downward-pointing shape). Held =
  continuous.
- **In free flight, up and down are camera-relative:** C/V move along the camera's up vector, perpendicular to the look
  direction, like strafing, rather than along world +z, which was hard to use.
  With WASD already following the full look direction, flight is fully
  camera-relative. Other media decide for themselves: on a rope, ascend means
  climbing the rope (`world/climbing-and-descent`); swimming is open.
- **Terra wants more climbing and descending than Minecraft**: vast vertical
  caves, ropes and mechanisms for descent
  (`dimensions/terra`). The mechanics that consume these intentions are in
  `world/climbing-and-descent` (ropes as block columns, falls by
  deceleration; unaided climbing open).
- Scope: this is **not** a creative-flight binding. Vertical locomotion is a
  general intention that any medium allowing it consumes: creative flight,
  swimming in water and other fluids, and future media or bodies where up/down
  makes sense in survival. The body (`sim/player-embodiment`) decides what the
  intention does in its current medium; on solid ground with no such medium it
  does nothing.
- Placement: C and V sit directly under WASD, so they stay reachable without
  leaving the movement keys — chosen because survival may use them often.
- Rejected: **Space/Shift** for vertical movement (it takes away normal
  jump/crouch while flying); **E/Q** (E is `input/interact-key`).
- Consequence: Space (jump) and Shift (crouch) keep their ground meanings in
  every mode.
- **Space while swimming is a kick**: the swimming
  equivalent of a jump — a short burst of extra speed, not ascent. Steady
  vertical movement in a fluid stays on C/V, so the two never overlap.
- **No flight toggle**: in creative, C/V simply move you
  up/down at any time; there is no separate "start flying" action (so no
  double-tap Space either).
- Open:
  - Kick details: burst direction (movement intent vs. look direction),
    cooldown/stamina, and whether other media reuse it.

## input/per-hand-use — Mouse buttons map to hands, not verbs; items define their own actions

- Decision:
  - **Left mouse button = left hand, right mouse button = right hand**, for every
    character. Each button uses the item held in that hand.
  - **No verb is bound to a button.** "Break" is what a breaking tool does;
    "place" is what a block stack or placing tool does. Each item defines its
    primary action, and optionally a secondary action reached through a modifier
    (which modifier is open; see below).
  - **Character handedness** is a **setting** (default: right) that chooses which
    hand is the *main* hand. It does not change the button mapping (so the
    on-screen hands always match the buttons); it changes which hotbar the
    unmodified scroll wheel selects (`input/hotbars`) and which hand previews
    (`input/preview-mode`). Players who want a different physical mapping rebind it.
  - **Bare hand** punches / breaks slowly, so a hand with nothing selected is
    never on a dead button. **H** sets the main hand bare (`input/hotbars`).
  - **No two-handed items**: every item is one-handed.
    Minecraft has none, and they would complicate hotbars, swapping and preview
    for little gain.
  - **Swap (F) swaps the two hands' hotbars** (F, matching Minecraft's swap-hands key), not single
    items and not focus. Each hotbar travels whole, *with its current selection*
    (a slot, or bare), so logically arranged loadouts (a set of building blocks,
    a set of tools) stay together.
    - Consequence: to preview or wheel-select something on the off-hand hotbar,
      swap it into the main hand; its items then work from the other mouse
      button, consistent with buttons = hands.
    - Consequence: **H then F** leaves the off hand bare but also moves the
      loadouts between hands; **Shift+H** bares the off hand in place.
    - Swapping cancels any in-progress use (a held placement, a charging action).
  - **Secondary actions** are reached with **Shift (crouch) + click** on that
    hand's button (provisional), the Minecraft "sneak-use"
    convention. Items should avoid secondary actions where possible; the wrench's
    counterclockwise turn is the current one. Revisit if a better scheme appears.
  - **An item without a secondary action treats Shift+click as its primary**
   , so Shift never turns a click into nothing:
    a block stack places on Shift+click, which also lets a held X + Shift
    (tipping the ghost) place without letting go of Shift.
- Rationale:
  - With 1-ft voxels there are **placing tools as well as breaking tools**, and
    they need the same first-class treatment. Example: a **blend placer** that
    holds several block stacks and places a random pick from them, for textured
    blends. Under per-hand use it is just a held item whose action places; no
    special casing.
  - The natural working loadout (breaking tool in one hand, placing tool in the
    other) falls out without mode switches.
- **Deferred: bodies with other than two hands.** `sim/player-embodiment` lets
  the player soul bind other bodies (mechanical bodies, possessed creatures);
  how hotbars, swap and bindings generalize to more or fewer manipulators is
  left until bodies are designed. Keeping hands as an indexed set in the
  intention layer is still the cheap way not to foreclose it.
- The **wrench** (`world/block-orientation`) is a one-handed tool like any other.
  Its primary action turns the clicked block 90° clockwise about the clicked
  face's axis; its secondary action turns it **counterclockwise** about the same
  axis. Every turn is then undoable from the face already being looked at, which
  matters because the opposite face is often buried in a wall or floor.

## input/hotbars — One hotbar per hand; crouch + wheel selects the off-hand

- Decision:
  - **Each hand has its own hotbar.**
  - **Hotbars have no empty slots and at most 9 slots**,
    like Elden Ring's quick slots: a short hotbar gives quicker access, a fuller
    one holds more (and weighs more, `inventory/encumbrance`). Every slot holds
    an item. 9 matches a 3×3 pouch (`inventory/slots-and-pack`).
  - **Bare hand is a selection state, not a slot.** A hotbar's selection is either
    one of its slots or *bare*. **H** sets the main hand bare and **Shift+H** the
    off hand (Shift = off hand, as everywhere); any slot selection (wheel, number
    keys) leaves bare. The selection travels with the hotbar when hands swap
    (`input/per-hand-use`), so H then F also gives a bare off hand, but moves the
    loadouts between hands — Shift+H doesn't.
    An empty hotbar is always bare.
  - **Each hotbar sits in its own lower corner**: the
    left hand's at the lower left and the right hand's at the lower right, so
    the screen matches hands and mouse buttons (LMB left, RMB right). The
    corners belong to the hands, so a swap (F) moves the hotbars' contents
    between corners. Notes (proposed, open): each corner also shows that
    hand's planned action and its selection or `bare`; the bottom centre
    stays free for status (the held container's mode, preview on/off). A
    vertical stack per corner is an alternative if 9 horizontal slots crowd
    narrow windows.
  - **Placeholder slot visuals**: a block slot shows its
    16×16 texture scaled up crisply; items with no texture show one letter
    (P pick, W wrench, S shovel, C container); the active
    slot's name sits centred above it, nudged sideways only at a screen edge.
    Very much a placeholder for real item icons.
  - The **unmodified scroll wheel** selects on the **main-hand** hotbar.
  - **Crouch + scroll wheel** selects on the **off-hand** hotbar.
- Rationale:
  - **Crouch is a "careful" action**, and changing the off-hand loadout is a
    careful, deliberate act: the player can always stop to do it. Being crouched
    while switching is acceptable and even fitting.
  - **The sprint modifier was rejected** because it would make the main hand
    unswitchable while sprinting, and sprinting is exactly when the main hand most
    likely holds a weapon that needs changing.
  - **Alt is avoided as a game modifier**: this is a Linux-only game
    (`engine/cpp-vulkan-linux`), and many Linux window managers bind Alt+drag
    and some bind Alt+scroll, so the game would not reliably receive them.
  - **Number keys follow the same rule**: **1–9** select a
    main-hand slot, **Shift (crouch) + 1–9** select an off-hand slot. One rule
    for wheel and keys.
- Consequence: while crouched, neither the wheel nor the number keys select on
  the main hand. Accepted. Number keys are therefore reserved for hotbars; debug
  views live on the function keys (`input/debug-keys`).
- **Only Shift changes what the wheel and number keys select.** Other held keys
  (Ctrl sprint, X orientation) leave them on the main hand, so the main hand
  stays switchable mid-sprint. The modifier budget is settled in `input/bindings`.
- **0 is unbound**. Hotbars stop at 9, so 0 selects nothing; it is
  reserved, not assigned.
- **The wheel wraps**: scrolling past the last slot goes to
  the first and vice versa. Bare is not a stop on the wheel; H / Shift+H are the
  only ways into bare.
- Open: which slot the wheel lands on when leaving bare (e.g. the last selected
  slot).

## input/debug-keys — Debug views live on F1–F10: a few fixed roles, a launcher and favourites

- Decision: number keys belong to the hotbars (`input/hotbars`), so all debug
  views live on **F1–F10**. One concern per key held through v0.1 but can't
  hold once v0.2's views arrive (the mosaic, goals, the Dispenser, grace periods,
  Vicinities), so from v0.2.7 the keys have **fixed roles plus favourites**,
  over two kinds of view (`engine/telemetry`):
  - **world overlays,** drawn in the 3D view and toggled while moving
    (wireframe, orientation, lighting, targeting, translucency buffers,
    grid, chunk and node bounds, mosaic levels, Vicinity bounds);
  - **panels,** inspector windows (stats, input, edit set, GoalSource, the
    Dispenser).
- Map from v0.2.7 (refined as views arrive):

  | Key | Role |
  |---|---|
  | F1 | Stats dashboard, tabs per subsystem (frame and GPU passes, terrain, goals, Dispenser, simulation), over the counter registry |
  | F2 | Buffer view, cycles: lit → albedo → normal → depth → roughness/metallic → material ID (→ velocity once it exists); Shift+F2 cycles backward |
  | F3 | Overlay menu: toggle any world overlay; the last choice is remembered |
  | F4 | Inspect under the crosshair: block, chunk, TerrainNode and its goals |
  | F5 | Launcher: a searchable list of every panel and overlay |
  | F6–F10 | Favourites the developer binds to any panel or overlay |

  Default favourites are chosen in v0.2.7 for the views most used in v0.2's
  work (the mosaic, goals, bounds); v0.1's per-key assignments carry no
  weight, and its overlays live in the overlay menu and the launcher.
- **Scenarios open views by name** (the scenario's `view` field), so
  captures and bug reports reproduce what was on screen without depending
  on key bindings.
- **Layouts** are saved per developer (Dear ImGui's docking, its ini file
  enabled), `render/dev-ui`.
- Rationale: F-keys are otherwise unused by gameplay and reachable without
  leaving the mouse; fixed roles for the few most frequent actions, a
  launcher for everything, and favourites for the rest scale with the
  number of views, where one key per concern runs out.
- Avoided: **F11** (conventional fullscreen toggle) and **F12** (commonly taken
  by screenshot tools / overlays such as Steam's).
- **Release builds:** the views ship and are available in **creative mode**,
  like Minecraft's F3 screen. Open: whether, and how much of, them is
  available in survival.
- Laptop Fn-lock is a known annoyance, so the keys stay rebindable like
  everything else.

## input/bindings — The default keyboard and mouse map

- Every binding is rebindable; this is the default map. All entries are
  intentions (`sim/player-embodiment`), not raw actions.

  | Key | Intention | Kind | Entry |
  |---|---|---|---|
  | W A S D | Move | hold | — |
  | C / V | Ascend / descend (flight: along the camera's up; swimming, fluids, ropes per medium) | hold | `input/vertical-movement` |
  | Space | Jump; kick while swimming | press | `input/vertical-movement` |
  | Shift | Crouch; modifier: off-hand hotbar, Shift+F2 cycles backward | hold | `input/hotbars` |
  | Ctrl | Sprint | hold | — |
  | LMB / RMB | Use left / right hand's item; Shift+click = secondary action | press/hold | `input/per-hand-use` |
  | MMB | Pick block (creative: fill main hand with the targeted block) | press | — |
  | Wheel, 1–9 | Main-hand hotbar (max 9 slots; wheel wraps) | — | `input/hotbars` |
  | Shift + wheel, Shift + 1–9 | Off-hand hotbar | — | `input/hotbars` |
  | 0 | Unbound (reserved) | — | `input/hotbars` |
  | E | Interact with the targeted object | press | `input/interact-key` |
  | F | Swap the two hands' hotbars | press | `input/per-hand-use` |
  | R | Unbound (reserved) | — | — |
  | H / Shift+H | Set the main / off hand bare | press | `input/hotbars` |
  | Q | Toggle preview mode | toggle | `input/preview-mode` |
  | X | Orientation modifier: shows the placement ghost; while held, the wheel turns it about the clicked face's normal (up CW, down CCW), Shift+wheel tips it about screen-right made perpendicular to that normal (up = top away), and clicks place with the orientation chosen; the choice ends on release | hold | `world/block-orientation` |
  | M | Cycle the held container's placement mode (for now, `inventory/hand-pointers`) | press | — |
  | G | Set down the main-hand item on the targeted face (at your feet if none); for a pouch in hand, places the pouch itself (`inventory/hand-pointers`). Shift+G is reserved for dropping as a loose, floating entity, if ever wanted | press | — |
  | Tab | Inventory | toggle | — |
  | T | Chat | press | — |
  | Esc | Release mouse / menu | press | — |
  | F1–F10 | Debug views (fixed roles, launcher, favourites) | toggle | `input/debug-keys` |
  | F11, F12 | Left free (fullscreen, screenshot tools) | — | `input/debug-keys` |

- Rationale / notes:
  - **Ctrl is sprint**, not a tool modifier, so the orientation modifier moved to X.
  - **Alt is never a game modifier** (Linux WMs; `input/hotbars`). That also
    keeps sprint + F-keys clear of the Ctrl+Alt+F1–F7 virtual-terminal switch.
  - **No reload key:** not a shooter; nothing currently needs one.
  - **X sits next to C/V**, so rotating a ghost while ascending is awkward.
    Accepted: you rotate while hovering.
- Harness note: the v0.x free camera has no body. Ctrl = fast-fly; Shift exists
  only as the hotbar/F2 modifier; Space does nothing until a body exists.

## input/preview-mode — A toggleable, polymorphic preview mode instead of a build mode

- Decision:
  - **Preview mode is a toggle, not a mode you build in.** It only changes what you
    *see*: while it is on, each held tool continuously shows what its next action
    would do at the current target. Clicking still commits immediately, exactly as
    with preview off. Blocks can be placed and broken with preview off; nothing
    requires entering a mode.
  - **Polymorphic: each tool supplies its own preview.** A block stack shows a
    ghost of the placed block in its inferred orientation; a breaking tool
    highlights what would be removed; a **shovel** (digging and filling to a level) shows both at once
    (ghosts where it would fill, highlights where it would cut); the blend placer
    shows its ghost with the blend pattern.
  - **Exception (`tools/catalog`):** rough tools (sledge, axe) are
    unpredictable on purpose; their area of effect is never previewed. Preview
    shows only the target block and possibly the cut direction.
  - **The preview is the tool's actual planned edit.** Each tool exposes one pure
    function `(tool state, target, world state) → edit set`; preview renders that
    edit set and commit applies the same one. The preview therefore cannot
    disagree with the result, and no tool has a separate preview code path to
    drift. "World state" here means public chunk answers
    (`core/opaque-chunk-queries`), not raw voxel reads.
- Rationale:
  - With 1-ft voxels, tools that edit many voxels per action (leveling, fills,
    blend placement) are expected, and a many-voxel edit without a preview is
    guesswork.
  - A dedicated build mode (Satisfactory, Fortnite, Valheim's hammer) forces a
    mode switch before every build action. A view toggle gives the same
    information with none of the switching, and players who want a clean view
    turn it off.
- Consequences / fits:
  - **Diegetic cost shows up in the preview.** The edit set includes what it
    consumes (material the tool draws from inventory, tool wear), so a leveling preview can mark
    the part it cannot afford.
  - **Client/server:** the preview is computed client-side from resident state
    (`net/client-server-seam`'s bounded prediction). Commit sends the intention;
    the server recomputes the edit set authoritatively, and if the world changed
    in between, the server's result wins and the preview simply updates. An
    edit set spanning several chunk owners (a shovel levelling, a fill) commits
    all-or-nothing as a Vicinity transaction (`sim/concurrency-model`).
  - **Rendering:** ghosts are translucent geometry on the normal
    `render/transparency` path; removal highlights are an overlay. Both are
    presentation-only.
  - **Orientation ghost** (`world/block-orientation`): the modifier-held ghost is
    the block placer's preview shown momentarily. With preview mode on it is
    always visible; the modifier is then only needed to rotate it.
- **Preview always shows the main hand only**. Showing both
  hands at once was judged visually confusing. To preview an off-hand tool, swap
  it into the main hand (`input/per-hand-use`). The off hand still works with
  preview on; it just acts without a preview, as everything does with preview off.
- Toggle key: **Q** (`input/bindings`).
- Open: how multi-step tools (e.g. pick a leveling height or region, then apply)
  show and adjust their parameters.
