#pragma once
// A face's appearance (render/block-textures, world/chunk-meshing): its vertex
// payload, everything a MeshVertex carries except position: texture layer,
// material id and the packed turn, set kind and random tile count (the normal
// follows from the direction). The fragment shader reads nothing else besides
// position, so faces of equal payload and direction merge invisibly. The state
// table interns each distinct payload as an appearance id, six per state
// (state_table.h); appearance ids are derived, never persisted.

#include <compare>
#include <cstdint>

struct FacePayload {
    std::uint32_t layer = 0, material = 0, texInfo = 0;
    constexpr auto operator<=>(const FacePayload&) const = default;
};
