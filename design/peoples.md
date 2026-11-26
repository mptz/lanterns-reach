# Peoples

The world's inhabitants: animal peoples (after Dwarf Fortress), who take the
place of other voxel games' villagers and traders. They give the world
ordinary people (`core/low-fantasy`) without pretending to human-level AI,
since stylized, alien behaviour reads as character in a fox where it
would read as broken in a human. They are a source of both trade and
threat, and their factions carry the player's reputation.

## peoples/peoples (draft) — Sapient animal peoples in tribes and camps, with values of their own and no moral alignment

- **Names: `<animal>folk`.** Each people is named for its animal with
  "-folk", one word (foxfolk, tigerfolk, horsefolk), hyphenated where the
  join is awkward (wolf-folk, elephant-folk, gorilla-folk). The stem stays
  singular (mousefolk), "a foxfolk" is one individual, and casually a
  people is just its animal ("the foxes"). "Peoples" is the collective
  term. There are no NPC humans in the setting, so a name like horsefolk
  can't be misread as people who keep horses.
- **Who they are.** Every people is sapient, though some are of low
  intelligence. They live in groups from a small tribe around one campfire
  to larger encampments, but not cities.
- **They build, as hunter-gatherers do:** camps, shelters, hunting
  grounds, paths. Settlements are features placed from `habitation`
  (`worldgen/regional-fields`) and from territory (who lives where,
  `peoples/territory`).
- **Whether they built the ruins is deliberately ambiguous**
  (`ambiguity/ruin-builders`, `design/ambiguities.md`): there are no other
  obvious builders, but the ruins show more technology and civilization
  than the peoples now have.
- **They cluster in ruins.** Where ruins are available, tribes use them
  opportunistically, since old walls are still more defensible than a
  camp; settlement placement therefore favours high `antiquity`. High
  `habitation` means tribes concentrated, not cities: Enduring country
  (`worldgen/provinces`) is ruins packed with tribes, and Besieged is
  such ruins held against the dread.
- **No moral alignment.** Peoples have reputation, emotions and perhaps
  politics, but are not good or evil, and the `alignment` field does not
  bear on them (it is the monsters' axis, `mobs/spawning`). Their values
  need not match the player's: the tigerfolk may be honourable by their own
  code and still consider eating humans acceptable. Each people's values
  decide how it judges a deed (`peoples/reputation`).
- **A third threat.** Monsters track `alignment` and wildlife tracks
  `savagery`, both set by worldgen. A people's hostility tracks the
  player's standing with it, so it is the one threat the player's conduct
  shapes. A people's baseline attitude to humans sets where standing
  starts: predatory peoples may begin hostile.
- **Open:** the list of peoples and their values; whether any are
  nomadic; how hostility maps from standing (a soft threshold,
  `core/soft-thresholds`); whether harming wild animals of a people's own
  species is a deed that people judges.

## peoples/reputation (draft) — Standing is computed on arrival from remembered deeds, weighted by how far news has travelled

- **Deeds are events.** A deed is recorded with a place, a time, who did
  it and what it was (fair trade, a kill, a gift, a broken bargain).
- **Standing is closed-form, evaluated on arrival.** When a player comes
  to a settlement of people P, its view of them is

  standing(s, P) = Σₑ impact(e, P) × affinity(people(e), P) × reach(dist(e, s), now − t(e))

  - impact(e, P) is how P's values judge the deed;
  - affinity is the faction term (`peoples/factions`): 1 for P's own
    people, negative for its enemies;
  - reach is a soft kernel (`core/soft-thresholds`): zero until news
    could have arrived, then rising, then fading with time.

  Nothing is simulated while the player is away (`core/closed-form-first`,
  `persist/presence-not-absence`); a settlement's view is computed when it
  matters.
- **Lossy by design, and diegetically so.** News travels slowly, weakens
  with distance and fades with time, so a frontier camp and a distant
  encampment of the same people know different things about you, and
  local deeds always outweigh distant ones. Rumour distortion
  (exaggeration, misattribution) is a possible later noise term.
- **Individual and group standing, both.** A deed is remembered against
  its doer and propagates, with lower weight, to the doer's groups: a
  player party or other group, and humans as a whole. A player's standing
  sums the levels: you are an individual, a member of your group (a group
  of one by default), and a human, and other humans' deeds colour how a
  people sees you before it knows you.
- **Open:** the event log's storage and compaction (old events merged
  into per-region aggregates is itself lossy and diegetic); the speed of
  news (placeholder: about travel speed); the level weights; how groups
  are formed and left; whether a people can learn to tell individual
  humans apart well (low-intelligence peoples might not).

## peoples/factions (draft) — A signed affinity matrix between peoples spreads reputation across factions

- **Affinity.** Each pair of peoples has a signed affinity: positive for
  allies, negative for enemies, near zero for strangers. A deed's effect
  on P is scaled by the affinity between the people it affected and P, so
  helping the foxfolk lowers your standing with the rabbitfolk.
- **The matrix is authored per people pair,** possibly varied by
  territory (neighbours feud, distant peoples don't care).
- **Open:** whether affinities change over time (politics), and whether
  player deeds can shift them (brokering a truce); whether peoples split
  into sub-factions (clans) with their own affinities.

## peoples/sojourners (draft) — Peoples, like players, arrive from elsewhere and hold reality open with lanterns of their own

- **Three relations to reality.** Wildlife is of the world: ordinary
  ecology, neither affected by stabilization nor affecting it
  (`mobs/wildlife`). Monsters are reality's response, spawning where
  stabilization is absent and provoked by its potency (`mobs/spawning`);
  whether they invade or protect is unclear. Peoples, like players, are
  **sojourners**: they arrive from elsewhere and fade back, and while here
  they hold reality open. Sapients are therefore not part of nature, and
  humans are, to the peoples, one more people.
- **Their own lanterns.** Each people stabilizes its camps with lanterns
  of its own, visually and functionally distinct from the player's and
  not directly usable by the player. They stabilize more weakly than a
  player's best. They lead to trade goods and desires
  (`peoples/trade`): what the lanterns need is something peoples want.
- **Persistence falls out of the existing model.** Decay reverts toward
  the seed (`persist/diegetic-persistence`), and a camp is in the seed.
  A player's changes inside a camp last while its lanterns hold, longer
  than in the wild but not forever; when a camp is destroyed or its
  lanterns fail, the land reverts and a tribe returns. The returning tribe
  is a new arrival, not a resurrection, and reputation is unaffected: it
  remembers deeds and the people they were done to, not individuals.
- **The immune response reacts to their stabilization too**
  (`mobs/spawning`): a camp's lanterns are one more local source in the
  stabilization field, so its frontier draws monsters of its own potency's
  tier, near a present player, as any stabilized place does. Monsters
  attack peoples (sapients) but not wildlife. So tribes holding reality
  open in old ruins are besieged in earnest (Besieged, `worldgen/provinces`), and a player
  visiting a camp may arrive to a fight.
- **Occupancy is static for now:** worldgen fixes which camps are
  occupied, and only player changes decay. Recorded possibility: a
  temporal component, with tribes arriving and departing on a closed-form,
  seed-keyed schedule (`core/closed-form-first`), so that an Abandoned
  camp is one whose lantern has died and `habitation` varies with time.
- **Open:** where the peoples arrive from and fade back to; how strong
  their lanterns are and what they need.

## peoples/trade (draft) — Barter with per-people preferences; nothing gated

- **Barter, not currency:** each people values goods by its own
  preferences, so what is cheap to one is precious to another, which gives
  travel between territories a purpose.
- **Nothing is gated:** every good stays obtainable without trade
  (as with province types, `worldgen/provinces`); trade saves effort and
  offers novelty.
- **Standing sets terms:** whether a people will trade at all, and how
  generously.
- **Open:** each people's preferences; whether peoples hold stock (from
  the world's `yield`) or produce goods; trade's place in progression.

## peoples/territory (draft) — Which peoples live where

- Territory is the discrete subsystem `worldgen/territory` anticipated
  (seeded centres spreading influence, neighbour-dependent borders), not
  a noise field. A province's settlement tag sets how dense the camps are;
  territory sets whose they are.
- **Open:** everything, including whether territory is fixed by worldgen
  or shifts with the factions' politics.
