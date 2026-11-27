#include "camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <glm/geometric.hpp>

namespace {
constexpr double kSpeed = 4.0;       // free-fly units per second; placeholder, tune in play (a body brings real speeds)
constexpr double kFastFactor = 4.0;  // Ctrl: crosses the v0.1 scene (32 cells) in about 2 s; chosen by eye
// Just short of straight up/down, so right() = forward × up never degenerates.
constexpr double kPitchLimit = std::numbers::pi / 2.0 - 0.001;
}  // namespace

glm::dvec3 Camera::forward() const {
    return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
}

glm::dvec3 Camera::right() const {
    // forward × world-up, with pitch clamped short of ±90° so this never degenerates.
    return {std::sin(yaw), -std::cos(yaw), 0.0};
}

glm::dvec3 Camera::up() const { return glm::cross(right(), forward()); }

void Camera::update(const CameraInput& in, double dt) {
    yaw = std::remainder(yaw + in.yawDelta, 2.0 * std::numbers::pi);
    pitch = std::clamp(pitch + in.pitchDelta, -kPitchLimit, kPitchLimit);

    // Free flight is fully camera-relative: C/V move along the camera's up,
    // like strafing (v0.1.8; input/vertical-movement).
    glm::dvec3 move = forward() * in.forward + right() * in.right + up() * in.up;
    const double len = glm::length(move);
    if (len > 1e-9) {
        const double speed = kSpeed * (in.fast ? kFastFactor : 1.0);
        position += move * (speed * dt / std::max(len, 1.0));  // f64 integration
    }
}

void Camera::lookAt(const WorldPos& target) {
    const glm::dvec3 d = target - position;
    yaw = std::atan2(d.y, d.x);
    pitch = std::clamp(std::atan2(d.z, std::hypot(d.x, d.y)), -kPitchLimit, kPitchLimit);
}

glm::mat4 Camera::view() const {
    const glm::vec3 f(forward());
    const glm::vec3 r(right());
    const glm::vec3 u(up());
    const glm::vec3 d = -u;                // camera down = view +y
    // Rows of the rotation are the view basis vectors (glm is column-major:
    // m[col][row]).
    glm::mat4 m(1.0f);
    for (int i = 0; i < 3; ++i) {
        m[i][0] = r[i];
        m[i][1] = d[i];
        m[i][2] = f[i];
    }
    return m;
}

glm::mat4 Camera::projection(float fovY, float aspect, float zNear, float zFar) {
    // View space x right, y down, z forward -> Vulkan clip (x right, y down,
    // depth 0 at near .. 1 at far, w = view z).
    const float fy = 1.0f / std::tan(fovY * 0.5f);
    glm::mat4 p(0.0f);
    p[0][0] = fy / aspect;
    p[1][1] = fy;
    p[2][2] = zFar / (zFar - zNear);
    p[3][2] = -zFar * zNear / (zFar - zNear);
    p[2][3] = 1.0f;
    return p;
}
