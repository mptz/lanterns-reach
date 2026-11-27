#pragma once
// The v0.1 game glue: the world (through WorldQuery), hands and hotbars, tool
// settings and the orientation choice, and the click path — apply exactly
// the planned edit (input/preview-mode).

#include <array>
#include <optional>

#include "blocks.h"
#include "camera.h"
#include "hands.h"
#include "scenario.h"
#include "tools.h"
#include "world_query.h"

inline constexpr double kReach = 12.0;  // blocks (owner, 2026-10-30: revised from 16)

struct Game {
    WorldQuery& world;  // everything the game does goes through the seam
    const BlockRegistry& registry;
    Hands hands;
    std::array<UseRepeater, kHandCount> repeaters;
    ToolSettings settings;
    OrientationLatch latch;
    bool preview = false;  // Q (input/preview-mode)

    ToolContext context() const { return {registry, settings, latch}; }

    std::optional<ToolTarget> aim(const Camera& camera) const {
        const auto hit = world.raycast(camera.position, camera.forward(), kReach);
        if (!hit) return std::nullopt;
        return ToolTarget{*hit, camera.position};
    }

    // The one edit-set function per tool (tools.h): the preview draws it, the
    // HUD describes it, and a click applies it.
    PlannedEdit plan(Hand h, bool secondary, const Camera& camera) const {
        return planEdit(hands.bar(h).held(), secondary, aim(camera), context(), world);
    }

    // The click path: apply exactly the planned edit. Returns whether the
    // world changed.
    bool use(Hand h, bool secondary, const Camera& camera) {
        const PlannedEdit p = plan(h, secondary, camera);
        const std::size_t changed = world.apply(p.edits).changed;
        afterCommit(hands.bar(h).held(), secondary, p, changed, settings, registry.states());
        return changed > 0;
    }

    void pick(const Camera& camera) {
        if (const auto t = aim(camera)) hands.bar(hands.main()).pick(registry.states().type(t->hit.state));
    }

    const Item* mainItem() const { return hands.bar(hands.main()).held(); }
};

// The harness loadout (main hotbar: blocks and the container; off hotbar:
// the tools) and tool defaults, plus the scenario's tool settings.
Game makeGame(WorldQuery& world, const BlockRegistry& registry, const Scenario& scenario);
