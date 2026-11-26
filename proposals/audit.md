# Design-doc consistency audit (2026-11-16)

Scope: `DESIGN.md`, `TENETS.md`, `versions.md`, `design/*.md`, `model/*.md`;
`attic/` only as link targets. Line numbers refer to the branch's base
commit, `c15ff0d` (`main`). **MECHANICAL** findings are applied on this
branch, one commit per check (check 5 also carries the index-line renames
that belong to check 3, since they sit in `DESIGN.md`'s index). **JUDGEMENT**
findings are left open. Every "suggestion" is only that.

| Check | Mechanical fixed | Judgement open |
|---|---:|---:|
| 1. `group/slug` references | 2 | 0 |
| 2. Release numbers | 0 | 1 |
| 3. Retired terms | 8 (+6 in index lines, in check 5's commit) | 3 |
| 4. Entity docs against `concurrency.md` and `world.md` | 9 | 5 |
| 5. `DESIGN.md` index | 15 | 2 |
| 6. Style | 70 | 5 |

## Check 1: `group/slug` references

Every `group/slug` in scope resolves to a heading, an entity title line, or
a bold open-question tracking bullet in `design/open-questions.md`. Other
slash pairs in prose (`solid/air`, `f32/f64`, …) are not slugs.

Fixed: two slugs split across a line at a hyphen, which defeated grep:
`core/authority-derived-split` (`design/sim.md:609`) and
`core/earned-vastness` (`design/dimensions.md:371`).

## Check 2: release numbers against the roadmap

Every `v0.2.N` reference in scope names the release that delivers the thing
described (after the renumbering): the stand-ins (`versions.md:153-176`),
the "Out of scope" lists of v0.2.1 to v0.2.3, `DESIGN.md:101`, `:231`,
`design/deps.md:387`, `:404`, `design/input.md:187-205`,
`design/concurrency.md:1922-1924`, `design/vicinity.md:293-296`. No change.

JUDGEMENT:
- **Occlusion flood fill.** `versions.md:296` (v0.2.16) delivers "the
  connectivity flood fill on the render table", and `design/frame-thread.md:65`
  runs "the visibility flood fill over the render table". `design/world.md:560-562`
  still lists occlusion culling (layer 3 of `world/chunk-meshing`) as
  "Later", `versions.md:894` puts it out of scope for v0.2.3, and
  `design/concurrency.md:1948` calls "the LoD flood fill" a World-owned
  sub-actor, "not started". Suggestion: decide whether v0.2.16's flood fill
  is layer 3 (then drop "Later" and say which release delivers it) and
  whether the World's LoD flood fill is a separate thing.

## Check 3: retired terms

Searched for the terms in the brief plus `sponsor`, `ReadyPlan`, `PureTurn`,
`liveReaders`, `exit cache`, "18-bit", "no Brick can overflow", appearance
handle, goal keys and `Kept<T>`. None remains. `epoch` survives only for the
eventcount's wake counter (`design/dispenser.md:138-186`) and for
persistence's snapshot epochs, which are different things. The QSBR
paragraph of `design/concurrency.md:281-300` now matches global QSBR.

Fixed:
- `versions.md:309` (v0.2.18): "forgotten results freed after a grace
  period" → "withdrawn goal actors freed after a grace period".
- `design/world-actor.md:42`: the goal actors it "registers with and
  retires from" its GoalSource → "withdraws from".
- `design/concurrency.md:1004`: "Cores shrink by cleaving" → "Vicinities
  shrink by cleaving".
- `design/coords.md:148`: "the dense/palette-index order" → "the
  palette-index order".
- `TENETS.md:158`, `:249`: "forces a dense rep" → "forces a wide palette".
- `design/persist.md:182`: "(dense / palette / sparse)" → "(uniform /
  palette)"; `:206`: "a dense in-memory array" → "a wide-palette
  in-memory Brick".
- Index lines (committed with check 5): `DESIGN.md:182`, `:301` ("cores
  shrink by cleaving", "complete core", "membership decided by the Surveyor
  (an actor)"), `:310` ("installing and retiring goal actors"),
  `design/concurrency.md:1822` ("retiring abandons waiting dependents").

JUDGEMENT:
- **"Dense" storage as a live option.** `design/world.md:501-504`, the
  decision line of `world/chunk-representation`: "(e.g. dense array,
  palette/RLE, sparse/octree) … use dense storage where volatile". Its own
  later bullet (`:523`) and `design/brick.md:34-37` say the representations
  are a Brick's: uniform and palette. `TENETS.md:45` lists "virtualized chunk
  representations, dense voxel arrays" as the *derived* layer, though the
  Brick holds authoritative chunk content. Suggestion: restate the decision
  line as "a Brick's uniform and palette representations (`world/brick`)",
  say whether RLE and a sparse octree are ruled out or deferred, and reword
  `TENETS.md:45` to make the representation *choice* the derived part.
- **"Job" for a turn.** `design/dispenser.md:63` says "'Job' in older docs
  means a task", but `design/concurrency.md` still uses it for the unit
  that runs on a chunk (`:41` "At most one job runs on a chunk at a time",
  `:58`, `:205`, `:224`, `:315-324`, `:542`, `:611`, `:692`, `:783-785`,
  `:1085`, `:1214`), and `design/world.md:660`, `:686`, `:693` use "job
  system" and "its job" (`world/chunk-scheduling`). Suggestion: "turn" for
  chunk work and "goal actor" for generation work. It is about 25 sites, so
  it deserves one reviewed pass.
- **`design/concurrency.md:487-495`**, "The Surveyor owns Vicinities'
  lifecycle … caches Vicinities by centre and shape and hands out shared,
  reference-counted handles", against `design/vicinity.md:21` ("The World's
  Surveyor decides membership; the Vicinity simulates") and the World's
  halts (`design/world-actor.md:112-125`). This is outside the history
  section (`:524-630`) but is superseded history. Suggestion: delete or move
  it to `attic/` together with that section (check 6).

## Check 4: entity docs against `design/concurrency.md` and `design/world.md`

Fixed (`design/concurrency.md` unless named):
- `:94`: "Chunk storage itself, through versioned subchunks" → "through a
  version counter per palette-form chunk" (its own `:222`, `world.md:537`).
- `:92`: "The scheduler's other queues (ready chunk jobs)" → "The sources'
  runnable sets (…; the Dispenser has no ready queue)" (`dispenser.md:118`).
- `:400`: the nudge mask is "on the record" → "on the chunk's actor" (`:372`,
  `surveyor.md:232`).
- "array-form chunk" → "palette-form chunk" (the Brick's term):
  `concurrency.md:217`, `:222`, `:1854`, `versions.md:338`, `DESIGN.md:182`.
- `design/world.md:575`: meshing "reads only the chunk's own snapshot" →
  "own cells" (`concurrency.md:35`: there is no published copy). The
  matching `DESIGN.md:125` index line is changed too.

JUDGEMENT:
1. **Function actor or goal actor for generation, refinement, meshing and
   blob I/O.** `design/dispenser.md:230`, `design/goal-source.md:368-371`,
   `design/surveyor.md:112-122`, `design/world-actor.md:31` and
   `design/world.md:333` make these goal actors. Still function actors:
   `dispenser.md:51` ("Uses: generation and refinement … meshing chunk data
   outside every Vicinity, writing a save's blobs"), `:206` ("Generation runs
   as a function actor offered by the World's source … the completed actor is
   kept (an octree node)"); `cartographer.md:9`, `:31`, `:103`, `:122-127`
   (invariant 3: "Called only from function actors' turns … never inside
   another kind of actor's turn"), `:150`, against `:33` ("function actors
   and goal actors") and `DESIGN.md:298`; `chunk.md:38`, `:76`, `:178`;
   `concurrency.md:316`, `:349`, `:1222-1224`, `:1242`; `surveyor.md:88`
   ("a real mip filter, in a function actor"); `world.md:365` ("meshed by a
   function actor"); `world-actor.md:81`, `:89`. Suggestion: function actors
   are for one-turn work with no dependencies; move every site above to goal
   actors, except any that truly have no dependencies (the mip filter may be
   one), and fix invariant 3 to "function actors and goal actors".
2. **Who offers the World's work.** `dispenser.md:8-10` and
   `concurrency.md:1475-1477`: "the World for generation, loading and
   meshing of chunks outside every Vicinity, which have no actor", against
   `goal-source.md:43` and `:63-68` (GoalSource is the source, one per World;
   the Registrar's work is a client). Suggestion: the World's own source
   covers its turns and Registrar-offered I/O, and GoalSource covers
   generation and meshing.
3. **Saves: function actors and a message, or goal actors.**
   `world-actor.md:81` ("written as new blobs … by function actors … that the
   Registrar offers") and `:106` ("Requests arrive as hard messages (commit
   this save …)"), against `goal-source.md:370` (the Registrar registers,
   withdraws and frees blob-write goal actors; the commit is a goal that
   depends on all of them) and `dispenser.md:230`. Suggestion: the goal
   actor reading in `goal-source.md`; the hard message then only carries the
   captured scarce rows.
4. **Function-actor machinery that is now goal-actor territory.**
   `dispenser.md:248-256`: shared `result()` "for many readers (children
   reading a parent node's grids, mesh turns)" and `claimResult()`, and "Its
   source … drops a result it no longer wants"; `:446-452`: abandonment
   ripples through chained waiters; `:464-476`: "Short-lived function actors
   are pooled per type (mesh, swap and blob steps)"; `:481`: `reset()` bumps a
   generation that `Borrow<T>` compares. Against `goal-source.md:307` ("No
   keys: the client deduplicates through its own index"), `:332` ("Results
   with a single consumer are messages") and goal actors held in the
   client's slab. Suggestion: restrict these bullets to function actors and
   drop the node and mesh examples, once item 1 is decided.
5. **`world/chunk-scheduling` against GoalSource and the Surveyor**
   (`world.md:654-703`): "sits on top of `sim/cpu-parallelism`'s job system"
   (`:660`), where the Dispenser is the executor; "CANCELLABLE if its job has
   not started, and DEMOTED-not-discarded if partway" (`:686`), where
   `goal-source.md` withdraws or lets a running turn finish and publish
   nothing; "pure reordering of independent jobs — no dependency untangling.
   Worldgen has almost no ordering constraints" (`:693`), where refinement is
   outside-in with dependencies (`surveyor.md:112-122`). Suggestion: restate
   it as the Surveyor's priority policy over goal actors, or mark it
   superseded in part.

## Check 5: `DESIGN.md` index

Counts verified: Brick 9, GoalSource 14, Dispenser 18, Surveyor 11,
Cartographer 13, Vicinity 15, frame thread 7. Every heading in the entity
index has a file and every `design/*.md` file is indexed.

Fixed:
- `DESIGN.md:313`: the `engine/goal-source` line sat under the
  `design/surveyor.md` heading; it has its own heading now.
- `DESIGN.md:182`: "six ownership and borrowing rules" → "seven" (R1-R7),
  with the seventh (dependency pointers) added to the list.
- `DESIGN.md:304`: `engine/frame-thread` gained "7 draft invariants".
- `design/concurrency.md:1469`: "12 invariants" → "18" for the Dispenser.
- `design/concurrency.md:1808-1840`: the entity-doc list lacked `world/brick`
  and `world/chunk`; both added.
- `DESIGN.md:310`: "dependants" → "dependents".
- `DESIGN.md:190` ("halted-cut persistence") and `design/engine.md:152`
  ("snapshot-epoch persistence") → "per-Vicinity saves", as in
  `design/concurrency.md` and `design/world-actor.md:139`.
- Retired terms in index lines: see check 3.

JUDGEMENT:
- **`design/brick.md` has no section under "Index: entity design docs"**
  (`DESIGN.md:293`); `world/brick` is only a topical line (`:123`).
  Suggestion: add an entity section whose line carries the Brick entry, and
  leave a pointer line under `design/world.md`.
- **`DESIGN.md:182` ends mid-list:** "…chunks have no mailboxes; Surveyor,
  Dispenser". Suggestion: complete it as "the Cartographer, Surveyor,
  Dispenser and GoalSource have entity docs" or cut the tail.

## Check 6: style (specs, not devlogs)

Fixed: 66 dated or attributed parentheticals and prefixes in `DESIGN.md`'s
index (for example "(Michael 2026-11-09)", "(2026-11-04, Michael)", "decided
2026-11-06:", "(`mana` renamed `liminality`, 2026-11-01 …)", "moved out of
`design/concurrency.md` 2026-11-09", the "(2026-11-09)" on the style rule's
own paragraph) and four in entity docs: `design/dispenser.md:118-119`,
`design/world-actor.md:78`, `design/frame-thread.md:389`,
`design/cartographer.md:223`.

JUDGEMENT:
- **Design history inside a spec section.** `design/concurrency.md:524-630`
  ("this section is the design history that led there") still states
  superseded mechanics in the present tense: "Merge before contact
  (assistant): merge when the rings of loaded but unsimulated chunks … would
  touch … union their … epoch lines" (`:558`), "I'm 100% convinced it's
  better than what we had before" (`:530`), "The Surveyor owns Vicinities'
  lifecycle" (`:606`). Nineteen of its headings carry dates (for example
  `:524`, `:1808` "Where the design stands (updated 2026-11-13)").
  Suggestion: move the section to `attic/` and leave a pointer.
- **Remaining dated notes in `DESIGN.md`:** "revised 2026-11-09:" at `:96`,
  `:133`, `:207`, each keeping the superseded alternative beside the current
  one; "consolidated 2026-10-30" at `:14`; "removed 2026-11-05" tombstones at
  `:183-185` and "the removed 2026-10-30 plan" at `:191`; "(Michael: iffy)" at
  `:149`. Suggestion: restate each as the current rule only; keep a
  tombstone's date only if a reader needs it to find the old text.
- **Other files, by hits (attribution / dated).** `design/concurrency.md`
  78 / 92, `design/inventory.md` 42 / 42, `design/world.md` 26 / 29,
  `design/worldgen.md` 23 / 15, `design/sim-execution.md` 22 / 22,
  `design/render.md` 22 / 19, `design/dimensions.md` 20 / 15,
  `design/tools.md` 17 / 19, `design/first-session.md` 15 / 1,
  `design/geology.md` 14 / 4, `design/sim.md` 14 / 32, `TENETS.md` 9 / 4,
  `versions.md` 7 / 13, `design/open-questions.md` 7 / 49 (open issues may
  name who raised what), `design/input.md` 5 / 20. These are older topical
  entries. The same regular parentheticals could be stripped with the
  regexes used on `DESIGN.md`, but each file deserves a read first.
  Suggestion: strip them when each file is next revised.
- **Status lines in `versions.md`** ("SPECIFIED 2026-11-10" at `:490`,
  `:575`, `:713`, "closed 2026-11-04" at `:52`, "direction, 2026-10-30" at
  `:994`, and the 2026-11-10 note at `:995`). Suggestion: decide whether a
  status date is part of the spec.
- **Entity-doc remnants:** `design/dispenser.md:63` ('"Job" in older docs
  means a task'), `design/cartographer.md:134` ("revises the 'pin the SIMD
  level per …'"), `design/surveyor.md:26-28` ("the starting point this
  requirement revises"). Suggestion: say the current rule only.
