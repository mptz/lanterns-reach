# Crafting

How recipes name their inputs. The recipe and dependency graph itself is
still deferred content (`progression.md`).

## crafting/material-classes — Recipes take material classes, not specific items

- Decision: a recipe names **material classes** (fibre, sheet, knappable
  stone, ...), and any material with the right properties qualifies. One
  common recipe then works everywhere: a **pouch** can be made from hide,
  **bark strips, banana leaves, linen**, woven grass and so on.
- Rationale: the first-session stress test (`first-session/biome-stress-test`)
  found that most gaps came from recipes naming specific items; with classes,
  every setting supplies something that qualifies, so alternatives exist by
  construction rather than by hand-listing. It mirrors tools acting on
  material classes (`tools/few-and-versatile`) and features parameterized on
  field combinations (`worldgen/field-driven-features`). It also gives
  no-kill paths where a setting needs them (`loop/opt-in-challenges`).
- **Specific materials still differ, in small trade-offs** (`core/no-meta`):
  obsidian is sharp but brittle, dung burns smoky, bark-strip pouches are
  lighter but wear faster than leather. Never a single best member.
- **Draft classes:**

| Class | Members (examples) |
|---|---|
| Fibre / cordage | plant fibre (yucca, agave, nettle, flax), grass, reeds, bark fibre, sinew |
| Sheet (flexible) | hide, rawhide, bark strips, large leaves (banana, palm), linen and other cloth |
| Knappable stone | flint, chert, obsidian, quartzite, crystal quartz; shell and bone as edge substitutes |
| Handle (rigid) | sticks, driftwood, bone, antler (shed, so no kill), cactus ribs, bundled reeds |
| Fuel | wood, charcoal, dry grass bundles, dung, peat, shrubs, fat and oil (in a lamp) |
| Aromatic smoke | juniper, sage, mugwort, other aromatic plants (`first-session/campfire-protection`) |
| Pigment | soot and charcoal (from any fire, so universal), ochre, mineral pigments later (`inventory/pouch-progression`) |
| Insulation | fur and wool (shed or moulted, or from hunting), plant fibre padding, cattail down, felt |
| Pane (translucent) | horn, mica ("isinglass"), thin scraped hide |
| Container body | gourd, hide skin, clay pot, carved soapstone |
| Resonant crystal | quartz, calcite, selenite, agate, fluorite, celestine (`geology/resonant-crystals`) |

- Open: the final class list and which properties define each; how a
  material declares its classes (in its block or item definition); whether
  class membership is graded (a better or worse member) or binary.

## crafting/per-world-recipes — Some recipes are randomized per world, to keep secrets worth seeking

- Decision: **"secrets" should be something it makes sense to seek**,
  so some content is **wiki-proofed**. Because recipes take material classes
  (`crafting/material-classes`), certain recipes, **potions** for example, can be
  **randomized per world**: common ingredients stay common, but which
  combination makes a given potion differs between worlds, and the recipe has
  to be found, typically in **Enigma** (its archive, finding aids,
  `worldgen/dimension-cosmology`).
- **It justifies block cardinality on the high side:** more materials mean
  more combinations for the combination lock (`geology/rock-blocks`).
- Constraints (proposed, open):
  - **Only for secrets, never for essentials:** first-session and other basic
    recipes stay fixed and learnable everywhere (`first-session/inventory`),
    so no one is locked out of survival.
  - **Deterministic from the world seed,** in the integer domain
    (`core/worldgen-integer-domain`), so a world's recipes are stable and
    shared by everyone in it. A wiki can describe what *kinds* of recipe
    exist, never this world's.
  - **Found, not rolled:** a recipe is a piece of information obtained by
    exploration (a page in the archive, a finding aid's answer), not a random
    drop, so seeking it is directed effort (`ethics/no-variable-ratio-core`).
    Experimenting can also find it, as a slower path.
  - The same idea already protects Enigma's artifacts: each has its own
    hidden rule.
- Open: which recipe families are per-world; how many ingredients each
  combination uses; whether partial knowledge (one ingredient known) is a
  thing.

