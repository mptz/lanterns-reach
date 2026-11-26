# engine/dispenser — The Dispenser: the worker pool and actor scheduler

The Dispenser encapsulates the worker thread pool and schedules actor
turns from its single root actor, **System**, down, and it has **no ready
queue of its own: all work comes from registered pull sources** through non-blocking calls, so each source ranks
its work against current state at the moment work is taken; sources only
signal that work may have appeared. Each Vicinity is the source for its
own turns and its members' turns; each World's `GoalSource` for work
with dependencies (generation, refinement, meshing, loading and saving,
`engine/goal-source`); the World for its own turns and its one-turn work;
the client's actors for themselves. It schedules across sources
internally, honouring their priorities while ensuring no class of work
completely starves; those choices are hidden from sources and workers.
Workers have a single acquisition point, `Dispenser::nextBatch()`, called
from each worker's loop, and no other responsibilities, so
they block there until work is available and wait on nothing else; they
affect other actors only by sending messages and nudges, which the
Dispenser turns into runnable work. The pool is resized only
cooperatively. The Dispenser never looks inside a work item and holds no
world state.

`design/concurrency.md` keeps the assumptions, invariants and connective
tissue the Dispenser shares with the rest of the model (the actor
convention, the locking rules); this file holds the Dispenser's own role,
mechanisms, invariants and open questions. It is the in-house scheduler
that `deps/parallelism` chose over TBB and Taskflow.

## Requirements
- **Runs work with dependencies natively,** such as the Surveyor's
  (`world/surveyor`: an octree level depends on the level above it, a
  render swap on its children's meshes), with dedicated support for:
  - **replanning:** new work added and old work abandoned cheaply, as the
    client's wants change;
  - **cleanup** that frees shared results only once nothing still running
    can read them.
  Met by `GoalSource` (`engine/goal-source`: clients register goal actors
  with their dependencies, and GoalSource sequences their turns) and
  `Reclamation`.

## Vocabulary
- **Turn:** one uninterrupted, single-threaded execution of an actor (the
  term in E's event loops and Orleans). Every work item is a turn of
  exactly one actor or a Dispenser-internal item (exit items). An actor
  runs at most one turn at a time, which is what lets a turn and its
  actor's state go without a synchronisation contract. Every turn,
  including a function actor's, is `runTurn(XTurn&)`, with a passkey only
  the Dispenser can construct.
- **Function actor:** an actor that exists for one turn. Its inputs are
  members set when it is constructed; its one turn writes its results as
  members; afterwards anyone reaches them through its any-thread methods
  (`Function actors`, below). **Function actors are for one-turn work
  that neither has dependencies nor is one:** its inputs are moved in and
  its result goes to whoever made it, such as the mip filter that
  downsamples an edited chunk's content (`world/surveyor`). Everything
  with dependencies, or that other work depends on, is a goal actor.
- **Goal actor:** an actor with one scheduled turn that computes its own
  data from its dependencies', owned and registered by a client, sequenced
  by `GoalSource`, and immutable once completed (`engine/goal-source`).
- **TerrainNode:** a node of `Terrain`, the World's octree outside every
  Vicinity, held in a sibling set, a goal actor owned by the World, holding const grids
  (`world/surveyor`, `world/octave-pyramid`). The halt hierarchy's nodes
  (below) are a different tree.
- **Work item:** one unit of work from a source, run as one actor's turn
  (generate a chunk to a stage, consume a chunk's mail, step a chunk).
- **Batch:** what the Dispenser hands a worker: one or more work items, run
  in order. Batch naming may still change.
- **Source:** a pull source implementing `DispenserSource`. Every actor is
  reached through exactly one source at a time.

## Architectural role
- **One per process, with a process-scoped lifetime:** threads are a
  host-level resource. It is shared by every world in the process
  (single-player's in-process server and client, test worlds, a server
  hosting several worlds); each world's sources register when the world
  loads and deregister when it stops. Cross-world and client-versus-server
  priority are scheduling policy inside it; the worked case for cross-world
  fairness is a head-to-head race on one seed, whose runners' chunks must
  stream in equally fast (`loop/speedrunning`).
- **A process-global object, started and destroyed explicitly, with access
  gated by header visibility:**
  - **explicit lifecycle:** `start(config)` and `destroy()`; threads are
    joined in `destroy()`, at a point process shutdown chooses, after every
    world has deregistered its sources and before `main` returns, so static
    destruction never runs with workers alive. The C++ destructor only
    asserts that `destroy()` was called; `start` asserts it isn't already
    running, so tests can restart it;
  - **access by header:** the instance lives in one `.cpp` with internal
    linkage; `dispenser_lifecycle.h` (start, destroy) is included only by
    process startup and shutdown, `dispenser_source.h` (`DispenserSource`
    and registration) only by pull source implementations (the Surveyor).
    Turn code's build targets can't see either header;
  - a source deregisters only when none of its batches is outstanding.
  - Considered and set aside: a non-global object passed down with an "at
    most one live" check; header gating and explicit `destroy()` keep its
    advantages.
- **Not an actor itself,** and not scheduled by itself; the frame thread
  and the audio callback thread are outside it (`engine/frame-thread`,
  `engine/audio`).
- **Workers report completion, not chunks:** a worker returns the finished
  batch to the Dispenser, which updates the actor's scheduling state and,
  for a function actor, fires its `done()` (`Conditions and chaining`).
  Turn code is pure work, inputs to outputs, and knows nothing about
  scheduling.
- **Workers are quiescent between turns:** the end of each turn is a
  worker's quiescent point for reclamation (`Reclamation`,
  `sim/concurrency-model`), and a sleeping worker is offline.

## Acquiring work
- **Pull, not push:** pull keeps live reprioritization, since a source
  ranks against current viewpoints when work is taken, so a chunk the
  player has turned away from never jumps the queue. No thread is ever tied
  to one type of work, because the Dispenser decides across all sources
  before handing anything out.
- **Two-level scheduling, co-encapsulated:** the Dispenser chooses among a
  modest number of sources (a handful of Vicinities, the Surveyor, the
  World actors, the client's actors) from one advertised summary each, and each source orders its own many items, as OS
  schedulers do (Linux's scheduling classes). One priority mechanism
  (values, ageing, advertising) serves both levels.
- **No ready queue: every actor is reached through a source.**
  A queue fixes an actor's order when it is enqueued, so a chunk enqueued as
  "near" that becomes "visible" while waiting wouldn't move up; a source
  ranks at `pullBatch`, so priority is decided when work is taken. A nudge or
  message that flips an actor from idle to scheduled (its idle-to-scheduled
  CAS) marks it in its source's runnable set, and the source advertises.
  Sources stay few: a Vicinity stands for all its members, so the
  Dispenser never scans one advertisement per actor.
- **The source interface:**
  ```cpp
  class DispenserSource {
  public:
      // Called on a worker; returns at once with a batch or nothing (no
      // work, or its work was invalidated). Must not wait on our own design.
      virtual std::optional<Batch> pullBatch(Budget) = 0;
      // A partial completion hands the unfinished remainder back.
      virtual void returnRemainder(Batch remainder) = 0;
  protected:
      // Level-triggered: publish the urgent flag and the priority of the
      // best available work; bumps the epoch and wakes one sleeping worker
      // if any. Lock-free; makes no decisions.
      void advertise(bool urgent, Priority best);
  };
  ```
  A source inherits from it privately and registers itself, so only the
  Dispenser can call `pullBatch` and `returnRemainder` (the actor convention in
  `sim/concurrency-model`). A `GoalSource`'s `pullBatch` claims its best
  runnable goal actor under its mutex and runs its turn outside it
  (`engine/goal-source`), so no
  planning runs on the caller's time or under a lock.
- **The Dispenser stores no work:** work stays in its source until a worker
  is about to run it; the Dispenser holds only advertised priorities,
  fairness state and its own exit items.
- **Choosing a source without taking from it:** each source advertises an
  urgent flag and the priority of its best available work, on one global
  scale that applies within and across sources (a trusted environment:
  priorities are tuned globally, nobody acquires resources adversarially).
  The worker picks the highest, urgent first, then calls `pullBatch`. **Ageing**
  raises a ready source's effective priority the longer it goes unserved,
  so no class starves. The advertisement is a hint and `pullBatch` is
  authoritative: a stale advertisement costs a slight misordering, and an
  empty `pullBatch` sends the worker to choose again.
- **Where scheduling runs:** detection runs on whichever thread made work
  available; the decision runs on a worker. A thread that changes a source
  (the frame thread publishing viewpoints, a worker sending a message)
  only advertises: an atomic store, an epoch bump and, if anyone sleeps,
  one wake. Handing a chosen item from the signalling thread to a sleeper is
  rejected: it would buffer work, put scheduling cost on the frame thread,
  and be stale by the time the worker ran.
- **The simulation budget is enforced through priority:** the Surveyor
  allocates the server's simulation budget across demand
  (`sim/residency-levels`) and expresses it as priorities and rates; the
  Dispenser only honours them.

## Waking: an eventcount, wake exactly one, chain wake
- A worker reads the epoch E, scans, and if nothing is ready waits while
  the epoch is still E (C++20 `atomic::wait`, a futex on Linux). A signaller
  advertises, bumps the epoch, then `notify_one`. **No wake-up can be
  lost:** a signal before the worker reads E is seen by its scan; one after
  changes the epoch, so the wait returns at once. Waking exactly one is
  therefore correct, and spurious wakes are harmless.
- **Chain wake** supplies throughput: a worker that takes work and still
  sees ready work wakes one more, so k units of work bring up about k
  workers without a thundering herd; a source that knows it just gained
  many items may wake several at once.
- **Skipping the wake syscall when nobody sleeps** uses a sleeper count the
  worker increments before its final check and the signaller reads after
  bumping the epoch; both sides' store-then-load must be sequentially
  consistent (or fenced), or each can miss the other (the Dekker race).
  Prior art: Vyukov's eventcount, `folly::EventCount`.
- **No counting semaphores:** a semaphore wakes one worker per token, which
  is correct only if tokens never under-count the work; level-state
  sources such as the Surveyor can't count, so tokens would drift. The
  eventcount only says "something changed, look".
- **Under load there are no wake-ups at all:** a worker that finishes a
  turn goes straight back to `nextBatch()`.
- **The one timed wait:** while cleanups are pending, the last worker to
  sleep waits with a timeout, to activate the ticker source
  (`Reclamation`); otherwise every wait is untimed.

## Work items
- **Workers never block on Dispenser work:** a worker needing something
  from another actor sends a message and ends its turn; the reply arrives
  as mail (for a chunk, queued in its Vicinity with the mail bit set),
  which makes it runnable again: a continuation through mail, not a wait.
- **Ownership moves within a turn, by message, or by taking a completed
  function actor's result; conditions and other completions carry only
  facts.** Generation is a goal actor in the World's `GoalSource`: for a
  chunk outside every Vicinity the completed goal actor is the terrain
  node the World keeps, and a joining chunk's actor receives its chunk data
  as a message and installs it in its own turn (`engine/goal-source`).
  Dependent work becomes runnable through GoalSource's dependencies, and
  outside waiters through conditions (`Conditions and chaining`). A result belonging to
  another actor is moved into that actor's mail; a finished mesh goes to
  the frame thread by the descriptor vector (`sim/concurrency-model`, "Meshes and the GPU").
- **Granularity:** a batch holds one or more work items, sized by its source.
  Generating a chunk costs about a millisecond or more and dispensing a
  work item microseconds, so one chunk per work item suits generation; cheap items
  (uniform chunks, idle simulation steps) are batched. Sources may size
  batches adaptively toward a target duration (a named placeholder, around
  1 ms). Large work items need a ceiling or yield points (refinement stages are
  natural ones), since a turn can't be preempted.
- **Partial completion gives the remainder back:** a worker that hits an
  unexpectedly expensive work item stops at the next yield point (between work items,
  a cheap elapsed-time check; inside a long work item, at its natural stages)
  and gives the remainder back to its source. The Surveyor re-ranks
  returned generation work against current viewpoints; an actor's
  unprocessed messages simply stay queued and it stays runnable. Work
  running late is advertised as urgent. There is no carry-over lane, since
  that would store work. A batch that ran long is charged to its class.
- **Function actors** are offered by a source like any other actor, for
  one-turn work that neither has dependencies nor is one, and any number
  run at once, one turn each, each counted in flight at its source's node.
  Work with dependencies (generation, refinement, meshing, a save's chunk blob
  writes and commit, chunk-blob reads for loads and joins) uses goal actors
  instead (`engine/goal-source`), which their clients own and register,
  and whose turns GoalSource sequences.
  - **Inputs are members:** values moved in, or keys of shared results,
    which its turn looks up, borrowing them for that turn
    (`invariant/concurrency/borrow-one-turn`); a plain pointer set at
    construction would outlive the turn that obtained it. A goal actor's
    dependency pointers are the one exception, made safe by GoalSource's
    withdrawal protocol (`invariant/concurrency/dependency-pointers`).
  - **Its one turn writes its results as members,** under its own
    passkey, like any actor's turn writing its state.
  - **Afterwards results are reached through any-thread methods,** as
    any actor's are (`sim/concurrency-model`'s actor convention), each
    safe internally by the actor's `done()` condition (`Conditions and
    chaining`), which fires with release after the turn and which the
    method checks with acquire; calling one before `done()` has fired, or
    when the outcome is Abandoned, throws. Each result member supports one
    of two patterns:
    - **shared read,** `const T& result() const`, for many readers;
    - **ownership transfer,** `T claimResult()`, by atomic exchange, so exactly
      one caller wins and later calls throw, for one consumer (the World
      taking a mip filter's downsampled grids).
    Never both on one member, so no shared reader holds what `claimResult`
    moves out.
  - Its source keeps its own record of what is in flight, so it never
    creates two for the same target, and drops a result it no longer
    wants. The worker marker records the current actor, a function actor
    included.
- **Compute and I/O items, by duty cycle:** compute items are mostly
  CPU-bound (occasional buffered logging is fine); I/O items have a low CPU
  duty cycle (saving, loading). When an I/O item starts, the Dispenser adds
  a thread, and when it ends it dispenses an exit item, so the number of
  threads available for CPU work stays at its target. Concurrent I/O items
  are capped (a lane with a maximum). A misclassified item shows up cheaply
  as a low ratio of thread CPU time to wall time, sampled per item. Prior
  art: Java's `ForkJoinPool.managedBlock`, Go's runtime handing a blocked
  thread's processor to another.
- **Outside waiters poll conditions:** a non-worker client (tests, tools,
  the frame thread) learns that work has finished by reading a condition's
  `fired()`, such as a function actor's or a goal actor's `done()`
  (`Conditions and chaining`). **Workers never wait on a condition;** they
  chain on it with `when`.

## GoalSource
Work with dependencies runs through `GoalSource`, a source specified in its
own entity doc (`engine/goal-source`, `design/goal-source.md`): clients
allocate goal actors, link them into their own structures and register
them with their dependencies; `pullBatch` runs a goal actor's turn only
once every dependency has completed, and withdrawing one abandons, by
cascade, every dependent that hasn't started. GoalSource sequences; the
client owns. One per World.

## Reclamation (draft)
- **Pointers follow the ownership and borrowing rules** (the pointer rules
  of `sim/concurrency-model`): one owner, immutable once published (live chunk storage excepted, single-writer with validated reads), frees only
  through a grace period, borrows for one turn (keys across time), owners holding across their own
  turns, and dependency pointers dereferenced only in
  their holder's turn. The World owns the TerrainNodes it keeps in the
  tree; a goal actor reads its dependencies through its dependency
  pointers, and freeing a dependency needs its withdrawal from `GoalSource` before
  its grace period (`engine/goal-source`).
- **Global quiescent-state-based reclamation (QSBR)**, the QSBR flavour of
  userspace RCU (liburcu's `urcu-qsbr`; Desnoyers, McKenney and others,
  *User-Level Implementations of Read-Copy Update*), written in house:
  liburcu isn't a dependency (no native Windows support, LGPL, and its
  helper threads and blocking `synchronize_rcu` don't fit the Dispenser),
  but its memory ordering is followed deliberately.
  - **One global grace-period counter G** (64-bit, monotonic), and one
    counter per participating thread: the workers and the frame thread.
  - **Quiescent points:** a worker's is the end of every turn, before it
    asks for its next batch; the frame thread's is the end of every frame.
    At a quiescent point a thread holds no borrowed pointer
    (`invariant/concurrency/borrow-one-turn`), and it
    copies G into its counter, with a full fence before the store (its
    reads of shared objects are done) and after it.
  - **Offline:** a worker about to sleep on the eventcount, or the frame
    thread before any wait that may block (its pacing wait each frame:
    present, vsync, a throttled unfocused frame; or a long block such as a
    minimised window), stores 0 into its word with a full fence, after a
    quiescent point, so it holds nothing; going back online copies G with
    a full fence before any shared read, so a thread asking for work is
    online before anything a source resolves for it (a goal's
    dependencies, `engine/goal-source`). A server has no frame thread.
  - **One word per thread:** a thread's counter and its offline state are
    one atomic 64-bit word, with 0 reserved for offline and G starting at
    1 (as in liburcu), so a scan never sees a counter and an offline flag
    out of step. Going online must copy G, not keep the old value: the
    model shows a stale copy is safe but not live (a woken worker can fail
    its own scan and sleep again indefinitely).
  - **`retire(CleanupActor&)`** from any turn, after the owner
    has unlinked what it frees from everything new work can reach (the
    store, the tree), with a full fence between the unlink's stores and
    the load of G: the cleanup is tagged with t = G + 1, read after the
    unlink, and becomes runnable once every online thread's counter is at
    least t; its turn frees what it was given. Tagging t = G, or reading G
    before the unlink, is a use-after-free (the model checks both).
    One cleanup actor may batch many objects; `retire(SlabPtr<T>)` is the
    one-object convenience.
  - **The contract callers rely on:** retiring an object means **its use
    ends now and its life ends later.** The caller has unlinked it, so
    nothing new can reach it; readers that reached it before stay safe
    until they finish; then it is freed. When, and by which mechanism (a
    grace period here), is the Dispenser's business, not the caller's.
    Render items keep the same word for the same contract with the GPU as
    the reader: a retire descriptor ends a mesh's use, and its pool range
    is freed after the GPU's fence (`sim/concurrency-model`, "Meshes and the GPU").
  - **Why it is safe:** a thread that may hold an unlinked object obtained
    it before the unlink, so since its last quiescent point, whose counter
    copy was at most G at the unlink, below t; it reaches t only at a later
    quiescent point, after which it holds nothing. A thread that copies G
    afterwards, coming online or at a quiescent point, can't reach the
    object, since it was unlinked first.
  - **Read-side cost:** nothing on entering a turn; one fenced store at
    each quiescent point. The wait is about one turn on a busy server, and
    at least a frame on a client.
- **Opportunistic grace periods:** a worker asking for its next batch, and
  only while a global count of pending cleanups is nonzero, advances G by
  one step (a compare-and-swap) if a pending cleanup's tag exceeds it, and
  makes runnable every cleanup whose tag every online counter has reached
  (a scan over the participants). No thread is dedicated to it, and
  nothing is scheduled. The scan reads each online counter once, in any
  order, as separate atomic loads with no lock and no snapshot, and
  considers only the cleanups pending when the attempt began. Each
  thread's part of the safety argument stands alone, and counters only
  rise while online.
  A worker whose compare-and-swap fails scans anyway (another thread has
  advanced G for it). A cleanup normally takes two attempts, because the
  attempt that advances G can't count its own thread, whose counter is
  still the old value until its next quiescent point. The pending count
  includes cleanups made runnable but not yet run, and no worker sleeps
  while a runnable cleanup is untaken.
  When every worker sleeps, a **ticker source** keeps grace periods
  moving: while cleanups are pending, the last worker to go to sleep waits
  with a timeout (about a second) on the eventcount, and on timeout
  activates the ticker, whose turn is a no-op ranked lowest; that turn's
  end is a quiescent point and its worker's next request makes the
  attempt. One timeout suffices even though a cleanup takes two attempts:
  the woken worker's attempt advances G, the ticker turn's end copies it,
  and that worker's next attempt makes the cleanup runnable. With nothing
  pending nobody wakes, so an idle process costs no
  power. The timed wait is the ticker's private implementation; actors
  still have no timers. Delayed goals (`Open`) may later use the ticker as
  their clock.
- **Generic:** the Dispenser owns it; TerrainNodes, store entries and any
  shared data reached outside the owner's turns get it for free. A long or
  stuck turn, a thread that fails to go offline, or an online frame
  thread that stops ending frames, holds back every cleanup in the
  process, so grace-period age is watched (the release counters, "Checks").
- **Other threads** take part on the same terms only if they read
  reclaimable data (the network thread, perhaps); the audio callback never
  does.
- **Open:** the remaining races are `GoalSource`'s pending issues.

## Conditions and chaining (draft)
- **A condition is one-shot, enforced by its type:** its public surface
  is `fired()` (acquire) and being waited on; firing is private to the
  Dispenser and nothing resets it, except the pool when it recycles the
  actor that owns it (`Pools and recycling`). Once fired it stays fired
  for that incarnation, so everything built on it is monotonic.
- **Every function actor has one, `done()`,** fired only by the Dispenser
  after `runTurn` returns, so results are always written before it fires,
  and it carries an **outcome**, Completed or Abandoned. It is the one
  source of truth for "K has finished".
- **Used by function actors and continuations of stateful actors;**
  `GoalSource` keeps its own dependency bookkeeping in its goal actors'
  records under its mutex; whether its readiness should reuse conditions
  is open (`engine/goal-source`).
- **Waiting never blocks:** `when(conditions…, actor)` makes an actor
  runnable once every condition has fired: a function actor's one turn,
  or a nudge for a stateful actor (the World wanting a follow-up turn).
  Results, parking on work in flight and continuations are one
  mechanism; halts may later be expressed the same way.
- **Mechanism:**
  - **a condition is an atomic list of waiters:** firing swaps the head
    for a sealed marker and walks the old list; waiting pushes with a CAS,
    and finding the list sealed means already fired (the continuation
    list of futures and promises, as in folly or Rust's waker lists), so
    adding a waiter never races with firing, at O(1) per edge;
  - **a waiting actor holds a counter of unfired conditions,** and the
    firing that takes it to zero makes it runnable, through the existing
    idle-to-scheduled CAS into its source's runnable set; nothing polls,
    and no planning turn sits between a parent finishing and its child
    becoming runnable;
  - **waiter nodes are embedded in the waiting actor,** one per condition
    it waits on, so chaining allocates nothing;
  - **firing touches a waiter node last,** reading its `next` before
    decrementing the waiter's counter, so a waiter whose counter has
    reached zero has no links left in any list.
  ```cpp
  class Condition {                          // one-shot per incarnation
  public:
      bool fired() const;                    // acquire
  private:
      friend class Dispenser;
      void fire(Outcome);                    // seal the waiter list, notify each waiter
      std::atomic<Waiter*> head_{nullptr};   // sealed once fired
  };

  void when(std::span<const Condition* const>, ActorHandle);  // any actor
  ```

## Abandoning work (draft)
- **Most waste is in work not yet started,** and with goal actors,
  withdrawing one abandons it and, by cascade, every dependent not yet
  started, none of which ever runs (`engine/goal-source`).
- **Cooperative abandonment is up to the turn:** a function actor or goal
  actor may check its flag at yield points, but only unusually
  long-running turns are expected to.
- **A function actor's state machine** is the only path to firing
  `done()`:
  ```
  Pending ──pull──▶ Running ──turn ends──▶ Completed
     │                 │
     └──abandon()──▶ Abandoned ◀──yield point sees the flag──┘
  ```
  - **`abandon()` is any-thread.** On a Pending actor a CAS to Abandoned
    fires `done(Abandoned)` at once, racing safely with `pullBatch` (one CAS
    wins). On a Running actor it sets a flag the turn checks at its yield
    points (between Cartographer calls, between levels, between meshing
    stages: a relaxed load), and `done(Abandoned)` fires when the turn ends.
    The Cartographer keeps no cancellation parameter
    (`invariant/cartographer/no-cancellation`): the check is in the function
    actor between calls.
  - **Abandonment ripples:** a waiter whose condition fires Abandoned is
    abandoned itself without running and fires its own `done(Abandoned)`,
    so every waiter sees a definite outcome. Chains of dependent work are
    goal actors, abandoned by GoalSource's cascade (`engine/goal-source`);
    rippling covers function actors and actors waiting on conditions.
  - **Partial results of abandoned work are discarded,** unless
    measurement shows keeping finished levels pays.
  ```cpp
  enum class Outcome { Completed, Abandoned };
  class FunctionActor {
  public:
      void abandon();                        // any thread
      const Condition& done() const;         // fires once per incarnation
      Outcome outcome() const;               // once done() has fired
  protected:
      bool abandoned() const;                // for the turn's yield points
  };
  ```

## Pools and recycling (draft)
- **Short-lived function actors are pooled per type,** since they are
  made and retired constantly: `make` takes
  one from the pool's free list (or grows it) and sets its inputs; nothing
  allocates per unit of work. **TerrainNodes are not recycled actors:**
  allocated once, built out and long-lived (seconds to minutes in view,
  churning at tens to a few hundred per second only in fast movement), and
  never reset; they live in a **slab** (type-stable memory, as the pointer
  rules' generation check requires, `sim/concurrency-model`), and retiring
  one returns its slot to the slab after a grace period, bumping the slot's
  generation.
- **`reset()` is private to the pool** and the only way to reopen a
  condition. It is valid only on an actor whose state is Completed or
  Abandoned and every condition it waited on has fired (its counter of
  unfired conditions is zero, so it is in no waiter list), and only as the
  work of a cleanup actor (`retire`, `Reclamation`), so nothing still reads the old
  incarnation.
- **`reset()` bumps the actor's generation,** which `Borrow<T>`'s
  generation check compares on every dereference, fatally in release
  (`sim/concurrency-model`'s pointer rules): no code relies on a stale
  reference failing, since under the rules none exists. Pools are
  type-stable memory, as the check requires.
- **Checked:** `recycle` asserts the state and the zero counter in every
  build (two atomic loads).
  ```cpp
  template <class A> class FunctionPool {    // one per function-actor type
  public:
      A* make(Inputs…);                      // from the free list, else grow (by a rule)
  private:
      void recycle(A*);                      // from a cleanup actor: terminal, no links
  };
  ```

## The root actor, halts and shutdown
- **System is the Dispenser's single, distinguished root actor**, created
  by `start` and the root of the actor hierarchy (System → World →
  Vicinity and Registrar, `sim/concurrency-model`). A parent acts only on its
  children: it sets their desired state, or halts them with a
  continuation.
- **From outside the Dispenser, the only way in is an intention to
  System:** shut down, load a world, pause a world, resume a world. Each
  gives System a turn that does the work (loading a world is such a turn).
  The only way to learn the result is System's published state, polled:
  which worlds are loaded, paused or running, and whether System has shut
  down. Waiting for shutdown is polling System for "shut down"; together
  these are the one bridge between the actor hierarchy and the outside
  world.
- **Observing is a parent turn:** a parent never waits within a turn; it
  halts its children with a continuation and ends its turn, and the
  Dispenser runs the continuation as a later turn of the parent once they
  are quiet.
- **The Dispenser knows the hierarchy:** each source registers under its
  parent's node (`ParentOf<…>` makes a wrong parent a compile error), so
  the Dispenser holds the tree: System at the root, then Worlds, then each
  World's Vicinities and Registrar. For every node it keeps:
  - **a halt flag, owned by the Dispenser, not the source.** A node is
    effectively halted if it or any ancestor is halted, and the Dispenser
    never calls `pullBatch` on an effectively halted source, so source code
    can't break a halt even by mistake;
  - **a subtree in-flight count:** batches running anywhere in the node's
    subtree (a function actor counts at its source's node), incremented on
    every ancestor when a batch is dispatched and
    decremented on completion (the tree is at most four deep, so a handful
    of atomic adds per batch).
  Mail and nudges still reach a halted subtree and mark work as pending;
  the predicate below is about what is dispatched, so they don't touch it.
- **One call halts, and its continuation is the wait:**
  ```cpp
  // In the parent's turn only (its passkey). Halts the children's subtrees,
  // and runs `then` as a later turn of the parent once all are quiet.
  HaltToken<Child> halt(ParentTurn&, std::span<Child*>, Continuation then);
  void resume(HaltToken<Child>&&);   // consumes the token
  ```
  There is no separate "please halt" and "register a waiter": the halt
  intent and the continuation are one request, so nothing can be waited on
  that wasn't taken, and no parent waits on anything it didn't halt. Merge
  and cleave halt two Vicinities with one call and one continuation. Only
  the token's holder can resume.
- **The halt token is also the parent's passkey to a halted child.** While
  a parent holds a quiet child's token, no turn of the child can run, so
  the parent's own turns (its continuation) may call the child's turn-only
  API, which accepts the token in place of the child's turn passkey; that
  is how a World merges and cleaves Vicinities (`sim/vicinity`). A
  halted child runs nothing itself; work a child must do as an actor
  (a Vicinity's save and shutdown) runs after it is resumed in a state
  that offers only that work (frozen, `sim/vicinity`).
- **Halt intent flows down; each parent orders its own children's halts:**
  - a **leaf** (a Vicinity, whose chunks run through it; the Registrar)
    stops being offered at once; when its subtree count of batches in
    flight reaches zero it is quiet;
  - a node **with actor children** is given a **halting turn** first, and
    keeps running: in it, it halts its children itself, in the order it
    needs, each with a continuation (a World halts its Vicinities, which
    save while frozen and hand their captures to its Registrar, then halts the
    Registrar once the commits are sent); its last continuation completes
    its own halt, and only then is it dispatch-halted and quiet;
  - when every child named in a `halt` call is quiet, the caller's
    continuation runs as a turn of the caller.
  So a parent's coordination is never blocked by a halt above it, and a
  parent controls the order (the Registrar must still run while the final
  saves are committed, which halting all children at once would forbid). A
  halted node runs nothing further. Resuming releases halts top-down.
- **"Quiet"** is dispatch-halted with a subtree in-flight count of zero;
  **"shut down"** is quiet plus deregistered. These are the only conditions
  a continuation waits for; anything else (such as "loaded around the
  spawn") is published state a parent rechecks in its ordinary turns.
- **Why quiet is stable, by construction:** while a node is halted nothing
  in its subtree is dispatched, so its subtree count can only fall; zero is
  therefore absorbing; and only the token's holder can resume.
  Deregistration is irreversible, so "shut down" is stable too.
- **The continuation needs no nudge:** the completion that makes the last
  named child quiet is the moment the condition holds; the Dispenser sees
  it and makes the continuation runnable as one of its own items (like exit
  items). It runs as a turn of the caller (the worker marker names the
  caller), never alongside another turn of the caller, and exactly once.
- **Three levels of halt:**
  - **Vicinity:** for merge and cleave and frozen saves
    (`sim/vicinity`); the World halts the Vicinities involved (planned by
    its Surveyor component), and its continuation does the merge, cleave or
    save.
  - **World:** for pause or for shutting the world down; System halts the
    World (a paused World truly has nothing to do, so pause comes from
    outside it), and the World's halting turn halts its Vicinities, then
    its Registrar, as above (`engine/world`). System resumes it on a resume intention. In
    multiplayer, the server's System pauses a World once the players agree
    (`persist/presence-not-absence`).
  - **System:** every world, for a clean process shutdown; System halts its
    Worlds for shutdown, and its continuation reports System shut down.
- **Clean process shutdown**, initiated by the main thread (which normally
  runs the frame thread):
  1. it sends System the shutdown intention, which gives System a turn;
  2. System halts every World for shutdown; within each World the halt
     runs in the World's halting turn: it halts its Vicinities, which save
     while frozen and send their captures, with its records, to the Registrar,
     deregisters the Vicinities, then halts and deregisters the Registrar
     once the commits are sent; System's continuation runs once every World
     is shut down;
  3. meanwhile the main thread keeps running the frame loop (a "saving…"
     screen) and polls System's published state once per frame, never
     blocking;
  4. once System is shut down, it calls `destroy()`: there are no sources
     left, the workers take their exit items and are joined, the one real
     block, by then microseconds.
- **Warm and cold idle:** with nothing advertised, workers sleep on the
  eventcount and wake within microseconds (warm); after shutdown, `destroy()`
  sends them exit items and joins them (cold).

## Lockstep mode
The reference configuration for tests and reproducing bugs is one worker
in strict alternation with the frame thread (or a test driver), passing a
baton (two binary semaphores, or a condition variable with a turn flag):
- the worker runs a **counted** share of turns per handoff (a fixed number,
  or until the ready set is empty), never a timed one, chosen by the
  Dispenser's seeded draw, so runs are reproducible;
- the frame thread still runs no turn, so every check stays unconditional,
  and the threads are real, so ThreadSanitizer still sees the handoff's
  happens-before edges;
- lockstep versus N workers: generation, edits and later conserved state
  must match exactly; soft simulation must converge
  (`sim/concurrency-model`). Selected only by tests or a debug flag.

## Worker count
- A setting, defaulting to the number of cores minus two: one for the frame
  thread and one left for the OS and everything outside the game.
- **At least one free-running worker in play, whatever the formula gives**
  (a 2-core machine still gets one).
- I/O items add threads for their duration (above); the count changes only
  between work items.

## Checks
These mechanisms check the invariants below; each invariant's
**Enforced** line names the ones it relies on.

- **Build boundary** (type, compile time): turn code can't see
  `dispenser_lifecycle.h` or `dispenser_source.h`; thread creation and the
  worker loop are private to the Dispenser's translation unit.
- **API shape** (type, compile time): `pullBatch` returns to the calling
  worker only; a batch is a move-only token consumed by completion or
  `returnRemainder` (a second use doesn't compile; a token destroyed
  unconsumed asserts).
- **Release counters** (telemetry or watchdog): outstanding batches (never
  negative, zero at shutdown); advertised readiness and sleeping workers (a
  watchdog flags readiness with every worker asleep for more than a few
  milliseconds); grace-period age (a watchdog flags a grace period held
  back by one turn, frame or thread for longer than a threshold).
- **The worker marker** (fatal check; one thread-local read): the
  Dispenser sets it around every work item, recording the current actor;
  the Cartographer asserts it names a goal or function actor, and
  turn-only APIs assert the right actor.
- **Stress and model** (model and test; planned): TLA+ models of the
  eventcount (no lost wakeup, wake-one, chain wake) and of halts (no
  dispatch under a halt, zero absorbing, a continuation firing once and
  only after its children are quiet, bottom-up order), both due before
  v0.2.19 builds worker threads (halts are built single-threaded in
  v0.2.17); plus stress tests (`engine/graphics-api`'s formal targets).
- **Halt asserts** (fatal check): a node is dispatch-halted only once its
  children are quiet (bottom-up order); dispatch asserts no ancestor of the
  source is dispatch-halted; subtree counts never go negative and never
  increment under a halt; a continuation asserts its children are quiet
  when it runs; halt tokens, and halting only one's own children, are
  compile-time.

## Invariants
- **`invariant/dispenser/invisible-pool`** — The pool is invisible to
  clients: no client can name, count or address a worker.
  - **Enforced:** type: the build boundary.
- **`invariant/dispenser/no-submit`** — Only sources and actors create
  work: turn code has no Dispenser handle and no submit call, and makes
  work only by sending mail or nudges.
  - **Enforced:** type: the build boundary.
- **`invariant/dispenser/stores-no-work`** — The Dispenser stores no work:
  nothing taken from a source waits inside it, and a batch is taken by the
  worker that runs it.
  - **Enforced:** type: the API shape.
- **`invariant/dispenser/waits-only-at-acquisition`** — A worker waits only
  at its acquisition point, and only while nothing is ready (or, in
  lockstep mode, for the baton).
  - **Enforced:** type: the build boundary (no blocking API of our design
    is visible to turn code).
- **`invariant/dispenser/no-lost-wakeup`** — If any source advertises work,
  some worker is awake or will wake.
  - **Enforced:** telemetry or watchdog: the release counters' readiness
    watchdog; model and test: stress and model (planned, before v0.2.19).
- **`invariant/dispenser/non-blocking-signalling`** — Signalling never
  blocks and makes no decisions: `advertise` and the idle-to-scheduled flip
  are lock-free (atomics plus at most one wake).
  - **Enforced:** review: the API shape, whose signatures and bodies are
    reviewed to stay lock-free; telemetry or watchdog: the release
    counters.
  - **Why:** so the frame thread is never slowed by scheduling.
- **`invariant/dispenser/sources-never-block`** — `pullBatch` and
  `returnRemainder` return at once and don't wait on our own design (a
  short critical section, such as resolving a goal source's best goal, is
  allowed).
  - **Enforced:** review: `pullBatch` and `returnRemainder` bodies;
    telemetry or watchdog: a duration histogram per source
    (`engine/telemetry`).
- **`invariant/dispenser/every-source-served`** — Every ready source is
  eventually served, urgent first, then priority with ageing.
  - **Enforced:** telemetry or watchdog: the release counters (per-class
    wait times, in debug).
- **`invariant/dispenser/thread-count-between-items`** — The thread count
  changes only between work items, cooperatively, never below one; exit
  items are served first and the Dispenser joins the exiting thread.
  - **Enforced:** type: the API shape.
- **`invariant/dispenser/batch-completed-once`** — Every dispensed batch is
  completed or given back exactly once.
  - **Enforced:** type: the API shape (a token destroyed unconsumed is a
    fatal check); telemetry or watchdog: the release counters.
- **`invariant/dispenser/worker-marker`** — The Dispenser alone sets the
  worker marker, around each work item.
  - **Enforced:** type: the build boundary (the setter is private).
- **`invariant/dispenser/work-only-through-sources`** — Work enters only
  through sources: the Dispenser runs nothing a source didn't hand it,
  apart from its own exit, halting and continuation items.
  - **Enforced:** type: the build boundary and the API shape.
- **`invariant/dispenser/no-dispatch-under-halt`** — Nothing is dispatched
  under a halt: no batch of an effectively halted node's subtree starts.
  - **Enforced:** fatal check: halt asserts; model and test: stress and
    model (planned, before v0.2.19).
- **`invariant/dispenser/holder-releases-halt`** — Only the holder releases
  a halt, and a parent waits only on children it halted itself (halt and
  continuation are one call).
  - **Enforced:** type: `HaltToken` and `halt`'s signature.
- **`invariant/dispenser/continuation-once`** — A continuation runs exactly
  once, after every child it names is quiet, as a turn of the caller and
  never alongside another turn of the caller.
  - **Enforced:** fatal check: halt asserts; model and test: stress and
    model (planned, before v0.2.19).
- **`invariant/dispenser/bottom-up-halts`** — Halts take effect bottom-up:
  a node is dispatch-halted only once its children are quiet and its
  halting turn's last continuation has run; the parent chooses the order
  of its children's halts.
  - **Enforced:** fatal check: halt asserts.
  - **Why:** so no coordination turn waits behind a halt above it.
- **`invariant/dispenser/function-actor-one-turn`** — A function actor runs
  at most one turn per incarnation (an abandoned one may run none), reading
  only its input members and the immutable data they point to, and writing
  only its own members; others reach its results only through its
  any-thread methods, after `done()` has fired Completed. Its `done()`
  fires exactly once per incarnation, only from the Dispenser, and only
  `reset()`, from the pool, on a released, terminal actor in no waiter
  list, starts a new incarnation.
  - **Enforced:** type: the build boundary (passkeys, private fire and
    reset); telemetry or watchdog: the release counters; fatal check: the
    worker marker.
- **`invariant/dispenser/grace-period`** — A cleanup actor registered with
  `retire` runs only after every online thread has passed a quiescent
  point since it was registered: a worker at the end of each turn, the
  frame thread at the end of each frame, and a thread going offline at
  that moment. The callers' side (borrows within a turn, unlinking before
  `retire`) is the pointer rules of `sim/concurrency-model`.
  - **Enforced:** model: TLA+ `QSBR` `NoUseAfterFree`; telemetry or
    watchdog: the grace-period-age watchdog of the release counters.

## Open
- The full set of System intentions beyond shutdown, load, pause and
  resume (unloading a world, switching which world the frame thread
  shows), and how a multiplayer pause vote reaches System.
- The priority scale and the ageing rate.
- Batch naming.
- Latency-critical work: a high-priority class for player-body steps (and
  a multiplayer client's local physics), perhaps with a worker kept free
  for it, and short fast turns for player bodies against capped bulk
  turns (`sim/vicinity`'s turn budget). Noted as policy, tunable, not yet
  specified.
- How cross-world fairness is expressed (per-world shares, or the
  Surveyor's priorities per world).
- **Demand-driven work:** `GoalSource`'s own open questions are in
  `engine/goal-source`. Also open here: whether halts become conditions;
  how the ticker source would serve delayed goals.
