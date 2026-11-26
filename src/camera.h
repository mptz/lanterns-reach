#pragma once
// 6DOF free-fly camera. Authoritative position is f64 (WorldPos); motion
// integrates in f64. The render view is built around the camera at the f32
// origin, so the view matrix is rotation only.

#include "coords.h"

#include <glm/mat4x4.hpp>

struct CameraInput {
    double forward = 0.0;  // +W / -S
    double right = 0.0;    // +D / -A
    double up = 0.0;       // +C / -V (camera up; input/vertical-movement)
    double yawDelta = 0.0;    // radians
    double pitchDelta = 0.0;  // radians
    bool fast = false;        // Ctrl
};

class Camera {
public:
    WorldPos position{};
    double yaw = 0.0;    // about world +z; 0 looks along +x (east)
    double pitch = 0.0;  // about camera right; + looks up

    void update(const CameraInput& in, double dt);
    void lookAt(const WorldPos& target);

    glm::dvec3 forward() const;
    glm::dvec3 right() const;
    glm::dvec3 up() const;  // right × forward: perpendicular to the look direction

    // World (z-up RH) -> view basis (x right, y down, z forward). That basis
    // is also right-handed, so this is a proper rotation (no axis flip) and
    // triangle winding is preserved — see BUILD-NOTES.md (frontFace).
    glm::mat4 view() const;
    static glm::mat4 projection(float fovY, float aspect, float zNear, float zFar);

    // Near plane: 0.6" (blocks are 1 ft), close enough that a face the
    // player stands against isn't clipped (v0.1 build choice). Far: past
    // anything a 32³ grid holds (arbitrary until v0.2 streams chunks; the
    // reverse-Z question goes with it).
    static constexpr float kNear = 0.05f;
    static constexpr float kFar = 1000.0f;
    static constexpr float kFovY = 1.2f;  // vertical FOV, ~69°: a common desktop default; chosen by eye
};
