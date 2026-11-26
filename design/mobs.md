# Mobs

Hostile-mob spawning and suppression, and wildlife. Mostly TBD; the governing decision is the
decoupling from lighting.

## mobs/spawning — Spawning keyed to "unreality"; suppression is multi-lever, decoupled from lighting

- Decided direction:
  - **Spawning is NOT gated by lighting** (`light/visibility-only`). The
    Minecraft "light level suppresses spawns" coupling is removed.
  - Mobs spawn in **unstabilized "unreality"** — areas with low/zero reality
    stabilization-field strength (`persist/diegetic-persistence`). The
    stabilization field is the authoritative "how unstable is here" signal, read
    by both decay and spawn logic (one source of truth for unreality).
  - Keeping mobs at bay is deliberately a **combination of levers**, not a single
    one, to avoid re-coupling spawning tightly to any one system the way
    Minecraft coupled it to light:
    1. reality stabilization field (reduces spawn pressure),
    2. defensive fortification (physical/structural),
    3. repellent resources that drive off already-spawned mobs (candidate
       mechanism: a relaxation-field kernel, a repellence field read by the
       mob decision layer, `sim/process-kernels`, rather than bespoke code).
       Decided carriers (`pressure/hearth`): consumable fires
       (campfire, hearthstone, bonfire) repel; an additive to a bonfire can
       temporarily **banish** mobs that can't flee; lanterns do NOT repel
       (stabilization only, and light has no gameplay role). Pure-repellent
       items later in the progression are left open.
  - **Distance-from-player term (soft, per `core/soft-thresholds`).** Spawn
    probability is modulated by distance to the player as a smooth **window/bump**
    (product/difference of two sigmoids): suppressed point-blank (no unfair
    point-blank hostile spawns, no pop-in next to the player), peaked at
    mid-range, falling off far away (don't spawn mobs the player will never
    reach). Evaluated against an **approximate / stale player position** — it
    explicitly does NOT need tick-accurate player coordinates, because the smooth
    gradient makes a small position error an imperceptible probability error
    (the `core/soft-thresholds` property in action).
- **Field POTENCY → mob TIER (the "immune response").** The stabilization field
  is read on a second axis: its *location* gates where mobs spawn (above), and
  its *strength/potency* gates *what tier* of mob spawns at the stabilizer's
  frontier. Larger, more potent reality stabilizers provoke larger, more powerful
  mobs — as though reality has an **immune system** reacting to the intrusion of
  stabilization. Late-game bases therefore provoke late-game mobs.
  - This is a diegetic **auto-scaling difficulty loop**: the reward for
    progression (a potent stabilizer) carries its own matched antagonist, so the
    challenge curve tracks player power automatically, driven by the same
    authoritative variable (field potency) the player is investing in — no
    external leveling system needed. Reads existing authoritative state on a new
    axis; not a new system.
  - **Thematic unification.** Passive decay (reality reverting toward seed) and
    active mobs (reality attacking) become two expressions of ONE antagonist:
    *the world resisting stabilization*. This gives the whole persistence/decay
    mechanic a diegetic motive — stabilizing reality is a transgressive act, and
    both the decay pressure and the mobs are the world pushing back.
- **INVARIANT: mobs threaten the PRESENT player; they are never simulated in the
  player's absence.** The purpose of mobs is to threaten the player (and
  secondarily to damage the base *while the player is there* to defend/witness),
  NOT to overrun territory while the player is away. Mob pressure is a property of
  *loaded, player-present* space only — an unloaded region is not ticking, so
  nothing spawns or paths there (`core/chunk-locality`; same lazy/on-load posture
  as decay, which is closed-form evaluation on load, `core/closed-form-first`).
  A Vicinity's frozen chunks initiate nothing either
  (`sim/residency-levels`), so spawning and mob AI run only in simulated
  chunks.
  - **A mob left in a frozen chunk** (it walked into one, or its chunk
    was demoted) would otherwise sit frozen. Candidate: it
    despawns on demotion, a level-triggered check run as the chunk leaves
    its Vicinity (a frozen chunk materializes no checks), consistent with
    "never simulated in absence". Open with the demotion path (U13) and
    entities at seams (U5) in `sim/execution-use-cases`.
  - The intrusion the immune response reacts to is the player's **presence inside
    stabilized reality** — holding reality open by being there — not the
    stabilizer existing in the abstract. A potent base provokes potent mobs *when
    you are home*, which is exactly when a fight is wanted and you are there to
    have it.
  - **The peoples' lanterns provoke it too** (`peoples/sojourners`): they
    are more local sources in the same field, and the response is local
    everywhere (each place's potency sets its tier, whichever player or
    people holds it). It still acts only while a player is present
    (nothing is simulated in absence), so a camp is besieged only when
    someone is there to see it.
  - **Monsters attack sapients, not wildlife:** players and the animal
    peoples, never non-sapient animals (`mobs/wildlife`).
  - **Consequence this locks in:** this places the immune-response coupling on the
    *challenge* side of `persist/presence-not-absence` by CONSTRUCTION, not by
    tuning. There is no code path where provoked mobs erode a base during absence,
    because there is no mob simulation during absence. The appointment-mechanic
    failure mode is structurally unrepresentable, not merely avoided by numbers.
  - **Absence consequences collapse to decay only.** While present there are two
    antagonists (decay-pressure + mobs); while absent there is exactly one (decay,
    which stabilization holds off and pause freezes). Returning to a decayed-if-
    unstabilized base is the only absence consequence; returning to an *overrun*
    base is not a thing. Future "should mobs damage unattended bases?" is already
    answered: **no.**
- Open (see `open-questions`): the spawn model itself (rates, conditions, which
  mobs, how stabilization strength maps to spawn pressure, the exact distance
  window shape), the fortification system, and the repellent-resource system.
  Already decided for the spawn model: spawning and despawning are
  level-triggered checks (population below target density, `sim/random-ticks`
  catalog); candidate framing: a hazard-process kernel (`sim/process-kernels`)
  whose rate is set by field strength × the distance window.
- **Alignment shifts pressure and tier.** Stabilization stays the base
  mechanism: it alone decides where monsters can spawn. The `alignment`
  field (`worldgen/regional-fields`) then scales spawn pressure and shifts the
  tier mix, so evil country spawns more monsters and worse ones, and good
  country fewer and lesser ones. Monsters and wildlife
  (`mobs/wildlife`) are independent threats: monsters track `alignment`,
  wildlife tracks `savagery`.

## mobs/wildlife (draft) — Animals are ordinary ecology; savagery scales how they behave, not what they are

- **Wildlife is not unreality.** Animals are part of the world, so
  stabilization neither suppresses nor provokes them; they follow ecology
  (the fields that feed feature placement, `worldgen/field-driven-features`)
  and the `savagery` field. They are a real threat at normal savagery
  (`core/low-fantasy`'s deadly animals).
- **Species sets the nature; savagery scales the behaviour.** A hippo is
  always a hippo and always dangerous up close. Where `savagery` is low it
  tolerates the player nearer before it reacts and breaks off sooner; where
  it is high it has a wider aggression range, presses longer, and may be
  bigger and harder to fight. Scaled traits, all smooth in `savagery`
  (`core/soft-thresholds`):
  - aggression range (how close before it reacts),
  - persistence (how long before it breaks off),
  - size and toughness,
  - density (how many there are).
- **Open:** whether savagery also shifts the species mix (more predators)
  or only scales individuals; how wildlife despawns and repopulates
  (presumably the same level-triggered population checks as monsters);
  whether hearth repellents (`pressure/hearth`) work on animals.
