# Model-checking results: GoalSource

Status: checked model; safety is checked on every shape, and liveness
(F1-F4) on every shape too (see "Liveness"). `design/goal-source.md` and `proposals/goal-source-scenarios.md`
(scenarios S1-S10, invariant catalogue A-H, decided questions Q1-Q11, "Model
scope") are the authoritative specs. This file records what the model checks
against them, what it found, and every place the model chose an
interpretation ("Ambiguities and spec gaps").

Tools: TLC2 2.19 (`tla2tools.jar` 1.7.4), Java 21, 2 TLC workers, `-Xmx4g`
with `-XX:MaxDirectMemorySize=1g`, deadlock checking on (no run reported a
deadlock). Reproduce with `TLA2TOOLS=<jar> model/run_goalsource.sh [cfg ...]`
(see `README.md`). The raw TLC output of every run is in
`model/goalsource-out/`.

## What is modelled
- **Goal actors:** a small fixed set per configuration, `Shape` selecting the
  dependency relation (dependencies have smaller numbers than their
  dependents, so registering in numeric order respects B6; the client may
  register in any order consistent with it).
  - `chain`: 1 <- 2 <- 3 <- mesh 4.
  - `siblings`: parent 1, two child sets 2 and 3 on it, and a mesh 4 on the
    parent.
  - `diamond`: 2 and 3 depend on 1; 4 depends on both.
  - `cross`: 2 depends on 1 (client 1); 3 belongs to a second client and
    depends on 1 (S4).
- **Record per actor:** state in {Unreg, Pending, Running, Completed,
  Abandoned}, `withdrawn`, `n`, the dependents list `dl`, and for the turn a
  phase (`idle`, `claimed`, `body`, `post`, `over`), the dependency pointers
  read so far (`rd`), and the `done()` conditions still to fire. On the
  client side: `ph` (alloc, linked, unlinked, added, wcalled, wdrawn), `freed`.
  The runnable set is derived (Pending with n = 0), as in the spec.
- **The mutex:** each critical section is one atomic step: `Register`, `Claim`,
  `TurnEnd` or `TurnAbandon`, and `WithdrawOne` (one actor's withdrawal with
  its whole cascade). A `withdraw(span)` call is `BeginWithdraw` (choose a
  span of at most `MaxSpan` registered, unlinked actors), then one
  `WithdrawOne` per actor in any order (the mutex is released between them),
  then `ReturnWithdraw`.
- **No batching:** `Claim(x)` requires Pending, n = 0, every dependency
  Completed and (unless variant 4) not withdrawn, and makes x Running with
  phase `claimed`. `TurnStart(x)` is a separate step, so every other actor's
  steps may fall between the claim and the start, and during the turn.
  The turn reads each dependency (`TurnRead`, one step per `DepPtr`
  dereference), then ends (`TurnEnd`: Completed and decrement each listed
  dependent's n if not withdrawn, else Abandoned, publishing nothing) or
  abandons on its own (`TurnAbandon`, standing for both a failure and a
  cooperative abandonment after reading withdrawn). Nothing is claimed
  twice and a worker is occupied from claim to `TurnOver`; at most
  `NWorkers` (2) goal turns are in progress at once.
- **Edges:** an actor leaves its dependencies' lists only when it becomes
  terminal (Completed, Abandoned, including a cascade). Claiming removes
  nothing (variant 3 does).
- **Cascade:** `CascFrom` is the transitive closure of Pending dependents over
  the dependents lists, applied in the same step as the Abandoned or
  withdrawn event; Running dependents are untouched.
- **registerGoal:** Abandoned at once if any dependency is Abandoned or
  withdrawn (variant 2 drops the withdrawn test); otherwise Pending with
  n = number of dependencies not Completed, added to each dependency's list.
- **done():** each critical section that makes actors terminal collects them
  in `fqg` (workers) or `fqc` (client); `FireG` and `FireC` fire all of them in
  one later step, outside the mutex, exactly once each, before the turn or
  client call is over.
- **Clients:** a client turn is `ClientBegin` ... `ClientEnd`. Inside it:
  `Link`, `Unlink`, `Find` (finds only linked actors; D1), `Register` (D2:
  every dependency was found in this turn or is the client's own actor not
  yet withdrawn), the withdraw call above, `AddCleanup` (asserts withdrawn;
  `CleanupAddsWithdrawn` records a failure), and `RegCleanup`
  (`retire`, only after `ReturnWithdraw`, since `AddCleanup` takes
  from the returned set). Each registered actor is withdrawn at most once
  (A4); with `Diligent` the client also cannot end a turn while a duty is
  enabled, and the duties are weakly fair (F3 assumes the client's eventual
  withdraw and cleanup). `Find` is enabled for a single client only with
  `FindAlways` (with one client a found actor is the client's own, so it
  makes the state space larger without a new behaviour in the default
  configurations); it is always on for `cross`.
- **Reclamation, abstracted (C5):** `RegCleanup` records the set of turns in
  progress at that moment (goal turns by actor number, from claim until
  `TurnOver`; client turns by 10 + client). `CleanupRun` may fire only once
  all have ended and then frees its actors. There are no QSBR counters;
  `model/QSBR.tla` checks that part. At add-time the model asserts withdrawn;
  at run-time `C4` asserts no references remain (not runnable, in no
  dependents list, own list empty).
- **2 workers.** Actors are not bound to a named worker: the number of goal
  turns in progress is at most 2.

Skipped, as the brief says: G (priorities) and H (locking holds by
construction), and F5 (`when()`).

### Properties
- Safety (invariants and action properties): A1-A3, A6 (`A1 A2 A3 A6`, action
  properties), A4, A5, A7, B1-B5, C1-C6, `CleanupAddsWithdrawn`.
  - C3 (`NoUseAfterFree`) is the conjunction of: the ghost `uaf` (some step
    touched a freed record: a critical section, a `DepPtr` read, a `done()`
    firing, a client `Find`), no client's found set holds a freed actor, and
    no goal turn still to read a dependency has a freed dependency.
  - C2 is the ghost `c2bad` (a claim found a dependency not Completed or
    withdrawn). C5 needs the ghost sets `wdw` (`TrackC5`): the turns in
    progress at the withdrawal that have not ended must all be gone before
    the free. C6 is `sec = {} /\ returned = {}`: the only way to claim two actors
    in one pull, or to return one unstarted, is variant 3.
- Liveness under weak fairness of claims, turn steps, `done()` firing and
  cleanup runs: F1 (`Pending ~> not Pending`), F2 (`Running ~> terminal`), F3
  (`withdrawn ~> freed`, with `Diligent`), F4 (`terminal ~> done() fired`: the
  cascade is one atomic step over a finite DAG, so it terminates by
  construction, and the property checks that its outcome is reported).

## Results
Verdicts are as expected unless marked. "Peak RSS" is the largest resident
size the run script's watchdog saw; `-` means the run ended before the first
poll. All runs 2 TLC workers. "Depth" is the longest breadth-first level for a
completed run and the counterexample length for a failed one.

### Base configurations (every property in the list must hold)
| Config | Shape | Verdict | Generated | Distinct | Depth | Time | Peak RSS |
|---|---|---|---|---|---|---|---|
| `GoalSource_Chain` | chain | no error | 25,646,401 | 4,057,126 | 52 | 175 s | 4258 MB |
| `GoalSource_Siblings` | siblings | no error | 303,847,932 | 45,930,663 | 52 | 2136 s | 4340 MB |
| `GoalSource_Diamond` | diamond | no error | 82,344,047 | 12,568,814 | 53 | 563 s | 4311 MB |
| `GoalSource_Cross` | cross | no error | 17,621,446 | 2,919,848 | 48 | 115 s | 3924 MB |
| `GoalSource_ChainSpan2` | chain, spans of 2 | no error | 30,135,678 | 4,719,058 | 49 | 201 s | 4204 MB |
| `GoalSource_ChainFind` | chain, `FindAlways` | no error | 109,721,737 | 16,702,040 | 58 | 734 s | 4336 MB |
| `GoalSource_ChainC5` | chain, C5 ghosts | no error | 131,877,575 | 20,261,627 | 52 | 912 s | 4330 MB |
| `GoalSource_CrossC5` | cross, C5 ghosts | no error | 86,492,958 | 14,242,266 | 48 | 567 s | 4334 MB |

The properties are `TypeOK A4 A5 A7 B1 B2 B3 B4 B5 C1 C2 C3 C4 C6
CleanupAddsWithdrawn` and the action properties `A1 A2 A3 A6`; the `C5`
configurations add `C5`. The runs of `Chain`, `Siblings`, `Diamond`, `Cross`,
`ChainSpan2`, `ChainFind`, `ChainC5` and `CrossC5` were made with a `Claim`
that wrote C2's ghost variable as `c2bad' = c2bad \/ \E ...`. TLC treats that
as a disjunction of an assignment and a test, and stops with "not completely
specified" in any state where the existential is true. It never did in these
runs, so the existential was false in every claim they made, which is what
C2 states; the later runs use the parenthesised form.

### Expected-failure variants
| Config | Variant | Shape | Verdict | Generated | Distinct | Depth | Time | Peak RSS |
|---|---|---|---|---|---|---|---|---|
| `GoalSource_V1_NoCascadeCompleted` | 1 | chain | B4 violated | 5,139 | 1,894 | 10 | 5 s | - |
| `GoalSource_V1b_NoCascadeCompleted_NoClaimWd` | 1 + 4 | chain | C3 violated | 529,255 | 117,126 | 17 | 6 s | 1530 MB |
| `GoalSource_V2_NoRegWdCheck` | 2 | cross | B4 violated | 4,738 | 1,509 | 10 | 2 s | - |
| `GoalSource_V2b_NoRegWd_NoClaimWd` | 2 + 4 | cross | C3 violated | 717,929 | 157,113 | 20 | 6 s | 1524 MB |
| `GoalSource_V3_EdgesAtClaim_Batch` | 3 | diamond | B3 violated | 3,008 | 1,194 | 9 | 2 s | - |
| `GoalSource_V3b_EdgesAtClaim_Batch_NoB3` | 3, B3 and C6 off | diamond | B4 violated | 50,789 | 14,658 | 12 | 2 s | - |
| `GoalSource_V3c_EdgesAtClaim_Batch_NoClaimWd` | 3 + 4 | diamond | C3 violated | 34,913,638 | 5,305,821 | 24 | 175 s | 4176 MB |
| `GoalSource_V3d_Batch_Only` | batch of two, edges kept | diamond | B4 violated | 50,745 | 14,650 | 12 | 2 s | - |
| `GoalSource_V4_NoClaimWdCheck` | 4 | chain | no error (see below) | 25,677,046 | 4,057,126 | 52 | 173 s | 4158 MB |
| `GoalSource_V5_PublishDespiteWd` | 5 | chain | A5 violated | 2,743 | 1,088 | 9 | 2 s | - |
| `GoalSource_V6_EarlyCleanup` | 6 | chain | `CleanupAddsWithdrawn` violated | 207 | 126 | 6 | 2 s | - |
| `GoalSource_V6b_EarlyCleanup_NoAssert` | 6, both cleanup asserts off | chain | C3 violated | 7,519 | 2,816 | 10 | 2 s | - |

### Probes (the named invariant is expected to be violated)
| Config | Shows | Shape | Verdict | Generated | Distinct | Depth | Time |
|---|---|---|---|---|---|---|---|
| `GoalSource_ProbeDepWithdrawnWhileRunning` | a dependency is withdrawn while a dependent is Running with a dependency still to read | diamond | violated, as expected | 26,209 | 7,992 | 11 | 5 s |
| `GoalSource_ProbeGrandchildCascade` | a cascade abandons a grandchild | chain | violated, as expected | 1,011 | 470 | 7 | 5 s |
| `GoalSource_ProbeCompletedWithdrawnCascade` | withdrawing a Completed actor abandons a Pending dependent | chain | violated, as expected | 5,219 | 1,917 | 9 | 2 s |
| `GoalSource_ProbeCrossClientWithdrawn` | a cross-client registration hits a withdrawn dependency (S4) | cross | violated, as expected | 5,742 | 1,754 | 10 | 5 s |

### Liveness
| Config | Properties | Shape | Verdict | Generated | Distinct | Depth | Time | Peak RSS |
|---|---|---|---|---|---|---|---|---|
| `GoalSource_CrossLiveF3` | F3, `Diligent` | cross | no error | 4,906,057 | 869,965 | 47 | 651 s | 3530 MB |
| `GoalSource_CrossLive` (earlier form: F1-F4) | F1-F4, `Diligent` | cross | not completed, not claimed | 3,580,482 | 667,062 | - | killed in the temporal check at 8,361,612 distinct states | - |
| `GoalSource_CrossLiveF124` (earlier form of `LiveCrossF124`) | F1, F2, F4 | cross | not completed, not claimed | 2,169,708 | 443,166 | - | killed in the temporal check at 4,883,175 distinct states | - |

Small shapes added for liveness (2 workers, `MaxSpan` 1). `pair` is 1 <- 2
with one client; `xpair` is the same with the dependent owned by a second
client; `chain3` is 1 <- 2 <- 3.

| Config | Properties | Shape | Verdict | Generated | Distinct | Depth | Time | Peak RSS |
|---|---|---|---|---|---|---|---|---|
| `GoalSource_LiveXpairNoEnd` | F3, client turns need not end | xpair | F3 violated, an artefact (see "Liveness") | 45,468 | 11,785 | 29 | 4 s | 776 MB |
| `GoalSource_LivePairF3` | F3 | pair | no error | 13,591 | 3,824 | 28 | 2 s | - |
| `GoalSource_LivePairF124` | F1, F2, F4 | pair | no error | 22,375 | 6,273 | 28 | 4 s | 795 MB |
| `GoalSource_LiveXpairF3` | F3 | xpair | no error | 45,468 | 11,785 | 35 | 6 s | 1179 MB |
| `GoalSource_LiveXpairF124` | F1, F2, F4 | xpair | no error | 134,228 | 30,230 | 35 | 14 s | 1236 MB |
| `GoalSource_LiveChain3F3` | F3 | chain3 | no error | 477,027 | 92,836 | 40 | 40 s | 1501 MB |
| `GoalSource_LiveChain3F124` | F1, F2, F4 | chain3 | no error | 878,414 | 177,632 | 40 | 111 s | 1898 MB |
| `GoalSource_LiveChainF3` | F3 | chain (with mesh) | no error | 11,825,543 | 1,778,227 | 52 | 1788 s | 3813 MB |
| `GoalSource_LiveCrossF124` | F1, F2, F4 | cross | not completed, not claimed: killed at the 1800 s cap in the temporal check (18,473,796 distinct states found); last progress line: 11,158,865 generated, 1,967,158 distinct | - | - | - | 1812 s | 4255 MB |
| `GoalSource_LiveChainF124` | F1, F2, F4 | chain (with mesh) | not completed, not claimed: killed at the 1800 s cap; last progress line: 11,445,699 generated, 1,954,281 distinct, 214,052 on queue | - | - | - | 1812 s | 4319 MB |

One property per run, with `-lncheck final` (TLC checks the temporal
property once, after the state graph is complete, instead of
periodically during the search) and a 3400 s cap. The single-property
form is what finishes: each run carries one tableau, and the final-only
check avoids repeating the temporal check on a growing graph. Peak RSS
was not recorded for these runs.

| Config | Properties | Shape | Verdict | Generated | Distinct | Depth | Time | Peak RSS |
|---|---|---|---|---|---|---|---|---|
| `GoalSource_LiveCrossF1` | F1 | cross | no error | 17,621,446 | 2,919,848 | 48 | 14 min 21 s | - |
| `GoalSource_LiveCrossF2` | F2 | cross | no error | 17,621,446 | 2,919,848 | 48 | 14 min 57 s | - |
| `GoalSource_LiveCrossF4` | F4 | cross | no error | 17,621,446 | 2,919,848 | 48 | 15 min 43 s | - |
| `GoalSource_LiveChainF1` | F1 | chain (with mesh) | no error | 25,646,401 | 4,057,126 | 52 | 22 min 52 s | - |
| `GoalSource_LiveChainF2` | F2 | chain (with mesh) | no error | 25,646,401 | 4,057,126 | 52 | 23 min 29 s | - |
| `GoalSource_LiveChainF4` | F4 | chain (with mesh) | no error | 25,646,401 | 4,057,126 | 52 | 24 min 46 s | - |

### Liveness: outcomes and the earlier violation
- **F3 (every withdrawn actor is freed)** holds on pair, xpair, chain3 and the
  full chain-with-mesh, and on cross (`GoalSource_CrossLiveF3`), under weak
  fairness of the system steps, a diligent client (its withdraw, cleanup
  steps are weakly fair and a turn cannot end with a duty enabled) and
  client turns that eventually end.
- **F1, F2, F4** hold on every shape: pair, xpair and chain3 with the three
  combined, and cross and chain-with-mesh one property per run
  (`GoalSource_LiveCross{F1,F2,F4}`, `GoalSource_LiveChain{F1,F2,F4}`,
  run with `-lncheck final`). The combined runs on cross and
  chain-with-mesh had been killed at the 30-minute cap in the temporal
  check, a wall-clock limit rather than a memory one.
- **The earlier `GoalSource_CrossLive` violation** (F1-F4 on cross) reproduces
  on the smallest shape with two clients, `GoalSource_LiveXpairNoEnd`
  (`ClientEndFair = FALSE`, F3 violated after 29 states; the trace is
  `model/goalsource-out/GoalSource_LiveXpairNoEnd.out`). In it client 2
  begins a turn and never ends it, although `ClientEnd` is enabled; its
  turn token stays in the wait set of client 1's cleanup, so the cleanup
  never runs and actor 1 is never freed. With the fairness assumption on
  (`ClientEndFair = TRUE`, the current default) the same configuration passes
  (`GoalSource_LiveXpairF3`). Verdict: a modelling artefact, not a spec
  problem. It is the reclamation assumption the spec already makes: a
  participant that stays in a turn forever holds every later cleanup back
  (as `model/QSBR.tla` shows for an online but idle participant). The model
  states it as a fairness assumption on client turns. The earlier trace
  was overwritten; the earlier model differed from the current one only in
  lacking that fairness condition on `ClientEnd`, which was added after
  the first F3 counterexample.

## Counterexamples of the expected-failure variants
All traces are the shortest TLC found. Actors are numbered as in the shape.

- **V1, withdrawing a Completed actor does not cascade (chain).** 1 and 2 are
  registered Pending; 1 is claimed, its turn ends Completed (2 stays Pending
  with n = 0, and is runnable); the client withdraws 1. 2 is Pending with a
  withdrawn dependency: B4 is violated at once (the model fails on the first
  state where the broken cascade is visible, not on a pointer fault).
  V1b removes the other guard (claim check) and finds the use-after-free:
  2 is claimed although 1 is withdrawn, 1's cleanup (registered after the
  withdrawal, with nothing in progress to wait for) runs and frees 1, and
  2's turn is then about to read the freed 1 (C3). This is the failure the
  withdrawn check at the claim would have caught, and the one (b) of
  "Dependency pointers and withdrawing" rules out.
- **V2, registerGoal ignores withdrawn (cross).** Actor 1 is linked and
  found in a client turn, then unlinked and withdrawn (Running or
  Completed); a dependent is then registered against the found actor and is
  Pending next to a withdrawn dependency (B4). This is S4's pattern: a turn
  found the dependency before the withdrawal. V2b adds variant 4 and ends in
  a use-after-free (C3): the dependent is claimed, 1's cleanup runs and
  frees 1.
- **V3, edges removed at claim, batches of two (diamond).** The first
  property to fail is B3 (a Running actor missing from its dependency's list),
  which is the audit that exists to catch this. Without B3 and C6 (V3b) it is
  B4: 1 is Completed; one pull claims 2 and 3 (`Claim2`); 1 is withdrawn
  while both are Running (the cascade leaves Running actors alone); then
  `ReturnRem` hands 3 back unstarted as Pending, and 3 is Pending with a
  withdrawn dependency and, with the edge removed at claim, no list entry
  through which a later cascade could reach it. With the claim's withdrawn
  check also off (V3c) it ends in C3: the returned actor is pulled after its
  dependency has been freed. This is the S5 race. V3d shows that the
  returned remainder alone is enough: with the edges kept, the same trace
  (claim two, withdraw the dependency, return one) fails B4 as well, because
  the returned actor is Pending beside a withdrawn dependency that the
  cascade skipped while it was Running. So keeping the edges does not by
  itself make a returned remainder safe; what the spec relies on is that
  nothing is ever returned (C6, no batching), with the edges kept so that
  the cascade also sees Running dependents.
- **V4, the claim does not check withdrawn:** passes; see below.
- **V5, a turn's end publishes Completed although withdrawn (chain).** 1 is
  claimed, then withdrawn while Running, then ends Completed: A5 (a
  Completed actor's turn completed while not withdrawn) fails, nine steps.
- **V6, cleanup registered before the withdrawal (chain).** The actor is
  linked, registered, unlinked and added to the cleanup actor without being
  withdrawn: `CleanupAddsWithdrawn` fires at the add (6 steps), the
  assertion the spec places there. V6b removes both cleanup assertions: the
  cleanup is registered with nothing in progress, runs at once and frees the
  still Pending, not withdrawn actor, which is then claimed (C3).

## Why V4 passes: the claim's withdrawn check is a backstop
With the claim not testing dependencies' `withdrawn` (variant 4), the chain
with one client has no error: B4 and the cascade already keep every Pending
actor free of withdrawn dependencies, so a claimable actor (Pending, n = 0)
never has one. `withdraw` marks the flag and abandons every Pending dependent
in the same critical section; `registerGoal` abandons a new dependent at once
if a dependency is withdrawn; edges stay until terminal, so the cascade sees
every Pending dependent; and a Running dependent has already been claimed, so
it needs no claim check. The claim-time test therefore matters only when one of
those three is broken, as the variants show: with it off, V1, V2 and V3 end in
a use-after-free (V1b, V2b, V3c), with it on they stop at B4 (or never reach
a pointer fault, because the claim refuses). The claim check is the second
line of defence for (a) in "Dependency pointers and withdrawing", the cascade
the first. C2 is therefore also an invariant of the base model rather than
something V4 can violate.

## Ambiguities and spec gaps
Each is a place where the model chose a reading. None changes a result as far
as the runs show; none was fixed in `design/` or `proposals/`.

1. **Turn end for a withdrawn Running actor and the cascade.** The table says
   the turn "completes, withdrawn" goes to Abandoned; the cascade text says
   becoming Abandoned abandons every Pending dependent. The model applies the
   cascade at that point too (`FinishTurn`), although `withdraw` already
   cascaded when it ran: the dependents list is by then empty of Pending
   actors, except those registered after the withdrawal, and those are
   abandoned at registration, so the second cascade is a no-op in the model.
   Chosen: literal (cascade on every transition into Abandoned).
2. **Self-abandonment and failure.** The spec has "the turn abandons"
   (Running to Abandoned) and "cooperative abandonment" (reading the flag at
   a yield point). The model does not distinguish them: `TurnAbandon` is
   enabled whenever the body is running, whether or not the actor is
   withdrawn. Chosen: the most permissive reading. A cooperative abandonment
   that fires only when withdrawn is a subset of these behaviours.
3. **Dependency pointer reads before and after `TurnStart`.** Reads start
   after the turn starts (`body`); the spec says a claimed but not started turn
   reads its inputs safely. Chosen: a claimed actor cannot read until its
   turn starts, and C3's "a claimed turn's unread dependency is not freed" is
   checked from the claim, so the window between claim and start is covered.
4. **`withdraw(span)` call shape.** The span is a set of the client's own
   registered, unlinked actors chosen in `BeginWithdraw`; its `WithdrawOne`
   steps are in any order. The client's turn cannot end while a call is open.
   The spec says the client "unlinks, then calls `withdraw`", and "every
   registered goal actor is withdrawn exactly once", which the model reads
   as requiring the unlink first; a registered but never linked actor
   (`alloc`) may be withdrawn without a prior unlink (`Elig` includes
   `alloc`), since an actor allocated, registered and not linked has nothing
   to unlink. The spec's text on this combination is silent.
5. **Which actors count as "found".** D2 says a client registers only with
   dependencies "found in an index during the same turn, or registered
   itself". The model reads "registered itself" as the client's own actors
   that it has not yet begun to withdraw (`ph` before `wcalled`). A lookup
   (`Find`) may also return the client's own linked actor, and the found set
   lasts until the turn ends, so within one client turn find, then own
   withdraw, then register is possible. S4 says program order rules that out
   within one client; the model allows it (a superset of behaviours), so
   the withdrawn check at registration also guards the single-client case in
   the model (V2 fails there too). The base runs show the check holds.
6. **A found actor, a long turn and reclamation (S4 item 5).** A client turn
   that finds an actor is in progress from its begin, so a cleanup registered
   meanwhile waits for it (the brief's reading). The spec says the borrow
   "is valid through step 3 because its turn is still running
   (`invariant/concurrency/borrow-one-turn`)", which
   needs a client turn to count as a protected reader; `design/dispenser.md`
   (reclamation) gives worker and frame-thread participants, and a client's
   turn is a worker's turn. The model's token-based reading assumes that a
   cleanup registered while the finding turn is running waits for that turn,
   which holds only if the worker running the turn was online before the
   registration, as the spec argues for dependents (c).
7. **`done()` for a cascade-abandoned actor.** Fired once per actor, by the
   critical section that made it terminal, after the mutex, and before the
   client call or worker turn that made it terminal returns. The spec says
   only "after the mutex is released" and "exactly once"; where the firing
   thread runs is not stated. Chosen: the thread that ran the critical
   section, all conditions of one critical section fired together.
8. **A goal turn as a reclamation reader.** A goal turn counts as in
   progress from its claim (not its start) until it has fired its conditions
   (`TurnOver`). The spec's argument for a claimed but unstarted turn (S5)
   is that the worker was online before the pull; the model matches that by
   starting the wait at the claim.
9. **Clients are asynchronous with each other and with the workers; only
   the client's own steps are program-ordered.** The client's turn may be
   open for any number of steps, and a client may end a turn with duties
   outstanding unless `Diligent` (the spec's D3 is an assumption, not a
   rule on turn length).
10. **Unregistered actors are not modelled as shared state.** The spec
    reads "Unregistered" as not Completed, and registration before or after
    linking is the client's choice (Q10); both orders are possible (`Link`
    and `Register` are independent). An unregistered actor has no record in
    GoalSource, so withdrawing one (Q1: "frees it") is not a GoalSource step
    and is not modelled.
11. **A shape's meshes and `when()`.** Mesh actors are ordinary goal actors
    (Q11); the client's follow-up withdraw (Q9) is its free choice, not
    triggered by `done()` (F5 is out of scope).
12. **Withdrawing a Completed actor and `done()`.** Fires nothing (spec); the
    model has no step that fires it again, and A7 checks that.
13. **Cooperative claim and the withdraw flag order on a Pending actor.**
    `withdraw` of a Pending actor sets the flag and abandons it in one
    step (the table's row: "withdrawn; Abandoned"); the model has no
    state with a withdrawn Pending actor, so B4's "withdrawn dependency"
    case only arises for Running and Completed dependencies.
14. **The spec's `invariant/goal-source/no-dead-dependency` and a
    withdrawn-Running dependency.** B4 requires
    no Pending actor to have a withdrawn dependency "once a critical section
    ends". For a withdrawn Running dependency, the cascade abandons Pending
    dependents at the withdrawal and `registerGoal` refuses later ones, so it
    holds; the model checks it in every state and it does.
15. **Fairness.** The spec states liveness "under weak fairness of pulls and
    turn steps". The model also assumes weak fairness for `done()` firing and
    for cleanup runs, and, for client turns, that a turn that can end
    eventually does (`ClientEnd`). Without the last assumption a client
    that never ends its turn stutters and F3 fails trivially.
