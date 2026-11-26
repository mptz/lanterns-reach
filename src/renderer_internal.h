#pragma once
// Definitions shared by the renderer's setup (renderer_setup.cpp: shaders,
// layouts, pipelines, descriptors, targets) and its per-frame recording
// (renderer.cpp): push-constant blocks, GPU-side structs and tuning
// constants. Internal to the renderer.

#include <cstdint>

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace renderer_detail {

// The directional light (versions.md v0.1): L is the direction light
// travels, down and off to one side, with no zero component so no face is
// exactly head-on or edge-on to it. Its strength and the ambient level are
// LightingSettings (F5 panel).
inline const glm::vec3 kLightDir = glm::normalize(glm::vec3(0.187f, 0.463f, -0.867f));
// Chosen by eye unless noted: the background (linear; a dark blue stand-in
// for a sky), the depth view's white point (blocks; about the test scene's
// depth), the debug-line pull toward the eye (a fraction of distance: enough
// to win the depth test on the face a line lies on, small enough not to show
// hidden lines; lines.vert), the F3 wire colour (linear), and the segments
// per great circle of an F5 radius sphere.
inline constexpr glm::vec3 kBackground(0.02f, 0.03f, 0.05f);
inline constexpr float kDepthVisRange = 12.0f;
inline constexpr float kLineBias = 0.002f;
inline constexpr glm::vec3 kWireColor(1.0f, 0.75f, 0.1f);
inline constexpr int kSphereSegments = 32;
inline constexpr float kPi = 3.14159265f;

struct GBufferPush {
    glm::mat4 mvp;           // proj · view · translate(camera-relative mesh origin)
    glm::ivec4 originCell;   // xyz: the mesh origin's integer cell (variant hashing); w: flags (gbuffer.frag)
};
struct LightingPush {
    glm::mat4 invViewProj;
    glm::vec4 toLight;
    glm::vec4 sun;  // rgb radiance
    glm::vec4 background;
    float ambient;
    std::uint32_t lightCount;
};
// Every push-constant block fits the guaranteed minimum, 128 bytes (lavapipe
// allows more, so this is checked here, not left to the driver — REVIEW L1).
static_assert(sizeof(GBufferPush) <= 128, "push constants must fit the guaranteed minimum");
static_assert(sizeof(LightingPush) <= 128, "push constants must fit the guaranteed minimum");
struct PresentPush {
    std::int32_t view;
    float zNear;
    float zFar;
    float depthVisRange;
    float exposure;
    std::int32_t heatmap;
    float maxLights;
    std::int32_t oitView;
};
static_assert(sizeof(PresentPush) <= 128, "push constants must fit the guaranteed minimum");
// Both translucency passes (shaders/translucent_common.glsl).
struct TranslucentPush {
    glm::mat4 viewProj;
    glm::vec4 meshRel;  // xyz camera-relative mesh origin, w ambient
    glm::vec4 toLight;  // xyz −L, w moment bias
    glm::vec4 sun;      // rgb radiance, w overestimation
    glm::vec4 params;   // camera near, camera far, point-light count
};
static_assert(sizeof(TranslucentPush) <= 128, "push constants must fit the guaranteed minimum");
struct GpuLight {
    glm::vec4 posRadius;  // camera-relative xyz, radius
    glm::vec4 color;      // rgb = color × intensity
};
struct LinesPush {
    glm::mat4 viewProj;
    glm::vec4 offsetBias;  // xyz: camera-relative origin of the line buffer; w: toward-camera bias
};
static_assert(sizeof(LinesPush) <= 128, "push constants must fit the guaranteed minimum");
struct LineVertex {
    glm::vec3 pos;  // camera-relative
    glm::vec3 color;
};
struct GpuMaterial {
    glm::vec4 pbr;                  // roughness, metallic, reflectance, shading-model bits
    glm::vec4 emissive;             // rgb linear radiance (opaque)
    glm::vec4 absorption;           // rgb σ per block (translucent)
    glm::vec4 translucentEmission;  // rgb front-face emission (translucent)
};
static_assert(sizeof(GpuMaterial) == 64, "matches the std430 Material struct in shaders/material.glsl");

// Preview ghosts: a light grey volume (σ per block) whose faces glow faintly
// with the block's own texture (emission = texture × this). Chosen by eye
// (v0.1.7) so a ghost reads as "not yet placed" over any background without
// hiding what is behind it. The inflation (blocks) makes a ghost enclose the
// block it replaces (wrench) without z-fighting; small enough not to show.
inline constexpr float kGhostSigma = 0.25f;
inline constexpr float kGhostEmission = 0.45f;
inline constexpr float kGhostInflate = 0.004f;

}  // namespace renderer_detail
