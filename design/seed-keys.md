# Seed keys

How the world seed becomes a hierarchy of keys, so that each client gets
only the keys for what it may generate.

## worldgen/seed-keys — The seed is a key hierarchy: clients get only the keys for what they may generate

- **Seeds are strings**: the user-visible seed is a UTF-8 string.
  SHA-256 of it gives a 256-bit binary world seed, which is **never used
  directly as RNG state.** Note (proposed): normalize the string to Unicode
  NFC before hashing (the same text can be encoded two ways), and decide on
  trimming whitespace.
- **Sub-seeds by one-way derivation**: the server derives sub-seeds
  (keys) for regions of space with SHA-256 or another irreversible process,
  so a client can generate chunks near the player (at a server-confirmed
  position) **without ever learning the world seed.** A client can then write
  a local ore finder or x-ray, but can't search the seed for distant
  structures. Minecraft's seeds are cracked from observed terrain because its
  effective structure seed is 48 bits behind a weak generator; a 256-bit root
  behind one-way derivation isn't open to that.
- **Release tiers, one key per region.** Octaves are grouped by frequency
  band into a few **release tiers** (near, mid, far), each with its own grid
  of key regions in world units, sized to the distance at which the tier's
  octaves may be released, and one key per region per tier. Each field's
  seed and rotation in a region are derived one-way from that region's
  tier key (below), so a client holds a handful of tier keys
  rather than one per octave, and a box needs only the keys of the
  tiers whose octaves it uses. This matches information LoD
  (`world/progressive-lod`, `world/octave-pyramid`): a coarse level needs
  only coarse tiers' keys, so the server releases coarse keys widely
  (distant terrain is visible anyway) and fine keys only near the player.
  **A tier's release radius is the drawing radius of its coarsest octave:**
  a client needs a tier's keys wherever any of its octaves is drawn, and an
  octave of amplitude A is drawn out to about A / 10⁻³
  (`world/octave-pyramid`), about 125 λ with `surface`-like amplitudes
  (A ≈ λ/8). So a tier's boundary octave is λ ≈ R_release / 125, and its key
  spacing is a fraction of R_release, which bounds how many keys it needs.
  Placeholder tiers, for a view radius of 2¹⁷ ft (about 25 miles):

  | Tier | Octaves (lattice spacing) | Release radius | Key spacing |
  |---|---|---|---|
  | near | finer than about 64 ft, down to per-voxel | about 2¹³ ft (about 2.5 km) | about 2¹⁰ ft |
  | mid | about 64 ft to 256 ft | about 2¹⁵ ft (about 6 miles) | about 2¹³ ft |
  | far | coarser, continents included | the view radius, 2¹⁷ ft | 2¹⁷ ft |

  Octaves coarser than about 2¹⁷ / 125 ≈ 1,000 ft are drawn everywhere in
  view, so the coarsest levels exist only as data the finer levels build
  on. All of these assume `surface`-like amplitudes; each field's real
  amplitudes set its own boundaries.

  Each tier's key grid is offset from the others', so their transition
  lines never coincide.
- **Two key grids per tier: cells for 3D fields, columns for 2D fields.**
  3D fields are keyed on a 3D grid of key cells, each covering a cube of
  the world, labelled (x, y, z) and released by 3D distance (at most
  3×3×3 per point). 2D fields are keyed on a separate 2D grid of key
  columns, each covering every height in its column, labelled (x, y) and
  released to anyone near that column whatever their height (at most 3×3
  per point). A 2D field doesn't vary with height, so a column key is the
  natural unit; using the z = 0 slice of the 3D grid instead would hand a
  player deep underground keys named for a height they never visit, and
  tie 2D secrecy to 3D release.
- **Which fields a tier holds.** In Terra the far tier holds the octaves of
  `landmass` and `uplift` and the low octaves of `surface` and `erosion`,
  keyed by column, and the coarse octaves of the regional fields
  (`temperature`, `moisture`, `weirdness` and the rest), which are 3D,
  varying slowly in height (temperature with a vertical gradient), keyed by
  cell. So Terra's far-tier terrain shape is 2D, but the tier needs both
  grids. Terra's 3D density octaves end around the mid tier (huge caverns
  are a kilometre or so, far below a far key spacing). Bias-free dimensions (cave
  worlds, an underdark, `worldgen/dimension-profiles`) are dominated by 3D
  noise at large scale, so their far-tier terrain shape is 3D too.
- **One call per key, field and octave.** For each key point (a key
  region's centre) and field, a 32-bit **field seed** is derived from the
  key; octave j of that field is one FastNoise2 call, a graph of that
  octave and its domain warps, with seed `field seed + j` (as FastNoise2's
  own fractal node would seed it internally), and the warp nodes taking
  seed offsets the same way. One call per octave, rather than one graph
  summing a field's octaves, keeps each octave separately callable, as
  `world/octave-pyramid` needs, since a field's octaves fall in different
  levels and a level evaluates only its own; the cost is a little of
  FastNoise2's graph fusion. Each octave's
  spacing and offset are its own (`worldgen/noise-fields`: lattices are
  not aligned).
- **Blended fields across key regions.** Each key point i has its octave
  call's output nᵢ and a smooth weight wᵢ with a plateau over its region
  that falls off in a transition band near the edges; the weights sum to 1
  everywhere. A weight is a product of 1D weights, one per axis: at q's
  position in its key interval along that axis, 1 across the plateau with a
  smooth crossfade to the neighbour near each end, so the product is still
  a partition of unity. Plateau, edge and corner need no classification:
  near no edge one weight is nonzero, near one edge two, near a corner four
  (eight in 3D). With both warps written out, the field is

  `f(p) = Σ wᵢ(q) · nᵢ(p) / √(Σ wᵢ(q)²)`, where `q = p + W(p)`

  - **W, the boundary warp,** decides where transitions lie: the same for
    every field, public, of small amplitude (below);
  - **each nᵢ** is one key's octave call, its own terrain warp included,
    evaluated in its own frame and rotation (below).
  The two warps never interact: the boundary warp changes only the
  weights, the terrain warp only what each field outputs.

  Averaging independent fields would shrink their variance (by √2 at an
  even blend of two); the normalisation restores it exactly, so f has the
  same statistics everywhere, and since the weights and the denominator are
  smooth and never zero, f is smooth, with no seams. At any point at most
  2^d keys (4 in 2D, 8 in 3D) have a nonzero weight. Seeds themselves can't
  be blended (a hash input has no "between"); fields can.
- **Local frames and rotations:** each octave of each field is evaluated in
  its key point's frame through one affine transform per octave,
  `fⱼRᵢⱼ · (p − originᵢ) + offsetⱼ`, folding its rotation Rᵢⱼ (derived from
  the key, per field and octave), its frequency fⱼ and its lattice offset
  into a single matrix-vector multiply, so coordinates passed to the noise stay within about two
  key spacings of zero anywhere in the ±10⁹ world, and f32 precision holds
  (`deps/noise`). The fields are independent, so shifting each one's origin
  changes nothing about the result's statistics. The rotation (a uniform
  random 3D rotation from about 96 derived bits, by Shoemake's quaternion
  method, or an angle for a 2D field) points Perlin's axis bias in a
  different random direction in each key region and in each octave, so no
  axis-aligned tendency is shared across the world, and within a region
  the octaves' biases don't add into a common directional grain (Inigo
  Quilez's widely used fBm rotates per octave with one rotation-and-scale
  matrix, the same idea). Since the frequency has to be applied anyway, an
  independent rotation per octave costs nothing extra; it preserves
  distances (so precision is unaffected), and applies to isotropic noise
  only: 2D fields (`landmass`, `uplift`,
  `surface`'s octaves) rotate about the vertical axis; 3D noise added to a
  bias may rotate freely, the bias itself (`z − surface`) not being
  rotated; a field with a meaningful axis (a vertical gradient, anything a
  profile shapes by height) rotates only about that axis, or not at all.
- **Warped boundaries:** the weights are evaluated at a warped position,
  wᵢ(p + W(p)), with W a smooth, low-frequency displacement. Every weight
  is evaluated at the same warped point, so they still sum to 1 exactly and
  the normalisation still applies. Transition zones then follow no straight
  grid lines. W may use a public seed: it reveals only where boundaries
  lie, not terrain. Its amplitude stays well below a plateau's half-width,
  so a point's nonzero weights remain its own region's and its immediate
  neighbours'.
- **The terrain's domain warp lives inside each key's octave call** and is
  blended with it: warping moves where that key's field is read, never
  which key's field applies, so it doesn't affect which keys a point needs,
  and the earlier worry that a strong warp would break the key table is
  gone. In transition zones two differently warped fields are blended, so a
  warp feature fades from one field's into another's rather than crossing
  the boundary coherently; a warp that must stay coherent over long
  distances would instead be applied once to the blended world, with its
  amplitude added to `requiredKeys`.
- **Which keys are called, per box:** the keys whose weight is nonzero
  somewhere in the box (bounded with the warp amplitude) are found first.
  For the far tier, each of them is evaluated at every point of the box:
  coarse octaves are so sparse that multiplying some samples by a zero
  weight costs nothing that matters. For the near tier, the weights are
  computed first and each key's call gets only the positions where its
  weight is nonzero (the position-array API takes any point list); a
  chunk-sized box usually lies in one plateau, so most chunks need one call
  per octave.
- **Cost:** one evaluation per octave in a plateau, 2 to 8 in transitions.
  For the near tier, with transitions about 32 ft wide in 1024 ft regions,
  about 18% of voxels need more than one, so the finest octaves cost about
  1.2× on average; coarse tiers' transitions can be wide (a quarter of the
  spacing), since coarse octaves are evaluated so sparsely that the
  multiplier doesn't matter, and wide transitions blend continents gently.
- **Terrain shape is never public:** its octaves are keyed like everything
  else, and a client gets only the keys for what it may generate.
- **Cracking a region reveals nothing about any other.** A region's octave
  seeds are assumed crackable from terrain observed in it (2³² per octave),
  and that is fine; the requirement is only that what is learnt stops at
  that region and that octave. It holds because:
  - tier keys are KDF outputs from the root with their own labels (tier,
    key region), never offsets from a shared seed, so they are independent
    and the root can't be recovered from them;
  - field seeds are derived one-way from their tier key with their own
    labels, so a cracked field seed reveals neither the tier key nor the
    region's other fields or tiers; it does reveal all of that field's
    octaves in that tier, in that region, since they share the seed, which
    is acceptable, as they are observed together anyway;
  - a cracked field nᵢ is unbounded, but it shows only where wᵢ > 0 (its
    region plus the transition), so it predicts nothing elsewhere: the
    noise stack cracked influences only a bounded part of the world, which
    makes this the firmest of the properties;
  - a plateau shows one field per key, which is what can be cracked;
    transition zones mix several, which is harder;
  - with more regions than 2³² seeds, some regions share a seed by chance,
    but finding which would need the root, so a cracked seed predicts no
    particular region.
- **Keys are derived from the root, not from each other:** "hierarchical"
  means the tiers, not a chain of derivation between them: every tier key
  comes from the root, and holding a coarse tier's key yields nothing
  finer.
- **Non-local processes declare a maximum radius of influence:** their keys
  sit in a tier whose regions are at least that wide, or they stay on the
  server. Made local at a coarse scale where possible: rivers computed on a
  coarse grid from coarse elevation with a capped basin size become a
  coarse field, with fine detail (the riverbed) local around the path.
  Continent-wide drainage, as in Dwarf Fortress's worldgen, is too non-local
  for this design.
- **Domain separation and server-only branches:** derive every key with a
  context label through a proper key-derivation function (HKDF or BLAKE3's
  key derivation): a tier key as "worldgen / tier far / key region (x, y)",
  and from it each purpose as HKDF-Expand with its own label, taking as
  many bits as it needs ("field surface / seed" for a 32-bit field seed,
  "field surface / rotation" for about 96 bits). Purposes are derived by
  label, never by slicing a key into fixed pieces, so there can be as many
  as needed and adding one later shifts no other. Secrets live on branches no
  client key leads to: ore resolution (`geology/ore-sites`), structure
  plans (`worldgen/structure-plans`), per-world recipes
  (`crafting/per-world-recipes`), Enigma's secrets.
- **Per-point hashing needn't be cryptographic:** keys and octave seeds are
  derived once per region with the slow one-way function; lattice points
  are hashed by FastNoise2's own fast hash with the octave's 32-bit seed
  (the field seed plus the octave's index).
  Fast hashes may be invertible from their outputs, which is harmless: the
  client already holds that region's key, and the security sits in the
  derivations.
- **Keys can't be revoked:** a client keeps any key it was given, which is
  fine, since its player was there.
- **Boxes and keys:** a unit of work (a chunk, or a coarse volume: a
  world-aligned power-of-two box no larger than the key spacing of the
  coarsest tier it holds) touches at most 3 key regions per axis per tier,
  counting transitions and the boundary warp, so the per-request key table
  stays at 3×3×3 per tier for key cells and 3×3 for key columns; `requiredKeys` widens each
  weight's support by the warp amplitude (`worldgen/cartographer`).
- **Release:** a client receives, per tier, the key cells and key columns
  touched by its generation radius for that tier, widened by the transition
  width and the warp amplitude (columns by horizontal distance alone):
  near-tier regions within about 2¹³ ft, mid-tier within about 2¹⁵ ft,
  far-tier across the view radius of 2¹⁷ ft. It can predict at most about one key spacing
  beyond what it may generate. Secrets stay on server-only branches
  regardless.
- **Fallback: the fork** (`deps/noise`): if blended transitions show in
  practice, or a node type can't be blended (cellular noise, a distance to
  the nearest feature point, would show doubled cells where fields
  overlap), the fallback is the fork that hashes each lattice corner with
  its owner region's seed and region-local index, giving one true field.
- Open: the number of tiers and their spacings; transition width and
  plateau shape per tier; how cellular noise is keyed; the derivation
  function; whether client generation is used at all (`net/replication`).
