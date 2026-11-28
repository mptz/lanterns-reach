#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
// Cutout faces: the opaque shader with the alpha test (gbuffer_body.glsl).
#define GBUFFER_CUTOUT 1
#include "gbuffer_body.glsl"
