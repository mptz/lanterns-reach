#include "orientation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>

namespace orient {

namespace {

// R_axis(degrees) by Rodrigues' formula in f64, rounded. Every lattice-
// preserving rotation has entries in {−1, 0, 1}; anything else is a caller
// error.
Mat3i axisMatrix(glm::ivec3 axis, int degrees) {
    const double len = std::sqrt(static_cast<double>(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z));
    const double a[3] = {axis.x / len, axis.y / len, axis.z / len};
    const double t = degrees * std::numbers::pi / 180.0;
    const double c = std::cos(t), s = std::sin(t);
    const double cross[3][3] = {{0, -a[2], a[1]}, {a[2], 0, -a[0]}, {-a[1], a[0], 0}};
    Mat3i m{};
    for (std::size_t r = 0; r < 3; ++r)
        for (std::size_t col = 0; col < 3; ++col) {
            const double v = (r == col ? c : 0.0) + s * cross[r][col] + (1.0 - c) * a[r] * a[col];
            const double rounded = std::round(v);
            if (std::abs(v - rounded) > 1e-9) throw std::logic_error("rotation is not lattice-preserving");
            m[r][col] = static_cast<int>(rounded);
        }
    return m;
}

constexpr glm::ivec3 kZ{0, 0, 1};

glm::ivec3 frameUp(Face f) { return (f == PosZ || f == NegZ) ? glm::ivec3{0, 1, 0} : kZ; }

glm::ivec3 crossI(const glm::ivec3& a, const glm::ivec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

glm::ivec3 frameRight(Face f) { return crossI(frameUp(f), faceNormal(f)); }

Face faceOf(const glm::ivec3& n) {
    for (int f = 0; f < kFaceCount; ++f)
        if (faceNormal(static_cast<Face>(f)) == n) return static_cast<Face>(f);
    throw std::logic_error("not a face normal");
}

Group buildGroup() {
    Group g{};
    // F(facing): takes local +z to each facing, in kRepOrder order.
    const Mat3i facing[6] = {
        axisMatrix({1, 0, 0}, 0),   axisMatrix({1, 0, 0}, 180), axisMatrix({1, 0, 0}, 90),
        axisMatrix({1, 0, 0}, -90), axisMatrix({0, 1, 0}, 90),  axisMatrix({0, 1, 0}, -90),
    };
    for (int f = 0; f < 6; ++f)
        for (int spin = 0; spin < 4; ++spin)
            g.matrix[static_cast<std::size_t>(4 * f + spin)] = multiply(facing[f], axisMatrix(kZ, 90 * spin));

    auto find = [&](const Mat3i& m) -> Rot {
        for (int i = 0; i < kRotCount; ++i)
            if (g.matrix[static_cast<std::size_t>(i)] == m) return static_cast<Rot>(i);
        throw std::logic_error("matrix not in the rotation group");
    };
    for (int a = 0; a < kRotCount; ++a) {
        const auto ua = static_cast<std::size_t>(a);
        for (int b = 0; b < kRotCount; ++b)
            g.compose[ua][static_cast<std::size_t>(b)] = find(multiply(g.matrix[ua], g.matrix[static_cast<std::size_t>(b)]));
        for (int b = 0; b < kRotCount; ++b)
            if (g.compose[ua][static_cast<std::size_t>(b)] == kIdentity) g.inverse[ua] = static_cast<Rot>(b);
        for (int n = 0; n < kFaceCount; ++n) {
            const Face face = static_cast<Face>(n);
            const Face to = faceOf(rotateVec(g.matrix[ua], faceNormal(face)));
            const glm::ivec3 up = rotateVec(g.matrix[ua], frameUp(face));  // image of the source frame's up
            const glm::ivec3 u = frameUp(to), r = frameRight(to);
            // A k-quarter-turn CCW about the outward normal takes U to U, −R, −U, R.
            std::uint8_t k = 0;
            if (up == u) k = 0;
            else if (up == -r) k = 1;
            else if (up == -u) k = 2;
            else if (up == r) k = 3;
            else throw std::logic_error("face frame not carried in-plane");
            g.faceMap[ua][static_cast<std::size_t>(n)] = to;
            g.faceTurns[ua][static_cast<std::size_t>(n)] = k;
        }
    }
    return g;
}

std::vector<Rot> closure(const std::vector<Rot>& generators) {
    const Group& g = group();
    std::vector<Rot> h = {kIdentity};
    for (std::size_t i = 0; i < h.size(); ++i)
        for (Rot gen : generators) {
            const Rot next = g.compose[h[i]][gen];
            if (std::find(h.begin(), h.end(), next) == h.end()) h.push_back(next);
        }
    std::sort(h.begin(), h.end());
    return h;
}

SymmetryInfo buildSymmetry(const char* keyword, const std::vector<Rot>& generators) {
    const Group& g = group();
    SymmetryInfo s{};
    s.keyword = keyword;
    s.subgroup = closure(generators);

    for (int r = 0; r < kRotCount; ++r) {
        Rot best = static_cast<Rot>(r);
        for (Rot h : s.subgroup) best = std::min(best, g.compose[static_cast<std::size_t>(r)][h]);
        s.canonical[static_cast<std::size_t>(r)] = best;
    }

    std::array<bool, kFaceCount> seen{};
    for (Face rep : kRepOrder) {
        if (seen[rep]) continue;
        Orbit o{.rep = rep, .faces = {}, .stabilizerTurns = {}};
        for (Face f : kRepOrder)
            for (Rot h : s.subgroup)
                if (g.faceMap[h][rep] == f) {
                    o.faces.push_back(f);
                    seen[f] = true;
                    break;
                }
        for (Rot h : s.subgroup)
            if (g.faceMap[h][rep] == rep) o.stabilizerTurns.push_back(g.faceTurns[h][rep]);
        std::sort(o.stabilizerTurns.begin(), o.stabilizerTurns.end());
        o.stabilizerTurns.erase(std::unique(o.stabilizerTurns.begin(), o.stabilizerTurns.end()),
                                o.stabilizerTurns.end());
        for (Face f : o.faces) {
            s.repOf[f] = rep;
            for (Rot h : s.subgroup)  // sorted, so the first match is the lowest index
                if (g.faceMap[h][rep] == f) {
                    s.repTurns[f] = g.faceTurns[h][rep];
                    break;
                }
        }
        s.orbits.push_back(std::move(o));
    }
    return s;
}

std::array<SymmetryInfo, kSymmetryCount> buildSymmetries() {
    // Standard positions (versions.md v0.1.3): local +z = attach, −y = front.
    const Rot z90 = rotationAbout({0, 0, 1}, 90);
    const Rot z180 = rotationAbout({0, 0, 1}, 180);
    const Rot x180 = rotationAbout({1, 0, 0}, 180);
    const Rot y180 = rotationAbout({0, 1, 0}, 180);
    const Rot xy180 = rotationAbout({1, 1, 0}, 180);
    const Rot edge180 = rotationAbout({0, -1, 1}, 180);    // the +z/−y edge
    const Rot vertex120 = rotationAbout({1, -1, 1}, 120);  // the +x,−y,+z vertex
    std::vector<Rot> all;
    for (int r = 0; r < kRotCount; ++r) all.push_back(static_cast<Rot>(r));
    return {
        buildSymmetry("uniform", all),
        buildSymmetry("column", {z90, x180}),
        buildSymmetry("pointed", {z90}),
        buildSymmetry("brick", {x180, y180}),
        buildSymmetry("twisted-column", {z180, xy180}),
        buildSymmetry("pointed-brick", {z180}),
        buildSymmetry("edge", {edge180}),
        buildSymmetry("corner", {vertex120}),
        buildSymmetry("diagonal", {vertex120, xy180}),
        buildSymmetry("tetrahedral", {z180, vertex120}),
        buildSymmetry("oriented", {}),
    };
}

}  // namespace

glm::ivec3 faceNormal(Face f) {
    switch (f) {
        case PosX: return {1, 0, 0};
        case NegX: return {-1, 0, 0};
        case PosY: return {0, 1, 0};
        case NegY: return {0, -1, 0};
        case PosZ: return {0, 0, 1};
        case NegZ: return {0, 0, -1};
    }
    return {};
}

std::optional<Face> faceFromName(std::string_view name) {
    for (int f = 0; f < kFaceCount; ++f)
        if (name == kFaceNames[static_cast<std::size_t>(f)]) return static_cast<Face>(f);
    return std::nullopt;
}

glm::ivec3 rotateVec(const Mat3i& m, const glm::ivec3& v) {
    return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}

Mat3i multiply(const Mat3i& a, const Mat3i& b) {
    Mat3i m{};
    for (std::size_t r = 0; r < 3; ++r)
        for (std::size_t c = 0; c < 3; ++c)
            for (std::size_t k = 0; k < 3; ++k) m[r][c] += a[r][k] * b[k][c];
    return m;
}

const Group& group() {
    static const Group g = buildGroup();
    return g;
}

std::optional<Rot> indexOf(const Mat3i& m) {
    const Group& g = group();
    for (int i = 0; i < kRotCount; ++i)
        if (g.matrix[static_cast<std::size_t>(i)] == m) return static_cast<Rot>(i);
    return std::nullopt;
}

Rot rotationAbout(glm::ivec3 axis, int degrees) {
    const auto r = indexOf(axisMatrix(axis, degrees));
    if (!r) throw std::logic_error("rotation not in the group");
    return *r;
}

const SymmetryInfo& symmetry(Symmetry s) {
    static const auto table = buildSymmetries();
    return table[static_cast<std::size_t>(s)];
}

std::optional<Symmetry> symmetryFromKeyword(std::string_view keyword) {
    for (int i = 0; i < kSymmetryCount; ++i)
        if (keyword == symmetry(static_cast<Symmetry>(i)).keyword) return static_cast<Symmetry>(i);
    return std::nullopt;
}

}  // namespace orient
