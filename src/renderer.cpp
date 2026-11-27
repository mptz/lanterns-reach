// Renderer per frame: the world mesh upload, timestamps, and recording one
// frame through the render graph. Setup is renderer_setup.cpp.
#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include "block_mesh.h"
#include "render_graph.h"
#include "renderer_internal.h"

using namespace renderer_detail;

glm::mat4 Renderer::viewProjection(const Camera& camera) const {
    const vk::Extent2D extent = ctx_.swapchain.extent;
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    return Camera::projection(Camera::kFovY, aspect, Camera::kNear, Camera::kFar) * camera.view();
}

void Renderer::setWorldMesh(const WorldMesh& mesh) {
    // The current buffers may still be read by frames in flight: retire
    // them, tagged with the current frame number, and free them once every
    // frame that could use them has completed (see drawFrame).
    if (mesh_.vertices) retiredMeshes_.emplace_back(frameNumber_, std::move(mesh_));
    mesh_ = GpuMesh{.origin = mesh.origin};
    if (mesh.indices.empty()) return;
    using U = vk::BufferUsageFlagBits;
    mesh_.vertices = std::make_unique<HostBuffer>(ctx_, sizeof(MeshVertex) * mesh.vertices.size(), U::eVertexBuffer);
    mesh_.vertices->write(mesh.vertices.data(), sizeof(MeshVertex) * mesh.vertices.size());
    mesh_.indices = std::make_unique<HostBuffer>(ctx_, sizeof(std::uint32_t) * mesh.indices.size(), U::eIndexBuffer);
    mesh_.indices->write(mesh.indices.data(), sizeof(std::uint32_t) * mesh.indices.size());
    mesh_.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    std::vector<LineVertex> edges;
    edges.reserve(mesh.edgeLines.size());
    for (const glm::vec3& p : mesh.edgeLines) edges.push_back({p, kWireColor});
    mesh_.edges = std::make_unique<HostBuffer>(ctx_, sizeof(LineVertex) * edges.size(), U::eVertexBuffer);
    mesh_.edges->write(edges.data(), sizeof(LineVertex) * edges.size());
    mesh_.edgeVertexCount = static_cast<std::uint32_t>(edges.size());
    if (!mesh.translucentIndices.empty()) {
        mesh_.tVertices = std::make_unique<HostBuffer>(ctx_, sizeof(MeshVertex) * mesh.translucentVertices.size(),
                                                       U::eVertexBuffer);
        mesh_.tVertices->write(mesh.translucentVertices.data(), sizeof(MeshVertex) * mesh.translucentVertices.size());
        mesh_.tIndices = std::make_unique<HostBuffer>(ctx_, sizeof(std::uint32_t) * mesh.translucentIndices.size(),
                                                      U::eIndexBuffer);
        mesh_.tIndices->write(mesh.translucentIndices.data(), sizeof(std::uint32_t) * mesh.translucentIndices.size());
        mesh_.tIndexCount = static_cast<std::uint32_t>(mesh.translucentIndices.size());
    }
}

void Renderer::readTimestamps(Frame& f) {
    if (f.timedPasses.empty()) return;
    const auto n = static_cast<std::uint32_t>(f.timedPasses.size());
    // The frame's fence has signalled, so every query is available.
    const auto [result, ticks] = f.timestamps.getResults<std::uint64_t>(
        0, 2 * n, 2 * n * sizeof(std::uint64_t), sizeof(std::uint64_t),
        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
    if (result == vk::Result::eSuccess) {
        const std::uint64_t mask = ctx_.timestampBits >= 64 ? ~0ull : (1ull << ctx_.timestampBits) - 1;
        gpuTimes_.clear();
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uint64_t dt = (ticks[2 * i + 1] - ticks[2 * i]) & mask;
            gpuTimes_.push_back({f.timedPasses[i], static_cast<double>(dt) * ctx_.timestampPeriod * 1e-6});
        }
    }
    f.timedPasses.clear();
}

void Renderer::drawFrame(const Camera& camera, const FrameOptions& options, const std::vector<PointLight>& lights,
                         const std::vector<DebugLine>& debugLines, const std::vector<GhostCell>& ghosts) {
    devUi_->endFrame();  // even if this frame is skipped below

    Frame& f = frames_[frameIndex_];
    if (ctx_.device.waitForFences(*f.inFlight, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
        throw std::runtime_error("waitForFences failed");
    readTimestamps(f);
    // Readbacks recorded into this frame are complete now.
    if (f.captureBuffer) {
        const vk::Extent2D e = f.captureExtent;
        std::vector<std::uint8_t> raw(static_cast<std::size_t>(e.width) * e.height * 4);
        f.captureBuffer->read(raw.data(), raw.size());
        const bool bgr = f.captureFormat == vk::Format::eB8G8R8A8Srgb || f.captureFormat == vk::Format::eB8G8R8A8Unorm;
        Capture c{e.width, e.height, std::vector<std::uint8_t>(static_cast<std::size_t>(e.width) * e.height * 3)};
        for (std::size_t i = 0; i < static_cast<std::size_t>(e.width) * e.height; ++i)
            for (std::size_t ch = 0; ch < 3; ++ch) c.rgb[3 * i + ch] = raw[4 * i + (bgr ? 2 - ch : ch)];
        capture_ = std::move(c);
        f.captureBuffer.reset();
    }
    if (f.probeBuffer) {
        glm::vec4 texel;
        f.probeBuffer->read(&texel, sizeof texel);
        probe_ = texel;
        f.probeBuffer.reset();
    }
    // Frames are submitted in order and this frame's fence has signalled, so
    // every frame numbered ≤ frameNumber_ − kFramesInFlight has completed: a
    // mesh retired at frame N is unused once frameNumber_ ≥ N + kFramesInFlight.
    std::erase_if(retiredMeshes_, [&](const auto& r) { return frameNumber_ >= r.first + kFramesInFlight; });
    ++frameNumber_;

    auto recreate = [&] {
        ctx_.device.waitIdle();
        ctx_.recreateSwapchain();
        recreateSwapchainResources();
        resized_ = false;
    };

    std::uint32_t imageIndex = 0;
    const VkResult acq = vkAcquireNextImageKHR(*ctx_.device, *ctx_.swapchain.handle, UINT64_MAX,
                                               *f.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate();
        return;
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("vkAcquireNextImageKHR: " + vk::to_string(static_cast<vk::Result>(acq)));

    ctx_.device.resetFences(*f.inFlight);
    f.pool.reset();
    vk::CommandBuffer cmd = *f.cmd;
    cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    record(f, imageIndex, camera, options, lights, debugLines, ghosts);
    cmd.end();

    const vk::SemaphoreSubmitInfo wait{
        .semaphore = *f.imageAvailable, .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput};
    const vk::SemaphoreSubmitInfo signal{
        .semaphore = *renderFinished_[imageIndex], .stageMask = vk::PipelineStageFlagBits2::eAllCommands};
    const vk::CommandBufferSubmitInfo cmdInfo{.commandBuffer = cmd};
    ctx_.queue.submit2(vk::SubmitInfo2{
        .waitSemaphoreInfoCount = 1, .pWaitSemaphoreInfos = &wait,
        .commandBufferInfoCount = 1, .pCommandBufferInfos = &cmdInfo,
        .signalSemaphoreInfoCount = 1, .pSignalSemaphoreInfos = &signal}, *f.inFlight);

    const VkSemaphore renderDone = *renderFinished_[imageIndex];
    const VkSwapchainKHR swap = *ctx_.swapchain.handle;
    const VkPresentInfoKHR pi{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                              .waitSemaphoreCount = 1, .pWaitSemaphores = &renderDone,
                              .swapchainCount = 1, .pSwapchains = &swap, .pImageIndices = &imageIndex};
    const VkResult pres = vkQueuePresentKHR(*ctx_.queue, &pi);
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;

    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR || resized_) {
        recreate();
    } else if (pres != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR: " + vk::to_string(static_cast<vk::Result>(pres)));
    }
}

void Renderer::record(Frame& frame, std::uint32_t imageIndex, const Camera& camera, const FrameOptions& options,
                      const std::vector<PointLight>& pointLights, const std::vector<DebugLine>& debugLines,
                      const std::vector<GhostCell>& ghosts) {
    const vk::CommandBuffer cmd = *frame.cmd;
    const vk::Extent2D extent = ctx_.swapchain.extent;
    GBuffer& gb = *gbuffer_;

    // The camera is the render origin: the mesh origin (and every light and
    // debug line) is rebased against it in f64, then the f32 matrices built.
    const glm::mat4 viewProj = viewProjection(camera);
    const glm::vec3 meshRel = rebase(mesh_.origin, camera.position).v;
    // Mesh origins lie on the lattice; the fragment shader hashes variant
    // cells and edges from this cell plus the mesh-relative position (32-bit
    // wrap, as on the CPU, block_mesh.cpp resolvePositional).
    const CellPos originCell = cellContaining(mesh_.origin);
    const GBufferPush meshPush{viewProj * glm::translate(glm::mat4(1.0f), meshRel),
                               glm::ivec4(static_cast<int>(originCell.x), static_cast<int>(originCell.y),
                                          static_cast<int>(originCell.z), (options.view == DebugView::Variant ? 1 : 0) | (options.variants ? 0 : 2))};
    const bool hasMesh = mesh_.indexCount > 0;
    const bool drawEdges = options.wireframe && mesh_.edgeVertexCount > 0;

    std::vector<LineVertex> lines;
    for (const DebugLine& l : debugLines) {
        lines.push_back({rebase(l.a, camera.position).v, l.color});
        lines.push_back({rebase(l.b, camera.position).v, l.color});
    }

    // Point lights: rebased like everything else, then uploaded for this
    // frame (its fence has signalled, so the buffer is free).
    const LightingSettings& ls = options.light;
    std::vector<GpuLight> gpuLights;
    for (const PointLight& l : pointLights) {
        if (gpuLights.size() == kMaxPointLights) break;
        const RenderPos rel = rebase(l.pos, camera.position);
        gpuLights.push_back({glm::vec4(rel.v, l.radius), glm::vec4(l.color * (l.intensity * ls.pointScale), 0.0f)});
        if (options.lighting && ls.gizmos) {
            // A small cross at the light, and its radius sphere as three
            // great circles, in the light's color.
            const glm::vec3 c = glm::min(l.color, glm::vec3(1.0f));
            for (int axis = 0; axis < 3; ++axis) {
                glm::vec3 d(0.0f);
                d[axis] = 0.15f;
                lines.push_back({rel.v - d, c});
                lines.push_back({rel.v + d, c});
            }
            for (int plane = 0; plane < 3; ++plane)
                for (int i = 0; i < kSphereSegments; ++i) {
                    auto point = [&](int k) {
                        const float a = 6.2831853f * static_cast<float>(k) / kSphereSegments;
                        glm::vec3 p(0.0f);
                        p[plane] = std::cos(a) * l.radius;
                        p[(plane + 1) % 3] = std::sin(a) * l.radius;
                        return rel.v + p;
                    };
                    lines.push_back({point(i), c});
                    lines.push_back({point(i + 1), c});
                }
        }
    }
    if (!gpuLights.empty()) frame.lights->write(gpuLights.data(), sizeof(GpuLight) * gpuLights.size());

    if (lines.size() > kMaxLineVertices) lines.resize(kMaxLineVertices);
    if (!lines.empty()) frame.lines->write(lines.data(), sizeof(LineVertex) * lines.size());

    // Sun radiance π·E, so a white Lambert face facing it reflects exactly E.
    const LightingPush lightPush{glm::inverse(viewProj),
                                 glm::vec4(-kLightDir, 0.0f),
                                 glm::vec4(glm::vec3(kPi * ls.sun), 0.0f),
                                 glm::vec4(kBackground, 1.0f),
                                 ls.ambient,
                                 static_cast<std::uint32_t>(gpuLights.size())};
    const PresentPush presentPush{static_cast<std::int32_t>(options.view),
                                  Camera::kNear,
                                  Camera::kFar,
                                  kDepthVisRange,
                                  std::exp2(ls.exposureEv),
                                  options.lighting && ls.heatmap ? 1 : 0,
                                  static_cast<float>(kMaxPointLights),
                                  options.oit.view};
    const OitSettings& oit = options.oit;
    const TranslucentPush translucentPush{viewProj,  // the vertex shader adds meshRel itself
                                          glm::vec4(meshRel, ls.ambient),
                                          glm::vec4(-kLightDir, oit.momentBias),
                                          glm::vec4(glm::vec3(kPi * ls.sun), oit.overestimation),
                                          glm::vec4(Camera::kNear, Camera::kFar,
                                                    static_cast<float>(gpuLights.size()), oit.glassFrames ? 1.0f : 0.0f)};
    // Preview ghosts: one group (a ghost cell next to another shares no
    // face), mesh-relative to the first ghost's corner, rebased like the world.
    std::vector<MeshVertex> ghostVerts;
    std::vector<std::uint32_t> ghostIdx;
    const WorldPos ghostOrigin = ghosts.empty() ? WorldPos{} : cellCorner(ghosts.front().cell);
    {
        auto isGhost = [&](const CellPos& c) {
            return std::any_of(ghosts.begin(), ghosts.end(), [&](const GhostCell& g) { return g.cell == c; });
        };
        for (std::size_t i = 0; i < ghosts.size() && i < kMaxGhostCells; ++i) {
            const GhostCell& g = ghosts[i];
            const glm::vec3 corner(rebase(cellCorner(g.cell), ghostOrigin).v);  // small offsets: exact
            for (int w = 0; w < orient::kFaceCount; ++w) {
                const auto face = static_cast<orient::Face>(w);
                if (ghosts.size() > 1 && isGhost(g.cell + orient::faceNormal(face))) continue;
                // Ghost faces are single cells: a random or matched tile is
                // chosen here, with the shader's own code (variants.glsl), so
                // the ghost shows the tile the placed block will.
                const FaceTexture tex = resolvePositional(faceTexture(registry_.states(), g.state, face), g.cell, face);
                appendFace(ghostVerts, ghostIdx, tex, face, corner, ghostMaterialId(registry_.states().type(g.state)), kGhostInflate);
            }
        }
    }
    if (!ghostIdx.empty()) {
        frame.ghostVertices->write(ghostVerts.data(), sizeof(MeshVertex) * ghostVerts.size());
        frame.ghostIndices->write(ghostIdx.data(), sizeof(std::uint32_t) * ghostIdx.size());
    }
    const auto ghostIndexCount = static_cast<std::uint32_t>(ghostIdx.size());
    TranslucentPush ghostPush = translucentPush;
    ghostPush.meshRel = glm::vec4(rebase(ghostOrigin, camera.position).v, ls.ambient);
    const bool hasTranslucent = mesh_.tIndexCount > 0 || ghostIndexCount > 0;
    const LinesPush linesPush{viewProj, glm::vec4(0.0f, 0.0f, 0.0f, kLineBias)};  // already camera-relative
    const LinesPush edgesPush{viewProj, glm::vec4(meshRel, kLineBias)};          // mesh-relative

    // Swapchain contents are discarded every frame; the acquire semaphore
    // wait is at COLOR_ATTACHMENT_OUTPUT, so the first barrier chains off it.
    ImageState& swapState = swapchainStates_[imageIndex];
    swapState = {vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eNone,
                 vk::ImageLayout::eUndefined};

    RenderGraph graph;
    const ResourceId swap = graph.importImage(
        {"swapchain", ctx_.swapchain.images[imageIndex], *ctx_.swapchain.views[imageIndex],
         vk::ImageAspectFlagBits::eColor, &swapState},
        FinalUse::Present);
    const ResourceId gb0 = graph.importImage(gb.gb0.graphImage());
    const ResourceId gb1 = graph.importImage(gb.gb1.graphImage());
    const ResourceId gb2 = graph.importImage(gb.gb2.graphImage());
    const ResourceId gb3 = graph.importImage(gb.gb3.graphImage());
    const ResourceId depth = graph.importImage(gb.depth.graphImage());
    const ResourceId hdr = graph.importImage(gb.hdr.graphImage());
    const ResourceId textures = graph.importImage(textures_.image.graphImage());
    const ResourceId oitB0 = graph.importImage(gb.oitB0.graphImage());
    const ResourceId oitMoments = graph.importImage(gb.oitMoments.graphImage());
    const ResourceId oitAccum = graph.importImage(gb.oitAccum.graphImage());

    graph.addPass(PassDesc{
        .name = "gbuffer",
        .colors = {{.id = gb0, .clear = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}},
                   {.id = gb1, .clear = vk::ClearColorValue{0.5f, 0.5f, 0.0f, 0.0f}},
                   {.id = gb2, .clear = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}},
                   {.id = gb3, .clear = vk::ClearColorValue{std::array<std::uint32_t, 4>{0, 0, 0, 0}}}},
        .depth = DepthAttachment{.id = depth, .clearDepth = 1.0f},
        .renderArea = extent,
        .sampled = {textures},
        .record = [&](vk::CommandBuffer c) {
            c.bindPipeline(vk::PipelineBindPoint::eGraphics, *gbufferPipeline_);
            if (!hasMesh) return;
            c.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *gbufferPipelineLayout_, 0, *gbufferSet_, {});
            c.bindVertexBuffers(0, mesh_.vertices->get(), vk::DeviceSize{0});
            c.bindIndexBuffer(mesh_.indices->get(), 0, vk::IndexType::eUint32);
            c.pushConstants(*gbufferPipelineLayout_, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                            sizeof(GBufferPush), &meshPush);
            c.drawIndexed(mesh_.indexCount, 1, 0, 0, 0);  // one draw for the whole world
        },
    });
    graph.addPass(PassDesc{
        .name = "lighting",
        .colors = {{.id = hdr, .load = vk::AttachmentLoadOp::eDontCare}},  // every pixel written: emission + lighting
        .renderArea = extent,
        .sampled = {gb0, gb1, gb2, depth, gb3},
        .record = [&](vk::CommandBuffer c) {
            c.bindPipeline(vk::PipelineBindPoint::eGraphics, *lightingPipeline_);
            c.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *lightingPipelineLayout_, 0,
                                 {*lightingSet_, *frame.lightSet}, {});
            c.pushConstants(*lightingPipelineLayout_, vk::ShaderStageFlagBits::eFragment, 0, sizeof(lightPush),
                            &lightPush);
            c.draw(3, 1, 0, 0);
        },
    });
    // Both translucency passes draw the same geometry: the world's translucent
    // mesh, then the preview ghosts (order doesn't matter: both passes sum).
    auto drawTranslucent = [&](vk::CommandBuffer c, vk::PipelineLayout layout) {
        const auto stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        if (mesh_.tIndexCount > 0) {
            c.bindVertexBuffers(0, mesh_.tVertices->get(), vk::DeviceSize{0});
            c.bindIndexBuffer(mesh_.tIndices->get(), 0, vk::IndexType::eUint32);
            c.pushConstants(layout, stages, 0, sizeof(TranslucentPush), &translucentPush);
            c.drawIndexed(mesh_.tIndexCount, 1, 0, 0, 0);
        }
        if (ghostIndexCount > 0) {
            c.bindVertexBuffers(0, frame.ghostVertices->get(), vk::DeviceSize{0});
            c.bindIndexBuffer(frame.ghostIndices->get(), 0, vk::IndexType::eUint32);
            c.pushConstants(layout, stages, 0, sizeof(TranslucentPush), &ghostPush);
            c.drawIndexed(ghostIndexCount, 1, 0, 0, 0);
        }
    };

    // Moment-based OIT, pass 1: total absorbance + moments as boundary
    // integrals (render/transparency). The targets are cleared even with no
    // translucent geometry, since present reads them.
    graph.addPass(PassDesc{
        .name = "absorbance",
        .colors = {{.id = oitB0, .clear = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}},
                   {.id = oitMoments, .clear = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}}},
        .renderArea = extent,
        .sampled = {depth, textures},
        .record = [&](vk::CommandBuffer c) {
            if (!hasTranslucent) return;
            c.bindPipeline(vk::PipelineBindPoint::eGraphics, *absorbancePipeline_);
            c.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *absorbancePipelineLayout_, 0, *absorbanceSet_, {});
            drawTranslucent(c, *absorbancePipelineLayout_);
        },
    });
    // Pass 2: shade camera-facing translucent faces, attenuated by the
    // transmittance reconstructed from the moments, added up.
    graph.addPass(PassDesc{
        .name = "translucent",
        .colors = {{.id = oitAccum, .clear = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 0.0f}}},
        .renderArea = extent,
        .sampled = {depth, oitB0, oitMoments, textures},
        .record = [&](vk::CommandBuffer c) {
            if (!hasTranslucent) return;
            c.bindPipeline(vk::PipelineBindPoint::eGraphics, *translucentPipeline_);
            c.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *translucentPipelineLayout_, 0,
                                 {*translucentSet_, *frame.lightSet}, {});
            drawTranslucent(c, *translucentPipelineLayout_);
        },
    });
    graph.addPass(PassDesc{
        .name = "present",
        .colors = {{.id = swap, .load = vk::AttachmentLoadOp::eDontCare}},
        .renderArea = extent,
        .sampled = {hdr, gb0, gb1, depth, gb3, gb2, oitB0, oitMoments, oitAccum},
        .record = [&](vk::CommandBuffer c) {
            c.bindPipeline(vk::PipelineBindPoint::eGraphics, *presentPipeline_);
            c.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *presentPipelineLayout_, 0, *presentSet_, {});
            c.pushConstants(*presentPipelineLayout_, vk::ShaderStageFlagBits::eFragment, 0, sizeof(presentPush),
                            &presentPush);
            c.draw(3, 1, 0, 0);
        },
    });
    if (!lines.empty() || drawEdges) {
        // Caller lines (F4/F6/F10) + F5 light gizmos + the F3 face outlines,
        // over the final image.
        graph.addPass(PassDesc{
            .name = "lines",
            .colors = {{.id = swap, .load = vk::AttachmentLoadOp::eLoad}},
            .renderArea = extent,
            .sampled = {depth},
            .record = [&](vk::CommandBuffer c) {
                c.bindPipeline(vk::PipelineBindPoint::eGraphics, *linesPipeline_);
                c.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *linesPipelineLayout_, 0, *linesSet_, {});
                if (!lines.empty()) {
                    c.bindVertexBuffers(0, frame.lines->get(), vk::DeviceSize{0});
                    c.pushConstants(*linesPipelineLayout_, vk::ShaderStageFlagBits::eVertex, 0, sizeof(LinesPush),
                                    &linesPush);
                    c.draw(static_cast<std::uint32_t>(lines.size()), 1, 0, 0);
                }
                if (drawEdges) {
                    c.bindVertexBuffers(0, mesh_.edges->get(), vk::DeviceSize{0});
                    c.pushConstants(*linesPipelineLayout_, vk::ShaderStageFlagBits::eVertex, 0, sizeof(LinesPush),
                                    &edgesPush);
                    c.draw(mesh_.edgeVertexCount, 1, 0, 0);
                }
            },
        });
    }
    // Dev UI last (render/dev-ui): after tonemap, at native resolution.
    graph.addPass(PassDesc{
        .name = "ui",
        .colors = {{.id = swap, .load = vk::AttachmentLoadOp::eLoad}},
        .renderArea = extent,
        .record = [&](vk::CommandBuffer c) { devUi_->record(c); },
    });

    // Readbacks for headless checks: the final image (--capture) and one OIT
    // absorbance texel (--check-absorbance), copied into host buffers that
    // drawFrame reads after this frame's fence.
    if (wantCapture_) {
        wantCapture_ = false;
        frame.captureExtent = extent;
        frame.captureFormat = ctx_.swapchain.format;
        frame.captureBuffer = std::make_unique<HostBuffer>(
            ctx_, static_cast<vk::DeviceSize>(extent.width) * extent.height * 4, vk::BufferUsageFlagBits::eTransferDst);
        graph.addPass(PassDesc{
            .name = "capture",
            .transferSrc = {swap},
            .record = [&](vk::CommandBuffer c) {
                c.copyImageToBuffer(ctx_.swapchain.images[imageIndex], vk::ImageLayout::eTransferSrcOptimal,
                                    frame.captureBuffer->get(),
                                    vk::BufferImageCopy{.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                                        .imageExtent = {extent.width, extent.height, 1}});
            },
        });
    }
    if (wantProbe_) {
        const glm::ivec2 px = glm::clamp(*wantProbe_, glm::ivec2(0),
                                         glm::ivec2(static_cast<int>(extent.width) - 1, static_cast<int>(extent.height) - 1));
        wantProbe_.reset();
        frame.probeBuffer = std::make_unique<HostBuffer>(ctx_, sizeof(glm::vec4), vk::BufferUsageFlagBits::eTransferDst);
        graph.addPass(PassDesc{
            .name = "probe",
            .transferSrc = {oitB0},
            .record = [&, px](vk::CommandBuffer c) {
                c.copyImageToBuffer(gb.oitB0.image(), vk::ImageLayout::eTransferSrcOptimal, frame.probeBuffer->get(),
                                    vk::BufferImageCopy{.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                                        .imageOffset = {px.x, px.y, 0},
                                                        .imageExtent = {1, 1, 1}});
            },
        });
    }

    if (*frame.timestamps && graph.passCount() <= kMaxTimedPasses) {
        graph.timePasses(*frame.timestamps);
        for (std::size_t i = 0; i < graph.passCount(); ++i) frame.timedPasses.push_back(graph.passName(i));
    }
    graph.execute(cmd);
}
