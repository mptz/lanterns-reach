# engine/network-thread (draft) — The network thread: a many-headed frame thread for remote clients

The network thread is the frame thread's counterpart for remote players.
The frame thread serves one local client: it writes that player's input
slot and turns published state into a rendered frame. The network thread
serves many remote clients from one thread: it writes each one's input
slot from inbound packets and turns published state into each one's
outbound stream. It multiplexes every connection through asynchronous I/O,
and represents each remote player by a plain **Client** record, not an
actor. A process with no remote clients (single-player) has no network
thread.

`design/concurrency.md` keeps the assumptions and connective tissue it
shares with the rest of the model; replication's content is
`net/replication`, and the client-server split is `net/client-server-seam`
and `net/source-blind-client`.

## Architectural role
- **Outside the actor model,** like the frame thread and the audio callback
  thread: the Dispenser doesn't schedule it, it runs no turns, and it never
  waits behind work. It is the third such thread.
- **On a server** it serves every connected client. A server has no frame
  thread, so the network thread takes that slot in the default thread
  count (cores minus two).
- **On a multiplayer client** it serves one connection, to the server: it
  ships the local input slot's changes and receives replication, which
  chunk turns apply (replica chunks) and the frame thread's delta vector
  carries (trajectories).
- **Hub and spoke:** clients talk only to the server; each client receives
  one stream, from the server.

## One event loop, asynchronous I/O
- **One loop over every connection,** on the platform's readiness or
  completion API (epoll or io_uring on Linux, IOCP on Windows, kqueue on
  macOS), not a thread per connection.
- **One UDP socket serves every client,** with batched receives and sends
  (`recvmmsg`, `sendmmsg`), so a few hundred clients are a few thousand
  packets a second in a handful of syscalls per pass.
- **It wakes on inbound packets** through the readiness API, and on outbound
  work through an `eventfd` (or equivalent) or its own send timers.
- **The end of each loop pass is its quiescent point** for reclamation, as
  the end of a frame is the frame thread's.
- **Sharding later means more loops,** each owning a subset of clients, not
  per-client threads or actors.

## Clients: plain records
- **One Client per remote player**, owned by the network thread: the
  connection and identity, the player's server-side input slot, the state
  the client last acknowledged, its interest (from the perception index),
  its send rate, rate limits and bandwidth budget.
- **Inbound:** the loop writes the player's input slot (the triple buffer
  and event ring the body pulls from, `engine/frame-thread`), publishes the
  player's viewpoint to its World (`publishViewpoints`, an any-thread
  service), and turns pause votes, world switches and the like into
  intentions to System.
- **Outbound:** the loop builds each client's stream from published state
  (below).
- **The binding to a World and body** is a field of the Client; switching
  worlds rebinds it. The local player's equivalent is the frame thread
  itself.

## Outbound: versioned state through each client's interest
- **One rule for all authoritative state, spatial or not.** The player
  roster behaves like physics: it changes only by server-mediated actions
  (a client's intention, applied in the authority's turn), it is published
  as versioned state, it replicates through interest, and the server wins;
  a client may show a change provisionally (a chat line as "sending") until
  the authority's version confirms or replaces it. Spatial and global
  state differ only in their interest.
- **Four kinds of replicated data:**
  1. **spatial state** (bodies, mobs, arrows, chunks): versioned, filtered by
     the client's interest; another player is a mover like any other, with
     a few attributes (name tag, appearance) replicated with it;
  2. **spatial transient events** (sounds, a crumble, a particle burst): the
     per-region buffers below; losing one is harmless;
  3. **global state** (the player roster, world time, weather, whether the
     world is paused), published by the World, or by System for what spans
     the server: versioned like spatial state, in a global region that is
     always in every client's interest;
  4. **global messages** (chat, join and leave announcements): reliable and
     ordered, but still state: the World (or System, if server-wide chat ever exists)
     keeps an append-only log with sequence numbers as published state, and
     the loop sends each client the entries beyond its last acknowledged
     one, resending until acknowledged. No per-client queues.
- **Replication is state-based** (`net/replication`): deltas against what a
  client last acknowledged, so it needs no event log and no per-client
  queue.
- **Everything replicated carries a version:** a chunk's content version, an
  entity's segment version.
- **Per client, the loop walks only that client's interest** (the regions
  the perception index says it perceives) and sends what is newer than the
  client's acknowledged versions. Cost follows that client's interest, not
  the world. A client that falls behind gets the latest version next time,
  not every change in between.
- **Encode once, send to many:** the owner of a change encodes it once in its
  own turn (a chunk its delta, a Vicinity its segments), into a cache keyed
  by (thing, version); clients near each other reuse the same bytes, and the
  per-client work is selection, packing, encryption and the send.
- **Unedited chunks need not travel:** a client may generate them itself
  from released seed keys (`worldgen/seed-keys`).
- **Transient events** (sounds, subtitles, particle triggers, a crumble) go
  into short per-region buffers of a few hundred milliseconds; the loop
  reads only the regions in a client's interest. Missing one is harmless.
- **Sends follow changes:** a client whose interest saw nothing new gets
  little or nothing, and the send rate can fall under load.

## Rough cost (estimates, not measured)
For 200 clients at up to 20 sends a second: mover updates of perhaps
50–200 changed segments per client per send at about 100 ns each (under
10% of a core); a few thousand packets a second (easily one thread with
batched sends); encryption of about 20 MB/s (a percent or two of a core).
Chunk payloads would cost cores if encoded per client, which encode-once
and client-side generation avoid.

## Rules
- **It never waits behind work:** it reads published state and encode-once
  caches through the same short critical sections the frame thread uses,
  and writes only input slots, viewpoints and intentions.
- **It runs no simulation rules and writes no chunk.**
- **Everything it reads is published:** versions, encoded payloads, the
  perception index, transient event buffers.

## Open
- **The library:** something that provides reliable and unreliable channels,
  fragmentation and encryption over UDP: GameNetworkingSockets (BSD-3),
  ENet (MIT, no encryption), yojimbo (BSD) or a QUIC library such as msquic
  (MIT); to choose when multiplayer is planned (`deps/selection-principles`).
- **Whether the local client and remote Clients share one abstraction**
  (input slot writer, viewpoint publisher, interest, an output sink that is
  a renderer locally and an encoder remotely).
- The encode-once cache's lifetime and memory bound; the transient event
  buffers' size and region granularity; how acknowledgements are tracked
  per client without per-entity cost.
- How a server bounds a client's input rate, and authentication.
