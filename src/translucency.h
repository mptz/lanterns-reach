#pragma once
// Test control for render/transparency's central claim (no sorting needed):
// reorder the FACES of the translucent mesh's index buffer before upload.
// The GPU blends in index-buffer order within a draw, so this permutes the
// order fragments arrive at each pixel; both translucent passes draw from
// the same buffer.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "world_query.h"

struct FaceOrder {
    enum class Kind : std::uint8_t { AsBuilt, Forward, Reverse, Shuffle } kind = Kind::AsBuilt;
    std::uint64_t seed = 0;  // Shuffle

    // "forward" | "reverse" | "shuffle:<seed>" (nullopt if malformed)
    static std::optional<FaceOrder> parse(std::string_view s);
    std::string describe() const;
};

// Forward = nearest face first from `eye` (by face center), Reverse =
// farthest first (what naive "over" blending needs), Shuffle = seeded
// Fisher–Yates (std::mt19937_64, portable). AsBuilt leaves the mesh alone.
void orderTranslucentFaces(WorldMesh& mesh, const FaceOrder& order, const WorldPos& eye);
