#include "dev_ui.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "vk_context.h"

namespace {

void checkVk(VkResult r) {
    if (r == VK_SUCCESS) return;
    std::fprintf(stderr, "[sandbox] imgui_impl_vulkan: %s\n", vk::to_string(static_cast<vk::Result>(r)).c_str());
    if (r < 0) std::abort();
}

ImGui_ImplVulkan_PipelineInfo pipelineInfo(const vk::Format& format) {
    ImGui_ImplVulkan_PipelineInfo p{};
    p.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    p.PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = reinterpret_cast<const VkFormat*>(&format),
    };
    return p;
}

float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

}  // namespace

DevUi::DevUi(VkContext& ctx, std::uint32_t imageCount) : device_(ctx.device), format_(ctx.swapchain.format) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // no imgui.ini in the working directory
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;  // the app owns the cursor mode

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    // ImGui's colors are sRGB values; the swapchain is sRGB and encodes on
    // write, so convert them to linear once (alpha untouched).
    for (ImVec4& c : style.Colors) {
        c.x = srgbToLinear(c.x);
        c.y = srgbToLinear(c.y);
        c.z = srgbToLinear(c.z);
    }

    ImGui_ImplGlfw_InitForVulkan(ctx.window, true);
    const float scale = ImGui_ImplGlfw_GetContentScaleForWindow(ctx.window);  // needs the backend
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = *ctx.instance;
    info.PhysicalDevice = *ctx.physicalDevice;
    info.Device = *ctx.device;
    info.QueueFamily = ctx.queueFamily;
    info.Queue = *ctx.queue;
    // The backend's own pool: its font atlas plus our layer textures
    // (hotbar icons). Drivers enforce the pool's size (lavapipe doesn't).
    info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE + kMaxLayerTextures;
    info.MinImageCount = imageCount;
    info.ImageCount = imageCount;
    info.UseDynamicRendering = true;
    info.PipelineInfoMain = pipelineInfo(format_);
    info.CheckVkResultFn = checkVk;
    ImGui_ImplVulkan_Init(&info);
}

DevUi::~DevUi() {
    for (auto& [key, tex] : layers_) ImGui_ImplVulkan_RemoveTexture(tex.set);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();  // restores the app's GLFW callbacks
    ImGui::DestroyContext();
}

void DevUi::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void DevUi::endFrame() { ImGui::Render(); }

void DevUi::record(vk::CommandBuffer cmd) { ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd); }

void DevUi::setColorFormat(vk::Format format) {
    if (format == format_) return;
    format_ = format;
    const ImGui_ImplVulkan_PipelineInfo p = pipelineInfo(format_);
    ImGui_ImplVulkan_CreateMainPipeline(&p);
}

bool DevUi::wantsMouse() { return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse; }
bool DevUi::wantsKeyboard() { return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard; }

void DevUi::setMouseEnabled(bool enabled) {
    ImGuiIO& io = ImGui::GetIO();
    if (enabled) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    else io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
}

ImTextureID DevUi::layerTexture(vk::Image image, vk::Format format, std::uint32_t layer) {
    const auto key = std::make_pair(static_cast<VkImage>(image), layer);
    auto it = layers_.find(key);
    if (it == layers_.end()) {
        if (layers_.size() >= kMaxLayerTextures) return ImTextureID{};
        LayerTexture tex;
        tex.view = vk::raii::ImageView(device_, vk::ImageViewCreateInfo{
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, layer, 1},
        });
        tex.set = ImGui_ImplVulkan_AddTexture(*tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        it = layers_.emplace(key, std::move(tex)).first;
    }
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(it->second.set));
}

void DevUi::nearestSampler(ImDrawList* list) { list->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr); }
void DevUi::linearSampler(ImDrawList* list) { list->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr); }
