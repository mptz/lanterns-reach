// Test-side stand-ins: a BlockState-like entry, the per-cell face "mesh",
// a voxel raycast, and content generation. Air and solidity are queries
// outside the Brick (the Brick knows nothing of either).
#pragma once

#include "brick.hpp"

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace proto::test {

// Stands in for today's BlockState: registry index plus canonical orientation.
struct State {
    static constexpr std::uint16_t kAirType = 0xFFFF;
    std::uint16_t type = kAirType;
    std::uint8_t orientation = 0;
    bool operator==(const State&) const = default;
};
inline std::uint64_t canonicalEncoding(const State& s) {
    return (std::uint64_t{s.type} << 8) | s.orientation;
}
inline bool isAir(const State& s) { return s.type == State::kAirType; }

inline constexpr std::uint32_t kOrientations = 24;
// The k-th state of the stand-in registry: 0 is air, then (type, orientation).
inline State stateK(std::uint32_t k) {
    if (k == 0) return State{};
    return State{static_cast<std::uint16_t>((k - 1) / kOrientations),
                 static_cast<std::uint8_t>((k - 1) % kOrientations)};
}

using Cells = std::vector<State>;

inline std::uint64_t oracleDigest(const Cells& cells) {
    std::uint64_t d = 0;
    for (std::uint32_t c = 0; c < kBrickCells; ++c)
        d = addMod(d, mulMod(digestW(c), stateHash(cells[c])));
    return d;
}

// The per-cell "mesh": every face of a non-air cell whose neighbour is air
// (outside the Brick is air), as (cell << 3 | dir, state encoding), in cell
// then direction order, so two meshes compare as vectors.
struct Face {
    std::uint32_t cellDir;
    std::uint64_t state;
    bool operator==(const Face&) const = default;
};

template <class Get>
std::vector<Face> meshFaces(Get&& get) {
    static constexpr int kD[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    std::vector<Face> out;
    for (std::uint32_t z = 0; z < kBrickEdge; ++z)
        for (std::uint32_t y = 0; y < kBrickEdge; ++y)
            for (std::uint32_t x = 0; x < kBrickEdge; ++x) {
                State s = get(cellIndex(x, y, z));
                if (isAir(s)) continue;
                for (std::uint32_t d = 0; d < 6; ++d) {
                    int nx = static_cast<int>(x) + kD[d][0], ny = static_cast<int>(y) + kD[d][1],
                        nz = static_cast<int>(z) + kD[d][2];
                    bool outside = nx < 0 || ny < 0 || nz < 0 || nx >= 64 || ny >= 64 || nz >= 64;
                    if (outside || isAir(get(cellIndex(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny),
                                                       static_cast<std::uint32_t>(nz)))))
                        out.push_back({(cellIndex(x, y, z) << 3) | d, canonicalEncoding(s)});
                }
            }
    return out;
}

struct Ray {
    double o[3];
    double d[3];
};
struct Hit {
    int cell = -1;
    int steps = 0;
    bool operator==(const Hit&) const = default;
};

// Amanatides-Woo DDA from an origin inside the Brick to the first non-air cell.
template <class Get>
Hit raycast(Get&& get, const Ray& r) {
    int i[3], step[3];
    double tMax[3], tDelta[3];
    for (int a = 0; a < 3; ++a) {
        i[a] = static_cast<int>(std::floor(r.o[a]));
        if (r.d[a] > 0) {
            step[a] = 1;
            tDelta[a] = 1.0 / r.d[a];
            tMax[a] = (std::floor(r.o[a]) + 1.0 - r.o[a]) * tDelta[a];
        } else if (r.d[a] < 0) {
            step[a] = -1;
            tDelta[a] = -1.0 / r.d[a];
            tMax[a] = (r.o[a] - std::floor(r.o[a])) * tDelta[a];
        } else {
            step[a] = 0;
            tDelta[a] = INFINITY;
            tMax[a] = INFINITY;
        }
    }
    for (int n = 0; n < 256; ++n) {
        if (i[0] < 0 || i[1] < 0 || i[2] < 0 || i[0] >= 64 || i[1] >= 64 || i[2] >= 64) return {-1, n};
        std::uint32_t c = cellIndex(static_cast<std::uint32_t>(i[0]), static_cast<std::uint32_t>(i[1]),
                                    static_cast<std::uint32_t>(i[2]));
        if (!isAir(get(c))) return {static_cast<int>(c), n};
        int a = tMax[0] < tMax[1] ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
        i[a] += step[a];
        tMax[a] += tDelta[a];
    }
    return {-1, 256};
}

// Content with exactly n distinct states (n <= 2^18), spatially structured
// (z bands of a few states plus scattered others, air above a ground line).
inline Cells makeContent(std::uint32_t n, std::mt19937_64& rng, std::vector<std::uint32_t>& poolOut) {
    std::vector<std::uint32_t> pool;
    std::uint32_t base = static_cast<std::uint32_t>(rng() % 1000);
    for (std::uint32_t k = 0; k < n; ++k) pool.push_back(k == 0 && (rng() & 1) ? 0 : base + 1 + k);
    Cells cells(kBrickCells);
    std::uint32_t ground = 16 + static_cast<std::uint32_t>(rng() % 32);
    for (std::uint32_t c = 0; c < kBrickCells; ++c) {
        std::uint32_t z = c / (kBrickEdge * kBrickEdge);
        std::uint32_t pick;
        if (z > ground && pool[0] == 0) pick = 0;
        else if (rng() % 8 != 0) pick = std::min<std::uint32_t>(z % 4, n - 1);
        else pick = static_cast<std::uint32_t>(rng() % n);
        cells[c] = stateK(pool[pick]);
    }
    // Every state appears at least once, at random cells.
    std::vector<std::uint32_t> perm(kBrickCells);
    for (std::uint32_t c = 0; c < kBrickCells; ++c) perm[c] = c;
    for (std::uint32_t k = 0; k < n; ++k) {
        std::uint32_t j = k + static_cast<std::uint32_t>(rng() % (kBrickCells - k));
        std::swap(perm[k], perm[j]);
        cells[perm[k]] = stateK(pool[k]);
    }
    poolOut = pool;
    return cells;
}

}  // namespace proto::test
