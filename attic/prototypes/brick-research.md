# Proposal: Brick discretionary compaction and reverse lookup

Research, prototypes and measurements for two open points of `world/brick`
(`design/brick.md`): when to compact a Brick between saves, and which
reverse lookup (state id to palette index) to use above the scan threshold,
with the threshold itself. Nothing here changes `design/`; recommendations
are proposals, and every parameter is labelled **placeholder** (a tuning
choice) or **derived** (follows from a stated constraint or measurement).

Code: `prototypes/brick/` (`brick.hpp` now follows brick.md's
growth rule; `reverse_lookup.hpp`, `bench_lookup.cpp`, `bench_compaction.cpp`,
`test_reverse_lookup.cpp` are new). Raw benchmark output:
`prototypes/brick-research-results/`. `prototypes/NOTES.md` is the
earlier prototype's notes, kept unchanged; its compaction-on-demand items no
longer describe the code.

Build and run (Clang 15.0.7, CMake 3.22, `-std=c++20 -Wall -Wextra -Werror
-ffp-contract=off -Wthread-safety`):

```
cmake -S prototypes -B <dir> -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build <dir> && ctest --test-dir <dir>
<dir>/test_equivalence --long
<dir>/bench_lookup; <dir>/bench_compaction 500000; <dir>/bench_brick
```

Measurements: one thread, Release (`-O3`), AMD EPYC 9R14 (Zen 4) with
another heavy job running on the host, so single numbers carry a few
nanoseconds of noise. versions.md asks for both reference machines; only
this one was measured.

## 1. Summary

- **Discretionary compaction:** at the end of the owning actor's turn,
  compact a palette Brick when the **appends since its last compaction**
  reach **max(F, α · L₀)**, where L₀ is the live-entry count the last
  compaction (or fill) found. F = 4096 and α = 1/4 (placeholders, with the
  constraints in 3.5). The discretionary compaction keeps the current width
  unless narrowing leaves room for at least F appends, goes uniform if one
  state is live, and drops the reverse table. The signal costs one counter
  increment per append and one stored count; it never fired on the
  realistic workloads (building, mining, terrain edits: 0 compactions in
  500 000 edits each), and under adversarial churn it holds the palette to
  P ≤ L₀ + max(F, α L₀) (+ one turn's edits). Over 500 000 painting edits it
  kept a Brick at no more than 608 KiB instead of 7 MiB, at about 67 ns of compaction
  per appended state.
- **Reverse lookup:** keep brick.md's choice: open addressing with linear
  probing over 4-byte palette indices, compared through the palette, with
  power-of-two capacity and load at most 1/2. It was fastest on every time
  axis at every N tested (dependent-hit latency 8 ns at 4096 entries, 20 ns
  at 2¹⁸; inserts 6 to 13 ns amortised). It costs 8 to 16 bytes per entry
  (2 to 4 MiB at 2¹⁸). No candidate came close on lookup latency; the
  sorted structures win only on memory (8 bytes per entry).
- **Scan threshold: 32** (derived on this machine), not 256. At 256 entries
  the SIMD scan takes 19.5 ns per dependent hit against 7.8 ns for the
  table; at 32 the scan takes 11.9 ns against 8.8 ns, and below 16 they are
  equal.
- **Design problems found** (section 5). The ones that matter most: a save
  as chunk.md describes it does not compact the *live* Brick of a chunk
  that stays loaded. Repacking a growth inside the edit's write window
  makes that window last 0.3 to 1.1 ms. Palette appends reallocate storage
  that concurrent readers use.

## 2. Workloads and harness

`bench_compaction.cpp` drives one Brick through 500 000 edits per run, 32
edits per owner turn (placeholder), calling `turnEnd(policy)` after each
turn. An exact oracle with per-state counts, kept by the harness and never
by the Brick, gives the live count L. The start is a generated terrain
Brick: stone, dirt, grass, 4 ores, air above a ground line near z = 32; 8
states, width 4.

| workload | what each edit does |
|---|---|
| build | extends a random walk of placed blocks above ground from 48 building states (12 types × 4 orientations); 15 % remove to air |
| mine | walks tunnels through the ground, setting cells to air; 10 % torches, 5 % cobblestone |
| terrain | spheres of radius 2 to 6 near the ground, half explosions (air), half fills with one of 4 materials |
| paint-64, paint-4096 | a random cell of a 4³ or 16³ region gets a brand-new state (each edit appends one, and usually orphans one) |
| orient-sweep | one of 100 isolated blocks of distinct types turns to its next of 24 orientations (states recur) |
| random | any cell gets a state from a pool of 2²⁰ (live grows towards 2¹⁸; the pathological build) |

Memory is indices + palette (vector capacity) + reverse table.

## 3. Objective 1: discretionary compaction

### 3.1 Constraints restated
No per-entry counts; never inside an edit's write window; only in the
owner's turn; through the representation swap (build new, swap, retire).
The prototype's compaction is an export of every index, a mark of live
entries, a remap, and a build of the new representation; it is the same
operation a save performs.

### 3.2 Cost of a compaction (`bench_brick`)

| from width | live | to width | µs |
|---:|---:|---:|---:|
| 32 | 2 to 4096 | 1 to 16 | 231 to 265 |
| 8 or 16 | 200 | 8 | 261 |
| 16 | 3000 | 16 | 284 |
| 32 | 70 000 | 32 | 542 |
| 32 | 2¹⁸ | 32 | 1318 |

T_c ≈ 0.25 ms + about 4 ns per live entry. In the workload runs, the
largest single compaction was 2.0 to 2.4 ms (32 bits, about 200 000 live,
with the table rebuilt lazily afterwards). For comparison, growth costs
0.32 to 0.49 ms up to 8 → 16 bits and 1.13 ms for 16 → 32 bits.

### 3.3 Alternatives considered

1. **None** (saves and bulk fills only, today's design).
2. **Appends since the last compaction**, A ≥ max(F, α L₀). O(1): one
   increment when an edit appends, and L₀ stored by each compaction.
3. **Possible deaths (a witness bound),** a new O(1) signal. When an edit
   overwrites a cell holding index a, it looks for a *witness*, another
   cell holding a, in a small fixed neighbourhood: the edit's own 64-byte
   block of index words (512/w cells, tested bit-parallel; no extra cache
   line), then the y and z face neighbours. If no witness is found, it
   counts a possible death D̂. **D̂ is an upper bound on the dead entries
   since the last compaction.** Every entry that died lost its last holder
   in some edit; at that edit no other cell held it, so no witness existed
   and that edit was counted. Distinct deaths map to distinct edits, and
   revivals only loosen the bound. The trigger is D̂ ≥ max(F, α P). It
   bounds dead entries relative to the *current* palette (P < max(L + F,
   L/(1−α))), which no other O(1) signal gives.
4. **Width clause:** after a growth, compact if the live lower bound
   P − D̂ would fit the previous width with room left. Without the
   witness, the lower bound after a growth is P > C(w/2), so the clause
   never fires.
5. **Time or idle** (turns or edits since the last compaction, idle
   turns). These carry no information about dead entries: an idle Brick's
   dead entries cost static memory that the next save reclaims anyway.
   Not pursued as a trigger; see 3.7 for the reverse table.
6. **Palette fullness against width capacity:** this is the growth event
   by another name, and it says nothing about dead entries. Subsumed by 4.
7. **Reclaiming in place** (a free list of dead slots, the earlier
   prototype's on-demand compaction): needs a cell pass inside an edit,
   which the design forbids, and deletes from the reverse table. Rejected
   by the constraints. Every option above rebuilds through the swap, so
   **the reverse lookup never needs deletes.**

### 3.4 Measurements (500 000 edits each)

Selected rows (`bench_compaction.txt` has every policy). "Compact ms" is
the total over the run. Edit ns is the mean edit cost including growths,
excluding compactions.

| workload | policy | final P / L | width | max mem KiB | compactions | growths | compact ms | edit ns |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| build | none | 56 / 56 | 8 | 257 | 0 | 1 | 0 | 18.0 |
| build | witness 4096/0.5 | 56 / 56 | 8 | 257 | 40 | 1 | 10.4 | 29.8 |
| build | **appends 4096/¼** | 56 / 56 | 8 | 257 | 0 | 1 | 0 | 17.1 |
| mine | none / **appends** | 10 / 10 | 4 | 128 | 0 | 0 | 0 | 11.0 / 10.9 |
| terrain | none / **appends** | 12 / 10 | 4 | 128 | 0 | 0 | 0 | 10.8 / 10.8 |
| paint-64 | none | 500 008 / 72 | 32 | 7168 | 0 | 3 | 0 | 56.2 |
| paint-64 | **appends 4096/¼** | 360 / 72 | 16 | 608 | 122 | 2 | 33.3 | 39.7 |
| paint-64 | appends 1024/¼ | 360 / 72 | 16 | 536 | 488 | 2 | 131.7 | 40.5 |
| paint-4096 | none | 500 008 / 4104 | 32 | 7168 | 0 | 3 | 0 | 54.2 |
| paint-4096 | **appends 4096/¼** | 4392 / 4104 | 16 | 608 | 122 | 2 | 38.5 | 43.8 |
| orient-sweep | none / **appends 4096/¼** | 2408 / 108 | 16 | 560 | 0 | 2 | 0 | 16.5 |
| orient-sweep | appends 1024/¼ | 172 / 108 | 16 | 536 | 488 | 2 | 131.8 | 37.5 |
| orient-sweep | witness 4096/0.5 | 268 / 108 | 16 | 560 | 122 | 2 | 33.2 | 40.4 |
| random | none | 397 940 / 201 217 | 32 | 7168 | 0 | 3 | 0 | 73.9 |
| random | **appends 4096/¼** | 204 980 / 201 217 | 32 | 4096 | 22 | 3 | 18.7 | 78.5 |
| random | appends 4096/½ | 228 469 / 201 217 | 32 | 7168 | 13 | 3 | 13.8 | 80.7 |

Over edit count (P / L / width / memory KiB):

| run | 10k | 50k | 100k | 250k | 500k |
|---|---|---|---|---|---|
| paint-64, none | 10 024 / 72 / 16 / 704 | 50 024 / 72 / 16 / 1280 | 100 008 / 72 / 32 / 2560 | 250 024 / 72 / 32 / 4096 | 500 008 / 72 / 32 / 7168 |
| paint-64, appends 4096/¼ | 1896 / 72 / 16 / 536 | 936 / 72 / 16 / 524 | 1768 / 72 / 16 / 536 | 232 / 72 / 16 / 515 | 360 / 72 / 16 / 518 |
| paint-4096, none | 10 024 / 3752 / 16 / 704 | 50 024 / 4104 / 16 / 1280 | 100 008 / 4104 / 32 / 2560 | 250 024 / 4104 / 32 / 4096 | 500 008 / 4104 / 32 / 7168 |
| paint-4096, appends 4096/¼ | 5365 / 3752 / 16 / 608 | 4968 / 4104 / 16 / 608 | 5800 / 4104 / 16 / 608 | 4264 / 4104 / 16 / 608 | 4392 / 4104 / 16 / 608 |
| random, none | 9986 / 9794 / 16 / 704 | 48 826 / 44 519 / 16 / 1280 | 95 285 / 79 729 / 32 / 2560 | 222 322 / 149 096 / 32 / 4096 | 397 940 / 201 217 / 32 / 7168 |
| random, appends 4096/¼ | 9851 / 9794 / 16 / 704 | 44 649 / 44 519 / 16 / 1280 | 82 069 / 79 729 / 32 / 2560 | 156 839 / 149 096 / 32 / 4096 | 204 980 / 201 217 / 32 / 4096 |

Build, mine, terrain and orient-sweep stay flat under every policy that
does not compact them (identical rows in the raw output).

Witness precision and cost, possible deaths against true deaths over the
run: build 167 969 for 17; terrain 1079 for 2; mining 296 for 1; painting,
sweep and random within 0 to 19 % of the truth. Per-edit cost of the
witness (block + faces): build 18.0 → 29.3 ns, terrain 10.8 → 16.3 ns,
mining 11.0 → 12.6 ns; 1.6 to 22 ns in the random-cell microbenchmark
(`bench_brick`). The block-only witness is a little cheaper, at 28.1 ns on
build, with slightly more false positives.

### 3.5 Analysis

- **None is unbounded between saves.** P = L₀ + A, and A has no bound
  except the number of distinct states that can reach the Brick. Painting
  took a 72-live Brick to 32 bits and 7 MiB (ideal: 257 KiB). At player
  edit rates, with saves every few minutes, this is harmless: about 6000
  appends at 10 new states per second over 10 minutes stays at 16 bits and
  under 1 MiB. A trigger is a backstop for automated or adversarial edit
  sources, and for long gaps between saves.
- **Appends bounds memory by the last live count** (derived): at every
  turn end, P < L₀ + max(F, α L₀) + E_turn. The reverse table follows P
  (4 to 8 bytes per slot-occupying entry at load ≤ 1/2), so it is bounded
  too.
- **No thrash** (derived): each compaction needs at least F appends since
  the previous one. The amortised cost per append is therefore at most
  T_c / max(F, α L₀): ≤ 0.07 µs at small palettes (measured 67 ns on
  paint-64) and ≤ 1.3 ms / 65 536 ≈ 20 ns at 2¹⁸ live (measured 37 ns on
  random, whose L₀ was smaller for most of the run). Recurring states
  append only once, so sweeps, toggles and growth stages never trigger
  it: orient-sweep made 0 compactions at F = 4096.
- **Lower constraint on F** (derived from orient-sweep): F must exceed a
  Brick's working set of *recurring* states. Otherwise a compaction drops
  entries that are about to be re-appended, and the cycle repeats every F
  appends. At F = 1024 the sweep's 2408-state working set gave 488
  compactions (264 ns per edit). 4096 is a placeholder above that working
  set, which is 100 blocks × 24 orientations here. Upper constraint on F:
  the dead entries it tolerates, 4 + 8 to 16 bytes each with the table,
  so at most about 80 KiB at F = 4096.
- **α** (placeholder 1/4) matters only for large palettes. α = 1/2 let the
  random workload peak at 7 MiB, because P ≈ 1.5 L₀ crossed the table's
  next power of two. α = 1/4 held it at 4 MiB for 22 instead of 13
  compactions.
- **The width cost of dead entries is bounded but real:** the width can be
  at most smallestWidth((1+α) L₀ + F). For a small live set under
  painting this means 16 bits instead of 8 (608 against 257 KiB).
  Narrowing back would thrash. The first width-clause prototype narrowed
  16 → 8 whenever the lower bound allowed, and on paint-64 that cost 260
  compactions and 261 growths per 50 000 edits (1.7 µs per edit). The
  recommendation therefore narrows only where the new width leaves room
  for F appends, which in practice means never below 16 bits between
  saves. Narrowing is left to the save.
- **The witness signal does not pay for itself.** It is sound, but its
  false positives are high exactly where play is realistic: about a third
  of building edits are flagged, because a placed block's state is
  usually rare among its neighbours. That cost 40 useless compactions per
  500 000 build edits and 5 to 12 ns on every edit. Its one advantage is
  reclaiming dead entries that arise without appends (the sweep), and
  those cannot grow memory, since P ≤ L₀ + A.
- **The width clause** has information only with the witness, and with the
  room rule it can only act at 32 → 16 bits, a case that the appends
  trigger already prevents dead entries from causing. Rejected with the
  witness.

### 3.6 Recommendation
- **Trigger:** at the end of the owner's turn (a chunk's, or the World's
  for terrain Bricks), outside every edit's write window, compact a
  palette Brick when `appends ≥ max(F, α · L₀)`.
  - `appends`: entries appended since the last compaction or fill,
    incremented on the edit path only when an edit appends. Promotion
    counts its new entry.
  - `L₀`: the live count found by the last compaction, the distinct states
    of the last bulk fill, or a generator palette's size (an upper bound;
    unused generator entries are dead).
  - F = 4096 (placeholder; constrained below by the recurring-state working
    set and above by tolerated dead memory, 3.5); α = 1/4 (placeholder).
- **Action:** the save's compaction (renumber the live entries, uniform if
  one is live), except that the width stays unless the narrower width
  leaves room for max(F, ¼ · capacity) appends (placeholder ¼). The new
  representation is built from the old, swapped in and the old one
  retired; the reverse table is dropped and rebuilt lazily.
- **Telemetry:** as brick.md already plans (palette size against live
  entries at each save), plus discretionary compactions per Brick per hour
  and the largest one's duration.
- Prototype: `CompactionPolicy::recommended()` and `Brick::turnEnd` in
  `brick.hpp`; `testAppendsBound` checks the bound and that recurring
  states never trigger.

### 3.7 Not recommended, or left open
- An idle rule that drops the reverse table after some turns without edits
  would reclaim its 8 to 16 bytes per entry sooner than a freeze or save.
  It is cheap, but unmeasured, and only matters for Bricks with large
  palettes.
- A 2 ms compaction at 32 bits and 2¹⁸ live inside one turn: whether a
  chunk turn can absorb it depends on turn budgets, which are not
  specified. A save performs the same work.

## 4. Objective 2: reverse lookup

### 4.1 Candidates (`reverse_lookup.hpp`)
All index an append-only palette of distinct 4-byte ids (pal[i] = id) and
support find, insert (the next index) and rebuild (the whole palette). None
needs delete under 3.6.

| candidate | find: worst / expected | insert: worst / amortised | rebuild | memory per entry |
|---|---|---|---|---|
| linear scan (SSE2, AVX2) | O(N) / O(N) | O(1) (the palette append) | none | 0 |
| sorted array + binary search, memmove insert | O(log N) / O(log N) | O(N) / O(N) | O(N log N) | 8 B |
| sorted run + unsorted tail (the palette's own last ≤ max(64, 2√N) entries, SIMD-scanned), merged when full | O(log N + √N) | O(N) merge / O(√N) | O(N log N) | 8 B |
| Eytzinger layout + the same tail | O(log N + √N) | O(N) re-layout / O(√N) | O(N log N) | 8 B |
| linear probing, (id, index) 8-byte slots, load ≤ ½ | O(N) / O(1), 1.5 probes hit, 2.5 miss (Knuth) | O(N) regrowth / O(1) | O(N) | 16 to 32 B |
| **linear probing, 4-byte palette index compared through the palette, load ≤ ½** | O(N) / O(1), same probe counts, plus one palette read per probe | O(N) regrowth / O(1) | O(N) | 8 to 16 B |
| the same, load ≤ ¾ | O(N) / O(1), 2.5 hit, 8.5 miss | O(N) / O(1) | O(N) | 5.3 to 10.7 B |
| linear probing, 8-bit tag + 24-bit index (palette ≤ 2²⁴ − 1) | as above, fewer palette reads | as above | O(N) | as above |
| Swiss-table style (16-slot SSE2 control groups, 4-byte index), load ≤ ⅞ | O(N) / O(1) | O(N) / O(1) | O(N) | 5.7 to 11.4 B |

Memory ranges come from power-of-two capacity: just after a doubling,
load is half the cap. The Fibonacci hash (id × 2⁶⁴/φ, top bits) behaved
the same on random and on sequential ids (one block's states interned
together).

### 4.2 Measurements (`bench_lookup`, random distinct ids)

"Hit (dep)" is latency: each query's id depends on the previous answer,
as in an edit. "Hit" and "miss" are throughput. Insert@N is the mean over
up to 4096 inserts ending at N. "Worst ins" is the largest single insert
while building from empty (a regrowth or a merge). The tables are at
exactly N = 2^k, so the hash tables are at their maximum load and minimum
memory.

| structure | N | hit (dep) ns | miss ns | insert@N ns | worst ins µs | rebuild µs | B/entry |
|---|---:|---:|---:|---:|---:|---:|---:|
| sorted+memmove | 4096 | 30.7 | 10.5 | 108 | 0.6 | 109 | 8 |
| sorted+memmove | 2¹⁶ | 64.8 | 16.2 | 2937 | 21.6 | 3062 | 8 |
| sorted+memmove | 2¹⁸ | 99.7 | 23.3 | 12 938 | (≈13) | 13 560 | 8 |
| sorted+memmove | 2²⁰ | 176.7 | 33.5 | 49 440 | (≈50) | 59 713 | 8 |
| sorted+tail | 2¹⁸ | 100.4 | 39.1 | 316 | 438 | 13 557 | 8 |
| eytzinger+tail | 2¹⁸ | 71.8 | 28.8 | 1190 | 1228 | 13 954 | 8 |
| **linear idx ½** | 1024 | 7.9 | 9.0 | 6.1 | 1.4 | 1.6 | 8 |
| **linear idx ½** | 4096 | 9.1 | 9.5 | 6.9 | 5.6 | 8.5 | 8 |
| **linear idx ½** | 2¹⁶ | 14.1 | 11.1 | 10.5 | 93.5 | 337 | 8 |
| **linear idx ½** | 2¹⁸ | 19.9 | 12.6 | 12.6 | 414 | 1495 | 8 |
| **linear idx ½** | 2¹⁹ | 21.8 | 12.8 | 14.2 | 878 | 3028 | 8 |
| **linear idx ½** | 2²⁰ | 25.0 | 13.3 | 13.1 | 1877 | 6186 | 8 |
| linear (id,idx) 8 B ½ | 2¹⁸ | 18.7 | 13.5 | 14.0 | 487 | 1739 | 16 |
| linear tag+idx ½ | 2¹⁸ | 18.1 | 12.3 | 12.6 | 435 | 1626 | 8 |
| swiss ⅞ | 2¹⁸ | 27.5 | 3.4 | 4.2 | 833 | 932 | 10 |
| swiss ⅞ | 2²⁰ | 49.7 | 5.1 | 5.0 | 3973 | 4160 | 10 |

At true ¾ load (N = 3/4 of a power of two), linear idx ¾ against ½:
dependent hit 21.2 against 17.5 ns, miss 22.1 against 8.0 ns, at half the
memory (5.3 against 10.7 B per entry) at N = 196 608.

Sorted+memmove holds up to a few thousand entries (108 ns per insert at
4096), as the brief expected, but its lookups are 3 to 7 times slower than
the table's at every N. The tailed hybrids make inserts sublinear,
amortised O(√N), but leave lookups at 70 to 100 ns at 2¹⁸. Swiss has the
fastest misses and inserts, but hits need three dependent loads (control,
slot, palette), which makes it the slowest hash table on hits at large N.
The tag saves 1 to 2 ns per hit and caps the palette at 2²⁴ − 1.

### 4.3 Scan threshold

SSE2 is the x86-64 baseline; AVX2 is shown for comparison. Dependent
hits, ns:

| N | 16 | 24 | 32 | 48 | 64 | 128 | 256 | 512 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| scan, SSE2 (dep hit) | 6.9 | 9.7 | 11.9 | 13.8 | 14.6 | 16.4 | 19.5 | 26.2 |
| scan, SSE2 (miss) | 1.6 | 4.1 | 2.4 | 3.2 | 4.1 | 7.3 | 13.8 | 26.8 |
| table (dep hit) | 5.7 | 5.9 | 8.8 | 6.0 | 7.3 | 7.5 | 7.8 | 8.1 |
| table (miss) | 7.6 | 6.4 | 8.5 | 6.1 | 8.7 | 8.9 | 9.1 | 9.3 |

Most edits hit, since they reuse a state already in the palette. From
about 24 entries the table hit is faster, and by 64 it is twice as fast.
A table of 32 to 64 entries costs 256 B to 1 KiB and builds in 0.1 to
0.2 µs. **Threshold 32** (derived: the largest N at which the scan's hit
latency stays within about 3 ns of the table's). Below it, Bricks with
small palettes (the 1 to 4-bit majority) never build a table. The
prototype Brick's own per-edit numbers at 8 bits agree: scan 43 against
table 34 ns at 32 entries, and 75 against 31 ns at 256 (`bench_brick`;
its scan is scalar over the test's 4-byte struct). Re-measure on the
second reference machine and on ARM (NEON).

### 4.4 Memory at each N (palette + table, linear idx ½)

| N | palette | table (at N = 2^k; up to 2× just above) |
|---:|---:|---:|
| ≤ 32 | ≤ 128 B | none (scan) |
| 256 | 1 KiB | 2 KiB |
| 4096 | 16 KiB | 32 KiB |
| 2¹⁶ | 256 KiB | 512 KiB |
| 2¹⁸ | 1 MiB | 2 MiB |
| 2¹⁹ | 2 MiB | 4 MiB |
| 2²⁰ | 4 MiB | 8 MiB |

With the recommended compaction, P ≤ L₀ + max(4096, L₀/4) ≤ 327 680, so
the table is at most 4 MiB (2²⁰ slots). Without it, P has no bound (section
3.5).

### 4.5 Recommendation
- **Linear probing over 4-byte palette indices, compared through the
  palette, load ≤ ½, power-of-two capacity, Fibonacci hash of the state
  id;** built on the first lookup above the threshold, updated by appends,
  dropped at compaction, freeze or save. This confirms brick.md's design.
- **Scan threshold 32** (derived, this machine).
- Costs, separately:
  - Find: worst O(P), expected O(1) (1.5 probes on a hit, 2.5 on a miss).
  - Insert: worst O(P) (a regrowth: 0.41 ms at 2¹⁸), amortised O(1)
    (6 to 14 ns).
  - Rebuild: O(P) (1.5 ms at 2¹⁸, 0.34 ms at 2¹⁶).
  - Memory: 8 to 16 B per entry.
- Load ≤ ¾ is the option if table memory matters more than about 4 ns per
  hit and 14 ns per miss at large N.
- A refinement, not measured: keep the regrowth out of the edit by letting
  the load reach ¾ inside edits and regrowing at the turn end above ½. The
  edit's worst case then becomes a probe run rather than an O(P) rehash.

## 5. Problems found in the current design

1. **A save does not compact the live Brick of a chunk that stays
   loaded.** brick.md places compaction at saves, "so the blob, and the
   Brick on reload, are compacted for almost nothing". chunk.md has a
   running Vicinity's save take "a frozen copy of its plentiful content"
   while the chunk "keeps working on the live one". Read together, the
   in-memory Brick of a chunk that stays loaded across saves is never
   compacted: its dead entries and forced width survive every autosave.
   Those are the long-lived, heavily edited chunks where they matter
   most. The hand-over should build the compacted representation (the
   same one pass), swap it in as the live one, and give the saver a copy
   or a shared immutable snapshot. That costs T_c (0.25 ms at ≤ 16 bits,
   1.3 ms at 32 bits with 2¹⁸ live) in the owner's turn, once per save.
   With that in place, the trigger in 3.6 only has to cover the gaps
   between saves.
2. **Growth repacks inside the edit's write window.** brick.md says the
   width doubles and the indices repack "inside the edit's write window".
   concurrency.md's representation swap builds the new object first and
   only stores the pointer inside the window. Repacking in the window
   makes it 0.32 to 1.13 ms long (measured growth), and every concurrent
   reader of that chunk retries for that long. Build the grown
   representation outside the window (the owner is the only writer), and
   then apply the edit and the swap inside it.
3. **Palette appends reallocate storage that readers use.** Readers read
   the palette zero-copy under the seqlock. An append that grows the
   palette's buffer frees memory a reader may still hold, and that is a
   use-after-free, which the seqlock does not catch (a large buffer may
   be unmapped). concurrency.md covers "growth that reallocates storage"
   through the swap, but brick.md describes appends as in place. At 32
   bits the palette keeps appending, with about log₂ P reallocations. The
   palette needs chunked storage that never moves (like the state table),
   or each reallocation must go through the swap and the grace period. The
   reverse table is read only by the owner and needs neither.
4. **The reverse table's "about 2 MiB at 2¹⁸ entries and half load"** holds
   only at exactly 2¹⁸ entries. Power-of-two capacity at load ≤ ½ gives 8
   to 16 B per entry: 4 MiB just above 2¹⁸, and up to 8 MiB at 2²⁰, which
   a 32-bit palette can reach between saves without a trigger (measured:
   P ≈ 398 000, Brick total 7 MiB). The full-width cost should count the
   table: 2 MiB of indices and palette plus 2 to 4 MiB of table.
5. **The 256-entry scan threshold is too high** on this machine (4.3).
6. **The prototype's digest predates main.** `digest.hpp` still defines W
   over the layout index, not main's W(x, y, z) = 1 + (mix64(x | y≪6 |
   z≪12) mod (p − 1)) (commit 7ae1e41). This work did not change it, and
   the tests do not depend on the difference.
