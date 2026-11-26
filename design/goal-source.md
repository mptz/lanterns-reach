# engine/goal-source (draft) — GoalSource: sequencing work with dependencies

GoalSource is the Dispenser source for work with dependencies. A client
expresses a desired end state (a box of terrain populated and meshed, a
save committed) as **goal actors** it allocates, links into its own
structures and registers, each naming the goal actors it depends on, and
GoalSource runs each one's turn only once its dependencies have completed,
so the expensive work runs in parallel and the client never sequences it.
Nothing in it is client-specific: the octree is one client among several.

**GoalSource sequences; it doesn't own.** Allocating an object and hooking
it into a data structure is cheap, so the client does both in its own turn
(`invariant/concurrency/one-owner`). GoalSource holds only each goal actor's
scheduling record, under one mutex, and moves it through a fixed transition
table. A cancelled goal actor is abandoned, with a cascade to the goal
actors waiting on it, and cleaned up by its client; GoalSource never
re-creates or re-runs anything.

`design/dispenser.md` holds the scheduler GoalSource plugs into (sources,
`pullBatch`, reclamation, conditions); `design/concurrency.md` holds the
actor convention and the pointer rules ("Ownership and borrowing") every
client follows. This file holds GoalSource's role, transition table,
contracts, invariants and open questions. The design is a draft. `model/GoalSource.tla` checks it
in TLA+ (results, modelling choices and the readings it took in
`model/GOALSOURCE-RESULTS.md`): safety on four graph shapes, liveness on
the small ones.

**GoalSource** is the C++ class name.

## Vocabulary
- **Goal actor:** an actor with one scheduled turn, which computes its own
  data from its inputs and its dependencies' data (a TerrainNode sibling set
  refining itself from its parent, a mesh, a chunk-blob write). It is owned
  by its
  client, registered with GoalSource, and immutable once completed
  (`invariant/concurrency/immutable-once-published`). It derives from
  `GoalActor`, which holds its scheduling record.
- **Client:** the actor that allocates a goal actor, links it into its own
  structures (the World's octree), registers it, withdraws it and frees it.
- **Registration (`registerGoal`):** creating a goal actor's scheduling
  record with its dependencies and priority. Ownership stays with the
  client.
- **Dependency:** a goal actor whose completed data another goal actor's
  turn reads. Dependencies are fixed at registration.
- **Dependent:** a goal actor, seen from one of its dependencies.
- **Dependency pointer (`DepPtr<T>`):** the `const` pointer a dependent
  holds to a dependency, set at registration and dereferenceable only
  inside the dependent's own turn (`invariant/concurrency/dependency-pointers`).
- **Inputs:** values the client moves into a goal actor at construction
  (a chunk's captured cells for a chunk-blob write); the goal actor owns them.
- **Withdraw:** GoalSource's counterpart of the client's unlink: once
  `withdraw` returns, no dependent of the goal actor that hasn't started
  ever starts, whether registered before or after the withdrawal; if the
  goal actor itself hasn't started it never will, and if it is running it
  publishes nothing. A turn already running that reads it carries on. It
  frees nothing.
- **Publish:** the end of a turn, under the mutex, setting the state to
  Completed with a release store, which makes the goal actor's data readable
  (`invariant/concurrency/immutable-once-published`) and decrements its
  dependents' unmet counts. A turn that publishes nothing ends Abandoned:
  what it wrote into its own members is never marked readable.
- **Cleanup:** freeing goal actors after a grace period, through the
  ordinary reclamation path (`retire`,
  `invariant/concurrency/free-after-grace-period`), only once they are
  withdrawn.

## Requirements
- **The dependent is indifferent to its dependency's history:** it can't
  tell a dependency linked into the octree long ago from one completed by
  a recent turn, so nothing in a dependent's turn depends on a
  dependency's ownership.
- **No ownership changes in a dependent's turn:** it reads its
  dependencies through `const` pointers and writes only its own data; it
  claims, takes and transfers nothing.
- **Sequencing, not ownership:** GoalSource decides when a turn may run
  and when a goal actor can never run; the client decides what exists,
  where it is linked and when it is freed.
- **All compute is in a turn:** every goal actor has a turn, even a
  trivial one, so every piece of work is ranked, counted in flight and
  visible to telemetry the same way.

## Architectural role
- **One per World,** registered under the World's node in the halt
  hierarchy (`engine/dispenser`), so halts and in-flight counts attach to
  the World, and cross-world fairness has something to choose between.
  Dependencies never cross Worlds (asserted at registration).
- **Clients** are the World's planning (its Surveyor: generation,
  refinement, meshing) and its Registrar's work (chunk-blob writes and
  reads, the
  commit), in their own turns. Stateful actors' turns stay with their own
  sources.
- **A `DispenserSource` without batching:** each `pullBatch` hands out at
  most one goal actor, whose turn starts at once on the same worker, so
  nothing is claimed and left unstarted and `returnRemainder` never
  receives a goal actor (asserted). It advertises when goal actors become
  runnable.

## Client-supplied data
- **Data from the client's own state moves in as inputs.** The client's
  turn holds the passkey to its state, so it copies what is needed (a
  halted Vicinity's changed chunks, edited content outside every Vicinity)
  and moves it into the goal actor that uses it; that goal actor's turn
  does the expensive part (encoding and compressing a chunk blob). It needs no
  dependency pointer for what it owns.
- **Data shared by several dependents** moves into one goal actor whose
  turn prepares it (validates, indexes, or nothing), and the dependents
  depend on that goal actor. The cost is one dispatch of latency and,
  when there is nothing to prepare, a trivial turn.
- **So every Completed goal actor's turn ran,** and every published data
  item comes from a turn's release store.

## Two views of the graph
- **Bottom-up, in the dependents:** each dependent holds a `DepPtr` to each
  of its dependencies, as members, which its turn reads.
- **Top-down, in GoalSource:** each goal actor's record lists its
  dependents, which GoalSource walks under its mutex to make dependents
  runnable when a dependency completes and to abandon them when it is
  abandoned or withdrawn.
- **Both are written once, by `registerGoal`,** from the same list in one
  critical section, so they can't disagree. Afterwards edges are only
  removed, and only when a dependent reaches a terminal state (Completed
  or Abandoned): it then leaves its dependencies' lists. Being claimed by
  a worker removes nothing, so a dependency's list always holds every
  dependent not yet terminal, and the cascade sees all of them.
- **The client's own index** (the octree's parent and child links) is a
  third structure; GoalSource never reads it.

## States and transitions
Each goal actor's record holds its state, an atomic readable outside
GoalSource; a **withdrawn** flag, an atomic written only under the mutex
(release) and read by the goal actor's own turn at yield points (relaxed:
missing it costs only work); a count n of unmet dependencies; and its
dependents list. The state is one-shot past Pending and Running: once
Completed or Abandoned it never changes. Withdrawal is the separate flag.

| State | Pullable | Hit by a dependency's cascade | Counts as met at `registerGoal` | Its data readable |
|---|---|---|---|---|
| Pending (n unmet) | when n = 0 | yes | no | no |
| Running | no | no (its turn already has its inputs) | no | no |
| Completed | no | no | yes, unless withdrawn | yes |
| Abandoned | no | terminal | no | no |

Transitions, all under GoalSource's mutex:

| From | Event | To |
|---|---|---|
| (not registered) | `registerGoal(actor, deps, priority)` | Pending(n = deps not Completed); Abandoned at once if any dependency is Abandoned or withdrawn |
| Pending(n) | a dependency completes | Pending(n − 1) |
| Pending(0) | `pullBatch` | Running |
| Running | the turn completes, not withdrawn | Completed; each dependent's n drops by 1 |
| Running | the turn completes, withdrawn | Abandoned (it publishes nothing) |
| Running | the turn abandons | Abandoned |
| Pending | `withdraw(actor)` | withdrawn; Abandoned |
| Running | `withdraw(actor)` | withdrawn; still Running, until its turn ends |
| Completed | `withdraw(actor)` | withdrawn; still Completed |
| Abandoned | `withdraw(actor)` | withdrawn; still Abandoned (no cascade: its Pending dependents were abandoned with it) |
| Pending | a dependency is abandoned or withdrawn | Abandoned |

- **The cascade:** a goal actor becoming Abandoned, or being withdrawn,
  walks its dependents list and abandons every Pending dependent,
  recursively, in the same critical section. Running dependents stay
  listed and untouched: a running turn, even one claimed microseconds ago
  and not yet into its body, reads its inputs safely, since reclamation
  keeps them alive until it ends. At worst it computes data the client
  then withdraws, which is redundant work, not a correctness issue.
  Completed dependents are no longer listed, and no longer need their
  dependencies (storage is cumulative, `world/octave-pyramid`).
- **Withdrawing a running goal actor** marks it withdrawn; its turn runs to its
  end and then becomes Abandoned, publishing nothing. Its Pending
  dependents were abandoned when it was withdrawn, and later ones at
  registration, so the cascade on becoming Abandoned finds none.
  **Cooperative abandonment** is the turn reading its withdrawn flag at
  yield points, expected only in long turns; a turn may also abandon on
  its own (a failure) whether or not it is withdrawn.
- **Withdrawing a completed goal actor** leaves it Completed but withdrawn, so a
  client turn that found it before the unlink and registers a dependent on
  it gets an abandoned dependent, never one that would run after the free.
- **The withdrawn flag and the terminal states are never cleared before the
  memory is freed,** so a late reader within its turn sees them, not a
  reused slot.
- **Every registered goal actor is withdrawn exactly once,** whatever its
  state, before its cleanup; a goal actor allocated but never registered
  needs no withdrawal and is freed like any other object.
- **Readiness is derived:** Pending with n = 0 is membership of the
  runnable set, not a separate state.
- **The turn's end:** under the mutex, a turn that finishes while not
  withdrawn publishes its data (the state's release store of Completed),
  then decrements each dependent's n, and those reaching 0 become
  runnable, and GoalSource advertises.

## Outcomes for outside waiters: `done()` and `when()`
- **Every goal actor has a `done()` condition** (`engine/dispenser`'s
  conditions), fired once when the state becomes Completed or Abandoned,
  carrying that outcome. Withdrawing a completed goal actor doesn't fire it
  again.
- **Conditions fire after the mutex is released:** firing walks the
  waiters and makes them runnable in their own sources, so a critical
  section only collects the conditions to fire, and GoalSource fires them
  once it has released the mutex, on the same thread, before the call or
  turn that ran the critical section returns; a critical section's
  conditions fire together. No other lock is taken, and no
  condition fired, while GoalSource's mutex is held. The state is already
  terminal and visible before `done()` fires.
- **Outcomes are the client's to track:** GoalSource keeps no lists for
  clients. The client records what it registered in its own structures,
  reads `state()` in its turns, and prunes and withdraws as it sees fit,
  including goal actors a cascade abandoned.
- **`when()` is for prompt nudges,** and never blocks: the World asks for
  a follow-up turn once a sibling set's meshes have completed (to swap the
  mosaic), and a Vicinity once a join's data is ready; otherwise the client
  just looks on its next turn.
- **GoalSource's own edges are not conditions.** A condition's waiter list
  is consumed when it fires, so a completed dependency would no longer
  know its dependents when it is later withdrawn; a waiter registering after
  the firing sees it already fired; and unsubscribing a dependent that
  starts or is freed from a lock-free waiter list is hard. GoalSource keeps
  its edges in its records, under its mutex.

## Dependency pointers and withdrawing
The guarantee: **no dependency pointer is ever dereferenced after its
target is freed**, although dependents hold them across many grace
periods. It rests on three parts.
- **(a) The pull is the only way in.** A `DepPtr<T>` dereferences only with
  the dependent's `GoalTurn&` passkey, which only `pullBatch` constructs,
  and `pullBatch` claims a goal actor only when it is Pending with n = 0,
  every dependency Completed and none withdrawn, checked under the mutex in
  the same critical section as the claim. **The claim's withdrawn check is
  a backstop:** the cascade, the check at registration and keeping edges
  until a dependent is terminal already keep every claimable goal actor
  clear of withdrawn dependencies (the model passes without it), but it is
  the last guard if any of those is broken (removing it as well turns each
  of their failures into a use after free), so it stays, fatal in every
  build.
- **(b) Withdrawing goes through GoalSource, under the same mutex, and comes
  before the cleanup.** The client unlinks the goal actors from its own
  index, then calls `withdraw(turn, actors)`: for each, in a critical
  section, it is marked withdrawn and the cascade abandons every Pending
  dependent. Only after `withdraw` returns does the client add them to a
  cleanup actor and call `retire` once for the lot. The order is the whole
  coupling: the cleanup's tag must be read after the withdrawal, exactly as
  `invariant/concurrency/free-after-grace-period` requires it to be read
  after an unlink; otherwise a worker that copied the advanced G could pull
  a dependent before the withdrawal, and the dependency could be freed under
  its turn. **Adding a goal actor to a cleanup asserts, fatally, that it is
  already withdrawn** (an acquire load of the flag), so a wrong order is
  caught where it happens.
- **(c) Reclamation covers dependents already running.** A dependent
  pulled before the withdrawal runs on a worker online since before its
  pull, whose counter is below the cleanup's tag, so the dependency is
  freed only after that turn ends (`engine/dispenser`'s reclamation).
- **So:** every pull of a dependent is before the withdrawal's critical
  section, covered by (c), or after it, when the dependent can't be
  Pending with n = 0, by (b), and is never pulled.
- **Batches:** `withdraw` takes a span, so a fast mover's few thousand
  speculative goal actors are one call. Internally it may release the
  mutex between chunks of the span (say 256, a placeholder), so workers
  are never blocked for long; each goal actor's withdrawal is independent,
  and the cleanup comes only after the whole call returns. One cleanup
  actor carries everything withdrawn in a turn.
- **Supporting rules:**
  - **`registerGoal` checks each dependency's state and withdrawn flag under
    the mutex;** the client found the dependency in its own index during
    its turn, so reclamation keeps its memory valid for the check. The
    check guards a single client too, not only cross-client dependencies:
    one client turn can find an actor, withdraw it, then register a
    dependent against it.
  - **A client turn is a reclamation reader from its start,** as any
    worker's turn is, so an actor it found stays valid until the turn
    ends; a goal actor's turn is one from its claim.
  - **Withdrawing without an unlink:** a registered goal actor the client
    never linked into its index has nothing to unlink, and is withdrawn
    directly.
  - **A goal actor may be freed only once GoalSource holds no reference to
    it:** it is not in the runnable set, not in any dependency's
    dependents list, and its own list is empty. Running dependents stay
    listed until their turns end, so the empty list is asserted when the
    cleanup runs: by then every turn running at the withdrawal has ended
    and removed itself, and reclamation kept the record alive for those
    removals, since those turns' workers were online. A withdrawn Running
    goal actor likewise needs no deferral: its own worker keeps its memory
    alive through its turn's end.
  - **A goal actor's destructor is private, friended to the cleanup actor**
    (`invariant/concurrency/free-after-grace-period`), so the only way to
    free one passes both asserts.
  - **After the turn, dependency pointers are dead:** they may stay as
    members, but nothing can dereference them without the passkey.
  - **Fatal backstop:** a `DepPtr` records its target slot's generation
    at registration, and every dereference compares it, fatally in
    release, as `Borrow<T>` does; generations change only on a free.

## Readers outside GoalSource
- **Unregistered reads as not Completed:** a goal actor's state starts
  Unregistered, so whether a client links it into its index before or
  after registering it is the client's choice, made for its own readers;
  GoalSource requires nothing about the order.
- **Data is readable iff Completed:** meshing, the frame thread and
  physics reach goal actors through the client's index and read their data
  only after an acquire load of the state finds Completed, through one
  helper, so no caller writes the check by hand.
- **Borrows from the index follow `invariant/concurrency/borrow-one-turn`:**
  for one turn, protected by reclamation like any other.

## Bookkeeping
- **All bookkeeping is under one mutex:** states, withdrawn flags, counts,
  dependents lists and the runnable set. Turns run outside it. At our
  rates (turns of about a millisecond on 8–16 workers) a sub-microsecond
  critical section won't contend; it can be sharded later without
  changing the API.
- **The record is intrusive,** in `GoalActor`, so registration allocates
  nothing for the record; a dependents list is small (a parent's eight
  children plus its mesh) and lives inline with spill.
- **No keys:** the client deduplicates through its own index (the octree
  by position), and identity is the goal actor itself. A virtual
  `describe()` names a goal actor for logs, telemetry and replay;
  GoalSource never parses it.
- **Dependencies form a DAG by construction:** a dependency must already
  be registered when its dependent is.
- **Priorities:** the client gives each goal actor a priority at
  registration; a dependency's effective priority is the highest of its
  own and its Pending dependents', so ranking happens when work is taken
  (the pull principle) with no priority inversion. Speculative goal actors
  (read-ahead for a fast mover) rank below what is needed.

## Ownership and borrowing
- **The client owns every goal actor** (`invariant/concurrency/one-owner`), in a
  slab (type-stable
  memory, each slot with a generation), and links it into its own index.
- **Completed data is immutable**
  (`invariant/concurrency/immutable-once-published`): published by the
  release store of Completed, read after an acquire load.
- **Frees only through a cleanup, after `withdraw`,** then a grace period
  (`invariant/concurrency/free-after-grace-period`).
- **A dependent reads its dependencies through `DepPtr`**
  (`invariant/concurrency/dependency-pointers`), only in
  its own turn, and owns its inputs.
- **Everything else borrows from the client's index for one turn**
  (`invariant/concurrency/borrow-one-turn`), and the client may hold its
  goal actors across its own turns
  (`invariant/concurrency/owner-pointers-end-at-retire`).
- **Results with a single consumer are messages:** a goal actor's data is
  shared and const, so a mesh for the frame thread or cells for a Vicinity
  are sent from the turn as messages.

## The API (draft)
```cpp
class GoalActor {                                   // base of every goal actor
public:
    GoalState state() const;                        // acquire; Completed means its data is readable
    const Condition& done() const;                  // fires once: Completed or Abandoned
    virtual std::string describe() const = 0;       // logs, telemetry, replay
protected:
    virtual void runTurn(GoalTurn&) = 0;            // reads inputs and DepPtr members, writes its own data
    bool withdrawn(GoalTurn&) const;                  // for the turn's yield points
private:
    friend class GoalSource;
    GoalRecord record_;                             // state and withdrawn (atomics), unmet count, dependents; under GoalSource's mutex
};

template <class T> class DepPtr {                   // set at registration; dereferenceable only in the holder's turn
public:
    const T& get(GoalTurn&) const;                  // checks the turn and the slot's generation, fatally
};

class GoalSource : private DispenserSource {        // one per World
public:
    void registerGoal(ClientTurn&, GoalActor&, std::span<GoalActor* const> deps, Priority);
    void withdraw(ClientTurn&, std::span<GoalActor* const>);   // after the client's unlink, before its cleanup
private:
    std::mutex m_;
    std::optional<Batch> pullBatch(Budget) override;       // claim under the mutex; the turn runs outside it
    void returnRemainder(Batch) override;                  // never receives a goal actor: batches of one
};
```

## Clients
| Client | Goal actor | Its dependencies |
|---|---|---|
| terrain | a mesh per node of the desired mosaic | the sibling set holding the node |
| refinement | a node's eight children at once, as a sibling set (`world/octave-pyramid`) | the sibling set holding the parent |
| seed keys | a key region, made once and shared, owned by the World | none, or coarser regions |
| joining | chunk C as an actor in Vicinity V | its full-detail data, from its chunk blob or its leaf |
| saves | a chunk-blob write per changed chunk, owning that chunk's captured data, built by the halted Vicinity's turn and sent to the Registrar, which registers, withdraws and frees them; then the commit | the commit depends on every chunk-blob write |
| structures | the plan of a stronghold, made once and shared, owned by the World | its region's keys |

The World owns every terrain-side goal actor, seed-key regions and
stronghold plans included, so terrain has one client; the only
cross-client dependencies are joining's (a Vicinity's join depending on
the World's leaf or the Registrar's chunk-blob read). The joining cascade and a
portal's power-up are goal actors their owner waits on with `when()`.

## Invariants (draft)
- **`invariant/goal-source/registered-once`** — A goal actor is registered
  at most once, and its dependencies are fixed at registration.
  - **Enforced:** fatal check at registration.
- **`invariant/goal-source/bookkeeping-under-mutex`** — Bookkeeping is
  touched only under the mutex; the state is also readable outside it,
  with acquire.
  - **Enforced:** type: the record is private, friended to GoalSource, and
    `-Wthread-safety` checks the mutex.
- **`invariant/goal-source/unmet-count`** — A Pending goal actor's n
  equals the number of its dependencies not Completed, and is never
  negative.
  - **Enforced:** debug audit; model: TLA+ `GoalSource` B1 and B2.
- **`invariant/goal-source/claim-ready`** — A goal actor is pulled only
  when it is Pending with n = 0, and only once.
  - **Enforced:** fatal check at claim; model: TLA+ `GoalSource` A1, B5 and C2.
- **`invariant/goal-source/no-dead-dependency`** — No Pending goal actor
  has an Abandoned or withdrawn dependency once a critical section ends:
  the cascade completes inside it.
  - **Enforced:** debug audit; model: TLA+ `GoalSource` B4.
- **`invariant/goal-source/terminal-states`** — Completed and Abandoned
  are terminal, and the withdrawn flag is never cleared, before the free.
  - **Enforced:** review: the transition table is the only writer; fatal
    check on each transition; model: TLA+ `GoalSource` A2 and A3.
- **`invariant/goal-source/completed-ran-unwithdrawn`** — A Completed goal
  actor's turn ran and completed while it was not withdrawn.
  - **Enforced:** review: by construction, as the only transition into
    Completed; fatal check there; model: TLA+ `GoalSource` A5 and A6.
- **`invariant/goal-source/done-once`** — `done()` fires exactly once per
  goal actor, with Completed iff its state is Completed.
  - **Enforced:** review: only the transitions into the terminal states
    fire it; fatal check there; model: TLA+ `GoalSource` A7.
- **`invariant/goal-source/dep-ptr-in-turn`** — A `DepPtr` is dereferenced
  only in its holder's turn, and every dependency was Completed and not
  withdrawn when that turn was pulled.
  - **Enforced:** type: the `GoalTurn&` passkey; fatal check: the
    generation check on each dereference; model: TLA+ `GoalSource` C1, C2 and
    C3.
  - **Cost:** a load and compare per dereference.
- **`invariant/goal-source/freed-after-withdrawal`** — A goal actor is
  freed only by a cleanup registered after its withdrawal, after the
  client's unlink, once GoalSource holds no reference to it.
  - **Enforced:** type: the destructor is private, friended to the cleanup
    actor; fatal check when it is added to the cleanup (withdrawn) and
    when the cleanup runs (no references).
- **`invariant/goal-source/read-when-completed`** — A goal actor's data is
  read outside its turn only when it is Completed.
  - **Enforced:** review: the one read helper; fatal check in it.
- **`invariant/goal-source/turns-outside-mutex`** — Turns run outside the
  mutex.
  - **Enforced:** debug audit: an assert that the mutex isn't held when a
    turn runs.
- **`invariant/goal-source/dependency-dag`** — Dependencies form a DAG and
  never cross Worlds.
  - **Enforced:** review: dependencies are registered first, so the graph
    is a DAG by construction; fatal check at registration.
- **`invariant/goal-source/liveness`** — A Pending goal actor that is
  never withdrawn, and whose dependencies all complete without being
  withdrawn, is eventually pulled; every running turn ends; every cascade
  terminates; and every withdrawn goal actor is eventually freed.
  - **Enforced:** model: TLA+ `GoalSource` F1 to F4, under weak fairness of
    pulls, turn steps, firing `done()`, cleanup runs and client turns
    ending.
  - **Why:** the last fairness assumption is load-bearing: a client turn
    that never ends holds back every cleanup registered meanwhile, as any
    turn does under reclamation (`engine/dispenser`).

## Pending issues
- **Failures** that aren't bugs (a chunk-blob read failing): a turn that
  abandons with a reason the client reads.
- **How a goal actor's priority falls** when its dependents go.
- **Finding the best runnable goal actor cheaply,** and ranking against
  other sources' work.
- **Delayed goal actors** (later): runnable only once a delay has passed
  (a frame count or a time; the Dispenser's ticker source could be its
  clock), which may abandon themselves if no longer needed. First use: the
  Surveyor's fallback meshes of intermediate levels.
- **The budget on speculative work in flight.**
