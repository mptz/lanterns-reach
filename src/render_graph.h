#pragma once
// Minimal render-graph seam (engine/vulkan-abstraction).
//
// Passes declare which images they write as attachments and which they
// sample/copy into; the graph derives every synchronization2 barrier and
// layout transition from those declarations and opens/closes dynamic
// rendering around attachment passes. Pass record callbacks only bind and
// draw — they never place barriers.
//
// Image state (last stage/access/layout) is owned by whoever owns the image
// and persists across frames, so cross-frame hazards (frame N+1 overwriting
// a target frame N still samples) are covered by the same tracking.
//
// GPU timing is part of the seam too: given a query pool, execute() brackets
// every pass with a timestamp pair, so per-pass times need no pass-code
// changes.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

struct ImageState {
    vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eNone;
    vk::AccessFlags2 access = vk::AccessFlagBits2::eNone;
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;
};

struct GraphImage {
    const char* name = "";
    vk::Image image;
    vk::ImageView view;
    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
    ImageState* state = nullptr;  // persistent; updated by execute()
};

enum class ResourceId : std::uint32_t {};

// How an image is used at the end of the graph (after the last pass).
enum class FinalUse { None, Present, SampledFragment };

struct ColorAttachment {
    ResourceId id;
    vk::ClearColorValue clear{};  // used when load == eClear
    vk::AttachmentLoadOp load = vk::AttachmentLoadOp::eClear;
};

struct DepthAttachment {
    ResourceId id;
    float clearDepth = 1.0f;
};

struct PassDesc {
    std::string name;
    // Attachment passes: non-empty colors and/or depth => dynamic rendering
    // over renderArea, with viewport/scissor set to it.
    std::vector<ColorAttachment> colors;
    std::optional<DepthAttachment> depth;
    vk::Extent2D renderArea{};
    std::vector<ResourceId> sampled;      // read in the fragment shader
    std::vector<ResourceId> transferDst;  // copy destination (uploads)
    std::vector<ResourceId> transferSrc;  // copy source (readbacks)
    std::function<void(vk::CommandBuffer)> record;
};

class RenderGraph {
public:
    ResourceId importImage(const GraphImage& img, FinalUse finalUse = FinalUse::None);
    void addPass(PassDesc pass);
    // Optional: time each pass. execute() resets the first 2·passCount()
    // queries of `pool` and writes pass i's begin/end at queries 2i, 2i+1.
    // The pool must hold at least 2·passCount() timestamp queries.
    void timePasses(vk::QueryPool pool) { timestamps_ = pool; }
    std::size_t passCount() const { return passes_.size(); }
    const std::string& passName(std::size_t i) const { return passes_[i].name; }
    // Records all passes (with derived barriers) into cmd, then the final
    // transitions. The graph is single-use.
    void execute(vk::CommandBuffer cmd);

private:
    struct Resource {
        GraphImage img;
        FinalUse finalUse;
    };
    std::vector<Resource> resources_;
    std::vector<PassDesc> passes_;
    vk::QueryPool timestamps_;
};
