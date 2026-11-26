# Lighting

Lighting's role in the game. Divergence from Minecraft, which overloads per-block
flood-filled light with a gameplay role (spawn suppression).

## light/visibility-only — Lighting is presentation-only; no gameplay role

- Decision: Lighting exists **solely so the player can see**. It has NO gameplay
  role — it does not gate mob spawning, reality stabilization, or any other rule.
  Minecraft's three overloaded roles for light (visibility, spawn suppression,
  implied "controlled area") are split apart: visibility stays here, spawn
  suppression moves to `mobs/spawning`, claiming/marking territory moves to
  `persist/diegetic-persistence` (stabilization field).
- Rationales:
  - Light as a gameplay element interacts poorly with cross-platform rendering;
    gameplay elements should match across platforms, conflicting with heterogeneous
    rendering across platforms with different capabilities.
  - Per-block flood-filled light can propagate with a perceptible,
    immersion-breaking delay when small changes trigger large updates. We expect
    light to respond instantaneously but have no such expectations of artificial
    constructs introduced for gameplay reasons.
- Consequences:
  - Lighting is **fully derived / presentation-layer** state
    (`core/authority-derived-split`): never authoritative, never ledgered,
    freely recomputable, may differ by platform/quality setting.
  - **Dynamic/moving light sources and per-platform fidelity scaling are free** —
    nothing gameplay-authoritative reads light, so visual differences cause no
    gameplay divergence. (Same lesson as `coords/f64-authoritative` and
    `sim/gpu-speculative-compute`: keep gameplay-authoritative state off the
    fidelity-variable, platform-dependent layer.)
  - **Removes light from cross-chunk correctness obligations** (meshing
    reads no other chunk in the first pass, `world/chunk-meshing`; cross-chunk
    simulation in `sim/concurrency-model`; cross-chunk light bleed need only be
    visually acceptable, not gameplay-correct, so it can be approximate/deferred.
  - How light is actually rendered (deferred shading, many dynamic lights, no
    CPU flood-fill field) lives in `render/deferred-pipeline`.

## Governing principle (applies here and to the stabilization field)

Do NOT overload one spatial field with both a presentation role and a
gameplay-authoritative role — that is the Minecraft-light mistake. This is
`core/authority-derived-split` applied to fields:
- **Presentation-only fields** (lighting) may vary by platform/fidelity and live
  on the render layer.
- **Gameplay-authoritative fields** (the stabilization field,
  `persist/diegetic-persistence`) must be deterministic authoritative state and
  must NOT be moved onto the fidelity-variable render/GPU layer — even though the
  two can share the same flood-fill/field *shape*.
