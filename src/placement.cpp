#include "placement.h"

#include <cmath>
#include <stdexcept>

namespace placement {

namespace {

using orient::Symmetry;

glm::ivec3 crossI(glm::ivec3 a, glm::ivec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// The lowest-index rotation taking each `from` vector to its `to` vector.
orient::Rot rotationTaking(std::initializer_list<std::pair<glm::ivec3, glm::ivec3>> pairs) {
    const orient::Group& g = orient::group();
    for (int r = 0; r < orient::kRotCount; ++r) {
        bool ok = true;
        for (const auto& [from, to] : pairs) ok = ok && orient::rotateVec(g.matrix[static_cast<std::size_t>(r)], from) == to;
        if (ok) return static_cast<orient::Rot>(r);
    }
    throw std::logic_error("placement: no rotation takes the feature to the target");
}

}  // namespace

glm::ivec3 faceUp(glm::ivec3 n) { return n.z != 0 ? glm::ivec3(0, 1, 0) : glm::ivec3(0, 0, 1); }
glm::ivec3 faceRight(glm::ivec3 n) { return crossI(faceUp(n), n); }

glm::ivec3 snapPerpendicular(glm::dvec3 toPlayer, glm::ivec3 n) {
    const glm::dvec3 nd(n);
    const glm::dvec3 perp = toPlayer - nd * (toPlayer.x * nd.x + toPlayer.y * nd.y + toPlayer.z * nd.z);
    // Dominant axis; exact ties prefer x, then y, then z.
    int axis = 0;
    for (int a = 1; a < 3; ++a)
        if (std::abs(perp[a]) > std::abs(perp[axis])) axis = a;
    glm::ivec3 f(0);
    if (std::abs(perp[axis]) > 1e-9) {
        f[axis] = perp[axis] > 0.0 ? 1 : -1;
        return f;
    }
    for (int a = 0; a < 3; ++a)
        if (n[a] == 0) {
            f[a] = 1;
            return f;
        }
    return f;  // unreachable for a unit axis n
}

glm::dvec2 faceCoords(glm::dvec3 rel, glm::ivec3 n) {
    const glm::dvec3 r(faceRight(n)), u(faceUp(n));
    return {rel.x * r.x + rel.y * r.y + rel.z * r.z + 0.5, rel.x * u.x + rel.y * u.y + rel.z * u.z + 0.5};
}

orient::Rot placementRotation(Symmetry s, glm::ivec3 n, glm::ivec3 f, glm::dvec2 uv) {
    const glm::ivec3 R = faceRight(n), U = faceUp(n);
    switch (s) {
        case Symmetry::Uniform:
            return orient::kIdentity;
        case Symmetry::Column:
        case Symmetry::Pointed:
            return rotationTaking({{{0, 0, 1}, n}});
        case Symmetry::Brick:
        case Symmetry::PointedBrick:
        case Symmetry::TwistedColumn:
        case Symmetry::Oriented:
            return rotationTaking({{{0, 0, 1}, n}, {{0, -1, 0}, f}});
        case Symmetry::Corner:
        case Symmetry::Diagonal:
        case Symmetry::Tetrahedral: {
            // The new block sits across n, so the clicked face is its −n
            // side; its vertex there nearest the hit (ties → +R, +U). As a
            // direction from the new block's center ×2: −n ± R ± U.
            const int su = uv.x >= 0.5 ? 1 : -1, sv = uv.y >= 0.5 ? 1 : -1;
            return rotationTaking({{{1, -1, 1}, -n + R * su + U * sv}});
        }
        case Symmetry::Edge: {
            // Nearest of the clicked face's four edges (ties in the order
            // −R, +R, −U, +U), as a direction ×2 from the new block's center.
            const double d[4] = {uv.x, 1.0 - uv.x, uv.y, 1.0 - uv.y};
            const glm::ivec3 e[4] = {-n - R, -n + R, -n - U, -n + U};
            int best = 0;
            for (int i = 1; i < 4; ++i)
                if (d[i] < d[best]) best = i;
            return rotationTaking({{{0, -1, 1}, e[best]}});
        }
    }
    return orient::kIdentity;
}

}  // namespace placement
