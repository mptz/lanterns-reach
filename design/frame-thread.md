# engine/frame-thread (draft) — The frame thread: input in, a rendered frame out

The frame thread is one of three threads outside the actor model (the
others are the audio callback, `engine/audio`, and the network thread,
`engine/network-thread`, its many-headed counterpart for remote clients). It runs on the
process's main thread (GLFW requires window and event handling there), once
per frame: it gathers the player's input and routes it to the simulation as
intentions, and it presents the world. Its name
carries both senses of "frame": the render frame, and the frame through
which the player perceives the world. A headless server has no frame thread.

It owns presentation and nothing else. It writes no chunk, sends no hard
message, owns no simulation state and **runs no simulation rules: it never
predicts, it only evaluates curves the simulation hands it**; everything it shows comes from what
the simulation publishes, read through the client-facing read API, which is
the same in single-player and multiplayer (`net/source-blind-client`).

`design/concurrency.md` keeps the assumptions, invariants and connective
tissue the frame thread shares with the rest of the model (the locking
rules, the mesh path of "Meshes and the GPU"); this file holds the frame thread's own
role, loop, invariants and open questions.

## Architectural role
- **Not an actor, in every mode.** The Dispenser doesn't schedule it, it
  has no mail, and it never runs a turn.
- **Upstream, it publishes:**
  - **intentions** for the player's body, which its Vicinity owns and
    simulates; edits are things the body does, sent by the Vicinity;
  - **discrete actions** (a click to place) in a short, bounded,
    sequence-numbered event ring in the player's input slot;
  - **viewpoints** to the Surveyor (`publishViewpoints`: store the latest
    set, nudge the Surveyor);
  - **render hints:** `visibleUntil` on the render items it drew (chunks,
    and boxes of distant terrain).
- **Downstream, it consumes:**
  - **finished meshes** from the descriptor vector (`sim/concurrency-model`, "Meshes and the GPU");
  - **retire descriptors** from the Surveyor, through the same vector;
  - **published trajectories** of movers and entities: short timestamped
    segments, evaluated at render time;
  - **the body's target and tool preview,** published with its trajectory
    (Targeting, below); it makes no chunk reads of its own;
  - **the player's own trajectory,** from the authority in single-player
    or from the client's local physics in multiplayer (below), evaluated
    like every other mover's.
- **It alone owns the GPU side:** the device mesh pool, staging-block
  creation, the render table, the record list, command buffers, and every
  Vulkan call (workers never call Vulkan).

## The loop, once per frame
1. **Poll input** and turn it into intentions, each tagged with the base
   tick it applies to; write both into the player's input slot (below).
2. **Apply look, evaluate trajectories:** apply the mouse delta straight to
   the look offset (below); evaluate the player's predicted trajectory at
   render time for the body's position and view frame; the camera is the
   view frame composed with the offset.
3. **Publish viewpoints** from the camera.
4. **Pick up meshes:** `try_lock` the descriptor vector and swap it for an
   empty one (if the lock is held, next frame); keep the newest version per
   render item; install within the per-frame upload budget (allocate a pool
   range, record the copy); process retire descriptors.
5. **Evaluate published trajectories** for movers and entities at render
   time, with remote bodies drawn a small, tunable delay in the past
   (`sim/motion-segments`; how a Vicinity publishes many at once is open,
   below).
6. **Cull and hint:** run the visibility flood fill over the render table
   (not chunk storage), and write `visibleUntil = frame + N` for render
   items it drew, refreshing every few frames.
7. **Build the frame:** rewrite the record list (first implementation),
   record uploads first with their barrier, then the passes
   (`render/deferred-pipeline`), the GPU culling and indirect draws
   (`render/gpu-driven`), shadows within their budget (`render/shadows`),
   and the developer UI (`render/dev-ui`); submit on the single graphics
   queue.
8. **Release by timeline value:** return staging blocks whose copies have
   completed (one `try_lock` batch), and free pool ranges no frame in
   flight can still draw.
9. **Publish the listener pose** for audio, from the camera
   (`engine/audio`). Analysis and voice management run as the Listener
   actor's turns on workers; mixing runs on the OS's audio callback
   thread, another thread outside the actor model, with stricter rules
   still (no locks, no allocation, no waiting).
10. **Present and pace:** frame pacing is its only intended wait.
11. **Quiescent point:** the end of the frame is the frame thread's
    quiescent state for reclamation; nothing it read from shared structures
    during the frame is held past it. The pacing wait comes after it, with
    the thread offline, so how long pacing blocks never matters to
    reclamation.

## Look is authoritative input; movement is intention
- **Where a player is present (a client, or single-player), mouse look is an
  authoritative, instantaneous displacement from the body's view.** The
  body's physics give its default view frame (the head's orientation,
  part of the body's trajectory): knocked onto its side or hung upside
  down, that frame rotates with the body. Mouse look is an offset on top
  of it, which the frame thread applies at once from each mouse delta; the
  rendered view is the body's view frame composed with the offset. The
  offset travels with the intentions to the body and the authority. There
  is no physics of the offset and no maximum angular velocity: look has
  zero latency.
- **The body may absorb the offset, never fight it:** when walking, the
  body turns to face where the player looks, reading the offset as an
  intention. As the body's frame turns, the offset shrinks by the same
  amount, so the composed view never moves on its own. A body that can't
  turn (tied up, say) absorbs nothing, and the offset stays purely
  additive.
- **Thought exercise:** a body tied upside down, swinging like a pendulum.
  Its view frame sways with the swing, carried by the trajectory's angular
  velocity, and mouse look is added on top, unchanged and instantaneous;
  the player can still look around while swaying.
- **Movement is the opposite:** the body's physics are authoritative and
  running, jumping and the rest are intentions the body acts on. The body
  reads the look direction as an input to its physics (looking far aside
  from the direction of travel costs speed, `loop/speedrunning`), but never
  limits it.
- **Presentation effects are additive:** camera shake, or a stagger
  animation, offsets the rendered view without altering the authoritative
  look direction; impaired senses act on perception
  (`engine/audio`'s per-sense ranges, and render effects), not on look.

## Input reaches the body directly; local physics, not prediction
- **The input slot:** one per player (per soul), owned by the player's
  session and outliving any one body. It holds the latest continuous
  intentions (held keys, movement direction, the composed look direction,
  each tagged with its base tick) in a **triple buffer**: three copies and
  one `std::atomic<uint8_t>` holding the shared middle index and a fresh
  bit. The writer fills its private copy and swaps it into the middle; the
  reader swaps the middle out when it is fresh. Neither ever waits, and the
  reader always gets the latest complete value, which is right for
  latest-wins state. Discrete actions keep their bounded, sequence-numbered
  event ring beside it.
- **The frame thread never routes; the body pulls.** A soul's binding to
  its body is long-lived (`sim/player-embodiment`), so the body's record
  caches a pointer to its soul's slot, and the body's step loads it in its
  Vicinity's turn. The frame thread never needs to know where the body
  lives, which Vicinity owns it, or whether merge or cleave just moved it;
  possession and dreaming change the binding, not the routing.
- **Single-player:** the frame thread's store and the authoritative body's
  load are the whole path, and the frame thread evaluates the authoritative
  body's published trajectory directly. There is no separate client
  physics, since the authority is in-process.
- **Multiplayer: the client runs local physics.** Running a player's own
  inputs locally isn't a separate "prediction" layer; it is the client's own
  physics, in the client's implementation of the Vicinity (`sim/vicinity`),
  stepping the local player's body with the same pure `stepBody` as the
  authority, on the client's replica chunks, and publishing its trajectory.
  The client's network sender reads the same slot and ships changes; on
  the server, the receiver writes that player's server-side slot, which the
  authoritative body reads in the same way.
- **Merging with the authority is continuous:** authoritative state arrives
  more or less continuously, and the authority wins. The client's Vicinity
  keeps a short ring buffer (about a second) of tick, input and local
  state; when authoritative state for tick n disagrees beyond tolerance, it
  takes it and re-steps its inputs since n, smoothing the visible
  correction (Trajectories, below; "The lowest-latency path" in
  `design/concurrency.md`). Committed motion and limited control
  (`sim/motion-segments`) keep local and authoritative physics close; the
  measure is the rubberbanding the player sees. Whether the client's
  physics also steps nearby movers, or only the player's body, is open.
- **The frame thread can't tell the difference** (`net/source-blind-client`):
  it evaluates a trajectory, whether the authority or the client's local
  physics published it.
- **Viability: one cross-thread wake per input from the simulation's side,
  plus the frame thread's own pacing wake** (both OS wakes; frame pacing,
  in Open, below). At frame start the frame thread writes the slot and nudges; a
  worker wakes (one futex wake, tens of microseconds), runs the body's step
  and publishes the trajectory. Picking it up is a load, not a wake: the
  frame thread never sleeps waiting for it. **The realistic baseline is the
  next frame,** because the camera's position is needed early in a frame
  (step 2, for culling and viewpoints), before the step has usually
  finished. The terms that add up:
  - **base-tick quantization:** intentions apply at the next base tick, so
    up to one tick (16.7 ms at 60 Hz, 8.3 ms on average); at a frame rate
    above the base tick this, not the frame, dominates, and frames between
    steps evaluate the trajectory's extension;
  - **the wake and the step:** well under a millisecond with a worker free;
  - **up to one frame** to be picked up: 16.7 ms at 60 fps, 8.3 ms at
    120 fps, so higher frame rates make "next frame" cheaper.
  Look stays immediate throughout. If movement latency ever shows,
  **late latching** is the known refinement: compute culling with a
  slightly enlarged frustum, and write the final camera position into a
  small buffer just before submission, so a step that finished mid-frame
  still lands in that frame. Keeping the wake and step short is scheduling
  policy (a high-priority class, perhaps a worker kept free for it, and
  short fast turns for player bodies), tunable and not yet specified.
- **Tool previews are the body's:** computed in its step with its target
  and published (Targeting, below).

## Trajectories
- **Segment format:** `(t₀, p₀, v₀, q₀, ω)`, with gravity and drag implied
  by the body's kind: a ballistic position and a constant angular
  velocity, evaluated as `q(t) = exp(½ωt) ⊗ q₀` (`sim/multi-rate`'s
  tumbling note).
- **Never slerp between sampled orientations:** slerp takes the shortest
  arc, at most half a turn, so a body spinning several times between
  samples would seem to barely move or to spin backwards.
- **Corrections** between successive segments blend position with a cubic
  Hermite over a few frames and orientation in the tangent space (scale
  `log(q_new ⊗ q_old⁻¹)` down to zero), so only the small correction is
  interpolated, never the spin. Small corrections blend; large ones snap.
- **Remote bodies are drawn a small, tunable delay in the past;**
  committed motion and limited control (`sim/motion-segments`) are the
  main lever against rubberbanding, and the delay only a supplement, kept
  short for aiming's sake.

## Publishing trajectories: deltas, like meshes
- **Deltas, not snapshots:** a Vicinity's turn pushes only what changed
  (new or changed segments, spawns, despawns) into a mutex-guarded vector,
  which the frame thread swaps out with `try_lock` once per frame, as it
  does the mesh descriptor vector (the two may share one vector with typed
  entries). Most movers don't change in a given turn (passive movers between
  events, idle mobs), so a whole-set triple buffer would copy far more than
  changed.
- **Coherence by batch:** a turn's deltas go in as one batch, which the
  frame thread applies all or none, so everything one turn changed (a sword
  and the mob it hit, a minecart and its rider) appears in the same frame.
- **Coherence is only needed within a Vicinity:** the frame thread may see
  movers from several Vicinities (a separate player's Vicinity in view
  beyond simulation range, a distant place simulated for a telescope, a
  train with its Observer), but disjoint Vicinities can't interact, so
  there is nothing to keep coherent between them. It only takes deltas for
  movers within its render range.
- **The entity render table:** the frame thread keeps a table keyed by
  entity id, like its render table for chunks, applies deltas to it, and
  uploads changed entries to a GPU buffer of segments; the GPU evaluates
  every entity's pose each frame (closed-form position and orientation,
  animation clips sampled at time, skinning). Per-frame CPU cost is
  O(changes) (`render/gpu-driven`).
- **Attachments are relative:** a held item or a rider carries a segment
  relative to its parent's id, so it can't drift from the parent; the GPU
  composes the two.
- **On a multiplayer client** the deltas come from replication and from the
  client's local physics through the same vector, and the frame thread
  can't tell them apart.

## Effects and animation
- **Effects are closed forms, never physics on the frame thread:**
  particles and debris are evaluated from `(t₀, p₀, v₀, q₀, ω)`, mostly on
  the GPU. A particle either ignores blocks by its nature, or its
  intersections are computed by chunks or the client's Vicinity like any
  other physics. What makes effects special is that they are **never
  reconciled across machines:** the emitter (a burning block) is shared
  state, its sparks are each machine's own.
- **Animation is sampled, like a trajectory:** clips are sampled at a time,
  the trajectory carries the few state parameters that drive blending
  (gait, phase, crouch depth), and the GPU skins.

## Targeting is the body's, not the frame thread's
- **The body's step computes its target** (cell and face its tool is aimed
  at, from the composed look direction) and the tool's preview edit set,
  and publishes them with its trajectory; the frame thread draws the
  outline and the ghosts. A third-person camera's boom length against walls
  is computed there too.
- **So the frame thread reads no chunk storage at all,** and no seqlock
  reader ever runs on it; everything it draws is published.
- **The cost:** the highlight trails a fast mouse sweep by up to a step plus
  a frame (roughly 16–33 ms). If that shows, the frame thread may draw a
  presentation-only highlight from the G-buffer's depth and normal at the
  crosshair pixel (graphics data, not chunks); the click still acts on the
  body's target.

## Pause and focus
- **Pause is a World halt requested of System** (`engine/dispenser`): the
  pause menu sends a pause intention to System, which halts that World
  (world time stops, the workers go idle; a natural moment to save), and a
  resume intention brings it back. In multiplayer the server's System
  pauses by consensus (`persist/presence-not-absence`). The frame thread
  reads System's published state for the world; it never waits for it.
- **The UI's way into the back end is System's intentions:** shut down,
  load a world, pause, resume; everything else the player does goes through
  the body's input slot.
- **Process shutdown:** it sends the shutdown intention, keeps the frame
  loop running (a "saving…" screen), and polls System's published state
  once per frame until it reports shut down; then it calls the Dispenser's
  `destroy()`.
- **Then the frame thread goes quiet:** it renders one final frame and
  shows that still, redrawing only the pause menu on events (and on resize
  or expose), so the GPU and CPU idle.
- **Focus loss doesn't pause:** minimized, there is no surface, so it
  renders nothing while the simulation runs on; unfocused but visible, it
  keeps rendering, with an optional frame-rate cap to save power.
  Auto-pause on focus loss may be an option, off by default.

## Rules
- **It never waits behind work.** It may block only on short critical
  sections that move indices and pointers, never behind another thread's
  application-level work, and it uses `try_lock` where that costs nothing
  (`sim/concurrency-model`'s locking rules: no nested locks, atomics for
  signals, `std::mutex` for short exclusion).
- **Every lock it touches has safeguards:** capacity reserved so nothing
  reallocates under the lock, and debug hold-time statistics.
- **It reads, never writes, the world.** It changes chunks only by asking
  its body to act; its only writes into shared state are viewpoints,
  intentions and events in the input slot, and scheduling hints
  (`visibleUntil`), none of which are chunk content.
- **Its per-frame CPU cost is bounded:** O(changes) for meshes (the upload
  budget) and O(drawable chunks) for the record list rewrite, an accepted
  exception until persistent records are needed.

## Lockstep mode
The reference configuration for tests and reproducing bugs is **one worker
in strict alternation with the frame thread** (`sim/concurrency-model`): the
frame thread runs its part of the frame, hands a baton to the worker and
waits; the worker runs a counted share of turns and hands it back. The frame
thread still runs no turn, so its role and every check stay the same in all
modes. The one rule relaxed, by declaration, is waiting: in lockstep the
frame thread waits behind the worker on purpose, and its blocked-time
statistic (lock statistics, "Checks") is not counted. Lockstep is selected
only explicitly, by tests or a debug flag; play always has at least one
free-running worker.

## Checks
These mechanisms check the invariants below; each invariant's
**Enforced** line names the ones it relies on.

- **Build boundary** (type, compile time). The frame thread's code sees
  the intention and viewpoint publishers and the descriptor vectors, not
  chunk read or write APIs, Vicinity `send` or Dispenser headers
  (`deps/tooling`).
- **Thread asserts** (debug audit). The frame thread records itself at
  start; frame-thread-only APIs (the mesh pool, the render table, Vulkan
  submission) assert they are on it, and turn-only APIs assert they are
  not, in every mode.
- **Lock statistics** (telemetry, debug). Hold times per lock the frame
  thread touches, and the time it spent blocked per frame.
- **Synchronization validation** (debug audit, in debug runs). Khronos
  synchronization validation for the upload and deferred-release paths
  (`deps/tooling`).

## Invariants (draft)
- **`invariant/frame-thread/never-waits-behind-work`** — Apart from frame
  pacing (and the baton, in lockstep mode), the frame thread blocks only
  inside short critical sections: nanosecond ones, behind another thread
  leaving that critical section, never behind application-level work.
  - **Enforced:** review: `try_lock` where it costs nothing (the staging
    block pool, the descriptor vector); telemetry or watchdog: lock
    statistics (blocked time per frame stays near zero; not counted in
    lockstep mode).
- **`invariant/frame-thread/writes-no-chunk`** — The frame thread writes no
  chunk content and sends no hard message.
  - **Enforced:** type: the build boundary.
- **`invariant/frame-thread/sole-vulkan-thread`** — The frame thread is the
  only thread that calls Vulkan, allocates or frees the device mesh pool,
  or creates staging blocks.
  - **Enforced:** debug audit: thread asserts; type: the build boundary,
    for workers.
- **`invariant/frame-thread/nothing-outlives-frame`** — Pointers the frame
  thread reads from shared structures are not kept past the end-of-frame
  quiescent point, where it copies the grace-period counter as every
  worker does at the end of a turn (`engine/dispenser`'s reclamation). It
  goes offline for every wait that may block (the pacing wait: present,
  vsync, a throttled unfocused frame; and any long block: a minimised
  window, not rendering), and comes back online, copying G, before the
  next frame reads anything.
  - **Enforced:** review: convention; debug audit: thread asserts that
    reads happen inside a frame (its frame counter may stay a parity
    counter, odd inside a frame and even between, for asserts and
    telemetry; reclamation doesn't use it); model: TLA+ `QSBR`
    `NoUseAfterFree`.
- **`invariant/frame-thread/only-completed-uploads-drawn`** — A record
  enters the list only once its copy is recorded ahead of it in the same
  frame or completed earlier. A swap (`world/surveyor`: a parent's render
  item for its children's) is applied as a group: its records enter, and
  the retired one leaves, in one frame, once every upload in the group is
  complete.
  - **Enforced:** debug audit: synchronization validation.
- **`invariant/frame-thread/source-blind-view`** — Everything the frame
  thread reads is published state (trajectories, targets, meshes, UI draw
  lists), the same in single-player and multiplayer; it reads no chunk
  storage.
  - **Enforced:** type: the build boundary (no chunk read API in its build
    target).
- **`invariant/frame-thread/no-simulation-rules`** — The frame thread runs
  no simulation rules: it applies look input and evaluates published
  trajectories, and never steps physics or predicts.
  - **Enforced:** type: the build boundary (`stepBody` and the physics
    headers are outside its build target).

## Open
- **The player UI and inventory: a design pass to come.** Starting
  point: a client-side UI actor builds draw lists (layout, HarfBuzz text
  shaping, glyphs from a pre-rendered signed-distance-field atlas) when the
  state it shows changes, and the frame thread only draws the latest ones;
  hit-testing stays on the frame thread against the last layout, so hover
  and click are instant like look; UI actions are intentions the body
  carries out, and conserved moves show as provisional until confirmed.
- **The entity delta vector's capacity,** and whether it shares the mesh
  descriptor vector.
- **The extrapolation clamp,** and the remote-body delay's default (well
  under 100 ms).
- **The look offset's range when the body can't absorb it:** whether it is
  limited (a neck's range, or today's usual ±90° pitch) or free.
- **Comfort settings, at two layers** (direction agreed; details open):
  - **how the view frame derives from the body:** an option such as an
    always-up view, which keeps the view upright while the body is
    inverted or tumbling. The authority only needs the composed look
    direction (view frame plus offset), which the intentions carry, so the
    derivation can be per player without the server knowing the setting;
  - **rendering:** damping the view frame's sway, and turning off
    distortion effects; presentation only, applied by the frame thread.
  Whether a comfort setting can ever be an advantage (in a race, say) is
  worth a look once there are competitive categories (`loop/speedrunning`).
- **The client's local physics, its scope:** the player's own body is
  stepped locally in multiplayer; whether edits or nearby movers are too
  is open (`sim/concurrency-model`, the body).
- **The input slot:** which intentions are continuous state and which are
  discrete events; whether the look direction is sampled per base tick or
  carried as its own curve; the event ring's size, what happens when it's
  full, and how a body acknowledges events; how the server bounds a
  client's input rate.
- **Frame pacing and frames in flight:** the path
  from key press to photons has two OS wakes, the frame thread's pacing
  wake (scheduling it back onto a core) and the worker's wake for the
  body's step, plus the GPU and present queue, usually the largest term.
  So: poll input as late as possible after the pacing wake, so its jitter
  delays input rather than ageing it; make the pacing wake precise (sleep
  to shortly before the target then spin briefly, `VK_KHR_present_wait`,
  a raised timer resolution on Windows); keep frames in flight low (2) and
  consider pacing the start of each frame so input is sampled as late as
  the GPU allows (the idea behind NVIDIA Reflex and AMD Anti-Lag). Still
  open: the exact pacing policy, and how it interacts with the 20 Hz floor
  of `sim/multi-rate` and the base tick.
- **A render-thread split,** only if profiling demands it
  (`render/gpu-driven`): if the frame thread ever grows too busy, recording
  could move to a second thread, which would need its own place in these
  rules.
