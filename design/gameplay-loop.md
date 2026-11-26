# Gameplay loop

The moment-to-moment game: what the player does, the core loop, pacing,
progression. Deliberately deferred — this file records the deferral AS A CHOICE
(not an absence) and flags which "engine" decisions are secretly loop decisions.

## loop/deferred — The gameplay loop is deliberately deferred; the engine is loop-generative by design
- **Open:** the loop's detail (the deferral itself is intentional).

- Decision: We are NOT designing the beat-by-beat gameplay loop yet. The working
  bet is that the right *engine features* will make a satisfying loop
  concoctable later, so effort goes into the substrate first. This file exists so
  that choice is explicit and findable, not a gap someone later mistakes for an
  oversight.
- **Why the bet is sound for THIS design (not a blank check):** the engine's core
  mechanics are already the raw materials of sandbox loops, so loops are expected
  to *emerge from* the substrate rather than be bolted onto an indifferent one:
  - `persist/diegetic-persistence` is already a loop skeleton — build → it decays
    → stabilize to keep it → stabilization is consumable and progression-gated.
    A full investment/tension cycle; it needs numbers and a reason to range out.
  - `core/diegetic-cost` means the game's *costs are already legible gameplay*
    (stabilize, seal, mint) — a loop built on them inherits legibility for free.
  - `sim/logic-machines` + souls + `data/content-bundles` is a creative-expression
    loop (build behavior → distill → animate → share) that stands on its own.
- **The caveat (held explicitly): the engine is loop-GENERATIVE but not
  loop-NEUTRAL.** It has already pre-committed the loop's *shape* —
  investment-and-decay survival with diegetic authorship — even though the detail
  is open. That is deliberate (a strong opinion beats a blank check), but it means
  the loop is not a fully free variable to be chosen later; its genre is set.
- **Loop knobs hiding as engine open-questions** — these are secretly
  gameplay-loop decisions and should be recognized as such when their forks come
  up (none forces designing the loop now; they bound it):
  - `persist/decay-curve` — the decay rate / "meaningfully modified" threshold is
    the loop's PACING HEARTBEAT (how punishing vs. forgiving the game feels), not
    a storage detail.
  - `mobs/spawning-model` + `mobs/suppression-systems` — the survival-pressure
    loop.
  - `data/authoring-vocabulary` — the scope dial on the creative-expression loop's
    ceiling.
- **The failure mode to avoid (the only way the bet goes wrong):** deferring until
  the engine is "done" can over-build substrate the eventual loop never touches,
  then discover the loop wanted an affordance that was never prioritized.
- **Cheap insurance (not a design doc — a sanity tether):** keep a loose running
  list of the one-sentence loops being imagined, so that when an engine decision
  has a fork it can be checked against them. Keeps the intuition falsifiable
  instead of purely trusted. (Parked here; populate ad hoc.)
- **The loop is EMERGENT from a resource-dependency graph + pressures (the genre's
  model, à la Minecraft/DF):** the engine supplies no scripted loop; the player
  decomposes a chosen goal against material dependencies and pressures ("survive
  winter → plant crops → clear+defend a plot → …"). This locates the deferred
  work precisely: not "design a loop" but **design the dependency graph and make
  the pressures bite.** A shallow graph or toothless pressures = a loop exhausted
  in an afternoon; depth + friction is the design work the engine makes *possible*
  but does not *supply*.
  - First pressures now recorded in `design/pressures.md` (`pressure/*`): basic
    survival (heat/cold/food/drink, bi-directional shelter), mobs with a night
    cadence, the fed hearth, defensible space, consumable light. Several pressure
    edges were already implied by engine decisions made for other reasons
    (`sim/seasons` winter deadline, `persist/diegetic-persistence` decay upkeep,
    `mobs/spawning` unreality).
  - The dependency GRAPH itself (recipes/tools/transformation chains the pressures
    bottom out in) remains deferred — see `loop/dependency-graph` in
    `open-questions`.

## loop/opt-in-challenges — Player-controlled, game-enforced challenge and behavior settings

- Decision: players can opt into **challenges and behavior preferences that
  the game then enforces**, rather than relying on self-discipline. Example: a
  **pacifist playthrough** (no killing), supported by the game.
- Consequences:
  - Progression must offer **no-kill paths** where a setting forbids killing:
    for example the woven fibre pouch beside the rawhide one
    (`inventory/pouch-progression`). Designing no-kill alternatives is a
    check on recipes generally, not a special case.
  - **Not every setting must suit every challenge**:
    a pacifist or vegan run in the arctic is expected to be extremely hard,
    a challenge for masochists (`first-session/biome-stress-test`).
  - Settings are chosen by the player (per world, presumably at creation, with
    rules about changing them later), and enforcement is by the game, so a
    claimed pacifist run is real.
- Open: the list of settings (pacifist, vegetarian, no extradimensional
  storage, hardcore death, ...); whether settings can be relaxed mid-world;
  how enforcement works in multiplayer (per player or per world).

## loop/speedrunning (draft) — Support speedrunning and competitive races without bending the game around them
Not a priority, but cheap to keep possible if the engine's choices account
for it early. Competitive play in a shared dimension is `dimensions/discordia`;
this entry is about runs and races in the ordinary game.

- **Game-enforced constraints are the categories.** The opt-in challenges of
  `loop/opt-in-challenges` (pacifist, vegetarian, hardcore …) double as run
  categories, and because the game enforces them, a claimed category is real.
- **A built-in high-precision timer** for a variety of objectives:
  - **two clocks:** in-game time (`WorldTime`, which excludes pauses,
    `coords/typed-time`) and real time; load time can be reported separately
    so slow hardware isn't penalized;
  - **objectives as triggers from authoritative events**, not from client
    state: first lantern lit, a dimension reached, a depth reached, an
    item crafted. Each is a split, so runs of any length can be timed
    against any chosen sequence;
  - **splits and comparisons in the HUD**, off by default.
- **The fastest movement is the obvious one:** running forwards, without
  jumping, looking in one direction. No speed from strafing, jump chains,
  bunny-hopping, diagonal stacking or view-angle tricks, so runs stay easy
  to watch and the movement stays realistic (`core/realistic-in-kind`).
  Concretely: input direction is normalized; a jump puts the body's energy
  into going up, not forwards, so it slows you; and looking too far away
  from the direction of travel costs some speed, since nobody runs full
  tilt while looking sideways (the threshold and penalty are open). Swept
  collision (`sim/swept-collision`) and f64 positions
  (`coords/f64-authoritative`) already remove the classic clipping and
  far-lands precision exploits.
- **Skill should be route-finding and knowledge, not luck.** Several existing
  decisions already point this way: essential minerals are geologically
  ubiquitous (`geology/essential-minerals`), ore is resolved by the
  authority rather than guessed from terrain (`geology/ore-sites`), and
  core progression has no variable-ratio rewards
  (`ethics/no-variable-ratio-core`). Per-world recipes
  (`crafting/per-world-recipes`) make discovery part of a random-seed run
  and memorization part of a set-seed one; both are legitimate categories.
- **Set-seed and random-seed runs.** A set-seed race needs the same world
  for every runner, but worldgen is only reproducible on the same machine,
  binary and SIMD level (`worldgen/cartographer`). A competitive setting
  can pin the SIMD level and the generator version so that a seed means one
  world across machines; soft simulation still isn't deterministic, so runs
  share the world, not every event in it.
- **Verification is by video, as in most speedrunning communities:** no
  built-in run recording. The timer's display (time, splits, category
  settings, seed and version) is what a stream shows; races on a server are
  timed by the server, which is the authority anyway.
- **Watchable races:** spectator clients come naturally from
  `net/source-blind-client`; a ghost of a personal best needs only the
  body's recorded trajectory, not a deterministic replay.
- **Ethics still apply:** no daily or weekly seeds that reward showing up
  on schedule (`ethics/no-appointment-mechanics`, `ethics/no-fomo`); shared
  race seeds are fine as long as an old one is as playable as today's.
- **Open:**
  - the objective vocabulary, and whether players can define their own
    (from the authoring grammar, `data/grammar`). **A cheap route: reuse
    the game's event taxonomy.** Audio, subtitles and other feedback will
    need a typed vocabulary of in-game events and statuses anyway, and a
    decent subtitling system names most of what an objective would trigger
    on ("door opens", "lantern lit", "ore revealed"). Two refinements keep
    it sound:
    - **share the vocabulary, not the delivery:** subtitles are derived and
      perception-scoped (you hear what is near you, on your client), while
      objectives must trigger from authoritative events on the server. So
      the authority emits typed events; audio and subtitles derive from
      them, filtered by perception; objectives subscribe on the authority.
      Purely cosmetic client-side sounds (ambience, decorative particles)
      never reach objectives;
    - **events are not the whole vocabulary:** many objectives are states,
      not events ("hold 64 logs", "stand below depth N"), and those are
      level-triggered predicates checked against state
      (`core/level-triggered`), which events alone would miss after a load
      or a missed event;
  - how load time is measured and excluded fairly;
  - formats beyond solo runs: races, objective boards (bingo and lockout
    styles), and draft formats. **Draftout** (a Minecraft community
    format) is the worked example: two runners draft a 5×5 board of
    objectives from a pool of over 300, alternately picking one of a pair
    (12 picks each, plus one random slot); completing an objective locks
    the opponent out of it, and the first to 13 wins. Supporting it needs a
    large objective vocabulary drawn from authoritative events, a shared
    board the server owns (lockout is a race between two claims, so the
    server decides who got there first), and objectives that make sense on
    a seed neither runner has seen (per-world recipes,
    `crafting/per-world-recipes`, make some objectives discovery tasks);
  - **head-to-head runs are two worlds on one server:** the runners play
    the same seed in separate instances. The engine already allows this
    (one Dispenser per process shared by every world, each world with its
    own Surveyor and World actor, `sim/concurrency-model`), and it brings
    three things:
    - **identical worlds for free:** one machine, one binary, one SIMD
      level, so the same seed generates the same world in both instances
      with no pinning
      (`invariant/cartographer/same-machine-determinism`);
    - **fairness is a scheduling question:** the Dispenser's cross-world
      policy must split work evenly so neither runner's chunks stream in
      faster (`core/fair-share`); and the Cartographer's cache stays per
      world, since sharing it would let the runner who explores second
      load terrain the first has already generated, faster;
    - **the board lives above both worlds:** a lockout board is shared
      state neither world owns, so it belongs to a match object in the
      server process that both worlds report objectives to and that
      arbitrates simultaneous claims;
  - which movement techniques are acceptable skill (a well-timed fall
    break, say) rather than exploits, and how to keep the fastest path the
    obvious one as movement grows (swimming, climbing, mounts, the Aether's
    currents).
