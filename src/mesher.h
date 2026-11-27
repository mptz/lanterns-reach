#pragma once
// Which mesher builds a chunk's mesh (world/chunk-meshing, versions.md
// v0.2.3; the scenario's `render.mesher`): `Greedy` (the default) is the
// bitmask mesher with coplanar faces of equal appearance merged; `Unmerged`
// is the same path with every face its own quad, to separate culling bugs
// from merging bugs; `PerCell` is the reference rule over any representation.

#include <optional>
#include <string_view>

enum class Mesher { Greedy, Unmerged, PerCell };

constexpr const char* mesherName(Mesher m) {
    switch (m) {
        case Mesher::Greedy: return "greedy";
        case Mesher::Unmerged: return "unmerged";
        case Mesher::PerCell: return "per-cell";
    }
    return "?";
}

constexpr std::optional<Mesher> parseMesher(std::string_view name) {
    for (Mesher m : {Mesher::Greedy, Mesher::Unmerged, Mesher::PerCell})
        if (name == mesherName(m)) return m;
    return std::nullopt;
}
