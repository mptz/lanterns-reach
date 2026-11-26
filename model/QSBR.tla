-------------------------------- MODULE QSBR --------------------------------
(***************************************************************************)
(* Global quiescent-state-based reclamation (liburcu's urcu-qsbr flavour), *)
(* as specified in design/dispenser.md (Reclamation (draft), the           *)
(* authoritative spec; invariant/dispenser/ownership-and-borrowing),       *)
(* design/concurrency.md (the pointer rules other than                     *)
(* invariant/concurrency/dependency-pointers), design/frame-thread.md      *)
(* (invariant/frame-thread/nothing-outlives-frame) and                     *)
(* design/goal-source.md (Results and dependencies in a turn).             *)
(*                                                                         *)
(* Participants are 0..N: participant 0 is the frame thread, an ordinary   *)
(* participant whose quiescent point is the end of each frame; 1..N are    *)
(* the Dispenser's workers, whose quiescent point is the end of each turn, *)
(* before Dispenser::nextBatch(). (Variables and operators call the        *)
(* participants "readers".) The frame thread's own frame counter (parity   *)
(* or 64-bit) is used only for asserts and telemetry and is not modelled. *)
(*                                                                         *)
(* The spec keeps each participant's counter and its offline state in one  *)
(* atomic 64-bit word (0 = offline, G starts at 1). The model keeps them   *)
(* as off[p] and the counter abstraction below, but always changes and     *)
(* reads them together in one step, which is what the one word gives.      *)
(* Going online copies G (the stale copy of variant c1 is safe, not live). *)
(* retire(CleanupActor&) reads G after the unlink (the spec's    *)
(* full fence between the unlink's stores and the load of G; the model is  *)
(* sequentially consistent) and tags the cleanup t = G+1.                  *)
(* returnRemainder(Batch) and claimResult() are not modelled.              *)
(*                                                                         *)
(* The grace-period counter G is unbounded, so nothing here stores it.     *)
(* Every quantity is kept RELATIVE to the current G, and exactly enough of *)
(* each counter is kept to decide the one comparison the protocol makes,   *)
(* "counter >= tag":                                                       *)
(*   cur[p]        p's counter equals the current G;                       *)
(*   reached[o][p] p's counter >= the tag of o's cleanup (exact: a counter *)
(*                 changes only when p stores, and a tag never changes);  *)
(*   tag[o]        the tag minus the current G, clamped below at -2;       *)
(*   ld[p]         for a copy of G loaded but not yet stored: .b = G has   *)
(*                 moved since the load; .s = the registered cleanups whose *)
(*                 tag the loaded value already meets (snapshot at load).   *)
(* A cleanup registered after the load has a tag above the loaded G, except *)
(* under TagMode "G" with no bump since (then it is added to .s).          *)
(*                                                                         *)
(* Constants select the variants (see QSBR-RESULTS.md).                    *)
(***************************************************************************)
EXTENDS Integers, FiniteSets

CONSTANTS
    N,                  \* number of workers
    K,                  \* number of reclaimable objects
    TagMode,            \* "G+1" (doc) | "G" (variant a)
    ReadGBeforeUnlink,  \* TRUE: retire uses a G read before the unlink (variant b)
    OnlineCopiesG,      \* FALSE: going online keeps the old counter instead of copying G (variant c1)
    ResolveBeforeOnline,\* TRUE: a woken worker's pullBatch resolves pointers before it is online (variant c2)
    UseTicker,          \* FALSE: no timed wait / ticker source (variant d)
    BumpOnlyIfNeeded,   \* TRUE: CAS only when a pending tag exceeds G; FALSE: whenever cleanups are pending
    SplitCopy,          \* TRUE: a counter copy is a load of G then a separate store
    FrameMayGoOffline   \* TRUE: the frame thread may go offline between frames

Workers == 1..N
Objs    == 1..K
Readers == 0..N
FRAME   == 0

VARIABLES
    pc,       \* worker control state
    fpc,      \* frame thread control state
    off,      \* [Readers -> BOOLEAN] participant is offline (its word holds 0)
    cur,      \* [Readers -> BOOLEAN] counter = G
    reached,  \* [Objs -> [Readers -> BOOLEAN]] counter >= tag[o]
    ld,       \* [Readers -> [l: BOOLEAN, b: 0..1, s: SUBSET Objs]] a loaded, unstored copy of G
    gl,       \* [Workers -> BOOLEAN] G changed since this worker read it for its CAS
    rgk,      \* [Workers -> 0..2] variant b: bumps since the turn read G
    rgd,      \* [Workers -> BOOLEAN] variant b: the turn has read G
    idx,      \* scan position
    cand,     \* cleanups still candidates in the current scan
    held,     \* pointers each reader holds (obtained from the index)
    item,     \* "none" | "work" | "cleanup" | "ticker"
    cl,       \* the object a cleanup turn frees
    unl,      \* object this turn unlinked and has not yet passed to retire
    timed,    \* sleeping with the timeout
    obj,      \* "linked" | "unlinked" | "registered" | "freed"
    tag,      \* relative tag of a registered cleanup
    run,      \* cleanup marked runnable by a scan
    taken,    \* cleanup claimed by a worker's pullBatch
    ticker    \* the ticker source is activated

vars == <<pc, fpc, off, cur, reached, ld, gl, rgk, rgd, idx, cand, held, item,
          cl, unl, timed, obj, tag, run, taken, ticker>>

Pending    == \E o \in Objs : obj[o] = "registered"
RunnableCl == {o \in Objs : obj[o] = "registered" /\ run[o] /\ ~taken[o]}
Linked     == {o \in Objs : obj[o] = "linked"}
Met        == {o \in Objs : obj[o] = "registered" /\ tag[o] <= 0}
LoadRec    == [l |-> TRUE, b |-> 0, s |-> Met]  \* load G now
NoLoad     == [l |-> FALSE, b |-> 0, s |-> {}]  \* no copy in progress
Min2(x)    == IF x > 2 THEN 2 ELSE x

TypeOK ==
    /\ pc \in [Workers -> {"q","qs","nb","rg","cas","scan","choose","pull",
                           "turn","sleep","pre","on","ons"}]
    /\ fpc \in {"between","in","fq","fs","off","fon","fons"}
    /\ off \in [Readers -> BOOLEAN]
    /\ cur \in [Readers -> BOOLEAN]
    /\ reached \in [Objs -> [Readers -> BOOLEAN]]
    /\ ld \in [Readers -> [l: BOOLEAN, b: 0..1, s: SUBSET Objs]]
    /\ held \in [Readers -> SUBSET Objs]
    /\ obj \in [Objs -> {"linked","unlinked","registered","freed"}]
    /\ tag \in [Objs -> -2..1]
    /\ cand \in [Workers -> SUBSET Objs]

Init ==
    /\ pc = [w \in Workers |-> "nb"]       \* online, counter = G = 0
    /\ fpc = "between"
    /\ off = [p \in Readers |-> FALSE]
    /\ cur = [p \in Readers |-> TRUE]
    /\ reached = [o \in Objs |-> [p \in Readers |-> FALSE]]
    /\ ld = [p \in Readers |-> NoLoad]
    /\ gl = [w \in Workers |-> FALSE]
    /\ rgk = [w \in Workers |-> 0]
    /\ rgd = [w \in Workers |-> FALSE]
    /\ idx = [w \in Workers |-> 0]
    /\ cand = [w \in Workers |-> {}]
    /\ held = [r \in Readers |-> {}]
    /\ item = [w \in Workers |-> "none"]
    /\ cl = [w \in Workers |-> 0]
    /\ unl = [w \in Workers |-> 0]
    /\ timed = [w \in Workers |-> FALSE]
    /\ obj = [o \in Objs |-> "linked"]
    /\ tag = [o \in Objs |-> 1]
    /\ run = [o \in Objs |-> FALSE]
    /\ taken = [o \in Objs |-> FALSE]
    /\ ticker = FALSE

(* Storing into p's counter a copy of G described by rec (and online).   *)
StoreCopy(p, rec) ==
    /\ off' = [off EXCEPT ![p] = FALSE]
    /\ cur' = [cur EXCEPT ![p] = (rec.b = 0)]
    /\ reached' = [o \in Objs |-> [reached[o] EXCEPT ![p] =
                     obj[o] = "registered" /\ o \in rec.s]]

(* Generic copy: atomic (load+store) unless SplitCopy, in which case the   *)
(* load is this step and the store is the step at pc 'storePc'.            *)
(* --------------------------------------------------------------------- *)

(* Workers ------------------------------------------------------------- *)
(* Quiescent point: end of a turn, before nextBatch(). Nothing is held.   *)
(* Copies G into the worker's counter (fenced before and after).          *)
Quiesce(w) ==
    /\ pc[w] = "q"
    /\ IF SplitCopy
         THEN /\ ld' = [ld EXCEPT ![w] = LoadRec]
              /\ pc' = [pc EXCEPT ![w] = "qs"]
              /\ UNCHANGED <<off, cur, reached>>
         ELSE /\ StoreCopy(w, LoadRec)
              /\ pc' = [pc EXCEPT ![w] = "nb"]
              /\ UNCHANGED ld
    /\ UNCHANGED <<fpc, gl, rgk, rgd, idx, cand, held, item, cl, unl, timed,
                   obj, tag, run, taken, ticker>>

QuiesceStore(w) ==
    /\ pc[w] = "qs"
    /\ StoreCopy(w, ld[w]) /\ ld' = [ld EXCEPT ![w] = NoLoad]
    /\ pc' = [pc EXCEPT ![w] = "nb"]
    /\ UNCHANGED <<fpc, gl, rgk, rgd, idx, cand, held, item, cl, unl,
                   timed, obj, tag, run, taken, ticker>>

(* Dispenser::nextBatch(): the opportunistic grace period, only while     *)
(* cleanups are pending (the pending count includes cleanups made         *)
(* runnable but not yet run).                                              *)
NextBatch(w) ==
    /\ pc[w] = "nb"
    /\ pc' = [pc EXCEPT ![w] = IF Pending THEN "rg" ELSE "choose"]
    /\ UNCHANGED <<fpc, off, cur, reached, ld, gl, rgk, rgd, idx, cand, held,
                   item, cl, unl, timed, obj, tag, run, taken, ticker>>

(* Read G; decide whether to CAS; snapshot the pending cleanups to scan.  *)
ReadG(w) ==
    /\ pc[w] = "rg"
    /\ gl' = [gl EXCEPT ![w] = FALSE]
    /\ cand' = [cand EXCEPT ![w] =
                  {o \in Objs : obj[o] = "registered" /\ ~run[o]}]
    /\ idx' = [idx EXCEPT ![w] = 0]
    /\ pc' = [pc EXCEPT ![w] =
                IF ~BumpOnlyIfNeeded
                   \/ \E o \in Objs : obj[o] = "registered" /\ tag[o] = 1
                  THEN "cas" ELSE "scan"]
    /\ UNCHANGED <<fpc, off, cur, reached, ld, rgk, rgd, held, item, cl, unl,
                   timed, obj, tag, run, taken, ticker>>

(* CAS G -> G+1; fails if another worker moved G since ReadG, and a       *)
(* worker whose CAS fails scans anyway.                                    *)
Cas(w) ==
    /\ pc[w] = "cas"
    /\ IF ~gl[w]
         THEN /\ gl' = [v \in Workers |-> TRUE]
              /\ cur' = [p \in Readers |-> FALSE]
              /\ ld' = [p \in Readers |-> IF ld[p].l THEN [ld[p] EXCEPT !.b = 1] ELSE ld[p]]
              /\ rgk' = [v \in Workers |-> Min2(rgk[v] + 1)]
              /\ tag' = [o \in Objs |->
                           IF obj[o] = "registered" /\ tag[o] > -2
                             THEN tag[o] - 1 ELSE tag[o]]
         ELSE UNCHANGED <<gl, cur, ld, rgk, tag>>
    /\ pc' = [pc EXCEPT ![w] = "scan"]
    /\ UNCHANGED <<fpc, off, reached, rgd, idx, cand, held, item, cl, unl,
                   timed, obj, run, taken, ticker>>

(* Scan the participants' counters one at a time.                         *)
ScanStep(w) ==
    /\ pc[w] = "scan" /\ idx[w] \in Readers
    /\ cand' = [cand EXCEPT ![w] =
                  {o \in cand[w] : off[idx[w]] \/ reached[o][idx[w]]}]
    /\ idx' = [idx EXCEPT ![w] = idx[w] + 1]
    /\ UNCHANGED <<pc, fpc, off, cur, reached, ld, gl, rgk, rgd, held, item,
                   cl, unl, timed, obj, tag, run, taken, ticker>>

ScanDone(w) ==
    /\ pc[w] = "scan" /\ idx[w] = N + 1
    /\ run' = [o \in Objs |->
                 run[o] \/ (o \in cand[w] /\ obj[o] = "registered")]
    /\ cand' = [cand EXCEPT ![w] = {}]
    /\ pc' = [pc EXCEPT ![w] = "choose"]
    /\ UNCHANGED <<fpc, off, cur, reached, ld, gl, rgk, rgd, idx, held, item,
                   cl, unl, timed, obj, tag, taken, ticker>>

(* Rest of nextBatch(): pull from a source, or go offline and sleep; no   *)
(* worker sleeps while a runnable cleanup is untaken or the ticker source *)
(* is active. The last worker to sleep while cleanups are pending sleeps  *)
(* with a timeout. The eventcount is taken as correct (no lost wake-up). *)
Choose(w) ==
    /\ pc[w] = "choose"
    /\ \/ /\ pc' = [pc EXCEPT ![w] = "pull"]
          /\ UNCHANGED <<off, timed>>
       \/ /\ RunnableCl = {} /\ ~ticker
          /\ off' = [off EXCEPT ![w] = TRUE]
          /\ pc' = [pc EXCEPT ![w] = "sleep"]
          /\ timed' = [timed EXCEPT ![w] =
                         (\A v \in Workers \ {w} : pc[v] = "sleep") /\ Pending]
    /\ UNCHANGED <<fpc, cur, reached, ld, gl, rgk, rgd, idx, cand, held, item,
                   cl, unl, obj, tag, run, taken, ticker>>

(* DispenserSource::pullBatch(Budget): a runnable cleanup first, else    *)
(* the ticker source's no-op turn (ranked lowest) or ordinary work; work  *)
(* may resolve pointers from the index (a goal's DepView). 'next' is      *)
(* where the worker goes afterwards.                                      *)
Pull(w, next) ==
    /\ \/ \E o \in RunnableCl :
             /\ item' = [item EXCEPT ![w] = "cleanup"]
             /\ cl' = [cl EXCEPT ![w] = o]
             /\ taken' = [taken EXCEPT ![o] = TRUE]
             /\ UNCHANGED <<held, ticker>>
       \/ /\ RunnableCl = {} /\ ticker
          /\ item' = [item EXCEPT ![w] = "ticker"]
          /\ ticker' = FALSE
          /\ UNCHANGED <<held, cl, taken>>
       \/ /\ RunnableCl = {}
          /\ item' = [item EXCEPT ![w] = "work"]
          /\ \E S \in SUBSET Linked : held' = [held EXCEPT ![w] = S]
          /\ UNCHANGED <<cl, taken, ticker>>
    /\ pc' = [pc EXCEPT ![w] = next]
    /\ UNCHANGED <<fpc, off, cur, reached, ld, gl, rgk, rgd, idx, cand, unl,
                   timed, obj, tag, run>>

PullBatch(w) == pc[w] = "pull" /\ Pull(w, "turn")
(* Variant c2: a woken worker's pullBatch runs before it goes online.     *)
PullEarly(w) == pc[w] = "pre" /\ Pull(w, "on")

AfterOnline == IF ResolveBeforeOnline THEN "turn" ELSE "nb"

(* Going online: copy G, fenced before any shared read (or, variant c1,   *)
(* keep the old counter).                                                  *)
Online(w) ==
    /\ pc[w] = "on"
    /\ CASE ~OnlineCopiesG ->
              /\ off' = [off EXCEPT ![w] = FALSE]
              /\ pc' = [pc EXCEPT ![w] = AfterOnline]
              /\ UNCHANGED <<cur, reached, ld>>
         [] SplitCopy ->
              /\ ld' = [ld EXCEPT ![w] = LoadRec]
              /\ pc' = [pc EXCEPT ![w] = "ons"]
              /\ UNCHANGED <<off, cur, reached>>
         [] OTHER ->
              /\ StoreCopy(w, LoadRec)
              /\ pc' = [pc EXCEPT ![w] = AfterOnline]
              /\ UNCHANGED ld
    /\ UNCHANGED <<fpc, gl, rgk, rgd, idx, cand, held, item, cl, unl, timed,
                   obj, tag, run, taken, ticker>>

OnlineStore(w) ==
    /\ pc[w] = "ons"
    /\ StoreCopy(w, ld[w]) /\ ld' = [ld EXCEPT ![w] = NoLoad]
    /\ pc' = [pc EXCEPT ![w] = AfterOnline]
    /\ UNCHANGED <<fpc, gl, rgk, rgd, idx, cand, held, item, cl, unl,
                   timed, obj, tag, run, taken, ticker>>

(* Inside a turn ------------------------------------------------------- *)
Find(w, o) ==       \* borrow from the index, for this turn only (invariant/concurrency/borrow-one-turn)
    /\ pc[w] = "turn" /\ item[w] = "work"
    /\ obj[o] = "linked" /\ o \notin held[w]
    /\ held' = [held EXCEPT ![w] = held[w] \cup {o}]
    /\ UNCHANGED <<pc, fpc, off, cur, reached, ld, gl, rgk, rgd, idx, cand,
                   item, cl, unl, timed, obj, tag, run, taken, ticker>>

PreReadG(w) ==      \* variant b only: retire's G read, too early
    /\ ReadGBeforeUnlink
    /\ pc[w] = "turn" /\ item[w] = "work" /\ unl[w] = 0 /\ ~rgd[w]
    /\ rgd' = [rgd EXCEPT ![w] = TRUE]
    /\ rgk' = [rgk EXCEPT ![w] = 0]
    /\ UNCHANGED <<pc, fpc, off, cur, reached, ld, gl, idx, cand, held, item,
                   cl, unl, timed, obj, tag, run, taken, ticker>>

Unlink(w, o) ==     \* the owner unlinks from everything new work can reach
    /\ pc[w] = "turn" /\ item[w] = "work" /\ unl[w] = 0
    /\ ReadGBeforeUnlink => rgd[w]
    /\ obj[o] = "linked"
    /\ obj' = [obj EXCEPT ![o] = "unlinked"]
    /\ unl' = [unl EXCEPT ![w] = o]
    /\ UNCHANGED <<pc, fpc, off, cur, reached, ld, gl, rgk, rgd, idx, cand,
                   held, item, cl, timed, tag, run, taken, ticker>>

(* retire(CleanupActor&): full fence after the unlink's stores, *)
(* load G, tag t = G+1 (relative: G+1 -> 1; variant a's G -> 0).         *)
(* At registration a counter >= t iff t <= G and the counter = G (cur), *)
(* since no counter exceeds G; for an older t, cur is a safe              *)
(* under-approximation (variant b only).                                   *)
NewTag(w) ==
    IF ReadGBeforeUnlink THEN 1 - rgk[w]
    ELSE IF TagMode = "G" THEN 0 ELSE 1

Register(w) ==
    /\ pc[w] = "turn" /\ unl[w] # 0
    /\ LET o == unl[w]
           t == NewTag(w)
       IN /\ obj' = [obj EXCEPT ![o] = "registered"]
          /\ tag' = [tag EXCEPT ![o] = t]
          /\ reached' = [reached EXCEPT ![o] =
                           [p \in Readers |-> t <= 0 /\ cur[p]]]
          /\ run' = [run EXCEPT ![o] = FALSE]
          /\ ld' = [p \in Readers |->
                       IF t <= 0 /\ ld[p].l /\ ld[p].b = 0
                         THEN [ld[p] EXCEPT !.s = ld[p].s \cup {o}]
                         ELSE ld[p]]
    /\ unl' = [unl EXCEPT ![w] = 0]
    /\ rgd' = [rgd EXCEPT ![w] = FALSE]
    /\ UNCHANGED <<pc, fpc, off, cur, gl, rgk, idx, cand, held, item, cl,
                   timed, taken, ticker>>

Free(w) ==          \* the cleanup actor's turn frees its object
    /\ pc[w] = "turn" /\ item[w] = "cleanup"
    /\ obj[cl[w]] = "registered"
    /\ obj' = [obj EXCEPT ![cl[w]] = "freed"]
    /\ UNCHANGED <<pc, fpc, off, cur, reached, ld, gl, rgk, rgd, idx, cand,
                   held, item, cl, unl, timed, tag, run, taken, ticker>>

End(w) ==           \* turn ends: borrows dropped; next, the quiescent point
    /\ pc[w] = "turn" /\ unl[w] = 0
    /\ item[w] = "cleanup" => obj[cl[w]] = "freed"
    /\ pc' = [pc EXCEPT ![w] = "q"]
    /\ held' = [held EXCEPT ![w] = {}]
    /\ item' = [item EXCEPT ![w] = "none"]
    /\ cl' = [cl EXCEPT ![w] = 0]
    /\ rgd' = [rgd EXCEPT ![w] = FALSE]
    /\ UNCHANGED <<fpc, off, cur, reached, ld, gl, rgk, idx, cand, unl, timed,
                   obj, tag, run, taken, ticker>>

(* Sleeping ------------------------------------------------------------- *)
WakePc == IF ResolveBeforeOnline THEN "pre" ELSE "on"

Timeout(w) ==       \* the timed sleeper activates the ticker source and wakes
    /\ UseTicker /\ pc[w] = "sleep" /\ timed[w]
    /\ ticker' = TRUE
    /\ timed' = [timed EXCEPT ![w] = FALSE]
    /\ pc' = [pc EXCEPT ![w] = WakePc]
    /\ UNCHANGED <<fpc, off, cur, reached, ld, gl, rgk, rgd, idx, cand, held,
                   item, cl, unl, obj, tag, run, taken>>

Wake(w) ==          \* environment: some source advertised work (unfair)
    /\ pc[w] = "sleep"
    /\ timed' = [timed EXCEPT ![w] = FALSE]
    /\ pc' = [pc EXCEPT ![w] = WakePc]
    /\ UNCHANGED <<fpc, off, cur, reached, ld, gl, rgk, rgd, idx, cand, held,
                   item, cl, unl, obj, tag, run, taken, ticker>>

(* The frame thread ----------------------------------------------------- *)
FUnch == UNCHANGED <<pc, gl, rgk, rgd, idx, cand, item, cl, unl, timed, obj,
                     tag, run, taken, ticker>>

FStart ==
    /\ fpc = "between" /\ fpc' = "in"
    /\ UNCHANGED <<off, cur, reached, ld, held>> /\ FUnch

FRead(o) ==
    /\ fpc = "in" /\ obj[o] = "linked" /\ o \notin held[FRAME]
    /\ held' = [held EXCEPT ![FRAME] = held[FRAME] \cup {o}]
    /\ UNCHANGED <<fpc, off, cur, reached, ld>> /\ FUnch

FEnd ==             \* end of the frame: nothing read is kept
    /\ fpc = "in" /\ fpc' = "fq"
    /\ held' = [held EXCEPT ![FRAME] = {}]
    /\ UNCHANGED <<off, cur, reached, ld>> /\ FUnch

FQuiesce ==         \* the frame's quiescent point: copy G, as a worker does
    /\ fpc = "fq"
    /\ IF SplitCopy
         THEN /\ ld' = [ld EXCEPT ![FRAME] = LoadRec] /\ fpc' = "fs"
              /\ UNCHANGED <<off, cur, reached>>
         ELSE /\ StoreCopy(FRAME, LoadRec) /\ fpc' = "between" /\ UNCHANGED ld
    /\ UNCHANGED held /\ FUnch

FQuiesceStore ==
    /\ fpc = "fs" /\ StoreCopy(FRAME, ld[FRAME]) /\ fpc' = "between"
    /\ ld' = [ld EXCEPT ![FRAME] = NoLoad] /\ UNCHANGED held /\ FUnch

FOffline ==         \* before a long block (minimised window)
    /\ FrameMayGoOffline /\ fpc = "between"
    /\ off' = [off EXCEPT ![FRAME] = TRUE] /\ fpc' = "off"
    /\ UNCHANGED <<cur, reached, ld, held>> /\ FUnch

FWake ==            \* environment: the block ends (unfair)
    /\ fpc = "off" /\ fpc' = "fon"
    /\ UNCHANGED <<off, cur, reached, ld, held>> /\ FUnch

FOnline ==
    /\ fpc = "fon"
    /\ CASE ~OnlineCopiesG ->
              /\ off' = [off EXCEPT ![FRAME] = FALSE] /\ fpc' = "between"
              /\ UNCHANGED <<cur, reached, ld>>
         [] SplitCopy ->
              /\ ld' = [ld EXCEPT ![FRAME] = LoadRec] /\ fpc' = "fons"
              /\ UNCHANGED <<off, cur, reached>>
         [] OTHER ->
              /\ StoreCopy(FRAME, LoadRec) /\ fpc' = "between" /\ UNCHANGED ld
    /\ UNCHANGED held /\ FUnch

FOnlineStore ==
    /\ fpc = "fons" /\ StoreCopy(FRAME, ld[FRAME]) /\ fpc' = "between"
    /\ ld' = [ld EXCEPT ![FRAME] = NoLoad] /\ UNCHANGED held /\ FUnch

WorkerStep(w) ==
    \/ Quiesce(w) \/ QuiesceStore(w) \/ NextBatch(w) \/ ReadG(w) \/ Cas(w)
    \/ ScanStep(w) \/ ScanDone(w) \/ Choose(w) \/ PullBatch(w)
    \/ PullEarly(w) \/ Online(w) \/ OnlineStore(w) \/ PreReadG(w)
    \/ Register(w) \/ Free(w) \/ End(w) \/ Timeout(w)

FrameStep ==
    \/ FStart \/ FEnd \/ FQuiesce \/ FQuiesceStore \/ FOffline \/ FWake
    \/ FOnline \/ FOnlineStore

Next ==
    \/ \E w \in Workers :
         \/ WorkerStep(w) \/ Wake(w)
         \/ \E o \in Objs : Find(w, o) \/ Unlink(w, o)
    \/ FrameStep \/ \E o \in Objs : FRead(o)

(* Weak fairness on every worker step except environment wakes and the    *)
(* optional Find, Unlink and PreReadG steps. The frame thread, while      *)
(* online, keeps running frames (FStart fair) unless it goes offline      *)
(* (FOffline unfair); it may stay offline for ever (FWake unfair).         *)
Fairness ==
    /\ \A w \in Workers :
         /\ WF_vars(Quiesce(w)) /\ WF_vars(QuiesceStore(w))
         /\ WF_vars(NextBatch(w)) /\ WF_vars(ReadG(w)) /\ WF_vars(Cas(w))
         /\ WF_vars(ScanStep(w)) /\ WF_vars(ScanDone(w))
         /\ WF_vars(Choose(w)) /\ WF_vars(PullBatch(w))
         /\ WF_vars(PullEarly(w)) /\ WF_vars(Online(w))
         /\ WF_vars(OnlineStore(w)) /\ WF_vars(Register(w))
         /\ WF_vars(Free(w)) /\ WF_vars(End(w)) /\ WF_vars(Timeout(w))
    /\ WF_vars(FStart) /\ WF_vars(FEnd) /\ WF_vars(FQuiesce)
    /\ WF_vars(FQuiesceStore) /\ WF_vars(FOnline) /\ WF_vars(FOnlineStore)

Spec == Init /\ [][Next]_vars /\ Fairness

(* Properties ----------------------------------------------------------- *)
(* Safety: no reader holds (so none can dereference) a freed object.       *)
NoUseAfterFree == \A r \in Readers : \A o \in held[r] : obj[o] # "freed"
(* Stronger: no reader holds an object whose cleanup is already runnable. *)
NoHoldRunnable ==
    \A r \in Readers : \A o \in held[r] : ~(obj[o] = "registered" /\ run[o])
(* The proof's key step: a holder's counter is below the cleanup's tag.   *)
HolderBelowTag ==
    \A r \in Readers : \A o \in held[r] :
        obj[o] = "registered" => ~reached[o][r]

(* Non-vacuity probes: each is EXPECTED to be violated.                     *)
ProbeWorkerHoldsRetired ==   \* G has reached the tag while a turn holds o
    ~\E w \in Workers, o \in Objs :
        o \in held[w] /\ obj[o] = "registered" /\ tag[o] <= 0
ProbeFrameHoldsRetired ==
    ~\E o \in Objs : o \in held[FRAME] /\ obj[o] = "registered" /\ tag[o] <= 0
ProbeCasLost ==              \* two workers raced to CAS; one lost
    ~\E w \in Workers : pc[w] = "cas" /\ gl[w]
ProbeOfflineSkipped ==       \* a cleanup ran past an offline, lagging counter
    ~\E o \in Objs, p \in Readers :
        obj[o] = "registered" /\ run[o] /\ off[p] /\ ~reached[o][p]
ProbeStaleStore ==           \* a counter store of a G that has since moved
    ~(\E w \in Workers : pc[w] \in {"qs","ons"} /\ ld[w].b = 1)
ProbeFrameStaleStore ==
    ~(fpc \in {"fs","fons"} /\ ld[FRAME].b = 1)

CleanupsRun == \A o \in Objs : (obj[o] = "registered") ~> (obj[o] = "freed")
NoStall == (Pending /\ \A w \in Workers : pc[w] = "sleep")
             ~> (\E w \in Workers : pc[w] # "sleep")
=============================================================================
