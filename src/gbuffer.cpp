#include "gbuffer.h"

#include <stdexcept>
#include <string>
#include <utility>

AllocatedImage::AllocatedImage(VkContext& ctx, vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usage,
                               vk::ImageAspectFlags aspect, const char* name, std::uint32_t layers,
                               bool arrayView, std::uint32_t mipLevels)
    : allocator_(ctx.allocator.get()), format_(format), aspect_(aspect), name_(name) {
    const VkImageCreateInfo ici = vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = mipLevels,
        .arrayLayers = layers,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkImage raw = VK_NULL_HANDLE;
    if (vmaCreateImage(allocator_, &ici, &aci, &raw, &allocation_, nullptr) != VK_SUCCESS)
        throw std::runtime_error(std::string("vmaCreateImage failed: ") + name);
    image_ = raw;
    view_ = vk::raii::ImageView(ctx.device, vk::ImageViewCreateInfo{
        .image = image_,
        .viewType = (layers > 1 || arrayView) ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {aspect, 0, mipLevels, 0, layers},
    });
}

AllocatedImage& AllocatedImage::operator=(AllocatedImage&& o) noexcept {
    if (this != &o) {
        reset();
        allocator_ = std::exchange(o.allocator_, VK_NULL_HANDLE);
        image_ = std::exchange(o.image_, vk::Image{});
        allocation_ = std::exchange(o.allocation_, VK_NULL_HANDLE);
        view_ = std::move(o.view_);
        format_ = o.format_;
        aspect_ = o.aspect_;
        state_ = o.state_;
        name_ = o.name_;
    }
    return *this;
}

void AllocatedImage::reset() {
    view_.clear();
    if (allocator_ && image_) vmaDestroyImage(allocator_, image_, allocation_);
    image_ = vk::Image{};
    allocation_ = VK_NULL_HANDLE;
}

void GBuffer::checkFormats(const VkContext& ctx) {
    using F = vk::FormatFeatureFlagBits2;
    ctx.requireFormat(kGB0, F::eColorAttachment | F::eSampledImage, "GB0");
    ctx.requireFormat(kGB1, F::eColorAttachment | F::eSampledImage, "GB1");
    ctx.requireFormat(kGB2, F::eColorAttachment | F::eSampledImage, "GB2");
    ctx.requireFormat(kGB3, F::eColorAttachment | F::eSampledImage, "GB3");
    ctx.requireFormat(kDepth, F::eDepthStencilAttachment | F::eSampledImage, "depth");
    ctx.requireFormat(kHDR, F::eColorAttachment | F::eColorAttachmentBlend | F::eSampledImage, "HDR");
    // The OIT passes accumulate with additive blending, so 32-bit float
    // blending is required.
    ctx.requireFormat(kOitB0, F::eColorAttachment | F::eColorAttachmentBlend | F::eSampledImage, "OIT absorbance");
    ctx.requireFormat(kOitMoments, F::eColorAttachment | F::eColorAttachmentBlend | F::eSampledImage, "OIT moments");
    ctx.requireFormat(kOitAccum, F::eColorAttachment | F::eColorAttachmentBlend | F::eSampledImage, "OIT radiance");
}

GBuffer::GBuffer(VkContext& ctx, vk::Extent2D ext) : extent(ext) {
    using U = vk::ImageUsageFlagBits;
    using A = vk::ImageAspectFlagBits;
    gb0 = AllocatedImage(ctx, kGB0, ext, U::eColorAttachment | U::eSampled, A::eColor, "GB0");
    gb1 = AllocatedImage(ctx, kGB1, ext, U::eColorAttachment | U::eSampled, A::eColor, "GB1");
    gb2 = AllocatedImage(ctx, kGB2, ext, U::eColorAttachment | U::eSampled, A::eColor, "GB2");
    gb3 = AllocatedImage(ctx, kGB3, ext, U::eColorAttachment | U::eSampled, A::eColor, "GB3");
    depth = AllocatedImage(ctx, kDepth, ext, U::eDepthStencilAttachment | U::eSampled, A::eDepth, "depth");
    hdr = AllocatedImage(ctx, kHDR, ext, U::eColorAttachment | U::eSampled, A::eColor, "HDR");
    oitB0 = AllocatedImage(ctx, kOitB0, ext, U::eColorAttachment | U::eSampled | U::eTransferSrc, A::eColor,
                           "OIT absorbance");  // transfer src: the absorbance probe reads it back
    oitMoments = AllocatedImage(ctx, kOitMoments, ext, U::eColorAttachment | U::eSampled, A::eColor, "OIT moments");
    oitAccum = AllocatedImage(ctx, kOitAccum, ext, U::eColorAttachment | U::eSampled, A::eColor, "OIT radiance");
}
