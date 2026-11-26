#pragma once
// Developer UI (render/dev-ui): Dear ImGui with the official GLFW + Vulkan
// backends. The renderer records it as one render-graph pass at the end of
// the frame, onto the swapchain image at native resolution. Player-facing
// UI never goes through here.
//
// Frame protocol: beginFrame() → build windows (ImGui::*) → the renderer
// calls endFrame() and, when it records a frame, record() inside the UI pass.

#include <cstdint>
#include <map>
#include <utility>

#include <imgui.h>
#include <vulkan/vulkan_raii.hpp>

class VkContext;

class DevUi {
public:
    // Installs ImGui's GLFW callbacks, chained after the app's — so the app
    // must set its own callbacks first. imageCount ≥ frames in flight.
    DevUi(VkContext& ctx, std::uint32_t imageCount);
    ~DevUi();
    DevUi(const DevUi&) = delete;
    DevUi& operator=(const DevUi&) = delete;

    void beginFrame();
    void endFrame();                     // ImGui::Render(); call once per beginFrame()
    void record(vk::CommandBuffer cmd);  // draws the last endFrame()'s data
    void setColorFormat(vk::Format format);

    // ImGui's claim on input (e.g. the cursor is over a dev window). While
    // set, game input ignores the device.
    static bool wantsMouse();
    static bool wantsKeyboard();
    // Off while the cursor is captured for mouse-look, so the hidden cursor
    // never hovers (or clicks) dev windows.
    static void setMouseEnabled(bool enabled);

    // One layer (mip 0) of a 2D-array image in SHADER_READ_ONLY_OPTIMAL as
    // an ImGui texture (created on first use, kept until shutdown). Draw it
    // between nearestSampler() and linearSampler() for crisp pixel art.
    // At most kMaxLayerTextures distinct layers; past that, returns 0 (draw
    // nothing) rather than exhausting the backend's descriptor pool.
    ImTextureID layerTexture(vk::Image image, vk::Format format, std::uint32_t layer);
    static constexpr std::uint32_t kMaxLayerTextures = 256;  // room for every block-texture layer; arbitrary
    static void nearestSampler(ImDrawList* list);
    static void linearSampler(ImDrawList* list);

private:
    struct LayerTexture {
        vk::raii::ImageView view = nullptr;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    const vk::raii::Device& device_;
    vk::Format format_;
    std::map<std::pair<VkImage, std::uint32_t>, LayerTexture> layers_;
};
