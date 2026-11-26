------------------------------ MODULE GoalSource ------------------------------
(***************************************************************************)
(* GoalSource: the Dispenser source that sequences goal actors by their    *)
(* dependencies, as specified in `design/goal-source.md` (states and        *)
(* transitions, the cascade, "Dependency pointers and withdrawing", the     *)
(* invariants) and `proposals/goal-source-scenarios.md` (scenarios S1-S10,  *)
(* the invariant catalogue A-H, decided questions Q1-Q11, model scope).     *)
(*                                                                         *)
(* One atomic step is one critical section under GoalSource's mutex, or one *)
(* step outside it. There is no batching: a worker claims one goal actor    *)
(* (Claim) and its turn starts in a later step (TurnStart), with every      *)
(* other actor's steps allowed between; the turn's dependency reads are     *)
(* steps of their own. Reclamation is abstracted (catalogue C5): a cleanup  *)
(* records the turns in progress when it is registered and runs only once   *)
(* all of them have ended; there are no QSBR counters (model/QSBR.tla).     *)
(*                                                                         *)
(* Turns are identified by tokens: a goal actor's turn by the actor (1..K), *)
(* a client's turn by 10 + client. A goal turn is in progress from its      *)
(* claim until its done() conditions have fired; a client turn from         *)
(* ClientBegin to ClientEnd.                                                *)
(*                                                                         *)
(* Constants select the shape and the variants (GOALSOURCE-RESULTS.md).     *)
(***************************************************************************)
EXTENDS Integers, FiniteSets, Sequences

CONSTANTS
    Shape,               \* "chain" | "siblings" | "diamond" | "cross" | ...
    NWorkers,            \* number of workers
    MaxSpan,             \* largest withdraw span
    Diligent,            \* client duties are weakly fair and a client turn cannot end while one is enabled (F3)
    NoCascadeCompleted,  \* variant 1: withdrawing a Completed goal actor does not cascade
    NoRegWdCheck,        \* variant 2: registerGoal does not check the withdrawn flag
    EdgesAtClaim,        \* variant 3: a claim removes the claimed actor from its dependencies' lists
    BatchTwo,            \* variant 3: a pull may claim two actors, and the second may be returned unstarted
    NoClaimWdCheck,      \* variant 4: the claim does not check dependencies' withdrawn flags
    PublishDespiteWd,    \* variant 5: a turn's end publishes Completed although withdrawn
    EarlyCleanup,        \* variant 6: a cleanup may be registered before the withdrawal
    TrackC5,             \* keep the ghost sets that check C5 (enlarges the state space)
    TrackProbes,         \* keep the ghost sets the probes use
    ClientEndFair,       \* a client turn that can end eventually does (needed for every liveness property that waits on a client duty)
    FindAlways           \* a lookup is modelled with one client too (otherwise only with two)

(* Shapes: Deps[x] is the dependency set and Owner[x] the registering     *)
(* client. Dependencies have smaller numbers than their dependents, so    *)
(* registering in numeric order respects B6; the client may choose any    *)
(* order consistent with it.                                               *)
ShapeDeps ==
    CASE Shape = "chain"    -> <<{}, {1}, {2}, {3}>>          \* 1 <- 2 <- 3 <- mesh 4
      [] Shape = "siblings" -> <<{}, {1}, {1}, {1}>>          \* parent 1; child sets 2, 3; mesh 4
      [] Shape = "diamond"  -> <<{}, {1}, {1}, {2, 3}>>
      [] Shape = "pair"     -> <<{}, {1}>>                     \* smallest: one dependency
      [] Shape = "xpair"    -> <<{}, {1}>>                     \* the same with the dependent owned by a second client
      [] Shape = "chain3"   -> <<{}, {1}, {2}>>
      [] Shape = "cross"    -> <<{}, {1}, {1}>>               \* 3 belongs to a second client
      [] Shape = "chain5"   -> <<{}, {1}, {2}, {3}, {3}>>     \* chain of 3 with two meshes
      [] Shape = "siblings5" -> <<{}, {1}, {1}, {1}, {2}>>    \* adds a mesh on child set 2
ShapeOwner ==
    CASE Shape = "cross" -> <<1, 1, 2>>
      [] Shape = "xpair" -> <<1, 2>>
      [] Shape = "chain5" -> <<1, 1, 1, 1, 1>>
      [] Shape = "siblings5" -> <<1, 1, 1, 1, 1>>
      [] OTHER -> <<1, 1, 1, 1>>

Actors  == 1..Len(ShapeDeps)
Deps    == ShapeDeps
Owner   == ShapeOwner
Clients == {Owner[x] : x \in Actors}
Workers == 1..NWorkers

VARIABLES
    st,        \* [Actors -> {"Unreg","Pending","Running","Completed","Abandoned"}]
    wd,        \* [Actors -> BOOLEAN] the withdrawn flag
    n,         \* [Actors -> Int] unmet dependencies
    dl,        \* [Actors -> SUBSET Actors] the dependents list
    rd,        \* [Actors -> SUBSET Actors] dependency pointers dereferenced in the turn
    tph,       \* [Actors -> {"idle","claimed","body","post","over"}] the goal turn's phase
    sec,       \* actors claimed as the second of a batch of two and not finished (variant 3)
    ph,        \* [Actors -> {"alloc","linked","unlinked","added","wcalled","wdrawn"}] the client's view
    found,     \* [Clients -> SUBSET Actors] actors found in the index during the client's turn
    ct,        \* [Clients -> BOOLEAN] the client's turn is in progress
    wspan,     \* [Clients -> SUBSET Actors] actors of the current withdraw call not yet withdrawn
    wdone,     \* [Clients -> SUBSET Actors] actors of the current call already withdrawn
    wret,      \* [Clients -> SUBSET Actors] withdrawn actors (call returned) not yet in a cleanup
    cadd,      \* [Clients -> SUBSET Actors] the cleanup actor under construction
    cleanups,  \* set of [acts, wait] registered cleanups
    freed,     \* [Actors -> BOOLEAN]
    fqg,       \* [Actors -> SUBSET Actors] done() conditions a goal turn has still to fire
    fqc,       \* [Clients -> SUBSET Actors] done() conditions a client turn has still to fire
    fired,     \* [Actors -> 0..2] number of times done() fired
    doneOut,   \* [Actors -> outcome] the outcome done() carried
    wdw,       \* [Actors -> SUBSET Int] ghost: turns in progress at the withdrawal and not yet ended
    uaf,       \* ghost: some step touched a freed actor
    c2bad,     \* ghost: a claim found a dependency not Completed or withdrawn
    addBad,    \* ghost: actors added to a cleanup while not withdrawn
    compWd,    \* ghost: actors that became Completed while withdrawn
    wcount,    \* [Actors -> Int] ghost: withdrawals
    returned,  \* ghost: actors returned unstarted
    casc,      \* ghost: actors abandoned by a cascade
    regAbWd    \* ghost: actors abandoned at registration because a dependency was withdrawn

vars == <<st, wd, n, dl, rd, tph, sec, ph, found, ct, wspan, wdone, wret, cadd,
          cleanups, freed, fqg, fqc, fired, doneOut, wdw, uaf, c2bad, addBad,
          compWd, wcount, returned, casc, regAbWd>>

Terminal(s) == s \in {"Completed", "Abandoned"}
Registered(x) == st[x] # "Unreg"
Runnable == {x \in Actors : st[x] = "Pending" /\ n[x] = 0}
Tok(x) == x
CTok(c) == 10 + c
InProgress == {x \in Actors : tph[x] \in {"claimed", "body", "post"}}
              \cup {CTok(c) : c \in {c2 \in Clients : ct[c2]}}
FreedSet == {x \in Actors : freed[x]}
UafOf(T) == uaf \/ (T \cap FreedSet # {})

(* Dependents (Pending) reachable from S through the dependents lists,     *)
(* excluding S itself: the cascade's victims.                              *)
PendDeps(S) == {y \in Actors : st[y] = "Pending" /\ \E a \in S : y \in dl[a]}
CascFrom(S) ==
    LET RECURSIVE Go(_, _)
        Go(A, i) == IF i = 0 THEN A ELSE Go(A \cup PendDeps(A \cup S), i - 1)
    IN Go(PendDeps(S), Cardinality(Actors))

AllListed(S) == UNION {dl[a] : a \in S}
DepsOf(S) == UNION {Deps[a] : a \in S}

(* Ending a token's turn: cleanups stop waiting for it and the ghost       *)
(* withdrawal sets forget it.                                              *)
EndTok(t) ==
    /\ cleanups' = {[r EXCEPT !.wait = r.wait \ {t}] : r \in cleanups}
    /\ wdw' = [x \in Actors |-> wdw[x] \ {t}]

(***************************************************************************)
(* Initial state                                                           *)
(***************************************************************************)
Init ==
    /\ st = [x \in Actors |-> "Unreg"]
    /\ wd = [x \in Actors |-> FALSE]
    /\ n = [x \in Actors |-> 0]
    /\ dl = [x \in Actors |-> {}]
    /\ rd = [x \in Actors |-> {}]
    /\ tph = [x \in Actors |-> "idle"]
    /\ sec = {}
    /\ ph = [x \in Actors |-> "alloc"]
    /\ found = [c \in Clients |-> {}]
    /\ ct = [c \in Clients |-> FALSE]
    /\ wspan = [c \in Clients |-> {}]
    /\ wdone = [c \in Clients |-> {}]
    /\ wret = [c \in Clients |-> {}]
    /\ cadd = [c \in Clients |-> {}]
    /\ cleanups = {}
    /\ freed = [x \in Actors |-> FALSE]
    /\ fqg = [x \in Actors |-> {}]
    /\ fqc = [c \in Clients |-> {}]
    /\ fired = [x \in Actors |-> 0]
    /\ doneOut = [x \in Actors |-> "none"]
    /\ wdw = [x \in Actors |-> {}]
    /\ uaf = FALSE
    /\ c2bad = FALSE
    /\ addBad = {}
    /\ compWd = {}
    /\ wcount = [x \in Actors |-> 0]
    /\ returned = {}
    /\ casc = {}
    /\ regAbWd = {}

(***************************************************************************)
(* The client (each step is inside one of its turns)                       *)
(***************************************************************************)
ClientBegin(c) ==
    /\ ~ct[c]
    /\ ct' = [ct EXCEPT ![c] = TRUE]
    /\ found' = [found EXCEPT ![c] = {}]
    /\ UNCHANGED <<st, wd, n, dl, rd, tph, sec, ph, wspan, wdone, wret, cadd,
                   cleanups, freed, fqg, fqc, fired, doneOut, wdw, uaf, c2bad,
                   addBad, compWd, wcount, returned, casc, regAbWd>>

(* Duties a diligent client cannot walk away from (F3 assumes the client   *)
(* eventually withdraws each registered actor and registers its cleanup).  *)
Elig(c) == {x \in Actors : Owner[x] = c /\ Registered(x)
               /\ (ph[x] \in {"alloc", "unlinked"} \/ (EarlyCleanup /\ ph[x] = "added"))}
DutyEnabled(c) ==
    \/ \E x \in Actors : Owner[x] = c /\ Registered(x) /\ ph[x] = "linked"
    \/ Elig(c) # {}
    \/ wret[c] # {}

ClientEnd(c) ==
    /\ ct[c]
    /\ fqc[c] = {} /\ wspan[c] = {} /\ wdone[c] = {} /\ cadd[c] = {}
    /\ (Diligent => ~DutyEnabled(c))
    /\ ct' = [ct EXCEPT ![c] = FALSE]
    /\ found' = [found EXCEPT ![c] = {}]
    /\ EndTok(CTok(c))
    /\ UNCHANGED <<st, wd, n, dl, rd, tph, sec, ph, wspan, wdone, wret, cadd,
                   freed, fqg, fqc, fired, doneOut, uaf, c2bad,
                   addBad, compWd, wcount, returned, casc, regAbWd>>

Link(c, x) ==
    /\ ct[c] /\ Owner[x] = c /\ ph[x] = "alloc"
    /\ ph' = [ph EXCEPT ![x] = "linked"]
    /\ UNCHANGED <<st, wd, n, dl, rd, tph, sec, found, ct, wspan, wdone, wret,
                   cadd, cleanups, freed, fqg, fqc, fired, doneOut, wdw, uaf,
                   c2bad, addBad, compWd, wcount, returned, casc, regAbWd>>

Unlink(c, x) ==
    /\ ct[c] /\ Owner[x] = c /\ ph[x] = "linked"
    /\ ph' = [ph EXCEPT ![x] = "unlinked"]
    /\ UNCHANGED <<st, wd, n, dl, rd, tph, sec, found, ct, wspan, wdone, wret,
                   cadd, cleanups, freed, fqg, fqc, fired, doneOut, wdw, uaf,
                   c2bad, addBad, compWd, wcount, returned, casc, regAbWd>>

(* A lookup finds only linked actors (D1). Only useful when something      *)
(* unregistered could depend on the found actor.                            *)
Find(c, x) ==
    /\ (FindAlways \/ Cardinality(Clients) > 1)
    /\ ct[c] /\ ph[x] = "linked" /\ x \notin found[c]
    /\ \E y \in Actors : x \in Deps[y] /\ st[y] = "Unreg"
    /\ found' = [found EXCEPT ![c] = found[c] \cup {x}]
    /\ uaf' = UafOf({x})
    /\ UNCHANGED <<st, wd, n, dl, rd, tph, sec, ph, ct, wspan, wdone, wret,
                   cadd, cleanups, freed, fqg, fqc, fired, doneOut, wdw,
                   c2bad, addBad, compWd, wcount, returned, casc, regAbWd>>

(* registerGoal (D2: dependencies found in this turn, or registered by     *)
(* this client and not yet withdrawn by it; B6: registered first).         *)
RegOK(c, x) ==
    /\ \A d \in Deps[x] :
         /\ Registered(d)
         /\ (d \in found[c] \/ (Owner[d] = c /\ ph[d] \in {"alloc", "linked", "unlinked"}))

Register(c, x) ==
    /\ ct[c] /\ Owner[x] = c /\ st[x] = "Unreg" /\ ph[x] \in {"alloc", "linked"}
    /\ RegOK(c, x)
    /\ LET D == Deps[x]
           bad == \E d \in D : st[d] = "Abandoned" \/ (~NoRegWdCheck /\ wd[d])
       IN /\ IF bad
               THEN /\ st' = [st EXCEPT ![x] = "Abandoned"]
                    /\ n' = n
                    /\ dl' = dl
                    /\ fqc' = [fqc EXCEPT ![c] = fqc[c] \cup {x}]
                    /\ regAbWd' = IF TrackProbes /\ \E d \in D : wd[d] THEN regAbWd \cup {x} ELSE regAbWd
               ELSE /\ st' = [st EXCEPT ![x] = "Pending"]
                    /\ n' = [n EXCEPT ![x] = Cardinality({d \in D : st[d] # "Completed"})]
                    /\ dl' = [d \in Actors |-> IF d \in D THEN dl[d] \cup {x} ELSE dl[d]]
                    /\ UNCHANGED <<fqc, regAbWd>>
          /\ uaf' = UafOf({x} \cup D)
    /\ UNCHANGED <<wd, rd, tph, sec, ph, found, ct, wspan, wdone, wret, cadd,
                   cleanups, freed, fqg, fired, doneOut, wdw, c2bad, addBad,
                   compWd, wcount, returned, casc>>

(* withdraw(span): a call of at most MaxSpan actors, one critical section  *)
(* per actor (the mutex is released between chunks).                       *)
BeginWithdraw(c, S) ==
    /\ ct[c] /\ wspan[c] = {} /\ wdone[c] = {}
    /\ S # {} /\ S \subseteq Elig(c) /\ Cardinality(S) <= MaxSpan
    /\ wspan' = [wspan EXCEPT ![c] = S]
    /\ ph' = [x \in Actors |-> IF x \in S THEN "wcalled" ELSE ph[x]]
    /\ UNCHANGED <<st, wd, n, dl, rd, tph, sec, found, ct, wdone, wret, cadd,
                   cleanups, freed, fqg, fqc, fired, doneOut, wdw, uaf, c2bad,
                   addBad, compWd, wcount, returned, casc, regAbWd>>

WithdrawOne(c, x) ==
    /\ ct[c] /\ x \in wspan[c]
    /\ LET doCasc == st[x] # "Abandoned" /\ ~(NoCascadeCompleted /\ st[x] = "Completed")
           A == IF doCasc THEN CascFrom({x}) ELSE {}
           selfAb == st[x] = "Pending"
           newTerm == A \cup (IF selfAb THEN {x} ELSE {})
           touched == {x} \cup A \cup AllListed({x} \cup A) \cup DepsOf(newTerm)
       IN /\ wd' = [wd EXCEPT ![x] = TRUE]
          /\ st' = [y \in Actors |-> IF y \in newTerm THEN "Abandoned" ELSE st[y]]
          /\ dl' = [d \in Actors |-> dl[d] \ newTerm]
          /\ fqc' = [fqc EXCEPT ![c] = fqc[c] \cup newTerm]
          /\ casc' = IF TrackProbes THEN casc \cup A ELSE casc
          /\ uaf' = UafOf(touched)
    /\ wcount' = [wcount EXCEPT ![x] = wcount[x] + 1]
    /\ wdw' = IF TrackC5 THEN [wdw EXCEPT ![x] = InProgress] ELSE wdw
    /\ wspan' = [wspan EXCEPT ![c] = wspan[c] \ {x}]
    /\ wdone' = [wdone EXCEPT ![c] = wdone[c] \cup {x}]
    /\ UNCHANGED <<n, rd, tph, sec, ph, found, ct, wret, cadd, cleanups, freed,
                   fqg, fired, doneOut, c2bad, addBad, compWd, returned, regAbWd>>

ReturnWithdraw(c) ==
    /\ ct[c] /\ wspan[c] = {} /\ wdone[c] # {} /\ fqc[c] = {}
    /\ wret' = [wret EXCEPT ![c] = wret[c] \cup wdone[c]]
    /\ ph' = [x \in Actors |-> IF x \in wdone[c] THEN "wdrawn" ELSE ph[x]]
    /\ wdone' = [wdone EXCEPT ![c] = {}]
    /\ UNCHANGED <<st, wd, n, dl, rd, tph, sec, found, ct, wspan, cadd,
                   cleanups, freed, fqg, fqc, fired, doneOut, wdw, uaf, c2bad,
                   addBad, compWd, wcount, returned, casc, regAbWd>>

(* Adding to the cleanup actor asserts withdrawn (addBad records a         *)
(* failure of the assertion). Variant 6 also lets the client add an        *)
(* unlinked, not yet withdrawn actor.                                       *)
AddCleanup(c, x) ==
    /\ ct[c]
    /\ \/ x \in wret[c]
       \/ (EarlyCleanup /\ Owner[x] = c /\ Registered(x) /\ ph[x] = "unlinked")
    /\ cadd' = [cadd EXCEPT ![c] = cadd[c] \cup {x}]
    /\ wret' = [wret EXCEPT ![c] = wret[c] \ {x}]
    /\ ph' = IF x \in wret[c] THEN ph ELSE [ph EXCEPT ![x] = "added"]
    /\ addBad' = IF wd[x] THEN addBad ELSE addBad \cup {x}
    /\ UNCHANGED <<st, wd, n, dl, rd, tph, sec, found, ct, wspan, wdone,
                   cleanups, freed, fqg, fqc, fired, doneOut, wdw, uaf, c2bad,
                   compWd, wcount, returned, casc, regAbWd>>

(* retire(cleanup): records the turns in progress now.           *)
RegCleanup(c) ==
    /\ ct[c] /\ cadd[c] # {}
    /\ cleanups' = cleanups \cup {[acts |-> cadd[c], wait |-> InProgress]}
    /\ cadd' = [cadd EXCEPT ![c] = {}]
    /\ UNCHANGED <<st, wd, n, dl, rd, tph, sec, ph, found, ct, wspan, wdone,
                   wret, freed, fqg, fqc, fired, doneOut, wdw, uaf, c2bad,
                   addBad, compWd, wcount, returned, casc, regAbWd>>

FireC(c) ==
    /\ fqc[c] # {}
    /\ fqc' = [fqc EXCEPT ![c] = {}]
    /\ fired' = [y \in Actors |-> IF y \in fqc[c] THEN fired[y] + 1 ELSE fired[y]]
    /\ doneOut' = [y \in Actors |-> IF y \in fqc[c] THEN st[y] ELSE doneOut[y]]
    /\ uaf' = UafOf(fqc[c])
    /\ UNCHANGED <<st, wd, n, dl, rd, tph, sec, ph, found, ct, wspan, wdone,
                   wret, cadd, cleanups, freed, fqg, wdw, c2bad, addBad,
                   compWd, wcount, returned, casc, regAbWd>>

(***************************************************************************)
(* Reclamation, abstracted (C5): the cleanup runs once every turn that was *)
(* in progress when it was registered has ended, and frees its actors.     *)
(***************************************************************************)
CleanupRun(r) ==
    /\ r \in cleanups /\ r.wait = {}
    /\ freed' = [x \in Actors |-> freed[x] \/ x \in r.acts]
    /\ cleanups' = cleanups \ {r}
    /\ UNCHANGED <<st, wd, n, dl, rd, tph, sec, ph, found, ct, wspan, wdone,
                   wret, cadd, fqg, fqc, fired, doneOut, wdw, uaf, c2bad,
                   addBad, compWd, wcount, returned, casc, regAbWd>>

(***************************************************************************)
(* Workers: pullBatch (claim), the turn, the end of the turn               *)
(***************************************************************************)
ClaimOK(x) ==
    /\ st[x] = "Pending" /\ n[x] = 0
    /\ \A d \in Deps[x] : st[d] = "Completed" /\ (NoClaimWdCheck \/ ~wd[d])

Occupied == {x \in Actors : tph[x] \in {"claimed", "body", "post"}}
FreeWorker == Cardinality(Occupied \ sec) < NWorkers

ClaimSet(S, T) ==
    /\ st' = [x \in Actors |-> IF x \in S THEN "Running" ELSE st[x]]
    /\ tph' = [x \in Actors |-> IF x \in S THEN "claimed" ELSE tph[x]]
    /\ rd' = [x \in Actors |-> IF x \in S THEN {} ELSE rd[x]]
    /\ sec' = sec \cup T
    /\ dl' = IF EdgesAtClaim
               THEN [d \in Actors |-> dl[d] \ S]
               ELSE dl
    /\ c2bad' = (c2bad \/ \E x \in S : \E d \in Deps[x] : (st[d] # "Completed" \/ wd[d]))
    /\ uaf' = UafOf(S \cup DepsOf(S))
    /\ UNCHANGED <<wd, n, ph, found, ct, wspan, wdone, wret, cadd, cleanups,
                   freed, fqg, fqc, fired, doneOut, wdw, addBad, compWd, wcount,
                   returned, casc, regAbWd>>

Claim(x) ==
    /\ FreeWorker /\ ClaimOK(x)
    /\ ClaimSet({x}, {})

\* Variant 3: one pull claims two actors; y is the second of the batch.
Claim2(x, y) ==
    /\ BatchTwo /\ x # y
    /\ FreeWorker /\ ClaimOK(x) /\ ClaimOK(y)
    /\ ClaimSet({x, y}, {y})

TurnStart(x) ==
    /\ tph[x] = "claimed"
    /\ tph' = [tph EXCEPT ![x] = "body"]
    /\ UNCHANGED <<st, wd, n, dl, rd, sec, ph, found, ct, wspan, wdone, wret,
                   cadd, cleanups, freed, fqg, fqc, fired, doneOut, wdw, uaf,
                   c2bad, addBad, compWd, wcount, returned, casc, regAbWd>>

(* A DepPtr dereference, only in the holder's turn.                        *)
TurnRead(x, d) ==
    /\ tph[x] = "body" /\ d \in Deps[x] \ rd[x]
    /\ rd' = [rd EXCEPT ![x] = rd[x] \cup {d}]
    /\ uaf' = UafOf({d})
    /\ UNCHANGED <<st, wd, n, dl, tph, sec, ph, found, ct, wspan, wdone, wret,
                   cadd, cleanups, freed, fqg, fqc, fired, doneOut, wdw,
                   c2bad, addBad, compWd, wcount, returned, casc, regAbWd>>

(* The end of a turn: Completed (publishing) or Abandoned.                 *)
FinishTurn(x, publish) ==
    LET A == IF publish THEN {} ELSE CascFrom({x})
        newTerm == {x} \cup A
        touched == {x} \cup Deps[x] \cup dl[x] \cup A \cup AllListed(A) \cup DepsOf(newTerm)
    IN /\ st' = [y \in Actors |->
                   IF y = x THEN (IF publish THEN "Completed" ELSE "Abandoned")
                   ELSE IF y \in A THEN "Abandoned" ELSE st[y]]
       /\ n' = [y \in Actors |-> IF publish /\ y \in dl[x] THEN n[y] - 1 ELSE n[y]]
       /\ dl' = [d \in Actors |-> dl[d] \ newTerm]
       /\ fqg' = [fqg EXCEPT ![x] = fqg[x] \cup newTerm]
       /\ tph' = [tph EXCEPT ![x] = "post"]
       /\ casc' = IF TrackProbes THEN casc \cup A ELSE casc
       /\ compWd' = IF publish /\ wd[x] THEN compWd \cup {x} ELSE compWd
       /\ uaf' = UafOf(touched)
       /\ UNCHANGED <<wd, rd, sec, ph, found, ct, wspan, wdone, wret, cadd,
                      cleanups, freed, fqc, fired, doneOut, wdw, c2bad, addBad,
                      wcount, returned, regAbWd>>

TurnEnd(x) ==
    /\ tph[x] = "body" /\ rd[x] = Deps[x]
    /\ FinishTurn(x, PublishDespiteWd \/ ~wd[x])

(* The turn abandons: a failure, or cooperatively after reading withdrawn  *)
(* at a yield point (the model does not distinguish them).                 *)
TurnAbandon(x) ==
    /\ tph[x] = "body"
    /\ FinishTurn(x, FALSE)

(* All the conditions a critical section collected fire together, after    *)
(* the mutex is released and before the turn (or call) ends.               *)
FireG(x) ==
    /\ fqg[x] # {}
    /\ fqg' = [fqg EXCEPT ![x] = {}]
    /\ fired' = [y \in Actors |-> IF y \in fqg[x] THEN fired[y] + 1 ELSE fired[y]]
    /\ doneOut' = [y \in Actors |-> IF y \in fqg[x] THEN st[y] ELSE doneOut[y]]
    /\ uaf' = UafOf(fqg[x])
    /\ UNCHANGED <<st, wd, n, dl, rd, tph, sec, ph, found, ct, wspan, wdone,
                   wret, cadd, cleanups, freed, fqc, wdw, c2bad, addBad,
                   compWd, wcount, returned, casc, regAbWd>>

TurnOver(x) ==
    /\ tph[x] = "post" /\ fqg[x] = {}
    /\ tph' = [tph EXCEPT ![x] = "over"]
    /\ sec' = sec \ {x}
    /\ EndTok(Tok(x))
    /\ UNCHANGED <<st, wd, n, dl, rd, ph, found, ct, wspan, wdone, wret, cadd,
                   freed, fqg, fqc, fired, doneOut, uaf, c2bad, addBad, compWd,
                   wcount, returned, casc, regAbWd>>

(* Variant 3 only: returnRemainder hands a claimed, unstarted actor back.  *)
ReturnRem(y) ==
    /\ BatchTwo
    /\ y \in sec /\ tph[y] = "claimed"
    /\ st' = [st EXCEPT ![y] = "Pending"]
    /\ tph' = [tph EXCEPT ![y] = "idle"]
    /\ sec' = sec \ {y}
    /\ returned' = returned \cup {y}
    /\ EndTok(Tok(y))
    /\ UNCHANGED <<wd, n, dl, rd, ph, found, ct, wspan, wdone, wret, cadd, freed,
                   fqg, fqc, fired, doneOut, uaf, c2bad, addBad, compWd,
                   wcount, casc, regAbWd>>

(***************************************************************************)
(* Next-state relation and specification                                   *)
(***************************************************************************)
ClientStep(c) ==
    \/ ClientBegin(c) \/ ClientEnd(c)
    \/ \E x \in Actors : Link(c, x) \/ Unlink(c, x) \/ Find(c, x)
                         \/ Register(c, x) \/ WithdrawOne(c, x) \/ AddCleanup(c, x)
    \/ \E S \in SUBSET Actors : BeginWithdraw(c, S)
    \/ ReturnWithdraw(c) \/ RegCleanup(c)
    \/ FireC(c)

WorkerStep ==
    \/ \E x \in Actors : Claim(x) \/ ReturnRem(x) \/ \E y \in Actors : Claim2(x, y)
    \/ \E x \in Actors : TurnStart(x) \/ TurnEnd(x) \/ TurnAbandon(x) \/ TurnOver(x)
                         \/ \E d \in Actors : TurnRead(x, d)
                         \/ FireG(x)
    \/ \E r \in cleanups : CleanupRun(r)

Next == (\E c \in Clients : ClientStep(c)) \/ WorkerStep

SysFair ==
    /\ \A x \in Actors :
         /\ WF_vars(Claim(x))
         /\ WF_vars(TurnStart(x))
         /\ WF_vars(\E d \in Actors : TurnRead(x, d))
         /\ WF_vars(TurnEnd(x))
         /\ WF_vars(TurnOver(x))
         /\ WF_vars(FireG(x))
    /\ WF_vars(\E r \in cleanups : CleanupRun(r))
    /\ \A c \in Clients : WF_vars(FireC(c)) /\ (ClientEndFair => WF_vars(ClientEnd(c)))

ClientFair ==
    \A c \in Clients :
        /\ WF_vars(ClientBegin(c))
        /\ WF_vars(\E x \in Actors : Unlink(c, x))
        /\ WF_vars(\E S \in SUBSET Actors : BeginWithdraw(c, S))
        /\ WF_vars(\E x \in Actors : WithdrawOne(c, x))
        /\ WF_vars(ReturnWithdraw(c))
        /\ WF_vars(\E x \in Actors : AddCleanup(c, x))
        /\ WF_vars(RegCleanup(c))

Spec == Init /\ [][Next]_vars /\ SysFair /\ (Diligent => ClientFair)

(***************************************************************************)
(* Properties                                                              *)
(***************************************************************************)
TypeOK ==
    /\ st \in [Actors -> {"Unreg", "Pending", "Running", "Completed", "Abandoned"}]
    /\ wd \in [Actors -> BOOLEAN]
    /\ \A x \in Actors : dl[x] \subseteq Actors /\ rd[x] \subseteq Deps[x]
    /\ tph \in [Actors -> {"idle", "claimed", "body", "post", "over"}]
    /\ \A c \in Clients : ct[c] \in BOOLEAN

\* A. The state machine. A1/A2/A3/A6 are action properties; A4/A5/A7 invariants.
A1 == [][\A x \in Actors :
            \/ st'[x] = st[x]
            \/ st[x] = "Unreg" /\ st'[x] \in {"Pending", "Abandoned"}
            \/ st[x] = "Pending" /\ st'[x] \in {"Running", "Abandoned"}
            \/ st[x] = "Running" /\ st'[x] \in {"Completed", "Abandoned"}]_vars
A2 == [][\A x \in Actors : Terminal(st[x]) => st'[x] = st[x]]_vars
A3 == [][\A x \in Actors : wd[x] => wd'[x]]_vars
A4 == \A x \in Actors : wcount[x] <= 1
A5 == compWd = {}
A6 == [][\A x \in Actors : (st[x] = "Running" /\ wd[x]) => st'[x] # "Completed"]_vars
A7 == \A x \in Actors :
        /\ fired[x] <= 1
        /\ fired[x] = 1 => (Terminal(st[x]) /\ doneOut[x] = st[x])
        /\ fired[x] = 0 => doneOut[x] = "none"

\* B. Counts and edges, in every state between critical sections.
B1 == \A x \in Actors : st[x] = "Pending" =>
        n[x] = Cardinality({d \in Deps[x] : st[d] # "Completed"})
B2 == \A x \in Actors : n[x] >= 0
B3 == \A d \in Actors : dl[d] = {x \in Actors : d \in Deps[x] /\ st[x] \in {"Pending", "Running"}}
B4 == \A x \in Actors : st[x] = "Pending" =>
        \A d \in Deps[x] : st[d] # "Abandoned" /\ ~wd[d]
\* B5: the runnable set is derived (Pending with n = 0), so the check is that
\* every member really has all dependencies Completed and none withdrawn.
B5 == \A x \in Runnable : \A d \in Deps[x] : st[d] = "Completed" /\ ~wd[d]

\* C. Pointer safety.
C1 == \A x \in Actors : (rd[x] # {} => tph[x] \in {"body", "post", "over"})
C2 == ~c2bad
C3 == /\ ~uaf
      /\ \A c \in Clients : found[c] \cap FreedSet = {}
      /\ \A x \in Actors : tph[x] \in {"claimed", "body"} =>
            (Deps[x] \ rd[x]) \cap FreedSet = {}
C4 == \A x \in FreedSet :
        /\ wd[x] /\ x \notin Runnable
        /\ \A d \in Actors : x \notin dl[d]
        /\ dl[x] = {}
C5 == \A x \in FreedSet : wdw[x] = {}
C6 == /\ sec = {}          \* every pull is a batch of one
      /\ returned = {}     \* nothing is returned unstarted
CleanupAddsWithdrawn == addBad = {}

\* F. Liveness (under SysFair; F3 under Diligent).
F1 == \A x \in Actors : (st[x] = "Pending") ~> (st[x] # "Pending")
F2 == \A x \in Actors : (st[x] = "Running") ~> Terminal(st[x])
F3 == \A x \in Actors : wd[x] ~> freed[x]
\* F4: a cascade is one critical section over a finite DAG, so it ends by
\* construction; the model checks it as: every actor reaching a terminal
\* state has its done() fired (the walk completed).
F4 == \A x \in Actors : Terminal(st[x]) ~> (fired[x] = 1)

\* Probes: expected to be violated, to show the states are reachable.
ProbeDepWithdrawnWhileRunning ==
    ~(\E x \in Actors : tph[x] = "body" /\ \E d \in Deps[x] \ rd[x] : wd[d])
ProbeGrandchildCascade ==
    ~(\E x \in casc : \E d \in Deps[x] : d \in casc)
ProbeCrossClientWithdrawn ==
    ~(\E x \in regAbWd : \E d \in Deps[x] : Owner[d] # Owner[x] /\ wd[d])
ProbeCompletedWithdrawnCascade ==
    ~(\E x \in casc : \E d \in Deps[x] : st[d] = "Completed" /\ wd[d])

=============================================================================
