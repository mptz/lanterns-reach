# Scenarios

A scenario is one JSON file that fully describes a run (v0.2.0): the world
and its scene, the camera, the window, what is shown, render and tool
settings, a timed input script, how long to run, and checks.

```
build-debug/sandbox --scenario scenarios/v0.1-scene.json        # interactive, in that state
build-debug/sandbox --scenario scenarios/headless/lit.json --capture lit.png
build-debug/sandbox --scenario scenarios/perf-1080p.json --gpu-report
build-debug/sandbox --scenario scenarios/v0.1-scene.json --check-absorbance
```

Without `--scenario`, `scenarios/v0.1-scene.json` runs. The remaining flags
are `--scenario`, `--capture`, `--gpu-report`, `--check-absorbance`,
`--blocks` and `--check-blocks`; every older flag is rejected with the name
of the field below that replaced it.

**Unknown fields are errors**, at every level, with the field's path in the
message (`events[3].key: unknown key "Q1"`), as in block definitions.

## Files
- `v0.1-scene.json`: the v0.1 test world (was `src/scene.cpp`), the start
  view, and the `--check-absorbance` cases.
- `headless/*.json`: one per `tools/headless_check.sh` shot; each includes
  the v0.1 scene and adds a camera, a view and events, except
  `glass_corners` (glass shapes with inner corners) and `cutout` (the grate
  in front of stone and glass), which build small scenes of their own
  (v0.2.5). The comparison sets
  (`order_*`, `ghost_order_*`, `variant_run`, `quad_*`) are run in pairs by
  the script.
- `one-frame.json`: runs one frame (the missing-validation-layer test).
- `perf-1080p.json`: the v0.1 scene at 1920×1080 for 1,500 frames, for
  `--gpu-report` (the v0.1.9 baseline's setup).

## Fields
All optional. Coordinates are world cells or positions (z up).

| Field | Meaning | Default |
|---|---|---|
| `include` | a scenario (path relative to this file) whose fields are the defaults; see below | — |
| `world.generator` | how the world's chunk is filled: `empty` (air; `scene` is applied on top). The world is one 64³ chunk, cells 0–63. `world.grid` (v0.1's dense grid) is rejected | `empty` |
| `scene` | a list of operations, applied in order as one edit set: `{"fill": type, "from": [x,y,z], "to": [x,y,z]}` (inclusive box) or `{"place": type, "at": [x,y,z], "rotation": r}` (`r` the rotation index 0–23, canonicalized; default 0). Type `"air"` clears. A missing block type is skipped with one warning | empty |
| `camera.pos`, `camera.look` | eye position and look-at point | (10, 10, 19), (16, 16, 15) |
| `window.size` | `[width, height]` at start | [1280, 720] |
| `window.resize` | `{"frame": n, "size": [w, h]}`: resize at frame n | — |
| `view.buffer` | the F2 view: `lit`, `albedo`, `normal`, `depth`, `roughmetal`, `material`, `variant` | `lit` |
| `view.overlays` | on at start: `stats` (F1), `wireframe` (F3), `orientation` (F4), `lighting` (F5), `targeting` (F6), `input` (F7), `editSet` (F8), `bounds` (F10) | none |
| `view.preview` | preview mode (Q) | false |
| `render.anisotropy` | cap on the block sampler's anisotropy (1 = off) | the device's, ≤ 16 |
| `render.variants` | random/matched tiles (false: tile 0, for timing) | true |
| `render.glassFrames` | connected-glass frames | true |
| `render.momentBias` | MBOIT moment bias | 5e-4 |
| `render.faceOrder` | translucent face order: `forward`, `reverse`, `shuffle:<seed>` | as built |
| `render.oitView` | F9: 0 off, 1 absorbance, 2 moments b1 b2, 3 moments b3 b4 | 0 |
| `render.mesher` | how chunks are meshed: `greedy` (bitmask mesher, coplanar faces of equal appearance merged), `unmerged` (the same path, one quad per face), `per-cell` (the reference rule). `render.mergedQuad` (v0.1.8's harness) is rejected | `greedy` |
| `tools.patch`, `tools.consume` | the held container's patch size N (1–9) and the survival consumption rule | 1, false |
| `events` | the timed input script (below) | none |
| `frames` | run this many frames, then exit | see below |
| `checks.absorbance` | `--check-absorbance` cases: `{"name", "eye", "look", "cubes": [{"at", "type"}]}`, the translucent cells the centre pixel's ray crosses | none |

**Events** go through the intention layer like real input. Each has a
`frame` and one of:
- `"key": k`: press at `frame`, release at `frame + 1`; `k` is a–z, 0–9 or
  F1–F12;
- `"hold": k, "until": m`: press at `frame`, release at `m`;
- `"click": b`: `left`, `right` or `middle`, pressed for one frame;
- `"wheel": d`: `up` or `down`, one step.

Any event may add `"shift": true`.

**Run length:** `frames` if given. Otherwise, with `--capture`, 20 frames past
the last press (60 past a resize, so the new size settles), at least 60;
without `--capture`, the run is interactive and ends when the window closes.

**`include`:** the included scenario is read first; this file's fields then
override it. `scene` appends (a shot can add blocks to the scene it
includes); `camera`, `window`, `view`, `render` and `tools` override field by
field; `events`, `frames`, `checks` and `world` replace. Includes nest (up to
8 deep).

**`--check-absorbance` pairs:** the first four cases are two pairs checked
together: 3 blocks against 1 (transmittance ratio exp(−2σ), σ of case 0's
glass), and glass on stone against glass over air (equal).
