// Renderer setup: shaders, descriptor layouts and pools, pipelines, the
// material table, per-frame resources and the swapchain-sized targets. The
// per-frame work is renderer.cpp.
#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <span>
#include <stdexcept>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

#include "block_mesh.h"
#include "render_graph.h"
#include "renderer_internal.h"

namespace {

using namespace renderer_detail;

// SPIR-V compiled at build time (glslc -mfmt=num).
const std::uint32_t kGBufferVert[] = {
#include "gbuffer.vert.spv.inc"
};
const std::uint32_t kGBufferFrag[] = {
#include "gbuffer.frag.spv.inc"
};
const std::uint32_t kFullscreenVert[] = {
#include "fullscreen.vert.spv.inc"
};
const std::uint32_t kLightingFrag[] = {
#include "lighting.frag.spv.inc"
};
const std::uint32_t kPresentFrag[] = {
#include "present.frag.spv.inc"
};
const std::uint32_t kLinesVert[] = {
#include "lines.vert.spv.inc"
};
const std::uint32_t kLinesFrag[] = {
#include "lines.frag.spv.inc"
};
const std::uint32_t kTranslucentVert[] = {
#include "translucent.vert.spv.inc"
};
const std::uint32_t kAbsorbanceFrag[] = {
#include "absorbance.frag.spv.inc"
};
const std::uint32_t kTranslucentFrag[] = {
#include "translucent.frag.spv.inc"
};


vk::raii::ShaderModule makeShader(const vk::raii::Device& device, std::span<const std::uint32_t> code) {
    return vk::raii::ShaderModule(device, vk::ShaderModuleCreateInfo{
        .codeSize = code.size_bytes(), .pCode = code.data()});
}

using Bindings = std::vector<vk::DescriptorSetLayoutBinding>;

// Fragment-stage bindings 0, 1, … of the given types.
Bindings fragmentBindings(std::initializer_list<vk::DescriptorType> types) {
    Bindings b;
    for (vk::DescriptorType t : types)
        b.push_back({.binding = static_cast<std::uint32_t>(b.size()), .descriptorType = t, .descriptorCount = 1,
                     .stageFlags = vk::ShaderStageFlagBits::eFragment});
    return b;
}

Bindings samplerBindings(std::uint32_t count) {
    Bindings b;
    for (std::uint32_t i = 0; i < count; ++i)
        b.push_back({.binding = i, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1,
                     .stageFlags = vk::ShaderStageFlagBits::eFragment});
    return b;
}

vk::raii::DescriptorSetLayout makeSetLayout(const vk::raii::Device& device, const Bindings& bindings) {
    return vk::raii::DescriptorSetLayout(device, vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings));
}

// A descriptor pool sized from the layouts it will serve: every set to be
// allocated, with its bindings. One source, so a new binding can't outgrow
// the pool (drivers enforce pool sizes; lavapipe doesn't — REVIEW Q10).
vk::raii::DescriptorPool makePool(const vk::raii::Device& device,
                                  std::initializer_list<std::pair<const Bindings*, std::uint32_t>> sets) {
    std::map<vk::DescriptorType, std::uint32_t> counts;
    std::uint32_t maxSets = 0;
    for (const auto& [bindings, n] : sets) {
        maxSets += n;
        for (const vk::DescriptorSetLayoutBinding& b : *bindings) counts[b.descriptorType] += b.descriptorCount * n;
    }
    std::vector<vk::DescriptorPoolSize> sizes;
    for (const auto& [type, count] : counts) sizes.push_back({.type = type, .descriptorCount = count});
    return vk::raii::DescriptorPool(device, vk::DescriptorPoolCreateInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, .maxSets = maxSets}.setPoolSizes(sizes));
}

template <typename Push>
vk::raii::PipelineLayout makePipelineLayout(const vk::raii::Device& device,
                                            std::initializer_list<vk::DescriptorSetLayout> sets,
                                            vk::ShaderStageFlags pushStages) {
    const vk::PushConstantRange range{.stageFlags = pushStages, .offset = 0, .size = sizeof(Push)};
    return vk::raii::PipelineLayout(device, vk::PipelineLayoutCreateInfo{
        .setLayoutCount = static_cast<std::uint32_t>(sets.size()), .pSetLayouts = sets.begin(),
        .pushConstantRangeCount = 1, .pPushConstantRanges = &range});
}

enum class Geometry {
    Fullscreen,  // fullscreen triangle, no vertex buffer
    Mesh,        // world mesh triangles (MeshVertex); back-face culled
    Translucent, // translucent mesh triangles (MeshVertex, same layout); cull per desc
    Lines,       // debug lines: position, color; line list
};

struct PipelineDesc {
    std::span<const std::uint32_t> vert, frag;
    vk::PipelineLayout layout;
    std::vector<vk::Format> colorFormats;
    vk::Format depthFormat = vk::Format::eUndefined;
    Geometry geometry = Geometry::Fullscreen;
    bool additive = false;  // blend ONE + ONE onto every attachment (lighting onto emission; OIT accumulation)
    std::optional<vk::CullModeFlags> cull;  // default: back for Mesh, none otherwise
};

vk::raii::Pipeline makePipeline(const vk::raii::Device& device, const PipelineDesc& d) {
    const vk::raii::ShaderModule vs = makeShader(device, d.vert);
    const vk::raii::ShaderModule fs = makeShader(device, d.frag);
    const vk::PipelineShaderStageCreateInfo stages[] = {
        {.stage = vk::ShaderStageFlagBits::eVertex, .module = *vs, .pName = "main"},
        {.stage = vk::ShaderStageFlagBits::eFragment, .module = *fs, .pName = "main"},
    };

    using F = vk::Format;
    const vk::VertexInputBindingDescription meshBinding{
        .binding = 0, .stride = sizeof(MeshVertex), .inputRate = vk::VertexInputRate::eVertex};
    const vk::VertexInputAttributeDescription meshAttrs[] = {
        {.location = 0, .binding = 0, .format = F::eR32G32B32Sfloat, .offset = offsetof(MeshVertex, pos)},
        {.location = 1, .binding = 0, .format = F::eR32G32B32Sfloat, .offset = offsetof(MeshVertex, normal)},
        {.location = 2, .binding = 0, .format = F::eR32Uint, .offset = offsetof(MeshVertex, layer)},
        {.location = 3, .binding = 0, .format = F::eR32Uint, .offset = offsetof(MeshVertex, material)},
        {.location = 4, .binding = 0, .format = F::eR32Uint, .offset = offsetof(MeshVertex, texInfo)},
    };
    const vk::VertexInputBindingDescription lineBinding{
        .binding = 0, .stride = sizeof(LineVertex), .inputRate = vk::VertexInputRate::eVertex};
    const vk::VertexInputAttributeDescription lineAttrs[] = {
        {.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(LineVertex, pos)},
        {.location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(LineVertex, color)},
    };
    vk::PipelineVertexInputStateCreateInfo vertexInput{};
    if (d.geometry == Geometry::Mesh) {
        vertexInput.setVertexBindingDescriptions(meshBinding);
        vertexInput.setVertexAttributeDescriptions(meshAttrs);
    } else if (d.geometry == Geometry::Translucent) {  // same layout
        vertexInput.setVertexBindingDescriptions(meshBinding);
        vertexInput.setVertexAttributeDescriptions(meshAttrs);
    } else if (d.geometry == Geometry::Lines) {
        vertexInput.setVertexBindingDescriptions(lineBinding);
        vertexInput.setVertexAttributeDescriptions(lineAttrs);
    }
    const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = d.geometry == Geometry::Lines ? vk::PrimitiveTopology::eLineList
                                                  : vk::PrimitiveTopology::eTriangleList};
    const vk::PipelineViewportStateCreateInfo viewport{.viewportCount = 1, .scissorCount = 1};

    // The world→view basis change is a proper rotation (both frames are
    // right-handed) and the projection keeps view +y = clip +y (down), so
    // outward-CCW world triangles stay CCW in Vulkan framebuffer space. Block
    // rotations are proper too (no mirrors), so they never flip winding.
    const vk::PipelineRasterizationStateCreateInfo raster{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = d.cull ? *d.cull
                           : (d.geometry == Geometry::Mesh ? vk::CullModeFlagBits::eBack : vk::CullModeFlagBits::eNone),
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.0f,
    };
    const vk::PipelineMultisampleStateCreateInfo multisample{.rasterizationSamples = vk::SampleCountFlagBits::e1};
    const bool hasDepth = d.depthFormat != vk::Format::eUndefined;
    const vk::PipelineDepthStencilStateCreateInfo depth{
        .depthTestEnable = hasDepth,
        .depthWriteEnable = hasDepth,
        .depthCompareOp = vk::CompareOp::eLess,
    };
    using B = vk::BlendFactor;
    const std::vector<vk::PipelineColorBlendAttachmentState> blend(
        d.colorFormats.size(),
        vk::PipelineColorBlendAttachmentState{
            .blendEnable = d.additive,
            .srcColorBlendFactor = B::eOne, .dstColorBlendFactor = B::eOne, .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = B::eOne, .dstAlphaBlendFactor = B::eOne, .alphaBlendOp = vk::BlendOp::eAdd,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA});
    const auto colorBlend = vk::PipelineColorBlendStateCreateInfo{}.setAttachments(blend);
    const vk::DynamicState dynStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const auto dynamic = vk::PipelineDynamicStateCreateInfo{}.setDynamicStates(dynStates);

    // Dynamic rendering: attachment formats instead of a VkRenderPass.
    const auto rendering = vk::PipelineRenderingCreateInfo{}
                               .setColorAttachmentFormats(d.colorFormats)
                               .setDepthAttachmentFormat(d.depthFormat);

    const vk::GraphicsPipelineCreateInfo info{
        .pNext = &rendering,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamic,
        .layout = d.layout,
    };
    return vk::raii::Pipeline(device, nullptr, info);
}


// Material table: index 0 is "no material" (GB3 clear value); block type t
// is at BlockRegistry::materialId(t); then type t's GHOST material at
// 1 + types + t (shading-model bit 1 in pbr.w). Per-face texture choice is
// baked into the meshes, so the table holds only per-material constants.
std::vector<GpuMaterial> buildMaterials(const BlockRegistry& registry) {
    const std::size_t types = registry.defs().size();
    std::vector<GpuMaterial> table(1 + 2 * types, GpuMaterial{});
    for (std::size_t t = 0; t < types; ++t) {
        GpuMaterial& ghost = table[1 + types + t];
        ghost.pbr = {0.5f, 0.0f, 0.5f, 1.0f};
        ghost.absorption = glm::vec4(glm::vec3(kGhostSigma), 0.0f);
        ghost.translucentEmission = glm::vec4(glm::vec3(kGhostEmission), 0.0f);
    }
    for (std::size_t t = 0; t < registry.defs().size(); ++t) {
        const BlockDef& def = registry.defs()[t];
        GpuMaterial& m = table[BlockRegistry::materialId(static_cast<BlockTypeId>(t))];
        m.pbr = {def.material.roughness, def.material.metallic, def.material.reflectance, 0.0f};
        m.emissive = glm::vec4(def.material.emissive, 0.0f);
        if (def.translucency) {
            m.absorption = glm::vec4(def.translucency->absorption, 0.0f);
            m.translucentEmission = glm::vec4(def.translucency->emission, 0.0f);
        }
    }
    return table;
}

}  // namespace

const char* debugViewName(DebugView v) {
    switch (v) {
        case DebugView::Lit: return "lit";
        case DebugView::Albedo: return "albedo";
        case DebugView::Normal: return "normal";
        case DebugView::Depth: return "depth";
        case DebugView::RoughMetal: return "roughness/metallic";
        case DebugView::Material: return "material id";
        case DebugView::Variant: return "texture variant";
    }
    return "?";
}

Renderer::Renderer(VkContext& ctx, const BlockRegistry& registry)
    : ctx_(ctx),
      registry_(registry),
      materials_(ctx, sizeof(GpuMaterial) * (2 * registry.defs().size() + 1), vk::BufferUsageFlagBits::eStorageBuffer),
      textures_(ctx, registry.layers()) {
    GBuffer::checkFormats(ctx_);
    // The G-buffer pass has four colour attachments, Vulkan's guaranteed
    // minimum (emission is computed in the lighting pass, REVIEW D2).
    // Host writes before the first submit need no barrier (submission makes
    // them visible to the device).
    const std::vector<GpuMaterial> materials = buildMaterials(registry);
    materials_.write(materials.data(), sizeof(GpuMaterial) * materials.size());

    const vk::raii::Device& dev = ctx_.device;
    targetSampler_ = vk::raii::Sampler(dev, vk::SamplerCreateInfo{
        .magFilter = vk::Filter::eNearest,
        .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .maxLod = 0.0f,
    });

    // Descriptors: G-buffer = texture array + material table; lighting = GB0/GB1/GB2/depth (set 0) + the frame's light buffer
    // (set 1); present = HDR/GB0/GB1/depth/GB3/GB2; lines = depth.
    using T = vk::DescriptorType;
    using S = vk::ShaderStageFlagBits;
    const Bindings gbufferB = fragmentBindings({T::eCombinedImageSampler, T::eStorageBuffer});
    // Lighting: GB0, GB1, GB2, depth, GB3 (material id) + the material table
    // (emission), set 0; the frame's lights, set 1.
    const Bindings lightingB = fragmentBindings({T::eCombinedImageSampler, T::eCombinedImageSampler,
                                                 T::eCombinedImageSampler, T::eCombinedImageSampler,
                                                 T::eCombinedImageSampler, T::eStorageBuffer});
    const Bindings presentB = samplerBindings(9);  // + OIT absorbance, moments, radiance
    const Bindings linesB = samplerBindings(1);
    const Bindings lightsB = fragmentBindings({T::eStorageBuffer});
    // OIT: absorbance = depth + materials (+ block textures at binding 4, as
    // in the shading pass: frame coverage); shading = depth, B0, moments +
    // materials + block textures (frames, ghosts).
    const Bindings absorbanceB = {
        {.binding = 0, .descriptorType = T::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = S::eFragment},
        {.binding = 1, .descriptorType = T::eStorageBuffer, .descriptorCount = 1, .stageFlags = S::eFragment},
        {.binding = 4, .descriptorType = T::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = S::eFragment},
    };
    const Bindings translucentB = fragmentBindings({T::eCombinedImageSampler, T::eCombinedImageSampler,
                                                    T::eCombinedImageSampler, T::eStorageBuffer,
                                                    T::eCombinedImageSampler});  // binding 4: block textures
    gbufferLayout_ = makeSetLayout(dev, gbufferB);
    lightingLayout_ = makeSetLayout(dev, lightingB);
    presentLayout_ = makeSetLayout(dev, presentB);
    linesLayout_ = makeSetLayout(dev, linesB);
    lightsLayout_ = makeSetLayout(dev, lightsB);
    absorbanceLayout_ = makeSetLayout(dev, absorbanceB);
    translucentLayout_ = makeSetLayout(dev, translucentB);
    descriptorPool_ = makePool(dev, {{&gbufferB, 1}, {&lightingB, 1}, {&presentB, 1}, {&linesB, 1},
                                     {&absorbanceB, 1}, {&translucentB, 1},
                                     {&lightsB, static_cast<std::uint32_t>(kFramesInFlight)}});
    const vk::DescriptorSetLayout setLayouts[] = {*gbufferLayout_, *lightingLayout_,   *presentLayout_,
                                                  *linesLayout_,   *absorbanceLayout_, *translucentLayout_};
    vk::raii::DescriptorSets sets(dev, vk::DescriptorSetAllocateInfo{
        .descriptorPool = *descriptorPool_, .descriptorSetCount = 6, .pSetLayouts = setLayouts});
    gbufferSet_ = std::move(sets[0]);
    lightingSet_ = std::move(sets[1]);
    presentSet_ = std::move(sets[2]);
    linesSet_ = std::move(sets[3]);
    absorbanceSet_ = std::move(sets[4]);
    translucentSet_ = std::move(sets[5]);

    const vk::DescriptorImageInfo texInfo{.sampler = *textures_.sampler, .imageView = textures_.image.view(),
                                          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    const vk::DescriptorBufferInfo materialInfo{.buffer = materials_.get(), .offset = 0, .range = VK_WHOLE_SIZE};
    const vk::WriteDescriptorSet gbufferWrites[] = {
        {.dstSet = *gbufferSet_, .dstBinding = 0, .descriptorCount = 1,
         .descriptorType = T::eCombinedImageSampler, .pImageInfo = &texInfo},
        {.dstSet = *gbufferSet_, .dstBinding = 1, .descriptorCount = 1,
         .descriptorType = T::eStorageBuffer, .pBufferInfo = &materialInfo},
        {.dstSet = *lightingSet_, .dstBinding = 5, .descriptorCount = 1,
         .descriptorType = T::eStorageBuffer, .pBufferInfo = &materialInfo},
        {.dstSet = *absorbanceSet_, .dstBinding = 1, .descriptorCount = 1,
         .descriptorType = T::eStorageBuffer, .pBufferInfo = &materialInfo},
        {.dstSet = *absorbanceSet_, .dstBinding = 4, .descriptorCount = 1,
         .descriptorType = T::eCombinedImageSampler, .pImageInfo = &texInfo},
        {.dstSet = *translucentSet_, .dstBinding = 3, .descriptorCount = 1,
         .descriptorType = T::eStorageBuffer, .pBufferInfo = &materialInfo},
        {.dstSet = *translucentSet_, .dstBinding = 4, .descriptorCount = 1,
         .descriptorType = T::eCombinedImageSampler, .pImageInfo = &texInfo},
    };
    dev.updateDescriptorSets(gbufferWrites, {});

    // Pipelines.
    gbufferPipelineLayout_ = makePipelineLayout<GBufferPush>(dev, {*gbufferLayout_}, S::eVertex | S::eFragment);
    lightingPipelineLayout_ = makePipelineLayout<LightingPush>(dev, {*lightingLayout_, *lightsLayout_}, S::eFragment);
    presentPipelineLayout_ = makePipelineLayout<PresentPush>(dev, {*presentLayout_}, S::eFragment);
    linesPipelineLayout_ = makePipelineLayout<LinesPush>(dev, {*linesLayout_}, S::eVertex);
    gbufferPipeline_ = makePipeline(
        dev, {.vert = kGBufferVert, .frag = kGBufferFrag,
              .layout = *gbufferPipelineLayout_,
              .colorFormats = {GBuffer::kGB0, GBuffer::kGB1, GBuffer::kGB2, GBuffer::kGB3},
              .depthFormat = GBuffer::kDepth, .geometry = Geometry::Mesh});
    lightingPipeline_ = makePipeline(dev, {.vert = kFullscreenVert, .frag = kLightingFrag,
                                           .layout = *lightingPipelineLayout_,
                                           .colorFormats = {GBuffer::kHDR}});
    // OIT (render/transparency): absorbance with cullMode NONE and no depth
    // test (the opaque-depth clamp does occlusion); shading with cullMode
    // BACK, depth-tested in the shader against the opaque depth.
    const auto both = S::eVertex | S::eFragment;
    absorbancePipelineLayout_ = makePipelineLayout<TranslucentPush>(dev, {*absorbanceLayout_}, both);
    translucentPipelineLayout_ = makePipelineLayout<TranslucentPush>(dev, {*translucentLayout_, *lightsLayout_}, both);
    absorbancePipeline_ = makePipeline(dev, {.vert = kTranslucentVert, .frag = kAbsorbanceFrag,
                                             .layout = *absorbancePipelineLayout_,
                                             .colorFormats = {GBuffer::kOitB0, GBuffer::kOitMoments},
                                             .geometry = Geometry::Translucent, .additive = true,
                                             .cull = vk::CullModeFlagBits::eNone});
    translucentPipeline_ = makePipeline(dev, {.vert = kTranslucentVert, .frag = kTranslucentFrag,
                                              .layout = *translucentPipelineLayout_,
                                              .colorFormats = {GBuffer::kOitAccum},
                                              .geometry = Geometry::Translucent, .additive = true,
                                              .cull = vk::CullModeFlagBits::eBack});
    createPresentPipeline();

    // Per-frame sync + command buffers.
    for (Frame& f : frames_) {
        f.pool = vk::raii::CommandPool(dev, vk::CommandPoolCreateInfo{
            .flags = vk::CommandPoolCreateFlagBits::eTransient, .queueFamilyIndex = ctx_.queueFamily});
        f.cmd = std::move(vk::raii::CommandBuffers(dev, vk::CommandBufferAllocateInfo{
            .commandPool = *f.pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1})[0]);
        f.imageAvailable = vk::raii::Semaphore(dev, vk::SemaphoreCreateInfo{});
        f.inFlight = vk::raii::Fence(dev, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
        if (ctx_.timestampBits > 0)
            f.timestamps = vk::raii::QueryPool(dev, vk::QueryPoolCreateInfo{
                .queryType = vk::QueryType::eTimestamp, .queryCount = 2 * kMaxTimedPasses});
        f.lines = std::make_unique<HostBuffer>(ctx_, sizeof(LineVertex) * kMaxLineVertices,
                                               vk::BufferUsageFlagBits::eVertexBuffer);
        f.lights = std::make_unique<HostBuffer>(ctx_, sizeof(GpuLight) * kMaxPointLights,
                                                vk::BufferUsageFlagBits::eStorageBuffer);
        f.ghostVertices = std::make_unique<HostBuffer>(ctx_, sizeof(MeshVertex) * 24 * kMaxGhostCells,
                                                       vk::BufferUsageFlagBits::eVertexBuffer);
        f.ghostIndices = std::make_unique<HostBuffer>(ctx_, sizeof(std::uint32_t) * 36 * kMaxGhostCells,
                                                      vk::BufferUsageFlagBits::eIndexBuffer);
        const vk::DescriptorSetLayout layout = *lightsLayout_;
        f.lightSet = std::move(vk::raii::DescriptorSets(dev, vk::DescriptorSetAllocateInfo{
            .descriptorPool = *descriptorPool_, .descriptorSetCount = 1, .pSetLayouts = &layout})[0]);
        const vk::DescriptorBufferInfo lightInfo{.buffer = f.lights->get(), .offset = 0, .range = VK_WHOLE_SIZE};
        dev.updateDescriptorSets(vk::WriteDescriptorSet{.dstSet = *f.lightSet, .dstBinding = 0, .descriptorCount = 1,
                                                        .descriptorType = T::eStorageBuffer, .pBufferInfo = &lightInfo},
                                 {});
    }

    recreateSwapchainResources();
    devUi_ = std::make_unique<DevUi>(ctx_, static_cast<std::uint32_t>(kFramesInFlight));
}

Renderer::~Renderer() { ctx_.device.waitIdle(); }

ImTextureID Renderer::blockIcon(std::uint32_t layer) {
    return devUi_->layerTexture(textures_.image.image(), textures_.image.format(), std::min(layer, textures_.layerCount - 1));
}

// Every pipeline that draws onto the swapchain image follows its format.
void Renderer::createPresentPipeline() {
    presentPipelineFormat_ = ctx_.swapchain.format;
    presentPipeline_ = makePipeline(ctx_.device, {.vert = kFullscreenVert, .frag = kPresentFrag,
                                                  .layout = *presentPipelineLayout_,
                                                  .colorFormats = {presentPipelineFormat_}});
    linesPipeline_ = makePipeline(ctx_.device, {.vert = kLinesVert, .frag = kLinesFrag,
                                                .layout = *linesPipelineLayout_,
                                                .colorFormats = {presentPipelineFormat_},
                                                .geometry = Geometry::Lines});
    if (devUi_) devUi_->setColorFormat(presentPipelineFormat_);
}

// Sizes everything that follows the swapchain. Device must be idle.
void Renderer::recreateSwapchainResources() {
    gbuffer_.reset();
    gbuffer_ = std::make_unique<GBuffer>(ctx_, ctx_.swapchain.extent);
    writeTargetDescriptors();
    if (ctx_.swapchain.format != presentPipelineFormat_) createPresentPipeline();

    const std::size_t n = ctx_.swapchain.images.size();
    renderFinished_.clear();
    for (std::size_t i = 0; i < n; ++i) renderFinished_.emplace_back(ctx_.device, vk::SemaphoreCreateInfo{});
    swapchainStates_.assign(n, ImageState{});
}

void Renderer::writeTargetDescriptors() {
    auto info = [&](const AllocatedImage& img) {
        return vk::DescriptorImageInfo{.sampler = *targetSampler_, .imageView = img.view(),
                                       .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    };
    const vk::DescriptorImageInfo lighting[] = {info(gbuffer_->gb0), info(gbuffer_->gb1), info(gbuffer_->gb2),
                                                info(gbuffer_->depth), info(gbuffer_->gb3)};
    const vk::DescriptorImageInfo present[] = {info(gbuffer_->hdr), info(gbuffer_->gb0), info(gbuffer_->gb1),
                                               info(gbuffer_->depth), info(gbuffer_->gb3), info(gbuffer_->gb2),
                                               info(gbuffer_->oitB0), info(gbuffer_->oitMoments),
                                               info(gbuffer_->oitAccum)};
    const vk::DescriptorImageInfo lines = info(gbuffer_->depth);
    const vk::DescriptorImageInfo translucent[] = {info(gbuffer_->depth), info(gbuffer_->oitB0),
                                                   info(gbuffer_->oitMoments)};
    const vk::WriteDescriptorSet writes[] = {
        {.dstSet = *lightingSet_, .dstBinding = 0, .descriptorCount = 5,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = lighting},
        {.dstSet = *absorbanceSet_, .dstBinding = 0, .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &lines},
        {.dstSet = *translucentSet_, .dstBinding = 0, .descriptorCount = 3,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = translucent},
        {.dstSet = *presentSet_, .dstBinding = 0, .descriptorCount = 9,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = present},
        {.dstSet = *linesSet_, .dstBinding = 0, .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &lines},
    };
    ctx_.device.updateDescriptorSets(writes, {});
}
