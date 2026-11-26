#pragma once
// Tools as pure edit-set functions (input/preview-mode): every held item
// exposes ONE function, planEdit(tool state, target, world) → edit set. The
// preview draws that edit set; a click applies the same one through
// WorldQuery::apply. No tool has a separate preview path to drift.
//
// planEdit reads the world only through WorldQuery's bounded queries (probe,
// editable) over the tool's own small footprint (core/opaque-chunk-queries),
// never modifies it, and is deterministic: "random" container picks are a
// hash of (cell, container id, the container's draw count), never a live RNG.
// The draw count is tool state: it advances only when a placement commits
// (afterCommit), so the preview always shows the next pick exactly.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "blocks.h"
#include "hands.h"
#include "world_query.h"

enum class PlacementMode : std::uint8_t { InOrder, RandomSlot, CountWeighted, MatchTarget };
inline constexpr int kPlacementModeCount = 4;
const char* placementModeName(PlacementMode m);

struct ContainerSlot {
    BlockTypeId type = BlockState::kAirType;  // air = empty slot
    std::uint32_t count = 0;                  // weights for CountWeighted; not consumed in creative
};

// The one hardcoded container (inventory/hand-pointers): three slots, a
// placement mode (M cycles it) and a patch size N, orthogonal to the mode
// (v0.1.8): a click places the N×N patch against the clicked face (N = 1:
// one block). The randomness is temporal: `draws` counts the blocks this
// container has placed; it is the in-order cursor and, hashed with the cell
// and the container id, the RNG state (a counter-based RNG), so it is
// replicable and the client can predict what the server will compute.
// `consume` is the survival rule (each placed block uses up one of its
// slot's count; count-weighted picks weight by what is left), off in
// creative and switched on by tests and the F8 panel.
struct ContainerState {
    std::uint32_t id = 1;
    std::array<ContainerSlot, 3> slots{};
    PlacementMode mode = PlacementMode::InOrder;
    std::uint64_t draws = 0;
    int patch = 1;  // N
    bool consume = false;
};

struct ToolSettings {
    BlockTypeId shovelFill = BlockState::kAirType;  // soft materials only
    ContainerState container;
};

// The orientation choice (world/block-orientation): a placement orientation
// chosen with the X ghost, per block type, in world frame (canonical), used
// by placements of that type WHILE X IS HELD; the caller clears it when X is
// released (a quasimode: no hidden state outlives the key).
using OrientationLatch = std::map<BlockTypeId, orient::Rot>;

struct ToolContext {
    const BlockRegistry& registry;
    const ToolSettings& settings;
    const OrientationLatch& latch;
};

// What the tool is aimed at: the ray hit, and the eye it came from (the
// placement rule's "direction toward the player").
struct ToolTarget {
    RayHit hit;
    WorldPos eye;
};

struct PlannedEdit {
    EditSet edits;
    std::string action;  // the HUD's pending-action line, e.g. "place log", "shovel: fill 12 / cut 7"
    std::size_t fill = 0, cut = 0, blocked = 0;
    std::vector<CellPos> unaffordable;  // cells the container can't pay for (consumption); previewed, not placed
};

// Whether Shift+click does something different from a click (input/per-hand-
// use): only the wrench (counterclockwise). Every other item treats
// Shift+click as its primary action.
bool hasSecondaryAction(const Item* item);

// The one function per tool. `item` null = bare hand; `secondary` =
// Shift+click (the wrench turns counterclockwise; other items act as on a
// click).
PlannedEdit planEdit(const Item* item, bool secondary, const std::optional<ToolTarget>& target,
                     const ToolContext& ctx, const WorldQuery& world);

// Tool state after a commit of `plan`: a container's draw count advances by
// the blocks placed, and (consuming) their counts are used up — only when
// the placement changed the world (`changed` cells), so an empty or
// rejected edit set uses no roll and no pick is skipped unseen.
void afterCommit(const Item* item, bool secondary, const PlannedEdit& plan, std::size_t changed,
                 ToolSettings& settings);

// --- pieces, exposed for the X ghost and tests ----------------------------------

// Soft materials (the shovel digs and places only these): dirt, grass,
// sand, gravel (those present in the registry).
bool isSoft(const BlockRegistry& registry, BlockTypeId t);

// The placement orientation of `type` placed against the target: the
// latched orientation if `type` has one, else the placement rule
// (placement.h). Canonical.
orient::Rot inferredOrientation(BlockTypeId type, const ToolTarget& t, const BlockRegistry& registry);
orient::Rot placementOrientation(BlockTypeId type, const ToolTarget& t, const ToolContext& ctx);

// The wrench turn: 90° about world axis n — clockwise as seen looking at the
// face whose outward normal is n, or counterclockwise. Applied in world
// frame to `current`; the result is canonicalized by the caller.
orient::Rot turnAbout(orient::Rot current, glm::ivec3 n, bool counterclockwise);

// X ghost turns. Both depend only on the already-placed block face the
// ghost is placed against (the clicked support face, normal n) and the
// camera — never on the ghost's own faces (owner, 2026-11-03). Each records
// the result as the type's choice, even when it equals the inferred
// orientation: the choice stays locked until X is released.
//
// Wheel: turn about n — clockwise as seen looking at the clicked face, or
// counterclockwise. Returns false if there's no face (n = 0).
bool rotateGhost(BlockTypeId type, const ToolTarget& t, bool counterclockwise, const BlockRegistry& registry,
                 OrientationLatch& latch);

// Shift+wheel: tip about the tip axis — `away` = the top tips away from the
// viewer. The tip axis is screen-right with its n component removed, snapped
// to a lattice axis, so it's always perpendicular to n: with the wheel's
// axis, two perpendicular axes, so every orientation is reachable from any
// viewpoint.
void tipGhost(BlockTypeId type, const ToolTarget& t, const glm::dvec3& cameraRight, bool away,
              const BlockRegistry& registry, OrientationLatch& latch);
glm::ivec3 tipAxis(glm::ivec3 n, const glm::dvec3& cameraRight);

// The lattice axis nearest `v` (dominant component; ties x, then y, then z).
glm::ivec3 nearestAxis(const glm::dvec3& v);

// The deterministic pick: a 64-bit hash of (cell, container id, draw count).
std::uint64_t placementHash(const CellPos& cell, std::uint32_t containerId, std::uint64_t draws);
