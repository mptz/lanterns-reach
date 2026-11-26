#pragma once
// G-buffer (subset of render/gbuffer-layout) + HDR lighting target.
// Recreated on swapchain resize.
//
//   GB0   R8G8B8A8_SRGB        albedo RGB, A = 1
//   GB1   R16G16_UNORM         octahedral-encoded world-space normal
//   GB2   R8G8B8A8_UNORM       roughness, metallic, reflectance, shading-model bits
//   GB3   R16_UINT             material id (0 = none) → material table
//   Depth D32_SFLOAT           depth (cleared to 1.0 = background)
//   HDR   R16G16B16A16_SFLOAT  emission + lighting, written by the lighting pass
//                              (emission from the material id × albedo);
//                              alpha = point lights covering the pixel (F5)
//   OIT targets (render/transparency), written by the translucency passes:
//   B0    R32G32B32A32_SFLOAT  total absorbance: rgb per channel, a = scalar (mean σ)
//   MOM   R32G32B32A32_SFLOAT  moments b1..b4 of the scalar absorbance (warped depth)
//   ACC   R32G32B32A32_SFLOAT  translucent radiance, already attenuated

#include "render_graph.h"
#include "vk_context.h"

// A VMA-allocated 2D image (or 2D array: layers > 1 or arrayView) + view +
// its graph-tracked state.
class AllocatedImage {
public:
    AllocatedImage() = default;
    AllocatedImage(VkContext& ctx, vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usage,
                   vk::ImageAspectFlags aspect, const char* name, std::uint32_t layers = 1, bool arrayView = false,
                   std::uint32_t mipLevels = 1);
    AllocatedImage(AllocatedImage&& o) noexcept { *this = std::move(o); }
    AllocatedImage& operator=(AllocatedImage&& o) noexcept;
    AllocatedImage(const AllocatedImage&) = delete;
    AllocatedImage& operator=(const AllocatedImage&) = delete;
    ~AllocatedImage() { reset(); }

    GraphImage graphImage() { return {name_, image_, *view_, aspect_, &state_}; }
    vk::ImageView view() const { return *view_; }
    vk::Image image() const { return image_; }
    vk::Format format() const { return format_; }

private:
    void reset();

    VmaAllocator allocator_ = VK_NULL_HANDLE;
    vk::Image image_;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    vk::raii::ImageView view_ = nullptr;
    vk::Format format_ = vk::Format::eUndefined;
    vk::ImageAspectFlags aspect_;
    ImageState state_;
    const char* name_ = "";
};

class GBuffer {
public:
    static constexpr vk::Format kGB0 = vk::Format::eR8G8B8A8Srgb;
    static constexpr vk::Format kGB1 = vk::Format::eR16G16Unorm;
    static constexpr vk::Format kGB2 = vk::Format::eR8G8B8A8Unorm;
    static constexpr vk::Format kGB3 = vk::Format::eR16Uint;
    static constexpr vk::Format kDepth = vk::Format::eD32Sfloat;
    static constexpr vk::Format kHDR = vk::Format::eR16G16B16A16Sfloat;
    static constexpr vk::Format kOitB0 = vk::Format::eR32G32B32A32Sfloat;
    static constexpr vk::Format kOitMoments = vk::Format::eR32G32B32A32Sfloat;
    static constexpr vk::Format kOitAccum = vk::Format::eR32G32B32A32Sfloat;  // fp32: fp16 adds round order-dependently

    // Checks that every target format supports attachment + sampled use.
    static void checkFormats(const VkContext& ctx);

    GBuffer(VkContext& ctx, vk::Extent2D extent);

    vk::Extent2D extent;
    AllocatedImage gb0, gb1, gb2, gb3, depth, hdr;
    AllocatedImage oitB0, oitMoments, oitAccum;
};
