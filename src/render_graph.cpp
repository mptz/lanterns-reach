#include "render_graph.h"

namespace {

using Stage = vk::PipelineStageFlagBits2;
using Access = vk::AccessFlagBits2;
using Layout = vk::ImageLayout;

constexpr vk::AccessFlags2 kWriteAccess = Access::eColorAttachmentWrite | Access::eDepthStencilAttachmentWrite |
                                          Access::eTransferWrite | Access::eShaderWrite | Access::eMemoryWrite;

struct Need {
    ImageState state;
    bool discard = false;  // previous contents not needed: transition from UNDEFINED
};

Need colorAttachment(vk::AttachmentLoadOp load) {
    vk::AccessFlags2 access = Access::eColorAttachmentWrite;
    if (load == vk::AttachmentLoadOp::eLoad) access |= Access::eColorAttachmentRead;
    return {{Stage::eColorAttachmentOutput, access, Layout::eColorAttachmentOptimal},
            load != vk::AttachmentLoadOp::eLoad};
}

Need depthAttachmentCleared() {
    return {{Stage::eEarlyFragmentTests | Stage::eLateFragmentTests,
             Access::eDepthStencilAttachmentRead | Access::eDepthStencilAttachmentWrite,
             Layout::eDepthAttachmentOptimal},
            true};
}

Need sampledFragment() {
    return {{Stage::eFragmentShader, Access::eShaderSampledRead, Layout::eShaderReadOnlyOptimal}, false};
}

Need transferDst() { return {{Stage::eCopy, Access::eTransferWrite, Layout::eTransferDstOptimal}, true}; }

Need transferSrc() { return {{Stage::eCopy, Access::eTransferRead, Layout::eTransferSrcOptimal}, false}; }

// The present transition must happen-before the frame's semaphore signal.
// With dstStage = NONE nothing orders it before the signal; a real stage
// (one inside the signal's ALL_COMMANDS scope) chains it.
Need present() { return {{Stage::eColorAttachmentOutput, Access::eNone, Layout::ePresentSrcKHR}, false}; }

class BarrierBatch {
public:
    void transition(const GraphImage& img, const Need& need) {
        ImageState& cur = *img.state;
        const bool curWrites = static_cast<bool>(cur.access & kWriteAccess);
        const bool needWrites = static_cast<bool>(need.state.access & kWriteAccess);

        // Read-after-read in the same layout needs no barrier; widen the
        // recorded reader scope so a later writer waits on every reader.
        if (!curWrites && !needWrites && cur.layout == need.state.layout) {
            cur.stage |= need.state.stage;
            cur.access |= need.state.access;
            return;
        }
        barriers_.push_back(vk::ImageMemoryBarrier2{
            .srcStageMask = cur.stage,
            .srcAccessMask = curWrites ? (cur.access & kWriteAccess) : vk::AccessFlags2{},
            .dstStageMask = need.state.stage,
            .dstAccessMask = need.state.access,
            .oldLayout = need.discard ? Layout::eUndefined : cur.layout,
            .newLayout = need.state.layout,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = img.image,
            .subresourceRange = {img.aspect, 0, vk::RemainingMipLevels, 0, vk::RemainingArrayLayers},
        });
        cur = need.state;
    }

    void flush(vk::CommandBuffer cmd) {
        if (barriers_.empty()) return;
        cmd.pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(barriers_));
        barriers_.clear();
    }

private:
    std::vector<vk::ImageMemoryBarrier2> barriers_;
};

}  // namespace

ResourceId RenderGraph::importImage(const GraphImage& img, FinalUse finalUse) {
    resources_.push_back({img, finalUse});
    return ResourceId{static_cast<std::uint32_t>(resources_.size() - 1)};
}

void RenderGraph::addPass(PassDesc pass) { passes_.push_back(std::move(pass)); }

void RenderGraph::execute(vk::CommandBuffer cmd) {
    auto res = [&](ResourceId id) -> const GraphImage& { return resources_[static_cast<std::uint32_t>(id)].img; };
    BarrierBatch batch;
    const auto queryCount = static_cast<std::uint32_t>(2 * passes_.size());
    if (timestamps_) cmd.resetQueryPool(timestamps_, 0, queryCount);
    // ALL_COMMANDS: each stamp waits for everything before it, so a pass's
    // pair brackets its own work (barriers included), not its neighbours'.
    auto stamp = [&](std::size_t query) {
        if (timestamps_)
            cmd.writeTimestamp2(Stage::eAllCommands, timestamps_, static_cast<std::uint32_t>(query));
    };

    for (std::size_t p = 0; p < passes_.size(); ++p) {
        const PassDesc& pass = passes_[p];
        stamp(2 * p);
        for (ResourceId id : pass.sampled) batch.transition(res(id), sampledFragment());
        for (ResourceId id : pass.transferDst) batch.transition(res(id), transferDst());
        for (ResourceId id : pass.transferSrc) batch.transition(res(id), transferSrc());
        for (const ColorAttachment& c : pass.colors) batch.transition(res(c.id), colorAttachment(c.load));
        if (pass.depth) batch.transition(res(pass.depth->id), depthAttachmentCleared());
        batch.flush(cmd);

        const bool rendering = !pass.colors.empty() || pass.depth.has_value();
        if (!rendering) {
            pass.record(cmd);
            stamp(2 * p + 1);
            continue;
        }

        std::vector<vk::RenderingAttachmentInfo> colorInfos;
        colorInfos.reserve(pass.colors.size());
        for (const ColorAttachment& c : pass.colors) {
            colorInfos.push_back(vk::RenderingAttachmentInfo{
                .imageView = res(c.id).view,
                .imageLayout = Layout::eColorAttachmentOptimal,
                .loadOp = c.load,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .clearValue = vk::ClearValue{c.clear},
            });
        }
        vk::RenderingAttachmentInfo depthInfo{};
        if (pass.depth) {
            depthInfo = vk::RenderingAttachmentInfo{
                .imageView = res(pass.depth->id).view,
                .imageLayout = Layout::eDepthAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .clearValue = vk::ClearValue{vk::ClearDepthStencilValue{pass.depth->clearDepth, 0}},
            };
        }
        const vk::Rect2D area{{0, 0}, pass.renderArea};
        cmd.beginRendering(vk::RenderingInfo{
            .renderArea = area,
            .layerCount = 1,
            .colorAttachmentCount = static_cast<std::uint32_t>(colorInfos.size()),
            .pColorAttachments = colorInfos.data(),
            .pDepthAttachment = pass.depth ? &depthInfo : nullptr,
        });
        cmd.setViewport(0, vk::Viewport{0.0f, 0.0f, static_cast<float>(area.extent.width),
                                        static_cast<float>(area.extent.height), 0.0f, 1.0f});
        cmd.setScissor(0, area);
        pass.record(cmd);
        cmd.endRendering();
        stamp(2 * p + 1);
    }

    for (const Resource& r : resources_) {
        if (r.finalUse == FinalUse::Present) batch.transition(r.img, present());
        else if (r.finalUse == FinalUse::SampledFragment) batch.transition(r.img, sampledFragment());
    }
    batch.flush(cmd);
}
