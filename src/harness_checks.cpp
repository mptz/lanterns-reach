#include "harness_checks.h"

#include <cmath>
#include <cstdio>

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>

namespace {

// Chord length of a ray through the unit cube at `c` (slab method, f64).
double chord(const WorldPos& o, const glm::dvec3& d, const CellPos& c) {
    double t0 = -1e300, t1 = 1e300;
    const WorldPos lo = cellCorner(c);
    const double origin[3] = {o.x, o.y, o.z}, low[3] = {lo.x, lo.y, lo.z};
    for (int a = 0; a < 3; ++a) {
        if (d[a] == 0.0) {
            if (origin[a] < low[a] || origin[a] > low[a] + 1.0) return 0.0;
            continue;
        }
        double ta = (low[a] - origin[a]) / d[a], tb = (low[a] + 1.0 - origin[a]) / d[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
    }
    t0 = std::max(t0, 0.0);
    return t1 > t0 ? t1 - t0 : 0.0;
}

}  // namespace

void AbsorbanceCheck::aim(Camera& camera) const {
    if (done()) return;
    camera.position = cases_[index_].eye;
    camera.lookAt(cases_[index_].look);
}

void AbsorbanceCheck::frame(Renderer& renderer, const Camera& camera, const BlockRegistry& registry) {
    if (done()) return;
    // Let the view settle, then read the center pixel back.
    const vk::Extent2D e = renderer.extent();
    const glm::ivec2 center(static_cast<int>(e.width / 2), static_cast<int>(e.height / 2));
    if (!requested_ && ++waited_ >= 3) {
        renderer.requestAbsorbanceProbe(center);
        requested_ = true;
    } else if (auto texel = renderer.takeAbsorbanceProbe()) {
        const AbsorbanceCase& c = cases_[index_];
        // The exact ray of that pixel's center, from the same matrices.
        const glm::mat4 inv = glm::inverse(renderer.viewProjection(camera));
        const glm::vec4 far = inv * glm::vec4((static_cast<float>(center.x) + 0.5f) / static_cast<float>(e.width) * 2.0f - 1.0f,
                                              (static_cast<float>(center.y) + 0.5f) / static_cast<float>(e.height) * 2.0f - 1.0f,
                                              1.0f, 1.0f);
        glm::dvec3 dir(far.x / far.w, far.y / far.w, far.z / far.w);
        dir /= std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        glm::dvec3 expected(0.0);
        double path = 0.0;
        for (const auto& [cell, type] : c.cubes) {
            const double len = chord(camera.position, dir, cell);
            path += len;
            expected += glm::dvec3(registry.def(*registry.find(type)).translucency->absorption) * len;
        }
        const glm::dvec3 got(texel->x, texel->y, texel->z);
        bool ok = true;
        for (int ch = 0; ch < 3; ++ch)
            ok = ok && std::abs(got[ch] - expected[ch]) <= 0.005 * expected[ch] + 2e-4;
        std::fprintf(stderr,
                     "[sandbox] %s %-30s path %.4f  absorbance (%.4f %.4f %.4f) expected (%.4f %.4f %.4f)\n",
                     ok ? "PASS" : "FAIL", c.name.c_str(), path, got.x, got.y, got.z, expected.x, expected.y,
                     expected.z);
        failures_ += ok ? 0 : 1;
        measured_.push_back(got);
        ++index_;
        waited_ = 0;
        requested_ = false;
        if (index_ == cases_.size() && measured_.size() >= 4) {
            // The two pairwise criteria, stated directly: 3 blocks vs 1
            // (transmittance ratio exp(−2σ)); glass on stone vs over air (equal).
            const glm::dvec3 sigma(registry.def(*registry.find("glass_blue")).translucency->absorption);
            bool pairOk = true;
            for (int ch = 0; ch < 3; ++ch) {
                const double ratio = std::exp(-(measured_[0][ch] - measured_[1][ch]));
                pairOk = pairOk && std::abs(ratio - std::exp(-2.0 * sigma[ch])) <= 0.01 * std::exp(-2.0 * sigma[ch]);
                pairOk = pairOk && std::abs(measured_[2][ch] - measured_[3][ch]) <=
                                       0.005 * measured_[3][ch] + 2e-4;
            }
            std::fprintf(stderr, "[sandbox] %s T(3 blocks)/T(1 block) = exp(-2σ); on-stone = over-air\n",
                         pairOk ? "PASS" : "FAIL");
            failures_ += pairOk ? 0 : 1;
        }
    }
}

int AbsorbanceCheck::result(int rc) const {
    if (!done()) {
        std::fprintf(stderr, "[sandbox] FAIL absorbance check incomplete (%zu of %zu cases)\n", index_, cases_.size());
        return 1;
    }
    std::fprintf(stderr, "[sandbox] absorbance check: %d failure(s)\n", failures_);
    return failures_ ? 1 : rc;
}
