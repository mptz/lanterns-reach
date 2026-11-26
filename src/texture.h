#pragma once
// The block texture array: every block-definition texture (and every tile of
// a variant set) as one layer of a single RGBA8 sRGB 2D array
// (sampler2DArray) with its full mip chain (16 → 1, built on the CPU in
// linear light, buildMipChain). The sampler filters linearly with mips and,
// where the device allows, anisotropy, clamped to each layer's edge; the
// shader turns that into anti-aliased nearest up close
// (shaders/block_texture.glsl). Vertices carry each face's layer.

#include <vector>

#include "blocks.h"
#include "gbuffer.h"

inline constexpr float kMaxAnisotropy = 16.0f;  // the common hardware maximum; clamped to the device's limit

class TextureArray {
public:
    // All layers must share one size. An empty list gets one generated
    // checker layer, so the descriptor is always valid.
    TextureArray(VkContext& ctx, const std::vector<Image>& layers);

    AllocatedImage image;
    vk::raii::Sampler sampler = nullptr;
    std::uint32_t layerCount = 0;
    std::uint32_t mipLevels = 0;
};
