// Tests: placement. Registered through sandbox_tests.cpp.

#include "test_support.h"

namespace {

using namespace orient;

// --- placement rule ---------------------------------------------------------------


void testPlacementInputs() {
    using namespace placement;
    CHECK(faceRight({0, 0, 1}) == glm::ivec3(1, 0, 0) && faceUp({0, 0, 1}) == glm::ivec3(0, 1, 0));
    CHECK(faceRight({1, 0, 0}) == glm::ivec3(0, 1, 0) && faceUp({1, 0, 0}) == glm::ivec3(0, 0, 1));
    CHECK(snapPerpendicular({0.2, -3.0, 5.0}, {0, 0, 1}) == glm::ivec3(0, -1, 0));
    CHECK(snapPerpendicular({5.0, 0.1, 0.3}, {1, 0, 0}) == glm::ivec3(0, 0, 1));
    CHECK(snapPerpendicular({0.0, 0.0, 5.0}, {0, 0, 1}) == glm::ivec3(1, 0, 0));   // degenerate: first ⊥ axis
    CHECK(snapPerpendicular({2.0, 2.0, 0.0}, {0, 0, 1}) == glm::ivec3(1, 0, 0));   // exact tie: x first
    const glm::dvec2 uv = faceCoords({0.4, 0.4, 0.0}, {0, 0, 1});
    CHECK(near(uv.x, 0.9) && near(uv.y, 0.9));
}

// For every (n, f, uv) combination: the placed rotation's feature lands
// where the rule says, and the number of distinct canonical results is the
// type's orientation count.
void testPlacementRule() {
    using placement::placementRotation;
    using placement::placeOrientation;
    const Group& g = group();
    const glm::dvec2 uvs[] = {{0.1, 0.1}, {0.9, 0.1}, {0.1, 0.9}, {0.9, 0.9}, {0.5, 0.05}, {0.95, 0.5}, {0.5, 0.95}, {0.05, 0.5}};
    struct Case {
        Symmetry s;
        std::size_t classes;
    };
    for (const Case c : {Case{Symmetry::Uniform, 1}, {Symmetry::Column, 3}, {Symmetry::Pointed, 6}, {Symmetry::Brick, 6},
                         {Symmetry::TwistedColumn, 6}, {Symmetry::PointedBrick, 12}, {Symmetry::Oriented, 24},
                         {Symmetry::Corner, 8}, {Symmetry::Diagonal, 4}, {Symmetry::Tetrahedral, 2}, {Symmetry::Edge, 12}}) {
        std::set<Rot> seen;
        for (const glm::ivec3 n : kNormals)
            for (const glm::ivec3 f : kNormals) {
                if (f == n || f == -n) continue;
                for (const glm::dvec2 uv : uvs) {
                    const Rot r = placementRotation(c.s, n, f, uv);
                    const Mat3i& m = g.matrix[r];
                    const glm::ivec3 R = placement::faceRight(n), U = placement::faceUp(n);
                    switch (c.s) {
                        case Symmetry::Uniform: CHECK(r == kIdentity); break;
                        case Symmetry::Column:
                        case Symmetry::Pointed: CHECK(rotateVec(m, {0, 0, 1}) == n); break;
                        case Symmetry::Corner:
                        case Symmetry::Diagonal:
                        case Symmetry::Tetrahedral:
                            CHECK(rotateVec(m, {1, -1, 1}) == -n + R * (uv.x >= 0.5 ? 1 : -1) + U * (uv.y >= 0.5 ? 1 : -1));
                            break;
                        case Symmetry::Edge: {
                            const glm::ivec3 e = rotateVec(m, {0, -1, 1});
                            CHECK(e.x * n.x + e.y * n.y + e.z * n.z == -1);  // an edge of the clicked face
                            break;
                        }
                        default:  // up along n, front toward the player
                            CHECK(rotateVec(m, {0, 0, 1}) == n && rotateVec(m, {0, -1, 0}) == f);
                    }
                    seen.insert(placeOrientation(c.s, n, f, uv));
                }
            }
        CHECK(seen.size() == c.classes);
    }
    // Spot checks: logs lie along the clicked normal (z, y, x axes → 0, 8, 16).
    CHECK(placeOrientation(Symmetry::Column, {0, 0, 1}, {0, -1, 0}, {0.5, 0.5}) == 0);
    CHECK(placeOrientation(Symmetry::Column, {0, 1, 0}, {1, 0, 0}, {0.5, 0.5}) == 8);
    CHECK(placeOrientation(Symmetry::Column, {-1, 0, 0}, {0, 0, 1}, {0.5, 0.5}) == 16);
    // A furnace on the floor faces the player; on a wall it lies on its back
    // (top out of the wall), front toward the player (the rule as written).
    CHECK(placeOrientation(Symmetry::Oriented, {0, 0, 1}, {0, -1, 0}, {0.5, 0.5}) == 0);
    CHECK(placeOrientation(Symmetry::Oriented, {0, 0, 1}, {1, 0, 0}, {0.5, 0.5}) == rotationAbout({0, 0, 1}, 90));
    const Rot wall = placeOrientation(Symmetry::Oriented, {0, -1, 0}, {0, 0, 1}, {0.5, 0.5});
    CHECK(rotateVec(g.matrix[wall], {0, 0, 1}) == glm::ivec3(0, -1, 0));
    CHECK(rotateVec(g.matrix[wall], {0, -1, 0}) == glm::ivec3(0, 0, 1));
    // Edge: nearest edge of the clicked +z face to a hit near its −U (−y) side.
    const Rot edge = placementRotation(Symmetry::Edge, {0, 0, 1}, {0, -1, 0}, {0.5, 0.05});
    CHECK(rotateVec(g.matrix[edge], {0, -1, 1}) == glm::ivec3(0, -1, -1));
}


// X ghost: once the wheel turns the ghost, the orientation it lands on is
// locked (in world frame) for placements of that type, even when it equals
// the inferred one, until X is released.
void testOrientationLatch() {
    ChunkWorld w(registry());
    w.apply({{{{4, 4, 0}, {typeOf("stone"), kIdentity}}}});
    const BlockTypeId furnace = typeOf("furnace");
    ToolSettings settings;
    OrientationLatch latch;
    const ToolContext ctx{registry(), settings, latch};
    const WorldPos eye{4.5, 1.0, 3.0};
    const auto t = aim(w, eye, {4.5, 4.5, 1.0});
    CHECK(t.has_value());
    if (!t) return;
    const Rot inferred = inferredOrientation(furnace, *t, registry());
    CHECK(rotateGhost(furnace, *t, false, registry(), latch));
    CHECK(latch.count(furnace) == 1 && latch.at(furnace) != inferred);
    const Item stack = Item::blockStack(furnace);
    CHECK(planEdit(&stack, false, t, ctx, w).edits.edits[0].state.orientation == latch.at(furnace));
    CHECK(rotateGhost(furnace, *t, true, registry(), latch));  // back to the inferred orientation …
    CHECK(latch.count(furnace) == 1 && latch.at(furnace) == inferred);  // … which is now LOCKED too
    // A different target would infer differently, but the lock holds.
    const auto t2 = aim(w, {7.5, 4.5, 3.0}, {4.5, 4.5, 1.0});
    CHECK(t2 && inferredOrientation(furnace, *t2, registry()) != inferred);
    if (t2) CHECK(planEdit(&stack, false, t2, ctx, w).edits.edits[0].state.orientation == inferred);
    latch.clear();  // X released
    if (t2) CHECK(planEdit(&stack, false, t2, ctx, w).edits.edits[0].state.orientation != inferred);
}

// From ONE fixed viewpoint, the wheel (turn about the clicked face's normal)
// and Shift+wheel (tip about screen-right made perpendicular to it) reach
// every orientation: all 3 of a log, all 24 of a furnace — on a floor, and
// on a wall seen obliquely, where raw screen-right would snap onto n.
void testGhostReachability() {
    ChunkWorld w(registry());
    w.apply({{{{4, 4, 0}, {typeOf("stone"), kIdentity}}, {{4, 4, 1}, {typeOf("stone"), kIdentity}}}});
    struct View {
        WorldPos eye, at;
        glm::dvec3 right;
    };
    const View views[] = {
        {{4.5, 1.0, 3.0}, {4.5, 4.5, 2.0}, {1.0, 0.0, 0.0}},                     // the top face (n = +z)
        {{6.5, 1.5, 1.5}, {5.0, 4.6, 1.5}, glm::normalize(glm::dvec3(1.0, 0.9, 0.0))},  // the +x wall, obliquely
    };
    for (const View& v : views) {
        const auto t = aim(w, v.eye, v.at);
        CHECK(t.has_value() && t->hit.normal != glm::ivec3(0));
        if (!t) continue;
        const glm::ivec3 tip = tipAxis(t->hit.normal, v.right);
        CHECK(tip.x * t->hit.normal.x + tip.y * t->hit.normal.y + tip.z * t->hit.normal.z == 0);  // ⊥ n, always
        for (const auto& [name, expected] : {std::pair{"log", 3u}, {"furnace", 24u}}) {
            const BlockTypeId type = typeOf(name);
            std::set<Rot> seen;
            std::vector<OrientationLatch> frontier(1);
            for (std::size_t i = 0; i < frontier.size() && i < 200; ++i)
                for (int op = 0; op < 4; ++op) {
                    OrientationLatch next = frontier[i];
                    if (op < 2) rotateGhost(type, *t, op == 1, registry(), next);
                    else tipGhost(type, *t, v.right, op == 2, registry(), next);
                    if (seen.insert(next.at(type)).second) frontier.push_back(next);
                }
            CHECK(seen.size() == expected);
        }
    }
    CHECK(nearestAxis({0.3, -0.9, 0.1}) == glm::ivec3(0, -1, 0));
    CHECK(tipAxis({1, 0, 0}, {1.0, 0.0, 0.0}) != glm::ivec3(1, 0, 0));  // degenerate: still ⊥ n
}

void testOrientationContext() {
    IntentionLayer layer(Hand::Right);
    // X held: the wheel turns the ghost; the mouse buttons still use the hands.
    layer.push({Device::Key, GLFW_KEY_X, true, false});
    layer.push({Device::Wheel, +1, true, false});
    layer.push({Device::Wheel, -1, true, false});
    layer.push({Device::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, true, false});
    IntentionFrame f = layer.process({});
    CHECK(f.orientHeld && f.fired.size() == 3);
    if (f.fired.size() == 3) {
        CHECK(f.fired[0].kind == IntentionKind::RotateGhost && f.fired[0].arg == +1);
        CHECK(f.fired[1].kind == IntentionKind::RotateGhost && f.fired[1].arg == -1);
        CHECK(f.fired[2].kind == IntentionKind::Use && f.fired[2].hand == Hand::Right);
    }
    CHECK(f.useHeld[1]);  // placing with X held
    CHECK(f.contexts.size() == 2 && f.contexts[1] == "orientation (X)");
    // Shift+wheel with X held tips the ghost (the second axis).
    layer.push({Device::Wheel, +1, true, true});
    const IntentionFrame tip = layer.process({});
    CHECK(tip.fired.size() == 1 && tip.fired[0].kind == IntentionKind::RotateGhost && tip.fired[0].secondary);
    // X released: the wheel is the hotbar again.
    layer.push({Device::MouseButton, GLFW_MOUSE_BUTTON_RIGHT, false, false});
    layer.push({Device::Key, GLFW_KEY_X, false, false});
    layer.push({Device::Wheel, -1, true, false});
    layer.push({Device::Key, GLFW_KEY_Q, true, false});
    layer.push({Device::Key, GLFW_KEY_M, true, false});
    f = layer.process({});
    CHECK(!f.orientHeld && f.fired.size() == 3);
    if (f.fired.size() == 3) {
        CHECK(f.fired[0].kind == IntentionKind::Scroll && f.fired[0].hand == Hand::Right);
        CHECK(f.fired[1].kind == IntentionKind::TogglePreview && f.fired[2].kind == IntentionKind::CycleMode);
    }
}


}  // namespace

void runPlacementTests() {
    testPlacementInputs();
    testPlacementRule();
    testOrientationLatch();
    testGhostReachability();
    testOrientationContext();
}
