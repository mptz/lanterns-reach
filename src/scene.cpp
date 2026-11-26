#include "scene.h"

#include <algorithm>
#include <cstdio>

namespace {

// Point light per emitter (v0.1.4's lamp lights): intensity in the
// falloff's units (pbr.glsl lightFalloff), radius in blocks. Chosen by eye so
// a lamp lights its neighbourhood without washing out the scene at the
// default exposure; placeholder until lights come from block data.
constexpr float kLampIntensity = 6.0f;
constexpr float kLampRadius = 7.0f;

}  // namespace

std::vector<PointLight> lightsFromEmitters(const std::vector<Emitter>& emitters, std::size_t max) {
    std::vector<PointLight> out;
    for (const Emitter& e : emitters) {
        if (out.size() == max) break;
        const float peak = std::max({e.emissive.x, e.emissive.y, e.emissive.z});
        out.push_back({cellCenter(e.cell), e.emissive / peak, kLampIntensity, kLampRadius});
    }
    return out;
}
