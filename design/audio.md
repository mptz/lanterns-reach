# engine/audio (draft) — Audio: analysis on workers, mixing on the audio callback, almost nothing on the frame thread

Audio is presentation only, like light (`light/visibility-only`): never
authoritative, never deterministic, absent on a headless server. It has
spatial mixing and processing (a cave's reverb, a muffled sound behind a
wall), and as much of its compute as possible runs off the frame thread.

## Three tiers, by time scale
1. **Acoustic analysis, slow and heavy, on workers** (about 5–10 Hz): what
   does this place sound like?
   - **Reverb environment:** 32–64 rays from the listener through the
     client-facing read API; hit distances and materials give the space's
     size, mean free path and absorption, hence a decay time
     (Sabine/Eyring) and wet mix. The share of rays escaping to open sky
     separates outdoors from enclosed, so a cave falls out of it.
   - **Per-source occlusion:** a ray from each audible source to the
     listener; solid cells along it add attenuation and a low-pass filter.
   - **Ambience:** distant sound that needs no simulation behind it (wind,
     a far river, surf), derived from what is resident: water surfaces,
     openness, worldgen fields.
   The output is soft, stale-tolerant published state, smoothed over time.
2. **Voice management, event-driven, on workers.** Authoritative events
   filtered by perception (the event taxonomy shared with subtitles and
   objectives, `loop/speedrunning`) become voices: start, stop, position,
   gain, plus a voice budget culling quiet or distant sounds. Latency from
   event to sound is one turn, so it runs at a priority like visible
   meshing.
3. **Mixing and DSP, per audio block, on the audio callback thread** (at
   48 kHz in 256–512-frame blocks, every 5–10 ms): per-voice
   spatialization (panning, distance attenuation, the occlusion filter;
   HRTF later), the mix, an algorithmic reverb (a feedback delay network)
   driven by tier 1's parameters, and output. Mixing stays here rather
   than on workers: mixing ahead on workers would add a lookahead's
   latency and put a hard deadline on Dispenser work every few
   milliseconds, and a missed audio block is an audible click, far worse
   than a late frame. If mixing outgrows the callback, a dedicated mixer
   thread feeding it through a short buffer is the standard next step.

## Who does what
- **The Listener actor** (one per client) runs tiers 1 and 2 as its
  turns: advertised at low priority with a rate cap for analysis, higher
  for new events. A headless server uses a no-op Listener, as it uses the
  `NoOpMesher`.
- **The audio callback thread** runs tier 3. It is one of three threads
  outside the actor model (with `engine/frame-thread` and
  `engine/network-thread`), driven by
  the OS's audio device, with stricter rules than the frame thread's: **no
  locks, no allocation, no waiting, ever.**
- **The frame thread publishes one thing:** the listener's pose each frame,
  from the predicted camera. The callback interpolates between poses so
  panning stays smooth at audio rate. Sound events never pass through the
  frame thread.
- **The Surveyor knows who perceives where** (below); it does no audio
  work.

## Handoffs to the callback
- **Parameter snapshots** (the environment, voice parameters, the listener
  pose) go through a triple buffer swapped by an atomic index: plain
  `std::atomic`, within the model's "atomics for signals" rule.
- **Voice commands** (start, stop) go through a single-producer,
  single-consumer ring buffer. This is the **named exception** to
  preferring standard primitives over lock-free structures: the callback
  can't take a mutex (a held lock is an audible glitch, and priority
  inversion against a real-time thread). The ring buffer is a well-known
  implementation (miniaudio ships one), not our own.

## Perception: viewpoints carry the sensorium's ranges
- **A viewpoint is the body's sensorium projected for scheduling and
  routing** (`sim/player-embodiment`): besides position, facing and
  velocity it carries a range per sense, starting with view and hearing.
  Sensory effects change the ranges and nothing else: super-hearing widens
  hearing, impaired vision narrows view; a dreaming player's spectator
  viewpoint carries its own.
- **The Surveyor makes the union of the ranges perceptible:** chunks within
  hearing range are loaded and readable even beyond view range (rarely),
  so acoustics and ambience have something to read.
- **Perception may ask for simulation, within a budget.** What is
  simulated is the Surveyor's allocation of a simulation budget across
  demand (`sim/residency-levels`), and perception is one source of demand:
  a heightened sense or a telescope may have distant places simulated, not
  only loaded, spending its owner's share of the budget rather than
  getting it free. A loaded place left unsimulated emits no authoritative
  events; what is heard from it is derived ambience.
- **The perception index:** the World's turn (its Surveyor) publishes a read-only
  index from coarse regions to the viewpoints that perceive them, by sense,
  in each planning turn. Server-side event routing (sound,
  subtitles, objectives) reads the snapshot to decide which clients
  receive an event, and never waits on the Surveyor; replication interest
  can use it later. Mob perception stays out: mobs perceive bodies through
  physics proxies and percepts inside the simulation (`sim/mob-perception`).

## Libraries
- **miniaudio** for the device layer: a single file, public domain or
  MIT-0, a callback API on every platform we target; our own mixer and DSP
  on top, so the dependency stays a thin seam (`deps/audio`).
- **Steam Audio** (Apache 2.0) for HRTF and propagation is a later
  option; voxel geometry makes our own ray-based estimation natural, so we
  may never need its propagation.

## Open
- The output latency target (I'd aim for 10–20 ms) and the block size.
- The voice budget and how voices are culled.
- How often the reverb environment updates, and how it is smoothed so
  walking into a cave sounds like a transition, not a switch.
- Ambience: which resident features drive it, and how far it reaches.
- The perception index's granularity, and whether hearing is
  omnidirectional with occlusion or also attenuated by facing.
- HRTF, and headphones versus speakers.
