# Concurrency and meshing: retired ideas worth revisiting

Not plan of record. These ideas were superseded in `sim/concurrency-model`
(`design/concurrency.md`) and `world/chunk-meshing` (`design/world.md`), but
may be worth revisiting if the need behind them comes back.

## Lai–Yang snapshots of a running Vicinity

**Superseded by:** per-Vicinity saves, frozen or optimistic against a
transformation counter (`design/concurrency.md`, "Conserved objects, hard
messages and persistence").

**What it offers:** a consistent cut of a busy Vicinity, soft state
included, without ever halting it. Worth revisiting for live checkpoints
with finer crash guarantees, or if optimistic saves abandon too often under
heavy transformation load.

**The design:** a periodic consistent snapshot of the Vicinity, using the
Lai–Yang algorithm for unordered channels (chunks' message queues are not
FIFO across senders):
1. a save starts epoch e; each chunk snapshots its own state at its next
   turn boundary;
2. **every message carries its sender's epoch.** A chunk that hasn't
   snapshotted yet and receives a message tagged e snapshots before
   processing it, so it never contains an effect from after the cut; a
   chunk that has snapshotted and receives a conserved message tagged
   older than e records it as part of the cut, so something in flight is
   saved exactly once;
3. snapshots are written gradually; epoch e becomes durable with **one
   commit record per Vicinity** once every member's part is written;
   unchanged chunks carry forward;
4. **after a crash,** the whole Vicinity reloads from its last complete
   epoch: every chunk agrees about every entity and block. A crash loses up
   to a save interval, never duplicates or loses an item;
5. merge and split happen at a quiesce and start a new epoch line from a
   consistent state; a chunk leaving its Vicinity is saved in a complete
   epoch first; frozen chunks need nothing.

**Why it lost:** optimistic saves give the same guarantee for conserved
state (a crash rolls instances back, never duplicates them) with no epoch
tag on every message and no per-chunk snapshot protocol, and soft state
needs no consistent cut.

**Related, rejected:** chunk checkpoints plus a per-Vicinity write-ahead
journal (a durable write per transfer, per arrow or falling block, with
checkpoints coupled to journal durability). A journal could return only
for rare, high-value operations that need finer crash guarantees, if any
turn out to.

## Neighbour halos for meshing

**Superseded by:** isolated meshing, a chunk's mesh being a pure function
of its own cells (`design/world.md`, "Meshing is isolated").

**What it offers:** correct faces at chunk seams without conservative
extra geometry, and two mechanisms that may return in the LoD and seams
design: **generation numbers as verification** and **push as a hint**.

**The design:** chunks published per-face visibility-class slices
("halos", 512 B bit-planes per class) with generation numbers, read as
atomic `(generation, slice)` snapshots; a face change pushed a remesh hint
to the neighbour, verified by generation. The 12 edge neighbours were
briefly added because connected-glass frame bits read a diagonal cell.

**Why it lost:** it made every chunk's mesh depend on its neighbours and
brought concurrency in through the back door.
