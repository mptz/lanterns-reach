# GoalSource: scenarios, invariant catalogue and open questions (proposal)

Working notes for `engine/goal-source` (`design/goal-source.md` on this
branch). The scenarios walk the participants through the transition table
step by step to find gaps; the invariant catalogue states every property
precisely enough to check, and marks where each is checked; the open
questions are what the walkthroughs turned up, each with a proposed
answer. Once the questions are ruled on, the catalogue becomes the model
brief.

Notation: **[M]** a step under GoalSource's mutex; **[T:x]** a step in
actor x's turn; **[W:k]** a step on worker k outside any turn's body.
S_P is a sibling set (eight OctreeNodes, one goal actor), M_n a mesh goal
actor for node n, C a cleanup actor.

## Scenarios

### S1. Refining a box several levels at once
The Surveyor wants node n at level L+3, below a completed sibling set
S_0 at level L.
1. [T:World] Allocates S_1, S_2, S_3 in the World's slab, links each into
   the tree under its parent node, then registers them in order:
   `registerGoal(S_1, {S_0})`, `registerGoal(S_2, {S_1})`,
   `registerGoal(S_3, {S_2})`, and `registerGoal(M_n, {S_3})`.
   - [M] S_1: S_0 Completed and not withdrawn, so n = 0; runnable.
   - [M] S_2: n = 1; S_2 is added to S_1's dependents. S_3 and M_n
     likewise.
2. [W:1] `pullBatch` [M]: claims S_1 (Pending, n = 0, S_0 Completed and
   not withdrawn); Running; S_1 stays in S_0's dependents list until it is
   terminal. Outside the mutex, S_1's turn starts at once.
3. [T:S_1] Reads its parent node's grids through `DepPtr<SiblingSet>`
   (plus the parent's index within S_0), computes its eight nodes'
   composed fields, writes them as its own data.
4. [W:1] Turn end [M]: not withdrawn, so Completed (release store); S_1
   leaves S_0's dependents list; S_2's n drops to 0; runnable; advertise.
   After the mutex: `S_1.done()` fires Completed.
5. Steps 2–4 repeat for S_2, S_3, then M_n, whose turn builds the mesh
   and sends it to the frame thread as a message.
6. [T:World] Learns (through `when()` on `M_n.done()`, or a batched
   notice) that M_n completed; withdraws it, since its data has gone as a
   message: `withdraw({M_n})` [M] withdrawn; it had no dependents; then
   adds M_n to a cleanup C (asserting it is withdrawn) and calls
   `retire(C)`.
- **Found:** a mesh goal actor is useless once its message is sent, so
  every mesh costs the client a follow-up withdraw (Q9). Registering before
  or after linking is the client's choice, as long as an unregistered goal
  actor reads as not Completed (Q10).

### S2. Evicting a parent while a child is running
S_C is Running, reading its parent through a `DepPtr` into S_P (Completed).
1. [T:World] Unlinks S_P from the tree, then `withdraw({S_P})`, then adds
   S_P to a cleanup C and calls `retire(C)`:
   - [M] S_P withdrawn, still Completed. Cascade over S_P's dependents
     list: S_C is listed but Running, so untouched; M_P, Pending on S_P,
     is Abandoned (and leaves the list), and the cascade continues to
     M_P's dependents (none).
   - After `withdraw` returns: `retire(C)` tags C with t = G + 1;
     `M_P.done()` fires Abandoned.
2. [T:S_C] Keeps reading S_P. Its worker has been online since before its
   pull, so its counter is below t, and C can't run yet.
3. [W:2] S_C's turn ends [M]: not withdrawn, so Completed; it leaves S_P's
   dependents list, now empty. The worker's quiescent point copies G.
4. A later scan finds every online counter at least t: C runs, asserts
   S_P's dependents list is empty, and frees S_P.
5. [T:World] Learns M_P was abandoned, unlinks and withdraws it.
- **Found:** withdrawing an already Abandoned goal actor isn't in the
  transition table (Q1).

### S3. Evicting a parent while a child is pending
S_P is Running; S_C (dep S_P) and S_G (dep S_C) are Pending.
1. [T:World] Unlinks S_P, `withdraw({S_P})`: [M] S_P withdrawn, still
   Running. Cascade: S_C Pending → Abandoned; S_C's dependents: S_G
   Pending → Abandoned. Each leaves its dependency's list. After the mutex:
   `S_C.done()` and `S_G.done()` fire Abandoned. The World then adds S_P
   to a cleanup C and calls `retire(C)`.
2. [W:1] S_P's turn ends [M]: withdrawn, so Abandoned; it publishes
   nothing; its dependents list is already empty. `S_P.done()` fires
   Abandoned after the mutex.
3. C runs after S_P's worker passes its quiescent point.
4. [T:World] Unlinks and withdraws S_C and S_G (Q1 again).
- **Found:** the client must withdraw every goal actor it registered,
  including those a cascade abandoned, so it needs to learn of
  abandonments in bulk (Q9).

### S4. Registering against a dependency another client withdraws
The Vicinity's joining actor J depends on the World's leaf sibling set
S_L (a cross-client dependency). The same case arises within one client:
a client turn can find an actor in its own index, withdraw it, and then
register a dependent against the actor it found, so the check at
registration guards both (the model confirms: removing it fails with one
client too).
1. [T:Vicinity] Finds S_L through the World's published index (a borrow,
   `invariant/concurrency/borrow-one-turn`), allocates J with a `DepPtr` to S_L.
2. [T:World] (concurrently, on another worker) unlinks S_L, then
   `withdraw({S_L})`: [M] withdrawn, still Completed. Cascade: none yet.
   Then the World's cleanup for S_L.
3. [T:Vicinity] `registerGoal(J, {S_L})`: [M] S_L withdrawn, so J is
   Abandoned at once. After the mutex: `J.done()` fires Abandoned.
4. Had step 3 come before step 2, J would be Pending or Running: Pending
   is abandoned by step 2's cascade; Running is covered by reclamation.
5. The Vicinity's borrow of S_L in step 1 is valid through step 3 because
   its turn is still running (`invariant/concurrency/borrow-one-turn`), so
   the check in step 3 reads live
   memory.
- **Found:** the withdrawn check at registration guards cross-client
  dependencies (joining, Q6) and the single-client find-withdraw-register
  sequence alike.

### S5. Withdrawn between claim and turn start
GoalSource has no batching: each pull claims one goal actor, B, whose
turn starts at once on the same worker.
1. [W:1] `pullBatch` [M]: B Running; B stays in its dependency D's list.
2. [T:World] (on another worker, in the microseconds before B's body
   starts) unlinks D, `withdraw({D})`: [M] D withdrawn; the cascade finds B
   listed but Running, so leaves it. Then `retire(C)` with D in
   C tags t = G + 1.
3. [T:B] Reads D. Worker 1 has been online since before step 1, so its
   counter is below t and C can't run before B's turn ends.
4. [W:1] B's turn ends [M]: Completed; B leaves D's list. C runs later.
- **Result:** correct; at worst B's work is redundant if the client
  doesn't want it. With batching, B could be returned unstarted and later
  pulled on a worker that wasn't online at the withdraw, a use-after-free;
  batches of one remove that path.

### S6. A save
A Vicinity halts for a save; its Registrar commits.
1. [T:Vicinity] For each changed chunk, copies its cells (or moves
   copy-on-write palettes) into a new blob-write goal actor W_i as an
   input; allocates the commit K, with a `DepPtr` to each W_i (it reads
   each blob's version and hash).
2. Ownership and registration: the Vicinity has the data, the Registrar
   owns the save (Q5). Proposed: the Vicinity sends the goal actors
   (`unique_ptr`s) to the Registrar in a message; [T:Registrar] registers
   each W_i with no dependencies, then K with all of them.
3. [T:W_i] Encodes, compresses, writes the blob under a versioned key; its
   data is the version and hash.
4. [T:K] Reads every W_i's version and hash, writes the manifest.
5. [T:Registrar] On `K.done()` Completed, withdraws K and every W_i.
- **Failure:** W_3 fails (disk full) and abandons with a reason; the
  cascade abandons K; the Registrar learns from `K.done()` (Abandoned),
  reads W_3's reason, and decides whether to retry with new goal actors
  (versioned keys make the retry harmless). It still withdraws every W_i.

### S7. Fast flight: a speculative region withdrawn in bulk
The Surveyor registered a few thousand goal actors ahead of a fast mover,
which turns away.
1. [T:World] Unlinks them all, then one `withdraw(span)`: [M] per chunk
   of the span, each marked withdrawn, with its cascade over Pending
   dependents; the mutex is released between chunks.
2. [T:World] After `withdraw` returns: adds them all to one cleanup C
   (each asserted withdrawn) and calls `retire(C)` once.
- **Found:** first written as thousands of separate calls, each with its
  own `retire`; decided as one span and one cleanup (Q7).

### S8. Priorities across a chain
M_n (high, near the viewer) depends on S_3, which depends on S_2.
1. [T:World] `registerGoal(M_n, {S_3}, high)`: [M] S_3's effective
   priority rises to high, then S_2's, up the Pending chain (O(depth)).
2. M_n withdrawn: S_3's and S_2's effective priorities should fall, which
   needs each one's maximum over its remaining Pending dependents
   (existing pending issue).

### S9. Cooperative abandonment
S_P's turn is long (a root's per-field grids). The World withdraws it
mid-turn.
1. [M] withdrawn set; the turn keeps running.
2. [T:S_P] At a yield point it reads its withdrawn flag outside the mutex
   and returns early; [M] at the end: withdrawn, so Abandoned.
- **Found:** the withdrawn flag is read outside the mutex, so it's an
  atomic (Q4).

### S10. Unloading a World
The World withdraws every goal actor it registered, in any order; each
withdraw cascades over what is still Pending. After the last grace period
the slabs are empty. A debug check at unload: every goal actor registered
was withdrawn exactly once.

## Invariant catalogue
Each invariant gives where it is checked: **TLA** in the GoalSource model,
**C++** as an assertion (fatal in release unless marked debug), **type**
at compile time. "After every critical section" means in every state the
model reaches between atomic steps.

Variables: for each goal actor x, `state(x)` ∈ {Unregistered, Pending,
Running, Completed, Abandoned}; `withdrawn(x)` boolean; `n(x)` integer;
`deps(x)` a set fixed at registration; `dependents(x)` a set;
`started(x)` (its turn's body has begun); `freed(x)`; plus the runnable
set R, and for each worker the goal actor whose turn it runs.

### A. The state machine
- **A1.** `state(x)` changes only by the transition table's rows. TLA (the
  actions are the table); C++ (one setter, asserting the row).
- **A2.** Completed and Abandoned are terminal. TLA, C++.
- **A3.** `withdrawn(x)` never goes from true to false. TLA, C++.
- **A4.** `withdraw(x)` is called at most once per goal actor. TLA (as an
  assumption on the client), C++.
- **A5.** `state(x)` = Completed implies x's turn ran to completion while
  `withdrawn(x)` was false. TLA.
- **A6.** A Running goal actor that is withdrawn ends Abandoned. TLA.
- **A7.** `done(x)` fires exactly once, after x reaches a terminal state,
  with outcome equal to that state. TLA, C++.

### B. Counts and edges
- **B1.** For every Pending x: `n(x)` = |{d ∈ deps(x) : state(d) ≠
  Completed}|. TLA; C++ (debug audit).
- **B2.** `n(x)` ≥ 0. TLA, C++.
- **B3.** `dependents(d)` = {x : d ∈ deps(x) ∧ state(x) ∈ {Pending,
  Running}}, after every critical section: every dependent not yet
  terminal is listed (so the cascade sees it) and nothing else is. TLA;
  C++ (debug audit).
- **B4.** No Pending x has a dependency that is Abandoned or withdrawn,
  after every critical section. TLA; C++ (debug audit).
- **B5.** R = {x : state(x) = Pending ∧ n(x) = 0}. TLA; C++ (debug audit).
- **B6.** `deps(x)` never changes after registration; every dependency
  was registered before x; dependencies form a DAG; x and its
  dependencies belong to one World. C++ (by construction, asserted).

### C. Pointer safety
- **C1.** A dependency pointer is dereferenced only by its holder's turn
  while the holder is Running and started. Type (the `GoalTurn&`
  passkey); TLA (reads happen only in the turn's actions).
- **C2.** When x is claimed, every d ∈ deps(x) is Completed and not
  withdrawn. TLA; C++ (asserted at claim).
- **C3.** No goal actor's memory is read after it is freed: no turn
  dereferences a `DepPtr` to a freed target, and no critical section
  touches a freed record. TLA (the central safety property, with
  reclamation abstracted as below); C++ (the generation check).
- **C4.** `freed(x)` implies `withdrawn(x)`, x ∉ R, x is in no
  `dependents(d)`, and `dependents(x)` = ∅. TLA; C++ (asserted when the
  cleanup runs, since Running dependents leave the list only at their
  turns' ends).
- **C5.** x is freed only after every turn that was running when x was
  withdrawn has ended (reclamation, abstracted: the model need not include
  counters, since `model/QSBR.tla` checks that part). TLA.
- **C6.** A claimed goal actor's turn starts on the claiming worker with
  no other claim in between, and nothing returns it unstarted
  (`returnRemainder` never receives a goal actor). TLA (claim and start
  as consecutive steps of one worker, with other actors' steps allowed
  between them); C++ (asserted).

### D. The client contract
These are assumptions in the model and conventions or asserts in C++.
- **D1.** The client unlinks x from its index before `withdraw(x)`; after the
  withdraw, no new lookup finds x. TLA (lookups as an action that only
  returns linked goal actors); C++ (convention).
- **D2.** A client registers x only with dependencies it found in an index
  during the same turn, or registered itself. TLA (registration chooses
  among linked or own goal actors); C++ (convention).
- **D3.** Every registered goal actor is eventually withdrawn exactly once.
  TLA (as a fairness assumption, for F3); C++ (debug check at World
  unload).

### E. Readers outside GoalSource
- **E1.** A goal actor's data is read outside its own turn only when its
  state is Completed (acquire). C++ (the one read helper).
- **E2.** A goal actor's data never changes once Completed. Type (`const`
  after publication); C++.

### F. Liveness
Under weak fairness of `pullBatch` and of turns ending.
- **F1.** A Pending x that is never withdrawn, and whose dependencies all
  become Completed without being withdrawn, eventually runs. TLA.
- **F2.** A Running x eventually reaches a terminal state. TLA (turns end).
- **F3.** Every withdrawn x is eventually freed. TLA (given D3 and that
  every turn ends).
- **F4.** Every cascade terminates (the graph is a finite DAG). TLA.
- **F5.** Every waiter registered with `when()` on `done(x)` is eventually
  notified. C++ (the conditions mechanism); TLA only if modelled.

### G. Priorities (later; not in the first model)
- **G1.** A Pending goal actor's effective priority is the maximum of its
  own and its Pending dependents' effective priorities.
- **G2.** `pullBatch` claims a runnable goal actor of maximal effective
  priority (or within a stated approximation).

### H. Locking
- **H1.** Bookkeeping is touched only under the mutex; turns run outside
  it. C++ (`-Wthread-safety`; debug assert in the turn path).
- **H2.** No other lock is taken, and no condition is fired, while
  GoalSource's mutex is held. C++ (debug lock-order check).

## Open questions, with proposed answers
- **Q1. Withdrawing an Abandoned goal actor.** **Decided:** the row
  Abandoned | `withdraw` | withdrawn; still Abandoned, with no cascade (its
  Pending dependents were abandoned when it was). Every goal actor is
  withdrawn exactly once, whatever its state; withdrawing one allocated but
  never registered just frees it.
- **Q2. `returnRemainder` racing with the cascade** (S5). **Decided:**
  GoalSource has no batching, so nothing is returned unstarted, and
  dependents stay in their dependencies' lists until they are terminal,
  so the cascade sees Running ones too. A goal actor withdrawn between
  another's claim and its turn's start is safe (reclamation covers the
  turn); the cost is at most redundant work. Whether batching suits the
  Dispenser's actor API at all is a separate question (a Vicinity's many
  small chunk turns are the main case for it).
- **Q3. Firing `done()` under the mutex.** **Decided:** no nested
  locking: each critical section collects the conditions to fire, and
  GoalSource fires them after releasing the mutex (H2). The state is
  already terminal and visible before `done()` fires, which A7 allows.
- **Q4. The withdrawn flag outside the mutex** (S9). **Decided:** it is an
  atomic, written only under the mutex (release) and read by the turn at
  yield points (relaxed is enough: missing it only costs work).
- **Q5. Who owns and registers a save's goal actors** (S6). **Decided:**
  the Vicinity's halted turn builds them with the captured data moved in
  and sends them to the Registrar in a message; the Registrar registers,
  withdraws and frees them. The Registrar is the single committer, so it
  should own everything that commits.
- **Q6. Owners of shared dependencies.** **Decided:** the World owns
  every terrain-side goal actor, seed-key regions and stronghold plans
  included, so terrain has one client and no cross-client edges; the only
  cross-client edges are joining (a Vicinity's join depending on the
  World's leaf or the Registrar's blob read), which S4 covers.
- **Q7. Bulk withdrawals** (S7). **Decided:** withdrawing and freeing are
  two operations coupled only by order. `withdraw(span)` is GoalSource's
  counterpart of an unlink (no dependent that hasn't started ever starts;
  it frees nothing), and may release the mutex between chunks of the
  span. Freeing is the ordinary cleanup: one cleanup actor for everything
  withdrawn in a turn, one `retire`, registered after
  `withdraw` returns. Adding a goal actor to a cleanup asserts it is
  withdrawn; the cleanup asserts no references remain when it runs.
- **Q8. The octree's index when a parent is evicted before its
  children.** **Decided (an octree policy, not GoalSource's):** a node is
  never evicted while it has resident descendants; coarsening evicts
  children first, so the resident tree stays connected and plain pointers
  suffice. Ancestors of resident nodes add at most about a seventh more
  nodes. Recorded in `world/surveyor`.
- **Q9. Learning of outcomes.** **Decided:** GoalSource keeps no lists for
  clients; its only outputs are each goal actor's `state()` and `done()`.
  The client tracks what it registered in its own structures, reads
  states in its turns, and prunes and withdraws as it sees fit; `when()` on
  a `done()` gives a prompt nudge where one is needed (a join, the World
  once a sibling set's meshes are done).
- **Q10. Registration before or after linking.** **Decided:** not
  GoalSource's concern; the client chooses for its own readers.
  `state()` reads Unregistered as not Completed, so tree readers never
  use an unregistered goal actor's data.
- **Q11. Mesh goal actors as goal actors at all.** **Decided:** they stay
  goal actors. A mesh's data leaves as
  a message, so its goal actor only carries its turn. That is fine under
  "all compute is in a turn", and Q9 makes its withdraw cheap; noted so the
  cost is visible.

## Model scope (for the brief)
- **Shapes:** a chain of three; a parent with two child sets and a mesh; a
  diamond (two dependencies with a common one), with K goal actors at most
  about six, and two workers.
- **Client:** nondeterministic registration (in an order consistent with
  B6), lookups only of linked goal actors (D1, D2), withdraws at any time,
  each exactly once (D3).
- **Reclamation abstracted** as C5.
- **Variants expected to fail:** no cascade when a Completed goal actor
  is withdrawn; no withdrawn check at registration; removing a dependent's
  edges at its claim combined with a batch returned unstarted (the S5
  race); a claim that doesn't check withdrawn; a turn's end that publishes
  Completed despite withdrawn.
- **Properties:** A1–A7, B1–B5, C1–C6, F1–F4.
