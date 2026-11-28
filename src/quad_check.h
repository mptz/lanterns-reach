#pragma once
// --check-quads (versions.md v0.2.5, acceptance 5): the shaders unpack packed
// quads as packed_quad.h does. A compute shader (shaders/quad_check.comp)
// runs quad.glsl over a pinned set of quads (every field at its extremes,
// then seeded random ones) and writes each quad's fields and corners; they
// are compared with quad::unpack and with the unit cube's corners scaled
// along the face's axes. Prints the result; true if every quad matches.

class VkContext;

bool checkQuadUnpacking(VkContext& ctx);
