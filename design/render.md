# Rendering

Rendering architecture: pipeline shape, G-buffer, transparency, anti-aliasing.
Everything here is presentation-layer (`light/visibility-only`,
`core/authority-derived-split`): nothing gameplay-authoritative reads it, so all
of it may vary by platform/quality setting and is reversible behind the
render-graph seam (`engine/vulkan-abstraction`) — a wrong call costs a renderer
rewrite, never a sim change.

Target hardware: **desktop GPUs only.** Low-spec targets (e.g. Nintendo Switch,
mobile tilers) are explicitly out of scope, so the design spends memory and
bandwidth where it buys material richness and image quality.

## render/deferred-pipeline — Deferred shading for opaque; fixed pass structure

- Decision: **opaque geometry is deferred-shaded.** A geometry pass writes a
  G-buffer (`render/gbuffer-layout`); a lighting pass shades from it into an HDR
  target. Translucency is forward-rendered after lighting (`render/transparency`).
  The frame has a **fixed pass structure** independent of scene content:
  1. **G-buffer pass** — opaque + alpha-tested (cutout) geometry. Emission is
     written directly into the HDR light target here.
  2. *(later)* screen-space passes reading the G-buffer — SSAO, SSR.
  3. **Lighting pass** — ambient + directional + many dynamic point lights,
     accumulated into the HDR target.
  4. **Translucency** — MBOIT, two passes over translucent geometry only.
  5. **Water surface** — single-layer forward pass (tint/absorption + SSR).
  6. **Resolve** — TAA (`render/anti-aliasing`), then tonemap → swapchain.
- Rationale:
  - **Many small dynamic lights are the expected case.** `pressure/light` puts
    consumable torches, carried lights, glowing materials and lava everywhere,
    and `light/visibility-only` makes dynamic/moving lights free in gameplay
    terms. Deferred lighting cost scales with lit pixels per light, not
    geometry × lights.
  - **No gameplay light field is required.** Minecraft needs one because its
    spawn rules read light; ours don't, so lighting can be fully
    presentation-side. A *derived* propagated light volume for broad emitters
    is still allowed as a render optimization (`render/shadows`, deferred).
  - **Screen-space effects come cheap off the G-buffer** — SSAO (which voxel /
    density-field terrain benefits from heavily), SSR for Aether's
    water, decals.
  - **It is exactly the multi-pass shape the render-graph seam exists for**
    (`engine/vulkan-abstraction`): passes declare reads/writes, the graph emits
    barriers/layout transitions.
- Consequences:
  - MSAA is effectively ruled out (it multiplies every G-buffer target); AA is
    temporal (`render/anti-aliasing`).
  - Bandwidth is a real cost; keep the G-buffer thin and move per-material
    constants to a material table (`render/gbuffer-layout`).
  - Translucent surfaces are forward-lit, so a **clustered/tiled light list** is
    needed for them; deferred lighting may share it.
- Alternatives considered:
  - **Clustered forward (Forward+)** — also scales to many lights and keeps MSAA,
    but loses the cheap screen-space effects and still wants a depth prepass.
    Rejected as primary.
  - **Visibility buffer** (store triangle/instance ID, shade later) — lower
    bandwidth on dense geometry and fits bindless/descriptor indexing, but more
    complex. Retained as a possible **later upgrade behind the same seam**, not
    the starting point.
- Open: clustered light-list structure (tile vs 3D cluster, light budget per
  cluster); SSAO technique. Light classes and shadows: `render/shadows`.

## render/gpu-driven — Resident pooled meshes, GPU culling, indirect draws; the CPU's per-frame cost is O(changes)

- Decision: every terrain mesh (chunk meshes near the player, distant
  boxes' surfaces and ray-march proxies further out, `world/octave-pyramid`)
  stays resident in a few large pooled device-local buffers. Each frame a compute shader culls them (frustum,
  distance and LoD, later occlusion) and writes an indirect draw list, and
  one indirect-count draw per pass draws everything. The CPU's per-frame cost
  is **O(changes), not O(chunks):** it uploads new meshes and retires old
  ones (the frame thread picks up finished meshes from the descriptor vector, `sim/concurrency-model`),
  updates moving entities' transforms (read lock-free from published
  state), and records a near-constant number of passes.
- **Deferred shading is not in tension with this:** deferred concerns when
  lighting is computed; the GPU sees only
  derived render data, never physics (`core/authority-derived-split`).
- **No dedicated pre-render thread to consolidate draws**:
  GPU-driven drawing leaves it little to do. A game-thread / render-thread
  split (pipelining one frame) is an option only if profiling shows the main
  thread is the limit, since it adds a frame of input latency against the
  jump path. Parallel command recording on workers isn't needed while the
  pass count stays near-constant.
- The likely limits at higher frame rates are GPU fill and shading
  (deferred lighting, translucency, shadows) and upload bandwidth during
  fast travel; measure those.

## render/gbuffer-layout — Thin per-pixel G-buffer + per-material table
- **Open:** exact formats, to tune.

- Principle: **store per pixel only what varies within a material; look up
  per-material constants by material ID** in a bindless material table
  (descriptor indexing, `engine/vulkan-abstraction`). This is how user-authored
  blocks (`data/authored-content`) get a rich property set without growing the
  G-buffer: new properties extend the table, not the render targets.
- Material model: **glTF 2.0 metallic-roughness PBR** for the per-pixel set, so
  user-authored blocks come straight out of standard tools (Blender, Substance)
  with no custom pipeline.
- Layout:

  | Target | Format | Contents |
  |---|---|---|
  | Depth | D32F | depth; world position reconstructed from it (never stored) |
  | GB0 | RGBA8 sRGB | base color RGB + material AO |
  | GB1 | RG16 unorm | normal, octahedral-encoded (16 bits/axis keeps normal-map detail) |
  | GB2 | RGBA8 | roughness, metallic, reflectance/specular, shading-model ID bits |
  | GB3 | R16 uint | material ID → material table |
  | Velocity | RG16F | motion vectors — written ONLY by moving entities (`render/anti-aliasing`) |
  | HDR light | RGBA16F | not G-buffer proper: written by the lighting pass, emission included (below) |

  ≈ 18 bytes/pixel of G-buffer (≈ 66 MB at 1440p, ≈ 150 MB at 4K) — comfortable
  on desktop GPUs.
- **Emission is computed, not stored** (v0.1.9 `attic/REVIEW-v0.1.md` D2):
  the lighting pass adds the material's emissive factor
  × the pixel's albedo, looked up by material ID, and writes HDR in one go.
  Emission still varies per texel (it follows the texture), but within one
  material which texels glow is tied to their colour: a texel can't glow
  independently of how bright it is. The G-buffer pass has four colour
  targets (Vulkan's guaranteed minimum) and about 16 bytes/pixel less
  traffic. Engines with arbitrary emissive content (Unreal, HDRP, Frostbite)
  store emission per pixel instead; voxel shader packs (LabPBR) store a
  per-texel strength.
  - **Why it's enough here:** lights in a block game are mostly
    special-purpose blocks (lamps, glow blocks) or blocks with a dedicated
    light feature (glowing orange veins), authored with the rendering rule
    in mind, so the texture can be painted to suit it.
  - **When it reopens** (a block needs glow chosen separately from colour,
    e.g. neon on a dark wall, runes in stone), in order of cost:
    1. **A per-material brightness threshold:** emission × smoothstep over
       the albedo's luminance, one number in the material table, no
       per-pixel data. Texels brighter than the threshold glow. Limits: a
       dark texel can't glow, a bright texel that shouldn't will, and at a
       distance mip-filtered albedo can fall below the threshold, so fine
       glowing detail fades. Texture authoring keeps glowing and
       non-glowing texels on opposite sides of it.
    2. **A one-byte per-texel emission mask** (GB0's alpha or a spare GB2
       channel): glow independent of colour, in one hue per material.
    3. **Per-pixel emissive RGB:** a target again, or a shading-model bit
       for the materials that need it.
- **Shading-model ID** bits select a lighting path for special cases (two-sided
  foliage transmission, subsurface-ish ice, clear-coat), with their extra
  parameters in the material table. Exotic materials add a path + table fields,
  never a render target.

## render/transparency — Commutative (order-independent) transparency; MBOIT target

- Decision: **no per-triangle sorting and no depth peeling.** Translucency is
  handled by a taxonomy, with **Moment-Based OIT (MBOIT)** as the general
  mechanism. The taxonomy matches the cell visibility classes of
  `world/chunk-meshing` (opaque / transparent / cutout / translucent-with-group);
  keep the two aligned:
  1. **Cutout first.** Leaves, grates, fences, grass are alpha-tested, written
     into the G-buffer, and are opaque for all purposes — most voxel-game
     "transparency" lives here. Near-opaque layers are also treated as cutout.
  2. **General translucency → MBOIT** (Münstermann et al., 2018): two passes over
     translucent geometry only, both commutative.
  3. **Water surface** — usually a single big layer: its own forward pass after
     lighting (tint/absorption + SSR); no sort needed because there is one layer.
  4. **Coarse sorted fallback** — per-chunk/per-object (never per-triangle) for a
     rare case MBOIT renders unacceptably; exact A-buffer only if a
     correctness-critical case ever appears.
- **Refraction is NOT supported** (accepted limitation). Translucent
  media — including water — tint/absorb (and water reflects), but never bend the
  view. Revisit only on a concrete need; it would be a separate screen-space
  effect, not part of OIT.
- Rationale — why commutativity is achievable, and what it takes:
  - **Pure absorption commutes exactly:** `T = Π Tᵢ = exp(−Σ σᵢ·dᵢ)`; in log
    space absorbance is additive. **Pure emission commutes exactly** (addition).
    The non-commutative part is their combination — emission attenuated by
    layers in front — i.e. the "over" operator.
  - **Lifting the color state gives associativity, not commutativity.** One layer
    acts as the affine map `x' = Tᵢ·x + Eᵢ`; lifting to homogeneous form
    `[[Tᵢ, Eᵢ],[0,1]]` makes compositing a matrix product (that is premultiplied
    "over") — associative, so sub-stacks can merge in any grouping, but not
    commutative, exactly as translation and scale don't commute. The order
    dependence is real physics; no finite lift of color removes it.
  - **Commutativity comes from carrying depth, the variable order stands in for:**
    `C = Σᵢ Eᵢ · T(zᵢ)`, with `T(z) = exp(−A(z))`, `A(z) = Σⱼ σⱼ·[zⱼ < z]`.
    Represent `A(z)` compactly by **power moments** `Σ σᵢ·zᵢᵏ` (k = 0..3, or ..7)
    — sums, hence order-free. Pass 1 accumulates the moments additively; pass 2
    reconstructs `T(zᵢ)` per fragment and adds `Eᵢ·T(zᵢ)`. Exact in the limit of
    infinite moments; with 4–8 moments a smooth approximation that fails
    gracefully.
  - **Weighted Blended OIT (McGuire & Bavoil 2013) is the degenerate, heuristic
    member of the same family:** its revealage target is the zeroth moment alone
    (total transmittance `Π(1−αᵢ)`), and it replaces the reconstructed `T(zᵢ)`
    with a hand-tuned depth weight `w(z, α)` — which is what lets it be one pass,
    and why it smears close high-alpha layers. MBOIT keeps WBOIT's commutative
    accumulate/composite structure and replaces the heuristic with an estimate
    derived from the moments.
- **Volumetric absorbance from boundaries (adopted for v0.1.6).** σ is
  absorption per unit path length of a
  *volume*, but the rasterizer only sees face fragments. Per-face constants
  get three ordinary cases wrong: a merged same-group run (1 or 50 blocks of
  glass, or deep vs shallow water, would absorb the same), glass against an
  opaque cell (its hidden face is missing), and oblique views (longer path,
  same face count). Instead, with σ piecewise constant per cell:
  - **Each translucent cell emits its own faces, wound outward,** wherever its
    neighbor isn't the same group (air, opaque, another group). A fragment at
    eye-relative depth z adds **−σ·z front-facing** (entering) and **+σ·z
    back-facing** (leaving). The sum over a pixel is exactly ∫σ dl along its
    ray — Beer–Lambert at any angle and thickness — and is a sum, so it
    commutes.
  - **Same-group culling becomes exact**, not a fudge: a face between
    same-group cells would contribute (σ − σ)·z = 0. Where a ray passes from
    group a into group b, the two coplanar faces sum to (σ_a − σ_b)·z: a's
    face is back-facing (+σ_a·z, leaving a), b's is front-facing (−σ_b·z,
    entering b).
  - **Occlusion by clamping, not depth testing:** z = min(z_frag, z_opaque),
    so an exit behind a wall ends the path at the wall and volumes entirely
    behind it cancel. A camera inside a medium needs no special case (the
    exit term alone is the path from the eye).
  - **Moments by antiderivative:** moment k accumulates ±σ·Fₖ(z), Fₖ the
    closed-form antiderivative of w(z)ᵏ, so the totals are moments of the
    continuous, non-negative density — what reconstruction assumes.
    **Verified in the v0.1.6 build** against the paper and the authors'
    shader code: the paper's absorbance is the CDF of a finite non-negative
    measure, and nothing in the moment problem requires Diracs, so the
    boundary moments of σ·dl are valid input.
  - **Requires** closed boundaries (translucent faces against opaque cells are
    emitted, for absorbance only), `cullMode = NONE` in the absorbance pass,
    and watertight rasterization (Vulkan's tie rule guarantees each crossing
    counts once). fp32 cancellation for thin, distant layers is limited by
    measuring depth from the translucent near bound.
  - **Depth is eye distance along the ray, not view-space depth** (v0.1.6
    build). Only ray distance makes Σ ±σ·r equal σ × path length; view depth
    undercounts by cos θ toward the screen edges. The log warp is applied to
    ray distance; any monotone coordinate works if accumulation and queries
    agree. The opaque clamp uses the same ray: r_opaque = r ·
    z_lin(opaque) / z_lin(fragment).
  - **Colored absorption (not in the paper):** total absorbance is stored
    per channel (exact Beer–Lambert); **one** moment set from the mean σ
    gives the depth shape of transmittance, scaled per channel. Exact for a
    single tint; approximate where differently colored media mix along a
    ray.
  - **Moment bias is raised to ~5e-4** (from the paper's single-precision
    value). Boundary moments are sums of large cancelling ±σ·Fₖ terms, so
    they carry more rounding than direct sums, and with the small bias the
    reconstruction flipped with summation order (4/255 between face
    orders). 5e-4 gives identical captures; the image cost is ≤ 5/255 in
    under 1% of pixels. Tunable.
  - **Glass needs a surface, not only a volume.** Volume + point-light
    specular alone reads as "vague solidified smoke" (owner, v0.1.6). Glass
    is recognized by its surface: frames, edges, Fresnel reflection. v0.1.6
    adds **connected-glass frames**: absorbing faces carry a neutral,
    untinted procedural band along a same-type region's outline and creases
    only (mesher-computed edge bits), contributing a per-surface absorbance
    −ln(1 − coverage) at the face's depth (the paper's original term, so
    still commutative) plus its lit neutral color. The tint stays in the
    volume. Still missing: an environment to reflect (IBL/SSR), which is
    the rest of what makes glass read as glass.
  - **Clear-glass fast path (deferred with the other optimizations).** Clear glass is far more common than tinted, and glass
    (not water) is where many layers stack. With σ = 0 a face contributes
    nothing to absorbance, so clear glass **skips the absorbance pass
    entirely** (the expensive one): it is drawn only in the shading pass,
    adding its specular/reflection, attenuated by the moments of any tinted
    media in front (still correct). Its frames become cutout (opaque,
    alpha-tested, G-buffer) rather than absorbing surfaces. Ignored: the few
    percent of Fresnel dimming a clear pane applies to what's behind it
    (could be added to the zeroth moment alone if ever visible).
  - **Liquids:** water fits the general path well — one group
    per fluid, depth and camera-underwater handled by the boundary integral,
    partial-height cells fine (only a closed surface is needed); it still
    needs surface reflection and animation. Other absorbing liquids (murky
    water, mud, oil, honey, potions) are just σ values. Lava is effectively
    opaque within a block, so it is opaque + emission (`render/shadows`), not
    MBOIT. **Volumetric emission in an absorbing medium** (magma under a thin
    crust, glowing slime) is NOT free: emitted light is dimmed by the medium
    in front of each point, so it needs ∫ e·T, not boundary terms alone;
    open.
  - **Open:** whether this lets water ride the general path rather than its
    own single-layer pass (item 3 above); volumetric emission (see Liquids); lighting the far-side frame
    of a pane (back faces aren't shaded, so it reads as a darker line).
- **Tint strength**: one or two layers of tinted glass
  must read as clearly coloured. Physical reference (from memory,
  approximate): real coloured glass is strongly absorbing. Stained ("pot
  metal") glass a few millimetres thick passes roughly 70–90 % of its own
  colour and only a few to ~20 % of the others, so a block of truly
  coloured glass (about a foot, `world/voxel-scale`) would be nearly black. Following `core/realistic-in-kind`, a
  block is tinted **like a sheet of stained glass**, not like a foot of solid
  coloured glass: σ is set from a target transmittance per block (about 0.85
  passed, 0.10–0.15 absorbed), giving σ ≈ 0.15 and ≈ 2. Two layers are then
  deeply coloured and a few are nearly opaque in the absorbed colours. (v0.1.8)
- Known drawbacks (accepted as manageable):
  - **Translucent geometry is drawn twice** (absorbance pass + shading pass).
    The absorbance pass can't depth-reject: a fragment behind a wall still
    contributes its clamped term, which cancels only with its pair. On the
    HD 530 (v0.1.6 test scene) absorbance ≈ 1 ms, shading ≈ 0.7 ms.
    **Overdraw stress:** camera against four touching
    tints, screen-filling, at 60 fps (vsync-capped): absorbance ≈ 5 ms,
    shading ≈ 4 ms — ~9 ms of a 16.7 ms frame. That's ~8 absorbance and ~4
    shaded fragments per pixel; cost scales roughly with overdraw. Too much
    budget on the reference GPU once shadows and real lighting arrive.
    Candidate mitigations, in order of payoff: **half-resolution
    translucency** with depth-aware upsampling (~4× fewer fragments; standard
    for OIT, cheap given smooth tints); the **clustered light list** (shading
    loops over a pixel's few lights, not all 16); hardware early-depth for
    the shading pass (helps only glass behind walls); fp16 moments with the
    paper's quantization (halves bandwidth, but risky given boundary-term
    cancellation). **Deferred:** no optimization until
    the frame actually runs out of budget on something closer to the real
    system; then pick and measure.
  - **Precision:** power moments are ill-conditioned — 32-bit float storage, or
    the paper's quantization scheme for 16-bit. ~16–20 B/pixel extra at 4
    moments (~2× at 8), transient during the translucency pass only.
  - **Per-fragment reconstruction** solves a small linear system — ALU-heavy but
    trivial on desktop GPUs.
  - **Light leaking / over-darkening** where transmittance steps sharply (a
    near-opaque pane close in front of an emitter); tuned via the paper's bias
    parameter, and mitigated by routing near-opaque layers to cutout.
  - **Depth warp needed** (log-style) so moment precision concentrates near the
    camera; requires a sensible near/far range for translucent geometry.
  - **Forward-lit:** pass 2 needs the clustered light list
    (`render/deferred-pipeline`).
  - **TAA interaction:** translucent pixels have no single correct motion vector
    and smear under TAA → write a **reactive mask** that down-weights history on
    translucent pixels (`render/anti-aliasing`).

## render/shadows — Light classes; cascaded sun shadows; sky/sun exposure from worldgen (working direction)

- Decision — **four light classes**, chosen per block definition (a
  `light_type` field, as Bedrock separates `static_light` / `point_light`):

  | Class | For | Mechanism |
  |---|---|---|
  | Emissive | every glowing block | self-glow + bloom; lights nothing |
  | Adjacent glow | broad emitters (lava, glowing fungi) | baked per vertex at meshing, reach of one block; leak-free |
  | Shadowed point light | small key emitters (torch, lantern, `pressure/hearth`'s lantern) | real light with shadows, long range |
  | Propagated volume | broad emitters lighting a whole cave | derived per-chunk light volume; **deferred** |

  - **Short unshadowed lights are leak-free by construction:** from a block
    center, the nearest surface behind a 1-block wall is 1.5 cells away, so a
    radius < 1.5 never crosses a wall (generally: radius < 0.5 + thinnest
    wall). A reach that short only touches a cell's neighbours, which is
    what the mesher already sees, so it is **baked into vertices** like
    per-vertex AO: no lights in the light list, chunk-local (no halos in
    the first pass, `world/chunk-meshing`, so it may seam at borders),
    cheap for a lava lake. Trade-off: per-vertex, soft, no specular.
  - **Key emitters reach well beyond 7** (the v0.1.4 harness radius, 7 units at
    our scale); plan for long-range shadowed lights.
  - **Propagated volume, if built:** flood-fill light through air a bounded
    distance, per cell, computed in the same turn that rebuilds the chunk mesh
    so it appears in the edit's frame (no visible propagation within a chunk;
    at borders, only once meshing reads neighbours, if it ever does). Presentation-only
    (`light/visibility-only`).
- Decision — **sun and moon: cascaded orthographic shadow maps**, soft or
  **blocky** (shadow lookup snapped to texture-pixel resolution, as Vibrant
  Visuals offers). Rendered by the GPU as depth-only passes of the existing
  chunk meshes through the render graph; cutout blocks need a discard
  shader.
  - **One directional caster at a time:** the dominant of sun/moon.
  - **Contain far-cascade cost** (our voxels are ~27× denser than
    Minecraft's): far cascades draw LOD meshes, are cached and refreshed in
    rotation (near cascade every frame), and cull chunks per cascade. Edits
    invalidate only the cached shadow regions their chunk touches.
  - Translucent blocks don't tint sun shadows at first (a colored
    transmittance map is a later option).
- Decision (direction) — **sun and sky exposure come from worldgen, not from
  loaded chunks.** Cascades only see geometry inside their boxes; a huge cave
  whose ceiling is out of render distance must still be dark. Layered,
  coarsest first:
  1. **Dimension profile:** does it have a sun/moon, a lit sky, neither, or
     something else (`worldgen/dimension-profiles`)? No sky → skip the rest.
  2. **Baseline exposure from the generator:** worldgen is a deterministic
     function of seed and position, so it answers "is this buried?" for
     unloaded or ungenerated terrain without reading any chunk (respects
     `core/opaque-chunk-queries`). **Octave-bounded evaluation:** summing only
     the low octaves gives an *interval* (skipped octaves' total amplitude
     bounds the error), classifying a sample as definitely solid, definitely
     air, or uncertain — refine only the uncertain ones. Deep rock resolves
     from one or two octaves; the answer is conservative by construction.
  3. **Corrections where chunks differ from baseline:** edited (persisted)
     chunks publish a small exposure summary that overrides the generator;
     unedited chunks need none.
  4. **Cascades** for near shadows once 1–3 say the sun is reachable.
  - Sky visibility (soft light from the whole dome) is a separate term over
    the same layers; per-vertex AO from meshing covers it to start. Also
    useful for "outdoors" ambience and audio (presentation-only).
- Decision (direction) — **a fixed shadow budget, not
  dirty tracking.** Shadow maps get a finite, fixed amount of GPU work per
  frame, spent by a scheduler; nothing tracks which shadows an edit or a
  moving light invalidated.
  - **The unit is a face:** each face of a point light's cube map is
    scheduled independently. A **handheld light** gets a seventh face, a
    perspective map aligned with the holder's field of view and refreshed
    at high frequency (a light near the eye casts
    shadows mostly hidden from it, and the occluders that matter lie
    roughly inside the view frustum; held slightly offset, its shadows
    show a little). Its other six faces join the scheduled pool.
  - **Everything slower shares the budget:** static point lights, the
    sun's and moon's far cascades (the near cascade stays every frame),
    and other players' and mobs' held lights.
  - **A cheap priority function, not strict round robin**: biased
    by proximity, activity and motion, with staleness growing until a face
    is refreshed, so nothing starves. Lights that can't contribute (out of
    range, or sealed off according to the chunks' face connectivity) don't
    compete.
  - **Edit nudges are deferred**: build the scheduler first and
    see whether edits near a light (placing a wall in front of a torch)
    need a priority boost; if so, it is a hint only (`core/level-triggered`).
  - Accepted cost: staleness grows with the number of lights; usually
    invisible, most visible right after an edit near a light.
  - Still open within it: the budget (faces per frame) on the HD 530, and
    the priority function's exact terms.
- Proposed, not decided: **bake static lights'
  diffuse contribution** into per-chunk light data (per vertex or per face)
  through the same budget, using the cached shadow maps for occlusion, so
  hundreds of static lights cost almost nothing per frame and only moving
  lights, the nearest lights' specular, and lighting on moving entities stay
  real-time (the mixed-lighting model of commercial engines); and cache
  static lights' cluster assignment per chunk, clustering only dynamic
  lights per frame.
- Open:
  - **Point-light shadow technique:** cube shadow maps, refreshed by the
    budget above, vs ray marching through a per-chunk occupancy bitmask (no
    per-light passes; cost grows with range). Long range weakens ray
    marching's main advantage; measure both.
  - **Budgets** (rules of thumb, unmeasured): ~100–500 unshadowed lights,
    ~8–16 overlapping per pixel; ~4–8 shadowed lights updated per frame.
    Verify with a lighting stress scene on the HD 530.
  - **Exposure details:** what granularity the generator walk runs at (per
    chunk or coarse region, cached, updated as the sun moves); the shape and
    coarseness of edit summaries; bounding every density contributor (cave
    fields included) and nonlinear combinations (interval arithmetic widens);
    measured per-octave bounds with margin; whether low octaves are a good
    occlusion proxy in non-heightmap dimensions such as Enigma.
  - Tinted light through glass (ray-marched shadows could sum absorbance).
- Rationale: Bedrock's Vibrant Visuals (Mojang's creator docs) uses the same
  shape — one shadowed directional light, a short list of shadowed point
  lights for small emitters, baked light for everything else — and keeps the
  classic flood-filled renderer for low-end devices. Our difference: no
  light value feeds gameplay, so every class is free to vary by platform.

## render/block-textures — 16×16 pixel-art faces, sharp sampling, random variants by position (edge matching only where features cross edges), larger patterns by orientation

- **Block face textures are real 16×16 sources** (a style rule the block
  validator checks), not larger textures that only look pixelated: upscaled
  copies sampled with nearest filtering look identical at 16× the memory.
  At our 1-ft blocks (`world/voxel-scale`) a texel is ¾", about three times
  finer per unit of world than Minecraft. PBR maps (roughness, metallic,
  normal) stay at 16×16 too, so lighting adds no detail the colors don't have;
  blocky shadows (`render/shadows`) then snap to 16 texels per block.
- **Sampling:** **anti-aliased nearest** up close (each texel a flat color,
  edges between texels blended over about one screen pixel using `fwidth`,
  so pixels stay crisp without crawling), plus **mipmaps** and anisotropic
  filtering at a distance (TAA, `render/anti-aliasing`, helps further). The 1×1
  mip is the block's average color, which is what a coarse-LOD chunk should
  show (`world/progressive-lod`).
- **Face variation by position: three principles** (edge-matched sets
  alone gave a pattern repeating every block in v0.1.8's first build):
  1. **Variant selection breaks up repetition; edge matching keeps coherent
     features continuous.** Picking one of several variants per face, by a
     hash of its cell and face, is what keeps large areas of one block from
     repeating. Edge matching is a separate tool with a narrower job: letting
     coherent small-scale features that cross a block edge (usually oriented
     ones such as grain or bedding) continue across it without a seam. A
     block needs it only if it has such features.
  2. **Match only the edges the features cross.** A log's bark grain runs
     along its axis, so only the end-to-end edges (log on log, up a trunk)
     are matched; the side-by-side edges between two logs are real breaks.
     Matching only applies between faces that should continue (same type,
     same orientation), which world-space labels give automatically.
  3. **Rendering-layer variation stays subtle; distinctive features are block
     content.** Variation chosen by position is presentation-only
     (`core/authority-derived-split`): the player can't choose, keep or carry
     it, and a moved block looks slightly different. That is fine only while
     no one would care which variant appears. Anything distinctive enough to
     have an opinion about belongs in block content (type, orientation, later
     a variant in block state), placed by worldgen or the player
     (`world/block-orientation`; the "orientation first" rule below).
- **How variant sets work:**
  - **Presentation-only and a pure function of position**: never in block
    state or chunk palettes.
  - **Chosen in the fragment shader from world position**, not per vertex,
    so it works across greedy-merged quads (`world/chunk-meshing`).
    Everything stays in **one 16×16 texture array**; variants are more
    layers (1 KB each), so memory is negligible.
  - **Selection rules, per set:** `random` (any number of variants, one per
    face by a hash of cell and face; no edge constraints), `matched` (tiles
    indexed by the labels of the matched edges only, each label a hash of the
    shared edge, so both neighbours agree without communicating; with 2
    labels, N/S-only matching is 4 tiles and all four edges 16; the four-edge
    case is Wang tiling, the hashed labels follow Wei 2004), and
    `connected` (glass frames, below). Examples:

| Block | Content | Rule | Matched edges |
|---|---|---|---|
| dirt, grass | per-texel noise | `random` | none |
| stone brick, ashlar | per-block wear and tint; the joint already shows the block | `random` | none |
| log (sides) | bark grain along the axis | `matched` | end-to-end only (4 tiles) |
| gravel | pebbles of 3–5 texels crossing edges | `matched` | all four (16 tiles) |

  - **The real cost is authoring**, so sets are **generated procedurally**
    from each block's recipe (the scripted texture generator), not drawn.
    Only `matched` sets carry an edge-matching obligation, and only on their
    labelled edges; it is met by construction in the generator, and the
    engine checks structure only (no visual seam check).
- **Appearance ids:** a process-wide **appearance table** interns each
  distinct **face payload** (texture layer, turn, set kind, random tile
  count, material ID: everything a mesh vertex carries except position)
  into a dense 32-bit **appearance id**. It is a separate id space from
  block states, mapping many to many: states that differ only in data the
  face doesn't show share an id, and one state's six faces may have six.
  - **Precomputed per state:** the state table (`world/brick`) holds six
    appearance ids per state, one per world face (orientation folded in),
    computed when the state is interned, so the hashing happens only
    there.
  - **What meshing uses:** the merge key (`world/chunk-meshing`) is the
    appearance id; a mesh copies each palette entry's six ids into a small
    table indexed by palette index, with no hashing per mesh, and merging
    compares ids. Anything chosen by world position or neighbours (`random`
    and `matched` tiles, translucent connected frames) is not in the
    payload, which is what makes a per-state id valid. Per-vertex
    attributes added later (ambient occlusion, baked light) join the
    payload.
  - **Lifetime:** derived from definitions and the active resource pack,
    never persisted; a pack reload rebuilds the table, recomputes the state
    table's column and remeshes everything, without changing any state id.
- **Larger patterns use orientation first, not macro tiling**: a
  pattern bigger than one block is built from oriented block variants and
  declarative placement and orientation heuristics, because that puts it
  under the player's control, and it is probably more fun.
  - **Veins** (veined marble, banded gneiss): a vein block's orientation sets
    the vein direction, and consistently oriented neighbors continue the vein
    across blocks. A player can lay a run of marble with its veins aligned,
    or break them up deliberately; a mismatch reads as a small fault.
    - **Parked, low priority**: recorded for later; not
      needed for world building.
    - **Several ports, veins defined in 3D** (more than one
      port). Each vein variant is a **sheet through the cube** (on
      Earth, veins are mineral-filled planar fractures, often in sets with a
      shared orientation), and all six faces are generated from it
      procedurally, so faces agree at the cube's own edges by construction.
      Sheets are chosen so every crossing of a cube edge lands on a **port**
      (e.g. texels 0, 4, 8, 12, 16 along each edge). Two neighbors continue a
      vein when their traces on the shared face match, which is a Wang-style
      edge-label condition; a declarative placement heuristic picks the
      variant and orientation that continue the neighbors' veins.
    - **Veins give the corner and diagonal symmetry types natural blocks**
      A sheet's symmetry
      *is* the stabilizer of its geometry, so the type follows from the
      sheet and the validator can check it:

| Vein sheet | Symmetry type | Orientations |
|---|---|---|
| plane parallel to a face, through the center | `column` | 3 |
| plane through two opposite edges (a face diagonal on two faces) | `twisted-column` | 6 |
| plane offset toward an edge, clipping it | `edge` | 12 |
| plane perpendicular to a body diagonal, through the center (hexagonal section) | `diagonal` | 4 |
| plane offset toward a vertex, clipping a corner | `corner` | 8 |
| the six face diagonals of one inscribed tetrahedron (a crossing net) | `tetrahedral` | 2 |

  - **Ashlar** (proposed, open): for a **2×1×1 stone without
    composite blocks**, each block is a **half-stone** of symmetry type
    `pointed` (`world/block-orientation`), pointing toward its partner; the
    joint is on the opposite face, and the other half is the same block
    rotated 180° about the vertical axis. Parity is plain orientation with no
    new state; the 6 orientations cover stones running along x or y with
    either parity, and vertical stones. Placement could pair halves
    automatically (placing against an unpaired half that points at the new
    cell makes the new block point back at it; otherwise it starts a new
    stone), so a row alternates halves, the player picks each course's offset
    by where it starts, and the wrench fixes any stone. Longer stones (3×1)
    need a small variant index in block state.
  - **Oriented natural rock:** bedding in sedimentary rock, cleavage and
    foliation in metamorphic rock, columnar jointing in basalt (and logs).
    Massive rocks stay `uniform`. Worldgen sets it from a slow dip field, so
    folds and tilted strata show in cliffs and tunnels. Cost stays small: a
    `column`-type rock has 3 orientations, and low-frequency folds keep most
    chunks to one orientation, so palettes stay small and greedy merging is
    rarely split. The Wang variant is picked by position, then the
    orientation's face turn applies; Wang edges won't match across an
    orientation boundary, which is a geological contact where a seam reads
    naturally (`geology/rock-blocks`).
- **World-anchored macro tiles: reserve only.** A texture spanning several
  blocks (texture coordinates from world position modulo a period, possibly
  Wang tiles at macro scale) fixes where joints and features fall, outside the
  player's control. It makes most sense where the effect should be barely
  perceptible, and cheaper methods exist for that, so it is not planned.
  (Cost on record: layers multiply by the period squared, e.g. 256 layers per
  rock for a 16-tile Wang set at 4×4.)
- **Future, off the critical path:** a **world-space low-frequency tint and
  roughness modulation** (noise varying over tens of blocks) to break up the
  "same block everywhere" look, considered **holistically with haze, fog and
  atmosphere** for world naturalism and reducing graphics artifacts
 . Not needed for world building.
- **Edge labels are metadata:** the
  renderer needs each `matched` tile's labels to pick one that matches its
  neighbours. The generator makes same-label edges continuous by
  construction; the engine doesn't check seams (it checks only that the set
  is complete for its declared edges). A **complete set indexed by its labelled edges' bits** makes the
  index itself the label set, with no separate table. Variants don't assert
  rotational invariance (`world/block-orientation`). Labels are chosen in
  world space, then the orientation's face turn applies, so oriented
  neighbours still match.
- **Connected-glass frames are an edge-bit texture set:** a translucent
  block's face texture holds 16 frame variants indexed by which edges are
  framed, the same edge-bit indexing as Wang sets, with alpha as coverage
  (the absorbance pass adds −ln(1 − α) at the face's depth). Edge-bit
  variant sets are one mechanism with three selection rules: `random`,
  `matched` (edge labels matched to neighbours) and `connected` (framed
  edges, from the mesher).
  - **Chosen per fragment from the quad** (v0.2.5): a merged translucent
    quad carries which of its four sides are framed and which of its four
    corners are inner corners; a fragment's tile is the edge tile for the
    framed sides its cell touches, plus, where a corner bit is set, that
    corner's square from the fully framed tile. Frames are not part of the
    appearance payload (they depend on neighbours), which is what keeps
    appearance ids per state.
  - **Inner corners** (owner, 2026-11-27): at a concave corner of a region
    the corner cell's two edges both continue, so neither has a band, but
    its diagonal neighbour doesn't continue, and the two bands meeting
    there would stop a band's width short. The corner bit closes it, so the
    16 edge tiles suffice where connected-texture schemes use 47 (blob
    sets).
- Open: decoupling materials from block types. A face's material id is
  its block type's, so two block types never share an appearance id and
  never merge in the mesher (`world/chunk-meshing`), even with identical
  textures; distinct blocks sharing a material would merge once materials
  are their own definitions. Not yet scheduled (dispositioned in v0.2.4).
- Open: which natural blocks beyond logs need matching (e.g. stone with
  faint bedding); the Wang edge-color count; the exact port positions and which vein
  sheets ship; whether
  the ashlar pairing rule extends `world/block-orientation`'s placement inputs
  (n, f, hit point) with a neighbor input.

## render/anti-aliasing — TAA with depth-based camera reprojection; SMAA fallback

- Decision: **temporal anti-aliasing (TAA)** — jitter the projection by a
  per-frame sub-pixel offset and blend with reprojected, neighborhood-clamped
  history — with **motion derived from depth for static geometry**:
  - **Static pixels** (terrain, structures, placed blocks — almost everything):
    reconstruct world position from depth and project with the *previous*
    frame's view-projection. No per-object velocity, and terrain/meshing never
    writes the velocity target.
  - **Moving things** (mobs, players, dropped items, moving logic-machine parts)
    write velocity; everything else reads "use camera reprojection" as default.
  - **Reactive mask** for translucent pixels (`render/transparency`).
  - **SMAA 1x** as a no-history fallback quality setting (no ghosting, fixes
    edges but not thin-feature/specular shimmer).
- Rationale:
  - Deferred makes MSAA expensive (every G-buffer target × N samples, plus
    per-sample edge shading); TAA fits deferred at near-constant cost.
  - TAA fixes edge, texture and specular aliasing alike — and voxel worlds are
    full of the thin high-contrast geometry that shimmers.
  - **A high framerate does not remove the need for reprojection** (mouse-look
    rotates the whole screen: a 180°/s turn at 144 fps, 1440p, 90° FOV is ~35 px
    per frame), but it does improve TAA quality (less disocclusion, fresher
    history). The real simplification comes from the world being mostly static.
  - TAA machinery is also what TAA-family upscalers use — **FSR 2/3** (open,
    Vulkan/Linux, any vendor) is a cheap later option.
- **Rebase interaction (`core/f32-origin-relative`):** the camera is the f32
  render origin and moves every frame, so the previous-frame reprojection matrix
  must include the camera displacement computed as `(f32)(prevCamPos_f64 −
  curCamPos_f64)` — subtract in f64, then downcast. Mixing two frames' f32
  origins is the bug to avoid.
- Known costs: ghosting (managed by history clamping), slight softness,
  disocclusion artifacts.

## render/dev-ui — Dear ImGui is the permanent developer UI, separate from the player UI

- Decision: **Dear ImGui** (immediate-mode C++ GUI, MIT) is the engine's
  developer UI — debug overlays (`input/debug-keys`), stats HUD, live parameter
  tuning, internal-state inspectors (edit sets, input context stack), authoring
  diagnostics. It **stays the dev UI permanently**, alongside whatever
  conventional toolkit the player UI later uses; it is not a stopgap to be
  replaced.
- Separation:
  - **Player-facing UI never uses ImGui** (HUD, inventory, menus, signs, chat).
    That path is a separate, later text/UI system (MSDF glyphs + HarfBuzz shaping
    + FreeType; dynamic glyph atlas), because ImGui looks like a dev tool and has
    limited complex-script / RTL support.
  - The two systems share nothing but the render graph; dev UI can be compiled
    out or gated without touching player UI.
- Render placement: one pass at the end of the graph, **after TAA and tonemap**,
  in display space at native resolution — so it is never jittered, ghosted or
  upscaled. (The same rule will apply to the player HUD.)
- Rationale: this split is standard practice in shipped engines — immediate mode
  makes exposing an engine value one line per value, with no widget/state sync,
  which is exactly what tooling needs and exactly what polished player UI does not.
- Integration: official `imgui_impl_glfw` + `imgui_impl_vulkan` backends
  (`deps/dev-ui`); first used in v0.1.2 for the F1 stats overlay.
- **Ships in release builds**, available in creative mode (`input/debug-keys`).
