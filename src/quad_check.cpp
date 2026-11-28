#include "quad_check.h"

#include <array>
#include <cstdio>
#include <random>
#include <span>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "cube.h"
#include "gpu_buffer.h"
#include "packed_quad.h"
#include "vk_context.h"

namespace {

const std::uint32_t kQuadCheckComp[] = {
#include "quad_check.comp.spv.inc"
};

struct Push {
    std::uint64_t quads = 0, results = 0;
    std::uint32_t count = 0;
};

constexpr std::size_t kWordsPerQuad = 7;  // uvec4s per quad in the results (quad_check.comp)

std::vector<std::uint64_t> pinnedQuads() {
    std::vector<std::uint64_t> out;
    for (int cell : {0, 63})
        for (int ext : {1, 64})
            for (int d = 0; d < 6; ++d)
                for (std::uint32_t bits : {0u, 5u, 10u, 15u})
                    for (std::uint32_t app : {0u, 1u, quad::kMaxAppearance})
                        out.push_back(quad::pack({.cell = {cell, 63 - cell, cell}, .w = ext, .h = 65 - ext,
                                                  .face = static_cast<orient::Face>(d), .sides = bits,
                                                  .corners = 15u - bits, .appearance = app}));
    std::mt19937_64 rng(25);
    for (int i = 0; i < 1000; ++i) out.push_back(rng());  // any 64 bits is a quad (a face of 6 or 7 is checked below)
    return out;
}

}  // namespace

bool checkQuadUnpacking(VkContext& ctx) {
    std::vector<std::uint64_t> quads = pinnedQuads();
    // Only directions 0–5 exist: random words with 6 or 7 there are remapped.
    for (std::uint64_t& v : quads)
        if (((v >> 30) & 7u) >= 6u) v &= ~(std::uint64_t{4} << 30);
    const auto count = static_cast<std::uint32_t>(quads.size());

    using U = vk::BufferUsageFlagBits;
    HostBuffer in(ctx, sizeof(std::uint64_t) * quads.size(), U::eStorageBuffer | U::eShaderDeviceAddress);
    in.write(quads.data(), sizeof(std::uint64_t) * quads.size());
    const std::size_t resultBytes = sizeof(glm::uvec4) * kWordsPerQuad * quads.size();
    HostBuffer out(ctx, resultBytes, U::eStorageBuffer | U::eShaderDeviceAddress);

    const vk::raii::Device& dev = ctx.device;
    const std::span<const std::uint32_t> code(kQuadCheckComp);
    const vk::raii::ShaderModule module(dev, vk::ShaderModuleCreateInfo{.codeSize = code.size_bytes(), .pCode = code.data()});
    const vk::PushConstantRange range{.stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = sizeof(Push)};
    const vk::raii::PipelineLayout layout(dev, vk::PipelineLayoutCreateInfo{.pushConstantRangeCount = 1, .pPushConstantRanges = &range});
    const vk::raii::Pipeline pipeline(
        dev, nullptr,
        vk::ComputePipelineCreateInfo{
            .stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = *module, .pName = "main"}, .layout = *layout});
    const vk::raii::CommandPool pool(dev, vk::CommandPoolCreateInfo{.queueFamilyIndex = ctx.queueFamily});
    vk::raii::CommandBuffers cmds(dev, vk::CommandBufferAllocateInfo{
        .commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1});
    const vk::CommandBuffer cmd = *cmds[0];
    cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    const Push push{in.address(), out.address(), count};
    cmd.pushConstants(*layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(Push), &push);
    cmd.dispatch((count + 63) / 64, 1, 1);
    // The shader's writes, made visible to the host's read after the fence.
    const vk::MemoryBarrier2 toHost{.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                                    .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                                    .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                    .dstAccessMask = vk::AccessFlagBits2::eHostRead};
    cmd.pipelineBarrier2(vk::DependencyInfo{.memoryBarrierCount = 1, .pMemoryBarriers = &toHost});
    cmd.end();
    const vk::raii::Fence fence(dev, vk::FenceCreateInfo{});
    const vk::CommandBufferSubmitInfo cmdInfo{.commandBuffer = cmd};
    ctx.queue.submit2(vk::SubmitInfo2{.commandBufferInfoCount = 1, .pCommandBufferInfos = &cmdInfo}, *fence);
    if (dev.waitForFences(*fence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess) {
        std::fprintf(stderr, "[sandbox] check-quads: FAIL (the dispatch didn't complete)\n");
        return false;
    }
    std::vector<glm::uvec4> got(kWordsPerQuad * quads.size());
    out.read(got.data(), resultBytes);

    std::size_t failures = 0;
    const auto& cube = cube::vertices();
    for (std::size_t i = 0; i < quads.size(); ++i) {
        const quad::Quad q = quad::unpack(quads[i]);
        const int d = static_cast<int>(q.face);
        std::array<glm::uvec4, kWordsPerQuad> want{};
        want[0] = {static_cast<unsigned>(q.cell.x), static_cast<unsigned>(q.cell.y), static_cast<unsigned>(q.cell.z),
                   static_cast<unsigned>(d)};
        want[1] = {static_cast<unsigned>(q.w), static_cast<unsigned>(q.h), q.sides, q.corners};
        want[2] = {q.appearance, 0u, 0u, 0u};
        for (int k = 0; k < 4; ++k) {  // the unit cube's corner k, scaled along the face's axes (as v0.2.4's vertices)
            const glm::vec3 unit = cube[static_cast<std::size_t>(d * 4 + k)].pos + glm::vec3(0.5f);
            glm::uvec3 p;
            for (int a = 0; a < 3; ++a) {
                const int scale = a == quad::kBitAxis[d] ? q.w : a == quad::kRowAxis[d] ? q.h : 1;
                p[a] = static_cast<unsigned>(q.cell[a] + static_cast<int>(unit[a]) * scale);
            }
            want[static_cast<std::size_t>(3 + k)] = glm::uvec4(p, 0u);
        }
        for (std::size_t w = 0; w < kWordsPerQuad; ++w)
            if (got[i * kWordsPerQuad + w] != want[w]) {
                if (failures < 10)
                    std::fprintf(stderr, "[sandbox] check-quads: quad %zu (0x%016llx) word %zu: shader (%u %u %u %u), C++ (%u %u %u %u)\n", i,
                                 static_cast<unsigned long long>(quads[i]), w, got[i * kWordsPerQuad + w].x, got[i * kWordsPerQuad + w].y,
                                 got[i * kWordsPerQuad + w].z, got[i * kWordsPerQuad + w].w, want[w].x, want[w].y, want[w].z, want[w].w);
                ++failures;
                break;
            }
    }
    std::fprintf(stderr, "[sandbox] check-quads: %s (%u quads, %zu differ)\n", failures == 0 ? "PASS" : "FAIL", count, failures);
    return failures == 0;
}
