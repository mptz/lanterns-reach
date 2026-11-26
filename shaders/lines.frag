#version 460
// Depth-tested against the G-buffer depth (sampled; no depth attachment), so
// lines behind geometry stay hidden.

layout(set = 0, binding = 0) uniform sampler2D gbDepth;

layout(location = 0) in vec3 inColor;
layout(location = 0) out vec4 outColor;

void main() {
    if (gl_FragCoord.z > texelFetch(gbDepth, ivec2(gl_FragCoord.xy), 0).r) discard;
    outColor = vec4(inColor, 1.0);
}
