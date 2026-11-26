# Model-checking results: global QSBR reclamation

Status: checked model. `design/dispenser.md`, Reclamation (draft), is the
authoritative spec; this file records what the model checks against it. The
clarifications the model raised are listed at the end, each marked as folded
into the spec or still open, with the reading the model chose.

Tools: TLC2 2.19 (`tla2tools.jar` 1.7.4), Java 21, 2 workers, `-Xmx2g`
unless a row says otherwise, deadlock checking on (no run reported a
deadlock). Reproduce with
`TLA2TOOLS=<jar> model/run_qsbr.sh` (see `README.md`).

## What is modelled
Spec: `design/dispenser.md` (Reclamation (draft);
`invariant/dispenser/ownership-and-borrowing`), `design/concurrency.md`
(Ownership and borrowing: the pointer rules other than
`invariant/concurrency/dependency-pointers`), `design/frame-thread.md`
(`invariant/frame-thread/nothing-outlives-frame`), `design/goal-source.md`
(Results and dependencies in a turn).

- **Participants:** workers `1..N` and the frame thread (participant 0, an
  ordinary participant). One global grace-period counter G. Each participant
  has one counter word: offline, or a copy of G. The spec makes it one atomic
  64-bit word (0 = offline, G starts at 1); the model changes and reads the
  offline state and the counter together, in one step, which is what the one
  word gives.
- **Worker loop:** turn, then `End` (borrows dropped), then the quiescent
  point (copy G into the counter), then `Dispenser::nextBatch()`. While
  cleanups are pending (runnable, not-yet-run cleanups included),
  `nextBatch()` makes the opportunistic attempt: read G; if a pending
  cleanup's tag exceeds G, CAS G to G+1 (it fails if another worker moved G
  first, and the worker scans anyway); then scan the participants' counters
  one at a time and mark runnable every cleanup (from a snapshot taken at
  the read) whose tag every online counter has reached. Then
  `DispenserSource::pullBatch(Budget)` (a runnable cleanup first, else the
  ticker source's turn or ordinary work, which may resolve pointers from the
  index, as `GoalSource` resolves a DepView), or, if nothing is ready and no
  runnable cleanup is untaken, go offline and sleep. `returnRemainder(Batch)`
  and `claimResult()` are not modelled.
- **Copying G** (quiescent point, and going online) is a load of G and a
  separate store (`SplitCopy = TRUE`), so a bump may fall between them; the
  copy is a step of its own, distinct from the turn's reads.
- **Inside a turn:** `find` while an object is linked; the owner unlinks an
  object, then `retire(CleanupActor&)` loads G (after the unlink's
  stores; the spec's full fence, the model being sequentially consistent)
  and tags the cleanup t = G+1; a cleanup actor's turn frees its object.
- **Sleep and the ticker source:** a woken worker goes online (copies G),
  then calls `nextBatch()`. Wakes for advertised work are unfair environment
  steps. While cleanups are pending the last worker to sleep sleeps timed; on
  timeout it activates the ticker source (a no-op turn, ranked lowest), whose
  end is a quiescent point. The eventcount is assumed correct and not
  modelled.
- **Frame thread:** frames start, read linked objects, end (dropping
  everything), then the quiescent point copies G, as a worker's does.
  Between frames it may go offline (unfair, `FrameMayGoOffline`) and later
  come back online by copying G. While online it keeps running frames
  (`FStart` weakly fair). Its own frame counter (a parity counter, or a
  64-bit one) serves asserts and telemetry only and is not modelled.
- **Finite state:** G is never stored. Each tag is kept relative to G
  (clamped at G-2), and for each cleanup and participant a flag says whether
  that counter has reached the tag. That is exact, because counters change
  only at stores and tags never change. A pending split copy carries the set
  of tags its loaded value already meets. The one approximation is in
  variant (b) only: after two or more bumps between the early read and the
  tag, the model treats counters as reaching the tag only if equal to G,
  which can only hide failures, not create them.
- **Fairness:** weak fairness on every worker step except environment wakes
  and the optional `find`/unlink steps; on frame end, quiescent copy and the
  going-online copy; and on `FStart` (an online frame thread keeps running
  frames). Going offline and coming back are unfair for the frame thread.

## Properties
- `NoUseAfterFree` (safety): no participant holds a pointer to a freed
  object (stronger than checking explicit uses).
- `NoHoldRunnable` (safety, stronger): no participant holds an object whose
  cleanup has been marked runnable.
- `HolderBelowTag` (safety, the proof's key step): a participant holding an
  object whose cleanup is registered has a counter below its tag.
- `CleanupsRun` (liveness): every registered cleanup is eventually freed.
- `NoStall` (liveness): if cleanups are pending and every worker sleeps, some
  worker eventually wakes.

## Runs
All runs have N = 2 except `QSBR_N3`. "Distinct" counts states reachable
before a violation, or all of them when the run passes.

| Config | K | Variant | Distinct states | Depth | Result | Expected |
|---|---|---|---|---|---|---|
| `QSBR` | 1 | base (split copies, frame may go offline) | 185,209 | 67 | all pass | pass |
| `QSBR_K2` | 2 | base, two objects (`-Xmx4g`) | 6,804,633 | 94 | all pass | pass |
| `QSBR_N3` | 1 | base, three workers (`-Xmx4g`) | 22,034,821 | 96 | all pass | pass |
| `QSBR_FrameOnline` | 1 | frame thread never offline | 78,027 | 71 | all pass | pass |
| `QSBR_AtomicCopy` | 1 | each copy one atomic load-and-store | 59,877 | 59 | all pass | pass |
| `QSBR_BumpAlways` | 1 | CAS on every attempt while pending | 608,976 | 73 | all pass | pass |
| `QSBR_TagG` | 1 | (a) tag t = G | 2,378 | 15 | **`NoUseAfterFree` fails** | fail |
| `QSBR_ReadGBeforeUnlink` | 2 | (b) G read before the unlink | 734,930 | 36 | **`NoUseAfterFree` fails** | fail |
| `QSBR_OnlineKeepsOldSafety` | 2 | (c1) online without copying G, safety only | 5,841,748 | 112 | safety passes | see below |
| `QSBR_OnlineKeepsOld` | 1 | (c1) as above, with liveness | 27,816 | – | **liveness fails** | see below |
| `QSBR_ResolveBeforeOnline` | 1 | (c2) `pullBatch` resolves before online | 70,675 | 35 | **`NoUseAfterFree` fails** | fail |
| `QSBR_NoTicker` | 1 | (d) no timed wait, no ticker | 26,625 | – | **liveness fails** | fail (liveness) |
| `QSBR_ProbeWorkerHoldsRetired` | 1 | probe | 727 | 9 | violated | violated |
| `QSBR_ProbeFrameHoldsRetired` | 1 | probe | 1,429 | 11 | violated | violated |
| `QSBR_ProbeCasLost` | 1 | probe | 2,350 | 14 | violated | violated |
| `QSBR_ProbeOfflineSkipped` | 1 | probe | 13,385 | 24 | violated | violated |
| `QSBR_ProbeStaleStore` | 1 | probe | 1,408 | 11 | violated | violated |
| `QSBR_ProbeFrameStaleStore` | 1 | probe | 1,631 | 12 | violated | violated |

**Large configurations.** `QSBR_K2` (base with K = 2) and `QSBR_N3` (base
with N = 3, K = 1) were run one at a time with `-Xmx4g
-XX:MaxDirectMemorySize=1g`, 2 TLC workers, under a watchdog that would have
killed either run above 6 GB resident or below 1.5 GB of free memory; neither
came close. Both completed and every invariant and property passed.

| Config | States generated | Distinct states | Depth | Wall time | Peak RSS (30 s samples) | Fingerprint collision estimate |
|---|---|---|---|---|---|---|
| `QSBR_K2` | 22,415,729 | 6,804,633 | 94 | 25 min 42 s | 4.2 GiB | 5.2E-6 |
| `QSBR_N3` | 96,670,240 | 22,034,821 | 96 | 1 h 24 min | 4.3 GiB | 7.4E-4 |

Liveness checking dominates the tail of each run (5.5 min for `QSBR_K2`,
15 min for `QSBR_N3`). `QSBR_N3`'s collision estimate is the larger of TLC's
two; a run with a different fingerprint seed (`-fp`) would tighten it. Before
these, `QSBR_FrameOnline` with K = 2 completed and passed (2,832,508 distinct
states, depth 99). A K = 2 run of `QSBR_AtomicCopy` was killed for memory at
`-Xmx2g` and is not claimed.

**Probes** (each expected to be violated, so the passing runs aren't vacuous):
a worker turn and a frame really do hold an object after G has reached its
cleanup's tag; two workers race their CASes and one loses; a cleanup becomes
runnable past an offline participant whose counter lags its tag; and a worker
and the frame thread both store a copy of G that a bump has overtaken.

A modelling note: a first encoding that clamped the bumps between a load and
its store at two gave a spurious liveness counterexample for `BumpAlways`. The
final encoding, a snapshot at the load, is exact, and that config passes.

## Counterexamples for the failing variants
Absolute values of G, starting at 1 as in the spec (0 is offline).

### (a) Tag t = G (`QSBR_TagG`)
1. Every counter is 1 = G. W1's `pullBatch` gives it work that resolves o.
2. W1 unlinks o and calls `retire`, tagging the cleanup 1.
3. W2 calls `nextBatch()`. No CAS is needed (the tag does not exceed G). Its
   scan finds every counter at 1 ≥ 1 and marks the cleanup runnable.
4. W2 pulls the cleanup and frees o. W1's turn still holds o.

A counter equal to G may belong to a turn that is still reading, so the
tag must exceed every counter at the unlink: G+1.

### (b) G read before the unlink (`QSBR_ReadGBeforeUnlink`, K = 2)
1. W1, in a turn, unlinks o1 and registers it (tag 2). Still in that turn,
   it reads G = 1 for a second `retire`.
2. W2's `nextBatch()` CASes G to 2 (o1's tag exceeded G). Its scan fails
   (counters at 1), and W2 sleeps, offline.
3. The frame thread ends a frame and copies G = 2. It starts a new frame
   and finds o2, still linked.
4. W1 unlinks o2 and tags it with its stale read: 1 + 1 = 2. The frame
   thread's counter, 2, already meets that tag.
5. W1 ends its turn, copies G = 2, and its scan finds every online counter
   (W1 and the frame thread) at 2 and W2 offline: o2 is runnable.
6. W1 pulls the cleanup and frees o2 inside the frame that holds it.

### (c2) Resolving before going online (`QSBR_ResolveBeforeOnline`)
1. W1 sleeps (offline). It is woken, and its `pullBatch` resolves o while W1
   is still offline.
2. W2 pulls work, unlinks o, tags it 2, ends its turn and copies G = 1. Its
   `nextBatch()` CASes G to 2; the scan fails (its own and the frame's
   counters are 1).
3. W2 runs another turn, copies G = 2. The frame thread goes offline.
4. W2's scan skips W1 (offline) and the frame thread (offline) and finds
   its own counter at 2: the cleanup is runnable. W2 frees o, which W1, not
   yet online, holds.

### (c1) Going online without copying G (`QSBR_OnlineKeepsOld`)
Safe: the exhaustive safety run passes, as the spec's argument predicts. A
counter is never above G, so a stale one only delays cleanups. Liveness
fails:
1. W1 unlinks and registers o (tag 2); W1's attempt CASes G to 2. W2 is
   asleep, its counter still 1 from before it slept.
2. W2 is woken by advertised work (which another worker takes), goes online
   with its counter still 1, calls `nextBatch()`, and its scan fails on its
   own counter. Finding nothing to pull, it sleeps again without a turn,
   and so without a quiescent point.
3. W1 sleeps. The frame thread, too, comes online without copying and goes
   offline again between frames. Steps 2 and 3 repeat for ever, with W2 woken
   each time before its timeout; the cleanup never runs.

So copying G on going online is needed for liveness, not safety, as the
spec now states. A worker
woken spuriously, which finds nothing and sleeps again, would otherwise hold
back reclamation indefinitely.

### (d) No ticker (`QSBR_NoTicker`)
W1 registers a cleanup and CASes G to 2. The scan fails, since its own
counter is still 1. Both workers find nothing to do and sleep untimed, while
the frame thread runs frames or goes offline. No worker ever returns to
`nextBatch()`, so the cleanup never runs and both workers sleep for ever.

## The frame thread's old race (question 5)
No analogue appears. In the epoch scheme, an advancer recorded the frame
counter relative to its bump, and a stale record let a frame begun before
the bump pass as clear. In QSBR, advancers record nothing about the frame
thread, which is an ordinary participant; its frame counter (parity or
64-bit) is for asserts and telemetry only. It writes only its own counter, and a cleanup's tag is fixed at
registration, so the scan compares two values that are each written once per
quiescent point. The nearest analogue is a bump between the frame thread's
load of G and its store (`ProbeFrameStaleStore`, reachable). The stored value
is then below G, which only delays cleanups. That case is inside the base
model, which passes. `QSBR_AtomicCopy` passes too, so neither reading of
"copies G" matters for safety.

## Clarifications for the docs
All concern `design/dispenser.md`, Reclamation (draft), unless stated. Items
marked folded are now stated in the spec; the rest remain open, with the
reading the model chose.

1. **When G is advanced** (folded: G advances by one step, by
   compare-and-swap, if a pending cleanup's tag exceeds it). Tags are at
   most G+1, so one step always suffices and "exceeds G" is the simpler
   test of the same condition the model checks (`BumpOnlyIfNeeded`).
   Advancing on every attempt while anything is pending (`QSBR_BumpAlways`)
   is also safe and live; the condition just avoids needless advances.
2. **A failed CAS** (folded: a worker whose compare-and-swap fails scans
   anyway). Losing is reachable (`ProbeCasLost`) and safe. G moves at most
   one step per successful CAS, which keeps "tag exceeds G" equivalent to
   "tag = G+1".
3. **Two attempts per cleanup** (folded: a cleanup normally takes two
   attempts, and one ticker timeout suffices when idle). The model does the
   CAS before the scan. The advancing worker's own counter was copied before
   its CAS, so the attempt that advances G to t never makes a tag-t cleanup
   runnable; a later attempt does, after that worker's next quiescent point
   or by another worker. When idle, the woken worker's attempt advances G,
   the ticker source's turn ends and copies G, and the next attempt makes
   the cleanup runnable.
4. **Going online copies G** (folded: going online must copy G; a stale
   copy is safe but not live). Going online copies G, so a woken worker's
   counter is current at once, and the model has it make the attempt in its
   first `nextBatch()`, before any turn. Without the copy (variant c1) it is
   still safe but not live (above).
5. **One word per participant** (folded: one atomic 64-bit word per thread,
   0 = offline, G starting at 1, as in liburcu). The scan reads the offline
   state and the counter together, and going online makes the copy and the
   online state visible in one store. A separate flag, which would need two
   ordered stores, was not modelled.
6. **Which cleanups a scan may mark** (folded: the scan reads each online
   counter once, as separate atomic loads with no lock and no snapshot,
   and considers only the cleanups pending when the attempt began). The
   model fixes the candidates in `ReadG` and reads one participant per
   `ScanStep`, with every other action free to interleave between reads.
   Including cleanups registered during the scan would also be safe (each
   has a tag above every counter read before its registration), but it is
   not modelled and not specified.
7. **Pending count and sleeping** (folded: the pending count includes
   cleanups made runnable but not yet run, and no worker sleeps while a
   runnable cleanup is untaken). This is what makes `NoStall` hold once a
   scan succeeds just before every worker would sleep.
8. **The frame thread online but idle** (folded: an online frame thread that
   stops ending frames holds back every cleanup, and the Dispenser's
   grace-period-age watchdog (its release counters) guards it;
   `invariant/frame-thread/nothing-outlives-frame`). The model assumes
   frames keep coming while online, and that any long block (a minimised
   window) goes offline first. Frame pacing is folded too: the pacing wait
   (present, vsync, a throttled frame) is an offline point after the
   end-of-frame quiescent point (`design/frame-thread.md` loop step 11,
   `invariant/frame-thread/nothing-outlives-frame`), so its length never
   matters to reclamation; this is the between-frames offline step the model
   already has (`FrameMayGoOffline`).
9. **Fence placement in `retire`** (folded: a full fence between
   the unlink's stores and the load of G). The model is sequentially
   consistent, so the unlink is visible before G is loaded. Variant (b)
   shows that reading G before the unlink is unsafe.
