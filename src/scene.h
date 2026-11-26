#pragma once
// The point lights derived from the world's emitters. (The v0.1 test world
// itself is data since v0.2.0: scenarios/v0.1-scene.json.)

#include <vector>

#include <glm/vec3.hpp>

#include "coords.h"
#include "world_query.h"

// A point light at one instant (render/deferred-pipeline). Position is
// authoritative f64, rebased per frame like everything else.
struct PointLight {
    WorldPos pos;
    glm::vec3 color{1.0f};  // linear
    float intensity = 1.0f;
    float radius = 1.0f;    // falloff reaches exactly 0 here
};

// One light per emitter (the first `max`), at the block's center, colored
// by its emissive.
std::vector<PointLight> lightsFromEmitters(const std::vector<Emitter>& emitters, std::size_t max);
