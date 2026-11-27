#pragma once
// Face appearance ids (render/block-textures, world/chunk-meshing): the merge
// key. A face's appearance is its vertex payload, everything a MeshVertex
// carries except position: texture layer, material id and the packed turn,
// set kind and random tile count (the normal follows from the direction). The
// fragment shader reads nothing else besides position, so faces of equal
// payload and direction merge invisibly. An appearance id is a dense number
// per distinct payload, so merging compares ids, never payloads.
//
// A stand-in until v0.2.4's state table holds them: every registry type times
// its 24 orientations, six faces each, computed through faceTexture once when
// the table is built. Per-mesh interning is never written.

#include <compare>
#include <cstdint>
#include <vector>

#include "blocks.h"

struct FacePayload {
    std::uint32_t layer = 0, material = 0, texInfo = 0;
    constexpr auto operator<=>(const FacePayload&) const = default;
};

class AppearanceTable {
public:
    explicit AppearanceTable(const BlockRegistry& registry);

    // The appearance id of world face `face` of a block in `state` (not air).
    std::uint32_t id(const BlockState& state, orient::Face face) const {
        return ids_[(static_cast<std::size_t>(state.type) * orient::kRotCount + state.orientation) * orient::kFaceCount + face];
    }
    const FacePayload& payload(std::uint32_t id) const { return payloads_[id]; }
    std::size_t distinct() const { return payloads_.size(); }

private:
    std::vector<std::uint32_t> ids_;
    std::vector<FacePayload> payloads_;
};
