#version 460
// Debug lines (F3 wireframe, F4–F6 and F10 overlays). Positions plus the
// pushed offset are camera-relative (rebased in f64 on the CPU: per-frame
// lines carry it in their positions, the static F3 buffer via the offset),
// so the camera is at the origin here and scaling a position by (1 − bias) moves it toward the eye by a
// fixed fraction of its distance — a depth bias that keeps lines in front of
// the faces they lie on at any range without letting hidden lines show
// through.

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;  // linear

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 offsetBias;  // xyz: camera-relative origin of this line buffer; w: bias
} pc;

layout(location = 0) out vec3 outColor;

void main() {
    outColor = inColor;
    gl_Position = pc.viewProj * vec4((inPos + pc.offsetBias.xyz) * (1.0 - pc.offsetBias.w), 1.0);
}
