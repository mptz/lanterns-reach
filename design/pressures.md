# Survival pressures

The forces that make the emergent gameplay loop turn. Per `loop/deferred`, the
fine-grained loop is NOT engine-authored — it arises from the player decomposing
goals against a **resource-dependency graph + pressures**. This file records the
*pressures* (the edges that spawn intermediate goals); the dependency graph
(recipes, tools, transformation chains) is still deferred content. A pressure is
good when it reliably generates an intermediate goal ("I am cold → I need a heat
source → I need fuel → I need to gather/produce it").

## pressure/basic-survival — Heat, cold, food, drink as continuous survival pressures

- The baseline survival pressures, each a continuous need that forces goals:
  - **Cold** — the player loses heat; survived via **warm clothing, shelter, or a
    heat source** (ties to `pressure/hearth`). Seasonally amplified — winter is a
    recurring cold spike via `sim/seasons` (survival reads AUTHORITATIVE
    temperature = baseline + seasonal offset, per that decision).
  - **Heat / sun / desiccation** — the opposite pole: exposure to sun/heat causes
    overheating and **desiccation** (water loss), survived via **shade/shelter from
    the sun** and water. So shelter is bi-directional (cold AND sun), not just a
    cold-weather need — a hot dimension/biome is as hostile as a cold one.
    **Forgiving model:** no heatstroke; heat and
    sun instead make thirst faster, **up to 2–3×**, worse in a desert than a
    jungle. Shade and shelter reduce the multiplier. Same spirit as water and
    sleep: real pressure, without tedium.
  - **Food** — hunger; survived by gathering/farming/hunting. The crop path is
    itself seasonally gated (plant/harvest windows → `sim/seasons`), which is what
    makes "survive winter → plant crops in time" a goal chain rather than a flat
    need.
  - **Drink** — thirst / water; survived by finding or storing water. Couples with
    desiccation (heat raises water demand).
- **Why these four:** they are the irreducible metabolic pressures, and they
  cross-couple (heat raises thirst; winter raises food+heat demand), so they
  generate *compound* goals rather than four independent meters. Compound,
  interacting needs are what keep the early loop from feeling like four chores.
- **Food and drink model:**
  - **Food:** a player can go days without food. The game tracks an
    **invisible caloric balance** with **fat stores**: after fasting, a player
    must eat extra to replenish them. It is shown only minimally, as a basic
    sense of how well-nourished the player is, and perhaps as simple
    **cravings** ("you crave fat"), which steer players away from rabbit
    starvation (protein poisoning from a lean-meat-only diet).
  - **Drink:** water is needed about **once per 2 days**, like sleep
    (`first-session/sleep-and-day`). **No container is needed to drink from a
    fresh water source**; containers only matter for carrying water away
    from one.
  - Both fit the closed-form drains below: balances as functions of time since
    the last meal or drink.
- **Thresholds are soft** (`core/soft-thresholds`): needs degrade the player
  gradually (performance/health falloff), not a hard cliff at zero.
- **Needs are closed-form drains** (`core/closed-form-first`): each need is a
  function of elapsed time from an anchor, re-anchored when the player eats,
  drinks or warms up — nothing is stepped per tick.
- Open (`open-questions`, `pressure/survival-tuning`): the actual rates/curves,
  how damage/impairment scales, and — the real work — the DEPENDENCY CHAINS each
  need bottoms out in (what clothing/shelter/food/water production requires),
  which is the deferred resource-graph content.

## pressure/mobs-night — Hostile mobs attack; night is the recurring attack cadence

- Hostile mobs attack the player and their space, creating a **defense** pressure.
- **Night is the attack CADENCE even though spawning is not light-gated.** This is
  compatible with `mobs/spawning` (which decouples spawn LOCATION from light,
  keying it to unreality instead): night is a **time-modulated pressure spike**,
  not a light-based spawn gate. Mobs still spawn by the unreality rules; night
  raises their aggression / numbers / approach — a predictable rhythmic deadline
  ("prepare before dark"), which is one of the most reliable goal generators
  (a recurring deadline forces a recurring prep loop). The player never infers a
  light-level spawn rule, preserving `mobs/spawning`'s anti-coupling intent.
- Defense is the multi-lever set from `mobs/spawning` (stabilization field,
  fortification, repellents) plus **blocking hostiles from your space**
  physically (walls/barriers → `pressure/defensible-space`).
- Open: the day/night aggression curve; how night interacts with the distance
  window and unreality terms in `mobs/spawning`.

## pressure/hearth — A reality-anchored base begins with a hearth that must be fed

- The early-game base is **reality-anchored** — it is the first reality-
  stabilized region (`persist/diegetic-persistence`), and the anchor begins with a
  **hearth**. The hearth is a *maintained, fed* structure (consumes fuel), making
  it a continuous upkeep pressure, not a one-time build.
- **The hearth unifies several pressures into one focal object** (which is why it
  is the natural first base element):
  - it is a **heat source** (answers `pressure/basic-survival` cold),
  - it is an early **light source** (answers `pressure/light`),
  - feeding it is an instance of `core/diegetic-cost` (upkeep is a visible in-world
    act, and plausibly feeds the stabilization that anchors reality — a fed hearth
    = a held base).
- Because it must be fed, it spawns a **fuel dependency chain** (gather/produce
  fuel → keep the hearth alive → keep warmth/light/anchor), one of the first loops
  the player enters.
- **First-session stabilization chain (decided):**
  1. **Campfire** — no precursor, basic supplies only; stabilizes a small
     area for a limited time (burns down on world time).
  2. **Hearthstone** — made by heating a stone in the campfire; can be
     fueled; a bit more powerful than the campfire (still consumes fuel and
     burns on world time).
  3. **Lantern** — mine quartz underground (or another crystal with the right
     properties, a material class: `crafting/material-classes`), heat it in
     the hearth to make resonant quartz, then craft a real lantern that
     stabilizes a small area
     **indefinitely**. This is the short-range, NON-consumable stabilizer, at
     roughly the progression depth of smelted iron in Minecraft — a good
     first-session goal.
- **Clear, then anchor — the pattern behind the chain:**
  - **Consumable fires repel monsters** as well as stabilizing for a limited
    time (campfire, hearthstone; later the bonfire). **Lanterns stabilize only**
    and **require a safe range to activate**: no hostile mob within the
    lantern's radius over an activation window. That is a level-triggered check
    (`core/level-triggered`) on timestamped level state ("no hostile within r
    since T"), so a mob wandering in simply resets the window — no
    notification needed. In the first session the campfire's repellence is what
    makes the lantern's safe range achievable, so the first-session stopping
    point gets no new gate (`ethics/no-appointment-mechanics` condition).
  - **Mid game, the same verb at a larger scale:** a **bonfire** clears a larger
    safe space (time-limited stabilization + repellence), so that a **grand
    lantern** (stabilization only, permanent) can be activated there. Adding
    something to a bonfire can temporarily **banish** monsters — for mobs that
    can't flee (trapped, cornered), so they don't block the lantern's activation.
  - **After activation, safety is the player's job:** lanterns don't repel, and
    light has no gameplay role (`light/visibility-only`; a lantern's visible
    light is presentation only). Stabilization reduces spawn pressure inside
    the area (`mobs.md`), but monsters can still walk in, so the player builds
    walls (`pressure/defensible-space`) or uses other methods. The door is left
    open for pure-repellent items later in the progression.
  - **Mechanisms are existing kernels** (`sim/process-kernels`): repellence and
    banishment as relaxation-field / timed effects read by the mob decision
    layer; burn-down as the timed container; activation as a level check.
- **Burn-down runs on world time, including while a multiplayer player is
  offline — a deliberate, recorded choice** relative to
  `ethics/no-appointment-mechanics` and `persist/presence-not-absence`: it
  keeps the excitement and urgency of a Minecraft-like "first day". The
  release valve is the lantern: a player who wants to step away reaches
  non-consumable stabilization within the first session. Burn-down is a
  closed-form timed container on absolute time (`sim/process-kernels`,
  `core/closed-form-first`), so it needs no ticking.
- Open (`open-questions`, `pressure/hearth-mechanics`): the fuel
  types and their dependency chain; the radii and burn times of each tier.

## pressure/defensible-space — You must physically block hostiles from your space

- Beyond the soft levers in `mobs/spawning`, the player must **physically wall off
  their space** — barriers/fortification that hostiles cannot cross. This is the
  "fortification" lever made concrete, and it spawns a build + material goal
  (walls need materials → extraction → tools).
- Fits `core/chunk-locality` / `core/opaque-chunk-queries`: "is my space sealed"
  is a connectivity/enclosure question a chunk/region answers via face-
  connectivity queries (is there an open path from outside to inside), not a
  voxel scan — the same visibility-graph machinery, asked for enclosure instead
  of transparency.
- Open: what counts as "sealed," whether enclosure is checked by the
  connectivity digest, how mobs path against barriers (ties to `sim/3d-pathing`).

## pressure/light — Light sources are needed; early-game light is consumable

- The player needs **light sources** to act at night / underground. Crucially,
  **early-game light is CONSUMABLE** (torches/fuel that burn down), making light a
  *recurring resource pressure* rather than a free permanent placement — an
  intermediate-goal generator ("I need to go dark-places → I need light → light
  burns → I need more fuel").
- **This is a pure GAMEPLAY-resource pressure and does NOT re-introduce light as a
  gameplay mechanic** — consistent with `light/visibility-only`. Light's RENDERING
  role stays presentation-only; what is authoritative here is the *consumable
  resource* (fuel/torch item) and the player's *ability to see to act*, not a
  light-LEVEL field that gates spawning or growth. The resource is gameplay; the
  glow is presentation. (Contrast Minecraft, which overloaded a light-LEVEL field
  with gameplay meaning — exactly what `light/visibility-only` rejects.)
- Progression hook: early light is consumable; later light is plausibly permanent
  (a progression unlock), paralleling the stabilization-tier arc in
  `persist/diegetic-persistence`. The first permanent step is the lantern in
  `pressure/hearth`'s first-session chain, which
  stabilizes a small area indefinitely. It emits visible light because that
  matches the name, but **light has no gameplay role**
  (`light/visibility-only`): the light is presentation
  only, so a lantern doesn't keep monsters away.
- **Torches burn down on world time, including while a multiplayer player is
  offline** — the same deliberate choice as the hearth (first-day urgency,
  recorded against `ethics/no-appointment-mechanics`; the lantern is the
  release valve). Burn-down is a closed-form timed container
  (`sim/process-kernels`), not ticked.
- Open: the consumable light economy (burn rates, fuel).
