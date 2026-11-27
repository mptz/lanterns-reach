# Prototypes: Brick (and an obsolete GoalSource)

Standalone, pure-logic C++20 prototypes written to find holes in the specs
before the real build. **The specs are authoritative:** `design/brick.md`
(`world/brick`), the v0.2.2 detailed spec in `versions.md`, and
`data/content-digest` in `design/data.md`. Where this code differs, the
spec wins. The research behind the current Brick rules, with its
measurements, is `prototypes/brick-research.md` in this directory.

## Build and test
Clang, `-std=c++20 -Wall -Wextra -Werror -ffp-contract=off -Wthread-safety`:

```
cmake -S prototypes -B <dir> -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build <dir> && ctest --test-dir <dir>
<dir>/test_equivalence --long                          # the longer run
<dir>/bench_brick; <dir>/bench_lookup; <dir>/bench_compaction
```

## Worth reusing in v0.2.2
- **The randomised equivalence test** (`brick/test_equivalence.cpp`) and its
  mutation checks: copies at every width (two of them compacting between
  edits), random edits across width boundaries, compared against an
  oracle by probes, every cell, raycasts, the digest and the per-cell face
  list.
- **The digest** (`brick/digest.hpp`): exact arithmetic mod 2⁶¹ − 1 and W as
  `data/content-digest` defines it, on (x, y, z), nonzero by construction,
  with a test over all 2¹⁸ cells.
- **The reverse-lookup candidates and their tests and benchmarks**
  (`brick/reverse_lookup.hpp`, `test_reverse_lookup.cpp`, `bench_lookup.cpp`).
- **The benchmarks** (`bench_brick.cpp`, `bench_compaction.cpp`), for v0.2.2's
  measurements.

## Not to reuse as is: `brick/brick.hpp` differs from the spec
- **The reverse table lives in the Brick's palette;** the spec puts it in
  the Brick's owner, sized to the palette's capacity.
- **Growth repacks in place** (single-threaded); the spec never resizes a
  Brick in place: the owner builds a new Brick at the next width or
  capacity and swaps it in. Compaction here already rebuilds and swaps.
- **No high-water or low-water marks:** widening happens only when the
  palette is full, and a discretionary compaction keeps the width unless
  the narrower one leaves room for F more entries (the research's rule);
  the spec widens proactively at 15/16 of capacity (with a fallback in
  the edit) and compacts to the narrowest width under 7/8 (reserving room
  for F more entries when the compaction is discretionary).
- **Palette entries are 4-byte stand-ins** for state ids, with a stand-in
  registry; there is no state table.

## GoalSource (`goal/`)
Obsolete: it prototypes the superseded key-based design. The current
design is `engine/goal-source` (`design/goal-source.md`), modelled in
`model/GoalSource.tla`.
