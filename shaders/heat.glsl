// The F5 light-count heatmap ramp, t ∈ [0, 1]: blue → cyan → green → yellow
// → red (a common false-colour ramp; chosen by eye). Valid GLSL and valid
// C++ (with glm, inside a namespace that does `using namespace glm;`), so
// present.frag and the F5 panel's legend share it.
vec3 heat(float t) {
    return clamp(vec3(4.0f * t - 2.0f, min(4.0f * t, 4.0f - 4.0f * t), 2.0f - 4.0f * t), vec3(0.0f), vec3(1.0f));
}
