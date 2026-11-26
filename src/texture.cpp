#include "texture.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "gpu_buffer.h"
#include "render_graph.h"

namespace {

Image checker() {
    Image p{16, 16, std::vector<std::uint8_t>(16 * 16 * 4)};
    for (std::uint32_t y = 0; y < 16; ++y)
        for (std::uint32_t x = 0; x < 16; ++x) {
            const bool light = ((x / 4) + (y / 4)) % 2 == 0;
            std::uint8_t* px = &p.rgba[(y * 16 + x) * 4];
            px[0] = light ? 220 : 60;
            px[1] = light ? 200 : 90;
            px[2] = light ? 160 : 140;
            px[3] = 255;
        }
    return p;
}

}  // namespace

TextureArray::TextureArray(VkContext& ctx, const std::vector<Image>& input) {
    const std::vector<Image> fallback = {checker()};
    const std::vector<Image>& layers = input.empty() ? fallback : input;
    const std::uint32_t w = layers.front().width, h = layers.front().height;
    layerCount = static_cast<std::uint32_t>(layers.size());
    // Guaranteed ≥ 256; lavapipe allows far more, so check it here (REVIEW L2).
    if (const std::uint32_t limit = ctx.physicalDevice.getProperties().limits.maxImageArrayLayers; layerCount > limit)
        throw std::runtime_error("block texture array needs " + std::to_string(layerCount) +
                                 " layers; this GPU allows " + std::to_string(limit));
    mipLevels = static_cast<std::uint32_t>(buildMipChain(layers.front()).size());

    image = AllocatedImage(ctx, vk::Format::eR8G8B8A8Srgb, {w, h},
                           vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                           vk::ImageAspectFlagBits::eColor, "block texture array", layerCount, /*arrayView=*/true,
                           mipLevels);

    std::vector<std::uint8_t> bytes;
    std::vector<vk::BufferImageCopy> regions;
    for (std::uint32_t i = 0; i < layerCount; ++i) {
        const Image& img = layers[i];
        if (img.width != w || img.height != h) throw std::runtime_error("texture array layers differ in size");
        const std::vector<Image> chain = buildMipChain(img);
        for (std::uint32_t m = 0; m < mipLevels; ++m) {
            regions.push_back(vk::BufferImageCopy{
                .bufferOffset = bytes.size(),
                .imageSubresource = {vk::ImageAspectFlagBits::eColor, m, i, 1},
                .imageExtent = {chain[m].width, chain[m].height, 1},
            });
            bytes.insert(bytes.end(), chain[m].rgba.begin(), chain[m].rgba.end());
        }
    }
    HostBuffer staging(ctx, bytes.size(), vk::BufferUsageFlagBits::eTransferSrc);
    staging.write(bytes.data(), bytes.size());

    ctx.submitOnce([&](vk::CommandBuffer cmd) {
        RenderGraph graph;
        const ResourceId tex = graph.importImage(image.graphImage(), FinalUse::SampledFragment);
        graph.addPass(PassDesc{
            .name = "upload texture array",
            .transferDst = {tex},
            .record = [&](vk::CommandBuffer c) {
                c.copyBufferToImage(staging.get(), image.image(), vk::ImageLayout::eTransferDstOptimal, regions);
            },
        });
        graph.execute(cmd);
    });

    const bool aniso = ctx.maxSamplerAnisotropy >= 1.0f;
    sampler = vk::raii::Sampler(ctx.device, vk::SamplerCreateInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .anisotropyEnable = aniso ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = aniso ? std::min(kMaxAnisotropy, ctx.maxSamplerAnisotropy) : 1.0f,
        .maxLod = static_cast<float>(mipLevels),
    });
}
