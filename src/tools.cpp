#include "tools.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <cmath>
#include <vector>

#include "placement.h"

namespace {

using orient::Rot;

// The shovel (tools/catalog, "shovel footprint and reach": a v0.1.7 build
// choice the owner called reasonable, to UX-test once the player has a
// body): 5×5 columns, each scanned at most 8 cells above the level (cut) and
// 8 at or below it (fill), so the footprint stays bounded.
constexpr int kShovelRadius = 2;
constexpr int kShovelReach = 8;

std::string blockName(const BlockRegistry& r, BlockTypeId t) { return r.def(t).name; }

}  // namespace

const char* placementModeName(PlacementMode m) {
    switch (m) {
        case PlacementMode::InOrder: return "in order";
        case PlacementMode::RandomSlot: return "random slot";
        case PlacementMode::CountWeighted: return "count-weighted";
        case PlacementMode::MatchTarget: return "match target";
    }
    return "?";
}

// The soft materials the shovel digs and places (tools/catalog: "soft
// materials (dirt, sand, gravel, clay)"; grass is dirt with a top). By name
// until block data carries a material class.
constexpr std::array<std::string_view, 4> kSoftMaterials{"dirt", "grass", "sand", "gravel"};

bool isSoft(const BlockRegistry& registry, BlockTypeId t) {
    if (t == BlockState::kAirType) return false;
    const std::string& n = registry.def(t).name;
    return std::find(kSoftMaterials.begin(), kSoftMaterials.end(), n) != kSoftMaterials.end();
}

orient::Rot inferredOrientation(BlockTypeId type, const ToolTarget& t, const BlockRegistry& registry) {
    const glm::ivec3 n = t.hit.normal;
    const glm::ivec3 f = placement::snapPerpendicular(t.eye - t.hit.point, n);
    const WorldPos faceCenter = cellCenter(t.hit.cell) + glm::dvec3(n) * 0.5;
    const glm::dvec2 uv = placement::faceCoords(t.hit.point - faceCenter, n);
    return registry.state(type, placement::placementRotation(registry.def(type).symmetry, n, f, uv)).orientation;
}

orient::Rot placementOrientation(BlockTypeId type, const ToolTarget& t, const ToolContext& ctx) {
    if (const auto it = ctx.latch.find(type); it != ctx.latch.end()) return it->second;
    return inferredOrientation(type, t, ctx.registry);
}

orient::Rot turnAbout(orient::Rot current, glm::ivec3 n, bool counterclockwise) {
    // Looking at the face means looking along −n; counterclockwise as seen
    // from there is the right-handed (positive) turn about n.
    const Rot turn = orient::rotationAbout(n, counterclockwise ? 90 : -90);
    return orient::group().compose[turn][current];
}

std::uint64_t placementHash(const CellPos& cell, std::uint32_t containerId, std::uint64_t draws) {
    auto mix = [](std::uint64_t z) {  // splitmix64 finalizer
        z += 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    std::uint64_t h = mix(static_cast<std::uint64_t>(cell.x));
    h = mix(h ^ static_cast<std::uint64_t>(cell.y));
    h = mix(h ^ static_cast<std::uint64_t>(cell.z));
    h = mix(h ^ containerId);
    return mix(h ^ draws);
}

bool hasSecondaryAction(const Item* item) { return item && item->kind == Item::Kind::Wrench; }

void afterCommit(const Item* item, bool secondary, const PlannedEdit& plan, std::size_t changed, ToolSettings& settings) {
    (void)secondary;  // a container has no secondary action, so Shift+click commits like a click
    if (!item || item->kind != Item::Kind::Container || changed == 0) return;
    ContainerState& box = settings.container;
    box.draws += plan.fill;
    if (!box.consume) return;
    for (const Edit& e : plan.edits.edits)
        for (ContainerSlot& s : box.slots)
            if (s.type == e.state.type && s.count > 0) {
                --s.count;
                break;
            }
}

bool rotateGhost(BlockTypeId type, const ToolTarget& t, bool counterclockwise, const BlockRegistry& registry,
                 OrientationLatch& latch) {
    if (t.hit.normal == glm::ivec3(0)) return false;
    const auto it = latch.find(type);
    const Rot current = it != latch.end() ? it->second : inferredOrientation(type, t, registry);
    latch[type] = registry.state(type, turnAbout(current, t.hit.normal, counterclockwise)).orientation;
    return true;
}

glm::ivec3 nearestAxis(const glm::dvec3& v) {
    int a = 0;
    for (int i = 1; i < 3; ++i)
        if (std::abs(v[i]) > std::abs(v[a])) a = i;
    glm::ivec3 out(0);
    out[a] = v[a] >= 0.0 ? 1 : -1;
    return out;
}

glm::ivec3 tipAxis(glm::ivec3 n, const glm::dvec3& cameraRight) {
    const glm::dvec3 nd(n);
    const glm::dvec3 r = cameraRight - nd * (cameraRight.x * nd.x + cameraRight.y * nd.y + cameraRight.z * nd.z);
    if (r.x * r.x + r.y * r.y + r.z * r.z < 1e-12) {
        // Screen-right along n (looking straight along a face's plane): any
        // axis perpendicular to n, the first of +x, +y, +z.
        for (int a = 0; a < 3; ++a)
            if (n[a] == 0) {
                glm::ivec3 out(0);
                out[a] = 1;
                return out;
            }
    }
    return nearestAxis(r);
}

void tipGhost(BlockTypeId type, const ToolTarget& t, const glm::dvec3& cameraRight, bool away,
              const BlockRegistry& registry, OrientationLatch& latch) {
    const auto it = latch.find(type);
    const Rot current = it != latch.end() ? it->second : inferredOrientation(type, t, registry);
    const glm::ivec3 axis = t.hit.normal == glm::ivec3(0) ? nearestAxis(cameraRight) : tipAxis(t.hit.normal, cameraRight);
    // A right-handed quarter turn about screen-right brings the top toward
    // the viewer, so "away" is the negative turn: turnAbout's clockwise.
    latch[type] = registry.state(type, turnAbout(current, axis, /*counterclockwise=*/!away)).orientation;
}

PlannedEdit planEdit(const Item* item, bool secondary, const std::optional<ToolTarget>& target,
                     const ToolContext& ctx, const WorldQuery& world) {
    PlannedEdit out;
    const BlockRegistry& reg = ctx.registry;
    if (!target) {
        out.action = "nothing in reach";
        return out;
    }
    // An item without a secondary action treats Shift+click as its primary
    // (input/per-hand-use), so Shift never turns a click into nothing.
    if (!hasSecondaryAction(item)) secondary = false;
    const RayHit& hit = target->hit;
    const glm::ivec3 n = hit.normal;
    const Item::Kind kind = item ? item->kind : Item::Kind::Pick;  // bare hand breaks like the pick
    const char* breaker = item ? "" : " (bare hand)";

    switch (kind) {
        case Item::Kind::Pick: {
            out.edits.edits.push_back({hit.cell, BlockState::air()});
            out.cut = 1;
            out.action = "break " + blockName(reg, hit.state.type) + breaker;
            return out;
        }
        case Item::Kind::Block: {
            const CellPos cell = hit.cell + n;
            if (n == glm::ivec3(0) || !world.editable(cell) || !world.probe(cell).isAir()) {
                out.action = "place " + blockName(reg, item->block) + " (no room)";
                return out;
            }
            out.edits.edits.push_back({cell, {item->block, placementOrientation(item->block, *target, ctx)}});
            out.fill = 1;
            out.action = "place " + blockName(reg, item->block);
            return out;
        }
        case Item::Kind::Wrench: {
            if (n == glm::ivec3(0)) {
                out.action = "wrench: no face";
                return out;
            }
            const BlockState turned = reg.state(hit.state.type, turnAbout(hit.state.orientation, n, secondary));
            out.edits.edits.push_back({hit.cell, turned});
            out.action = std::string("wrench: turn ") + blockName(reg, hit.state.type) + (secondary ? " ccw" : " cw") +
                         (turned == hit.state ? " (no change: its own symmetry)" : "");
            return out;
        }
        case Item::Kind::Shovel: {
            const std::int64_t h = hit.cell.z;
            const BlockTypeId fill = ctx.settings.shovelFill;
            const bool canFill = isSoft(reg, fill);
            for (int dy = -kShovelRadius; dy <= kShovelRadius; ++dy)
                for (int dx = -kShovelRadius; dx <= kShovelRadius; ++dx) {
                    const std::int64_t x = hit.cell.x + dx, y = hit.cell.y + dy;
                    for (int dz = 1; dz <= kShovelReach; ++dz) {  // above h: cut soft, leave rock
                        const CellPos c{x, y, h + dz};
                        const BlockState s = world.probe(c);
                        if (s.isAir() || !world.editable(c)) continue;
                        if (isSoft(reg, s.type)) {
                            out.edits.edits.push_back({c, BlockState::air()});
                            ++out.cut;
                        } else {
                            ++out.blocked;
                        }
                    }
                    for (int dz = 0; dz < kShovelReach && canFill; ++dz) {  // at/below h: fill down to the first solid
                        const CellPos c{x, y, h - dz};
                        if (!world.editable(c) || !world.probe(c).isAir()) break;
                        out.edits.edits.push_back({c, {fill, orient::kIdentity}});
                        ++out.fill;
                    }
                }
            out.action = "shovel: fill " + std::to_string(out.fill) + " / cut " + std::to_string(out.cut) +
                         (out.blocked ? " / rock left " + std::to_string(out.blocked) : "") +
                         (canFill ? "" : " (fill block isn't soft)");
            return out;
        }
        case Item::Kind::Container: {
            const ContainerState& box = ctx.settings.container;
            // The counts this plan draws from: a copy, used up cell by cell
            // when consuming (survival), fixed weights otherwise (creative).
            std::array<ContainerSlot, 3> counts = box.slots;
            auto available = [&] {
                std::vector<std::size_t> slots;
                for (std::size_t i = 0; i < counts.size(); ++i)
                    if (counts[i].type != BlockState::kAirType && counts[i].count > 0) slots.push_back(i);
                return slots;
            };
            const std::string label = std::string("container (") + placementModeName(box.mode) + "): ";
            if (n == glm::ivec3(0) || available().empty()) {
                out.action = label + (available().empty() ? "empty" : "no face");
                return out;
            }
            std::optional<BlockTypeId> only;
            if (box.mode == PlacementMode::MatchTarget) {
                for (std::size_t i : available())
                    if (counts[i].type == hit.state.type) only = counts[i].type;
                if (!only) {
                    out.action = label + "holds no " + blockName(reg, hit.state.type);
                    return out;
                }
            }
            // The N×N patch in the layer against the clicked face, centred on
            // the target, in a fixed scan order (rows along U, then R); N = 1
            // is the one cell against the face.
            const glm::ivec3 R = placement::faceRight(n), U = placement::faceUp(n);
            const int N = std::max(1, box.patch), lo = -(N - 1) / 2;
            const CellPos base = hit.cell + n;
            for (int b = lo; b < lo + N; ++b)
                for (int a = lo; a < lo + N; ++a) {
                    const CellPos c = base + R * a + U * b;
                    if (!world.editable(c) || !world.probe(c).isAir()) continue;
                    const std::vector<std::size_t> avail = available();
                    std::optional<std::size_t> slot;
                    if (box.mode == PlacementMode::MatchTarget) {
                        for (std::size_t i : avail)
                            if (counts[i].type == *only) slot = i;
                    } else if (!avail.empty()) {
                        const std::uint64_t h = placementHash(c, box.id, box.draws);
                        switch (box.mode) {
                            case PlacementMode::InOrder: slot = avail[(box.draws + out.fill) % avail.size()]; break;
                            case PlacementMode::RandomSlot: slot = avail[h % avail.size()]; break;
                            case PlacementMode::CountWeighted: {
                                // Weighted by the counts left at this cell: with
                                // consumption, drawing from a bag without replacement.
                                std::uint64_t total = 0;
                                for (std::size_t i : avail) total += counts[i].count;
                                std::uint64_t u = h % total;
                                for (std::size_t i : avail) {
                                    if (u < counts[i].count) {
                                        slot = i;
                                        break;
                                    }
                                    u -= counts[i].count;
                                }
                                break;
                            }
                            case PlacementMode::MatchTarget: break;
                        }
                    }
                    if (!slot) {  // the container can't afford this cell
                        out.unaffordable.push_back(c);
                        continue;
                    }
                    const BlockTypeId type = counts[*slot].type;
                    if (box.consume) --counts[*slot].count;
                    out.edits.edits.push_back({c, {type, placementOrientation(type, *target, ctx)}});
                    ++out.fill;
                }
            if (out.fill == 1) {
                out.action = label + "place " + blockName(reg, out.edits.edits.front().state.type);
            } else {
                out.action = label + "place " + std::to_string(out.fill);
                if (out.fill == 0 && out.unaffordable.empty()) out.action = label + "no room";
            }
            if (!out.unaffordable.empty()) out.action += " (" + std::to_string(out.unaffordable.size()) + " short)";
            return out;
        }
    }
    return out;
}
