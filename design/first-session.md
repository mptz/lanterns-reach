# First session

An inventory of what the first sustained engagement with the survival game
involves, kept deliberately small. It exists to stop the early game from
bloating: anything added here must earn its place against the budget below.

## first-session/scope — What "first session" means, and its priorities

- **"First session" is a timeline, not a sitting:** the first sustained
  engagement with the survival game. It need not be one marathon session:
  decay runs on a scale of real months of `WorldTime` (real time minus pauses,
  `coords/typed-time`) and doesn't start at all for real days
  (`persist/decay-curve`), and worlds are pausable
  (`persist/presence-not-absence`).
- **Top priorities, in no fixed order:**
  1. basic survival tooling;
  2. protection from hostile mobs;
  3. light for night and underground (visibility only, `light/visibility-only`);
  4. at least one magical pouch;
  5. a lantern (`pressure/hearth`'s campfire → hearthstone → lantern chain).
- **Food is not a first-session gate:** you won't die from a day without
  eating (`pressure/basic-survival`'s soft thresholds). Foraging and cooking
  are available, but nothing in the list requires them.
- **The stopping point is the lantern** (`ethics/no-appointment-mechanics`):
  a player who has a lantern and a walled space can step away with nothing
  urgent burning.

## first-session/inventory (draft) — The items and recipes, and the order they force

- **Materials** (all from the surface or shallow ground; `geology/essential-minerals`
  keeps them statistically likely near any spawn): sticks and wood; plant
  fibre (cordage); flint or chert; stone; small-game hide (or more fibre for
  the no-kill path); ochre; quartz (shallow underground); fuel.
- **Items, about 12:**

| # | Item | Made from | Serves | Notes |
|---|---|---|---|---|
| 1 | Cordage | plant fibre | everything bound or sewn | twisted by hand |
| 2 | Knapped knife | flint/chert + stick + cordage | survival tooling | the first knife (`tools/catalog`): cuts fibre, skins, carves |
| 3 | Digging tool | stick or bone/scapula + cordage | survival tooling | the first `shovel`; Neolithic diggers used ox shoulder blades |
| 4 | Antler or knapped pick | antler or flint + stick + cordage | survival tooling, quartz | the first `pick`; Neolithic flint mines used red-deer antler picks |
| 5 | Fire starter | bow drill (stick + cordage) or flint and pyrite | light, protection | an item, not a tool |
| 6 | Campfire | stones + wood | protection (repels, stabilizes briefly), light | `pressure/hearth` tier 1 |
| 7 | Torch | stick + fibre + fat or resin | light underground | consumable; burns down |
| 8 | Snare | cordage | hide | small game only; skipped on the no-kill path |
| 9 | First pouch | rawhide or woven fibre + ochre pattern | magical pouch | `inventory/pouch-progression`; needed before any blocks can be carried |
| 10 | Hearthstone | stone heated in the campfire | protection, the lantern step | `pressure/hearth` tier 2 |
| 11 | Resonant crystal | quartz (or another qualifying crystal) heated in the hearth | lantern | `crafting/material-classes` |
| 12 | Lantern | resonant crystal + housing | lantern, light, permanent stabilization | housing and panes are material classes: wood, bone, clay or soapstone; horn, mica or scraped hide; no glass yet |

- **The order it forces** (dependencies, not a script):
  - Knife first (cordage + flint), because it cuts the fibre and hide everything
    else needs.
  - **Pouch before walls:** body slots hold one block each and blocks weigh
    (`inventory/hand-pointers`), so the first pouch is what lets a player carry
    blocks in bulk and wall a space (`pressure/defensible-space`); a few blocks
    can be carried before it. Until then, the campfire's protection circle
    (`first-session/campfire-protection`) is the night-one protection.
  - Torch and pick before quartz, which lies underground.
  - Campfire → hearthstone → resonant quartz → lantern, as decided.
- **Weapons are not required:** protection comes from campfire repellence and
  walls, so a pacifist run (`loop/opt-in-challenges`) passes the whole list.
  A flint-tipped spear can be an optional extra, outside the budget.
- Open questions: `first-session/open`.

## first-session/campfire-protection — Early protection is a fed circle around the campfire

- **Very early protection from hostile mobs focuses on low-magic
  consumables added to the campfire**: they make a **circle of
  protection that dwindles unless kept fed**. Walls come later, because wood
  is not guaranteed near a spawn and blocks need a pouch, and no heavy tool
  (axe) is in the first session.
- This sharpens `pressure/hearth`'s "consumable fires repel monsters": the
  repelling radius is sustained by what you feed the fire, and shrinks as it
  burns down. A closed-form timed container (`sim/process-kernels`), so it
  needs no ticking.
- **Ingredients draft (proposed), from folk protective traditions:** dried
  aromatic herbs for smoke (juniper, sage, mugwort), salt, rowan wood, iron
  scraps (or ochre, already in the first-session materials). Mundane,
  gatherable, low magic; stronger additives later lead to the mid-game
  banishing additive for the bonfire.
- **Fuel without guaranteed wood:** the campfire burns whatever the land
  offers: wood where there are trees; dry grass bundles, dung, peat or
  driftwood elsewhere (`geology/soils-and-sediments`: peat).
- Open: the ingredient list and how each affects radius and duration;
  whether the circle is visible (a ring of smoke, scorched ground); how it
  composes with the campfire's own short stabilization.

## first-session/resolved — Settled first-session questions

- **The cutting tool is the knife** (`tools/catalog`).
- **The axe is not needed for initial survival.** The knife cuts wood slowly,
  wood isn't guaranteed near a spawn, and early protection doesn't rely on
  building (`first-session/campfire-protection`).

## first-session/sleep-and-day — Day length, and sleep paced like a real-world break

- **A day is 48 real minutes**: 1 real minute is 30 in-game minutes. A game
  that starts at **midsummer** has a **2:1 day-to-night ratio** (32 minutes of
  day, 16 of night), and the ratio then follows the seasons (`sim/seasons`).
- **Sleep:** a player needs **3 in-game hours of sleep every 2 nights** to
  avoid any ill effects. That is 6 real minutes per 96, or 3 per night. Below
  that, effects begin softly (`core/soft-thresholds`): visual effects,
  confusion, and susceptibility to disease.
- **Why this pacing**: the needs for sleep and water arrive about
  every 96 real minutes, within one sustained session and on the order of
  an hour of play, the cadence at which a player should consider a real
  leg-stretch and eyestrain break, so the character's need becomes the game's
  prompt (`ethics/wanting-coupled-to-liking`, `ethics/clean-stop`).
- **Notes (proposed):**
  - A sleep is break-sized (a few real minutes), so most of the night stays
    playable; there is no night-skip (`sim/dreaming-designation`).
  - Sleeping is a natural moment to step away: the body sleeps inside the
    protection circle, and dreaming (designation) is optional.
  - Because the need is spread over two nights, **the first night does not
    require sleep**, so a bedroll isn't a first-session gate.
- Open: how sleep is accrued (any time, or only at night); the exact ramp of
  ill effects; multiplayer, where players sleep at different times.

## first-session/onboarding — No text or scripted tutorial; per-world advancements

- **No texty help pages and no scripted tutorial.** Minecraft's in-game help
  is practically never used, wikis fill that role, and scripted tutorials fit
  a sandbox poorly.
- **Per-world advancements** carry onboarding, with **just enough
  explanatory text** to point at the next thing worth trying (e.g. "Make a
  knife", "Keep the circle fed through a night", "Light a lantern"). The
  first-session priorities (`first-session/scope`) are the natural first
  advancements.
- **Advancements are a tree, have no in-game effects, and can be turned
  off**. When on, **"significant" advancements are broadcast to all
  players** in the world.
- Consequences (proposed): with no effects and no meta-progression they
  inform rather than compel (`ethics/no-fomo`, `ethics/no-variable-ratio-core`);
  they read authoritative state with level-triggered checks
  (`core/level-triggered`).
- Open: the tree and its wording; which advancements count as significant;
  how they are shown (a quiet notification, a journal).

## first-session/biome-stress-test (draft) — Playing the first session through different settings

- **Settings differ in difficulty on purpose**: some starts are
  very difficult challenges. **A mountaintop start's priority is getting to
  lower ground**, not surviving on the summit. A truly inhospitable start can
  always be abandoned for a new world (`geology/essential-minerals`).
- **Forgiving pressures**: heat raises thirst up to 2–3× instead of
  causing heatstroke (`pressure/basic-survival`); cold still needs
  protection.
- **Some settings simply don't suit some playstyles, and that is expected**:
  the arctic is not a good place to be a pacifist or a vegan. An
  arctic pacifist or vegan run is a challenge for masochists, not something
  the material classes must make comfortable. No-kill paths should exist
  broadly (`loop/opt-in-challenges`), but not necessarily in every setting.
- **Walk-through** (design terms, not in-game biomes):

| Setting | Main threats | Tight materials | Difficulty |
|---|---|---|---|
| Temperate forest | none severe | flint may be buried under soil | easy |
| Grassland / steppe | wind, cold nights | wood (handles, fuel) | medium |
| Boreal forest | cold | ochre less common | medium |
| Tropical rainforest | heat (thirst), humidity, disease | knappable stone under deep soil; dry fuel | medium |
| Coast | sun (thirst), wind | fresh water; wood only as driftwood | medium |
| Swamp / bog | disease | stone of any kind | hard |
| Hot desert | sun and heat (thirst ×3), water | wood, water | hard |
| Alpine | cold, wind, sun | wood, fuel, fibre | hard: descend |
| Tundra / arctic | severe cold | wood, fibre, fuel | hardest; pacifist or vegan is a masochist challenge, by intent |

- **Gaps found, and the alternatives that close them** (now material classes,
  `crafting/material-classes`): handles without wood; fuel without wood (dung,
  peat, grass, fat lamps in carved soapstone); knife stone beyond flint;
  soot as a universal pigment; lantern panes and housings without horn or
  wood (mica, clay, soapstone); non-kill cold protection (plant padding,
  cattail down, shed wool, snow, earth and rock shelters); sun protection
  (woven hats, shade shelters, overhangs, dugouts, travelling at night);
  water containers for coasts, deserts and snow melt.
- **The lantern's crystal is a material class**: quartz, calcite,
  selenite, agate, fluorite or celestine, made resonant in the magical
  hearth (`geology/resonant-crystals`), so every kind of ground has a source.
- **Geology additions suggested:** soapstone (soft, carvable; lamps and
  housings) and mica (panes) to `geology/rock-blocks` and the mineral list.

## first-session/spawn-lantern (proposal) — A lantern already standing at world spawn

- **A world setting, not the default**, like Minecraft's Bonus Chest:
  an option to make a fresh world a bit more forgiving with a **preexisting
  lantern (lamp) at world spawn**. Making your own lantern
  becomes less urgent: it's something you need **before you strike out to
  start your own base**, not a race against the first night.
- **Atmosphere:** a bit of a Narnia feel, like the lamp-post in the
  Lantern Waste, subtly suggesting that **you have arrived from another
  world**.
- Notes (proposed):
  - It changes the shape of the first session (`first-session/scope`): the
    lantern stays a goal, but the stopping point becomes "a lantern of your
    own at a base of your own"; the spawn lantern is a safe harbour to return
    to meanwhile. The burn-down exception (`ethics/no-appointment-mechanics`)
    is less needed with it, since a player can always step away near spawn.
  - It fits `core/low-fantasy` as long as nothing explains it: the world isn't
    about the player, it just hints that someone came this way before. It
    could be a quiet thread to Aether or Enigma (`worldgen/dimension-cosmology`)
    without ever being answered.
  - As a world setting, it sits naturally beside the opt-in challenge settings
    (`loop/opt-in-challenges`), at the forgiving end.
- **Decided details:** it **doesn't decay** (stabilizers don't);
  there is **just one**, shared, not one per arrival; it **can be broken**.
- **Breaking it is nothing special**: it yields
  whatever breaking that kind of lantern ordinarily would. (Considered and
  rejected: having it crumble so that making a lantern stays a goal.
  It's an opt-in setting at the forgiving end anyway.)

## first-session/open (draft) — Open questions for the first session

- **Sleep details** (`first-session/sleep-and-day`): how sleep is accrued and
  how ill effects ramp.
- **Drink:** water is needed about once per 2 days (`pressure/basic-survival`),
  and a player can drink straight from a fresh water source without a
  container. Open: whether fresh water is reliably within reach of
  a spawn by day two, or the first session should include a container (a
  hide water skin, a clay pot, a gourd) for dry settings.
- **Lantern housing:** wood frame with horn panes (a historical "lanthorn")
  avoids needing glass; alternatives are a pierced clay or stone housing.
  Which, and from what materials?

- **Food:** not a gate (days without food are survivable,
  `pressure/basic-survival`); how much foraging and cooking a first session
  naturally includes.

## first-session/budget (draft) — A cap on first-session content

- **Budget:** at most about **15 distinct crafted items** (the list above has
  12) and **no crafting station** beyond the campfire and the hearthstone. A
  new first-session item must displace one or fit under the cap.
- **Test** (with `geology/essential-minerals`' spawn sampling): a new player
  reaches the lantern within a first sustained engagement, typically a few
  hours of play spread over one or more sittings, without needing a guide.
- Open: the exact cap, and how play time is measured in playtests.
