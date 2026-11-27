// Tests: tools. Registered through sandbox_tests.cpp.

#include "test_support.h"

namespace {

using namespace orient;


// planEdit is pure: identical results on repeated calls, world untouched;
// and committing applies exactly the planned states.
void checkPureAndCommits(ChunkWorld& w, const Item* item, bool secondary, const std::optional<ToolTarget>& t,
                         const ToolContext& ctx) {
    const std::uint64_t gen = w.generation();
    const WorldMesh before = w.buildMesh(ChunkWorld::kChunk);
    const PlannedEdit a = planEdit(item, secondary, t, ctx, w);
    const PlannedEdit b = planEdit(item, secondary, t, ctx, w);
    CHECK(a.edits == b.edits && a.action == b.action);
    CHECK(w.generation() == gen && w.buildMesh(ChunkWorld::kChunk).quads == before.quads);  // compute doesn't modify the world
    ChunkWorld committed = w;  // the click path: apply(planEdit(...))
    const ApplyResult r = committed.apply(a.edits);
    CHECK(r.rejected.empty());  // planned edits are always editable
    for (const Edit& e : a.edits.edits) CHECK(committed.probe(e.cell) == e.state);
}

void testToolsPureAndCommit() {
    ChunkWorld w(registry());
    EditSet ground;
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 12; ++x)
            for (int z = 0; z < 3; ++z) ground.edits.push_back({{x, y, z}, registry().state(typeOf(z == 2 ? "grass" : "dirt"), orient::kIdentity)});
    ground.edits.push_back({{6, 6, 3}, registry().state(typeOf("furnace"), orient::kIdentity)});
    w.apply(ground);
    ToolSettings settings;
    settings.shovelFill = typeOf("dirt");
    settings.container.slots = {{{typeOf("brick"), 6}, {typeOf("stone"), 3}, {typeOf("dirt"), 1}}};
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const Item items[] = {Item::blockStack(typeOf("log")), Item::blockStack(typeOf("furnace")), Item::pick(),
                          Item::wrench(), Item::shovel(), Item::container()};
    const WorldPos eyes[] = {{3.3, 2.7, 7.1}, {9.8, 4.2, 6.4}, {6.5, 1.5, 4.5}};
    const WorldPos ats[] = {{6.5, 6.5, 3.5}, {4.2, 5.1, 3.0}, {6.5, 6.5, 2.9}};
    for (const Item& item : items)
        for (const WorldPos& eye : eyes)
            for (const WorldPos& at : ats)
                for (bool secondary : {false, true}) checkPureAndCommits(w, &item, secondary, aim(w, eye, at), ctx);
    checkPureAndCommits(w, nullptr, false, aim(w, eyes[0], ats[0]), ctx);  // bare hand
    CHECK(planEdit(&items[0], false, std::nullopt, ctx, w).edits.edits.empty());  // nothing in reach
}

// The shovel on stepped soft ground with rock in it.
void testShovel() {
    ChunkWorld w(registry());
    const BlockTypeId dirt = typeOf("dirt"), stone = typeOf("stone");
    EditSet e;
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 12; ++x)
            for (int z = 0; z <= 3; ++z) e.edits.push_back({{x, y, z}, registry().state(dirt, orient::kIdentity)});
    // A step: columns x ≥ 7 rise to z = 5 (two soft cells above h = 3) …
    for (int y = 0; y < 12; ++y)
        for (int x = 7; x < 12; ++x)
            for (int z = 4; z <= 5; ++z) e.edits.push_back({{x, y, z}, registry().state(dirt, orient::kIdentity)});
    // … with a rock on that step inside the footprint, and a 2-deep hole.
    e.edits.push_back({{7, 5, 6}, registry().state(stone, orient::kIdentity)});
    e.edits.push_back({{4, 4, 3}, kAirState});
    e.edits.push_back({{4, 4, 2}, kAirState});
    w.apply(e);
    ToolSettings settings;
    settings.shovelFill = dirt;
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const Item shovel = Item::shovel();
    // Aim at the top of (5, 5, 3): h = 3, columns x 3..7, y 3..7.
    const auto t = aim(w, {5.5, 5.5, 9.0}, {5.5, 5.5, 3.5});
    CHECK(t && t->hit.cell == CellPos{5, 5, 3});
    const PlannedEdit p = planEdit(&shovel, false, t, ctx, w);
    CHECK(p.cut == 5 * 2);   // column x = 7 (5 rows) has two soft cells above h
    CHECK(p.blocked == 1);   // the rock at (7, 5, 6) is left alone
    CHECK(p.fill == 2);      // the hole at (4, 4): z = 3 and 2, down to the first solid
    bool rockKept = true;
    for (const Edit& x : p.edits.edits) rockKept = rockKept && !(x.cell == CellPos{7, 5, 6});
    CHECK(rockKept);
    settings.shovelFill = stone;  // rock isn't a shovel fill
    CHECK(planEdit(&shovel, false, t, ctx, w).fill == 0);
}

void testWrench() {
    const Group& g = group();
    for (int r = 0; r < kRotCount; ++r)
        for (const glm::ivec3 n : kNormals) {
            const Rot cw = turnAbout(static_cast<Rot>(r), n, false);
            CHECK(turnAbout(cw, n, true) == r);  // CW then CCW is identity
            Rot four = static_cast<Rot>(r);
            for (int k = 0; k < 4; ++k) four = turnAbout(four, n, false);
            CHECK(four == r);  // four CW clicks
        }
    // Clockwise as seen looking at the face: on the +z face, +x goes to −y.
    CHECK(rotateVec(g.matrix[turnAbout(kIdentity, {0, 0, 1}, false)], {1, 0, 0}) == glm::ivec3(0, -1, 0));
    // A click about a log's own axis is a no-op once canonicalized.
    ChunkWorld w(registry());
    const BlockTypeId log = typeOf("log");
    w.apply({{{{2, 2, 2}, registry().state(log, orient::kIdentity)}}});  // axis along z
    ToolSettings settings;
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const Item wrench = Item::wrench();
    const PlannedEdit top = planEdit(&wrench, false, aim(w, {2.5, 2.5, 5.5}, {2.5, 2.5, 2.5}), ctx, w);
    CHECK(top.edits.edits.size() == 1 && top.edits.edits[0].state == registry().state(log, orient::kIdentity));
    const PlannedEdit side = planEdit(&wrench, false, aim(w, {5.5, 2.5, 2.5}, {2.5, 2.5, 2.5}), ctx, w);
    CHECK(side.edits.edits.size() == 1 && testStates().orientation(side.edits.edits[0].state) != kIdentity);
}

void testContainerModes() {
    ChunkWorld w(registry());
    EditSet floor;
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x) floor.edits.push_back({{x, y, 0}, registry().state(typeOf("stone"), orient::kIdentity)});
    w.apply(floor);
    const BlockTypeId brick = typeOf("brick"), stone = typeOf("stone"), dirt = typeOf("dirt");
    ToolSettings settings;
    settings.container.slots = {{{brick, 60}, {stone, 30}, {dirt, 10}}};
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const Item box = Item::container();
    const WorldPos center{20.5, 20.5, 1.0};
    const CellPos above{20, 20, 1};
    // One block per click, against the clicked face; pure for a fixed state.
    for (int m = 0; m < kPlacementModeCount; ++m) {
        settings.container.mode = static_cast<PlacementMode>(m);
        const PlannedEdit a = planEdit(&box, false, aim(w, {20.4, 18.0, 9.0}, center), ctx, w);
        const PlannedEdit b = planEdit(&box, false, aim(w, {20.4, 18.0, 9.0}, center), ctx, w);
        const PlannedEdit c = planEdit(&box, false, aim(w, {25.0, 26.0, 6.0}, center), ctx, w);  // the camera moved
        CHECK(a.edits == b.edits && a.edits == c.edits);
        CHECK(a.edits.edits.size() == 1 && a.edits.edits[0].cell == above);
    }
    // The randomness is temporal: the pick in ONE cell, over successive
    // draws. `draws` advances only when a placement changed the world.
    auto pickAt = [&](std::uint64_t draws) {
        settings.container.draws = draws;
        const PlannedEdit p = planEdit(&box, false, aim(w, {20.5, 20.5, 9.0}, center), ctx, w);
        return p.edits.edits.empty() ? kAirType : testStates().type(p.edits.edits[0].state);
    };
    // In order: one cell cycles through the slots, click by click.
    settings.container.mode = PlacementMode::InOrder;
    for (std::uint64_t d = 0; d < 7; ++d) CHECK(pickAt(d) == settings.container.slots[d % 3].type);
    // Random slot: every slot comes up, roughly uniformly; count-weighted
    // tracks the counts.
    for (PlacementMode m : {PlacementMode::RandomSlot, PlacementMode::CountWeighted}) {
        settings.container.mode = m;
        std::map<BlockTypeId, int> freq;
        constexpr int kDraws = 3000;
        for (int d = 0; d < kDraws; ++d) ++freq[pickAt(static_cast<std::uint64_t>(d))];
        const double n = kDraws;
        if (m == PlacementMode::RandomSlot)
            CHECK(std::abs(freq[brick] / n - 1.0 / 3) < 0.04 && std::abs(freq[stone] / n - 1.0 / 3) < 0.04 &&
                  std::abs(freq[dirt] / n - 1.0 / 3) < 0.04);
        else
            CHECK(std::abs(freq[brick] / n - 0.6) < 0.04 && std::abs(freq[stone] / n - 0.3) < 0.04 &&
                  std::abs(freq[dirt] / n - 0.1) < 0.03);
    }
    // Commit advances the state only when the world changed; the preview
    // after a commit is the next draw.
    settings.container.mode = PlacementMode::InOrder;
    settings.container.draws = 0;
    {
        ChunkWorld g(registry());
        g.apply({{{{4, 4, 0}, registry().state(stone, orient::kIdentity)}}});
        const auto t = aim(g, {4.5, 4.5, 6.0}, {4.5, 4.5, 1.0});
        const PlannedEdit first = planEdit(&box, false, t, ctx, g);
        CHECK(first.edits.edits.size() == 1 && testStates().type(first.edits.edits[0].state) == brick);
        afterCommit(&box, false, first, g.apply(first.edits).changed, settings, registry().states());
        CHECK(settings.container.draws == 1);
        const auto t2 = aim(g, {4.5, 4.5, 6.0}, {4.5, 4.5, 2.0});  // now on top of the brick
        const PlannedEdit second = planEdit(&box, false, t2, ctx, g);
        CHECK(testStates().type(second.edits.edits[0].state) == stone);
        afterCommit(&box, false, second, 0, settings, registry().states());  // nothing changed: no roll used
        CHECK(settings.container.draws == 1);
    }
    // Match target ignores the state: the targeted block (stone) is in the
    // container → stone, every draw; take it out → nothing at all.
    settings.container.mode = PlacementMode::MatchTarget;
    for (std::uint64_t d = 0; d < 5; ++d) CHECK(pickAt(d) == stone);
    settings.container.slots[1] = {typeOf("log"), 30};
    CHECK(pickAt(0) == kAirType);
    CHECK(placementHash({1, 2, 3}, 1, 7) == placementHash({1, 2, 3}, 1, 7));
    CHECK(placementHash({1, 2, 3}, 1, 7) != placementHash({1, 2, 3}, 1, 8));
    CHECK(placementHash({1, 2, 3}, 1, 7) != placementHash({1, 2, 4}, 1, 7));
}

// v0.1.8: the container patch (N×N), orthogonal to the placement modes,
// and consumption (the survival rule).
void testContainerPatch() {
    ChunkWorld w(registry());
    EditSet floor;
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x) floor.edits.push_back({{x, y, 0}, registry().state(typeOf("stone"), orient::kIdentity)});
    w.apply(floor);
    const BlockTypeId brick = typeOf("brick"), stone = typeOf("stone"), dirt = typeOf("dirt");
    ToolSettings settings;
    settings.container.slots = {{{brick, 60}, {stone, 30}, {dirt, 10}}};
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const Item box = Item::container();
    const WorldPos center{12.5, 12.5, 1.0};
    const auto target = aim(w, {12.4, 10.0, 9.0}, center);
    // Every mode at every N: pure, the patch is N×N against the face, and
    // preview equals commit.
    for (int m = 0; m < kPlacementModeCount; ++m)
        for (int n : {1, 2, 3, 5}) {
            settings.container.mode = static_cast<PlacementMode>(m);
            settings.container.patch = n;
            settings.container.draws = 17;
            const PlannedEdit a = planEdit(&box, false, target, ctx, w);
            CHECK(a.edits == planEdit(&box, false, target, ctx, w).edits);
            CHECK(a.edits.edits.size() == static_cast<std::size_t>(n * n) && a.unaffordable.empty());
            for (const Edit& e : a.edits.edits) CHECK(e.cell.z == 1);
            checkPureAndCommits(w, &box, false, target, ctx);
        }
    // N = 1 is v0.1.7 exactly: one cell; in order = slot draws mod 3;
    // random slot / count-weighted = the v0.1.7 hash of (cell, id, draws).
    settings.container.patch = 1;
    const CellPos above{12, 12, 1};
    for (std::uint64_t d = 0; d < 40; ++d) {
        settings.container.draws = d;
        const std::uint64_t h = placementHash(above, settings.container.id, d);
        settings.container.mode = PlacementMode::InOrder;
        CHECK(testStates().type(planEdit(&box, false, target, ctx, w).edits.edits.at(0).state) == settings.container.slots[d % 3].type);
        settings.container.mode = PlacementMode::RandomSlot;
        CHECK(testStates().type(planEdit(&box, false, target, ctx, w).edits.edits.at(0).state) == settings.container.slots[h % 3].type);
        settings.container.mode = PlacementMode::CountWeighted;
        const std::uint64_t u = h % 100;
        const BlockTypeId expect = u < 60 ? brick : (u < 90 ? stone : dirt);
        CHECK(testStates().type(planEdit(&box, false, target, ctx, w).edits.edits.at(0).state) == expect);
    }
    // A patch's cells differ (the cell is in the hash), and in order
    // continues the cursor cell by cell; a commit advances draws by the
    // number placed.
    settings.container.patch = 3;
    settings.container.draws = 5;
    settings.container.mode = PlacementMode::InOrder;
    const PlannedEdit ordered = planEdit(&box, false, target, ctx, w);
    for (std::size_t i = 0; i < ordered.edits.edits.size(); ++i)
        CHECK(testStates().type(ordered.edits.edits[i].state) == settings.container.slots[(5 + i) % 3].type);
    {
        ChunkWorld g(registry());
        g.apply(floor);
        afterCommit(&box, false, ordered, g.apply(ordered.edits).changed, settings, registry().states());
        CHECK(settings.container.draws == 5 + 9);
    }
    // Consumption: a 5×5 patch from a container of 6 brick / 3 stone / 1
    // dirt places exactly the 10 it can afford — exactly 6/3/1, a bag drawn
    // without replacement — weights each pick by the counts left at that
    // cell, and marks the other 15.
    settings.container.slots = {{{brick, 6}, {stone, 3}, {dirt, 1}}};
    settings.container.consume = true;
    settings.container.patch = 5;
    settings.container.mode = PlacementMode::CountWeighted;
    settings.container.draws = 3;
    const PlannedEdit bag = planEdit(&box, false, target, ctx, w);
    CHECK(bag.edits.edits.size() == 10 && bag.unaffordable.size() == 15 && bag.fill == 10);
    std::map<BlockTypeId, int> placed;
    std::array<std::uint32_t, 3> left{6, 3, 1};
    for (const Edit& e : bag.edits.edits) {
        ++placed[testStates().type(e.state)];
        // Replay the pick from the counts left at this cell.
        std::uint64_t total = left[0] + left[1] + left[2];
        std::uint64_t u = placementHash(e.cell, settings.container.id, 3) % total;
        std::size_t k = 0;
        while (u >= left[k]) u -= left[k++];
        CHECK(testStates().type(e.state) == settings.container.slots[k].type);
        --left[k];
    }
    CHECK(placed[brick] == 6 && placed[stone] == 3 && placed[dirt] == 1);
    // In order and random slot skip an emptied type.
    settings.container.mode = PlacementMode::InOrder;
    const PlannedEdit cycle = planEdit(&box, false, target, ctx, w);
    std::map<BlockTypeId, int> cycled;
    for (const Edit& e : cycle.edits.edits) ++cycled[testStates().type(e.state)];
    CHECK(cycle.edits.edits.size() == 10 && cycled[brick] == 6 && cycled[stone] == 3 && cycled[dirt] == 1);
    // The commit uses the counts up; then the container is empty.
    {
        ChunkWorld g(registry());
        g.apply(floor);
        afterCommit(&box, false, bag, g.apply(bag.edits).changed, settings, registry().states());
        CHECK(settings.container.slots[0].count == 0 && settings.container.slots[1].count == 0 &&
              settings.container.slots[2].count == 0);
        CHECK(settings.container.draws == 3 + 10);
        const PlannedEdit none = planEdit(&box, false, target, ctx, g);
        CHECK(none.edits.edits.empty() && none.action.find("empty") != std::string::npos);
    }
    // Match target with consumption: only the matching type, until it runs out.
    settings.container.slots = {{{brick, 6}, {stone, 4}, {dirt, 1}}};
    settings.container.mode = PlacementMode::MatchTarget;
    settings.container.patch = 3;
    const PlannedEdit match = planEdit(&box, false, target, ctx, w);
    CHECK(match.edits.edits.size() == 4 && match.unaffordable.size() == 5);
    for (const Edit& e : match.edits.edits) CHECK(testStates().type(e.state) == stone);
}

// Shift+click (input/per-hand-use): only the wrench has a secondary action;
// every other item treats Shift+click as a click (owner, 2026-11-05: X+Shift
// placing did nothing).
void testSecondaryFallsBack() {
    ChunkWorld w(registry());
    w.apply({{{{4, 4, 0}, registry().state(typeOf("furnace"), orient::kIdentity)}}});  // oriented: every turn shows
    ToolSettings settings;
    settings.shovelFill = typeOf("dirt");
    settings.container.slots = {{{typeOf("brick"), 6}, {typeOf("stone"), 3}, {typeOf("dirt"), 1}}};
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const auto t = aim(w, {4.5, 2.5, 3.0}, {4.5, 4.5, 0.5});
    for (const Item& item : {Item::blockStack(typeOf("furnace")), Item::pick(), Item::shovel(), Item::container()}) {
        CHECK(!hasSecondaryAction(&item));
        const PlannedEdit a = planEdit(&item, false, t, ctx, w), b = planEdit(&item, true, t, ctx, w);
        CHECK(a.edits == b.edits && a.action == b.action);
    }
    CHECK(!hasSecondaryAction(nullptr) && planEdit(nullptr, true, t, ctx, w).edits == planEdit(nullptr, false, t, ctx, w).edits);
    const Item wrench = Item::wrench();
    CHECK(hasSecondaryAction(&wrench) && !(planEdit(&wrench, true, t, ctx, w).edits == planEdit(&wrench, false, t, ctx, w).edits));
    // A container's Shift+click commits like a click: draws advance.
    const Item box = Item::container();
    const PlannedEdit p = planEdit(&box, true, t, ctx, w);
    afterCommit(&box, true, p, w.apply(p.edits).changed, settings, registry().states());
    CHECK(settings.container.draws == 1);
}


}  // namespace

void runToolsTests() {
    testToolsPureAndCommit();
    testShovel();
    testWrench();
    testContainerModes();
    testContainerPatch();
    testSecondaryFallsBack();
}
