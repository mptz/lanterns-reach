#include "translucency.h"

#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <random>
#include <vector>

#include "packed_quad.h"

std::optional<FaceOrder> FaceOrder::parse(std::string_view s) {
    if (s == "forward") return FaceOrder{Kind::Forward};
    if (s == "reverse") return FaceOrder{Kind::Reverse};
    if (s.starts_with("shuffle:") && s.size() > 8) {
        const std::string digits(s.substr(8));
        char* end = nullptr;
        const unsigned long long seed = std::strtoull(digits.c_str(), &end, 10);
        if (end && *end == '\0') return FaceOrder{Kind::Shuffle, seed};
    }
    return std::nullopt;
}

std::string FaceOrder::describe() const {
    switch (kind) {
        case Kind::AsBuilt: return "as built";
        case Kind::Forward: return "forward";
        case Kind::Reverse: return "reverse";
        case Kind::Shuffle: return "shuffle:" + std::to_string(seed);
    }
    return "?";
}

void orderTranslucentFaces(WorldMesh& mesh, const FaceOrder& order, const WorldPos& eye) {
    if (order.kind == FaceOrder::Kind::AsBuilt) return;
    const std::size_t faces = mesh.translucentQuads.size();
    std::vector<std::size_t> perm(faces);
    std::iota(perm.begin(), perm.end(), std::size_t{0});
    if (order.kind == FaceOrder::Kind::Shuffle) {
        std::mt19937_64 rng(order.seed);  // the engine is specified by the standard; the reduction below is ours
        for (std::size_t i = faces; i > 1; --i) std::swap(perm[i - 1], perm[static_cast<std::size_t>(rng() % i)]);
    } else {
        const glm::dvec3 e = eye - mesh.origin;
        std::vector<double> dist(faces);
        for (std::size_t f = 0; f < faces; ++f) {
            // The quad's centre: on its face plane, halfway along both axes.
            const quad::Quad q = quad::unpack(mesh.translucentQuads[f]);
            glm::dvec3 c(q.cell);
            const int d = static_cast<int>(q.face);
            if (d % 2 == 0) c[d / 2] += 1.0;
            c[quad::kBitAxis[d]] += 0.5 * q.w;
            c[quad::kRowAxis[d]] += 0.5 * q.h;
            const glm::dvec3 v = c - e;
            dist[f] = v.x * v.x + v.y * v.y + v.z * v.z;
        }
        const bool nearFirst = order.kind == FaceOrder::Kind::Forward;
        std::stable_sort(perm.begin(), perm.end(), [&](std::size_t a, std::size_t b) {
            return nearFirst ? dist[a] < dist[b] : dist[a] > dist[b];
        });
    }
    std::vector<std::uint64_t> reordered;
    reordered.reserve(faces);
    for (std::size_t f : perm) reordered.push_back(mesh.translucentQuads[f]);
    mesh.translucentQuads = std::move(reordered);
}
