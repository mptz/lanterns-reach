#pragma once
// Instance / physical device / device / swapchain via vk-bootstrap, owned
// through Vulkan-Hpp RAII handles; VMA allocator. Vulkan 1.3 with
// dynamicRendering + synchronization2 required.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>

struct GLFWwindow;

struct ValidationStats {
    std::atomic<std::uint32_t> errors{0};
    std::atomic<std::uint32_t> warnings{0};
};

class VmaAllocatorOwner {
public:
    VmaAllocatorOwner() = default;
    explicit VmaAllocatorOwner(VmaAllocator a) : allocator_(a) {}
    VmaAllocatorOwner(const VmaAllocatorOwner&) = delete;
    VmaAllocatorOwner& operator=(const VmaAllocatorOwner&) = delete;
    VmaAllocatorOwner& operator=(VmaAllocatorOwner&& o) noexcept {
        std::swap(allocator_, o.allocator_);
        return *this;
    }
    ~VmaAllocatorOwner() { if (allocator_) vmaDestroyAllocator(allocator_); }
    VmaAllocator get() const { return allocator_; }

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
};

struct Swapchain {
    vk::raii::SwapchainKHR handle = nullptr;
    vk::Format format = vk::Format::eUndefined;
    vk::Extent2D extent{};
    std::vector<vk::Image> images;              // owned by the swapchain
    std::vector<vk::raii::ImageView> views;
};

// Process-lifetime counters, so messages emitted during teardown still count.
ValidationStats& validationStats();

// Whether VK_LAYER_KHRONOS_validation can be enabled. Needs no window or
// display, so the Debug-build check runs before GLFW is initialized.
bool validationLayerAvailable();

class VkContext {
public:
    // enableValidation makes the validation layer REQUIRED: instance
    // creation fails if it is missing (never a silent fallback).
    VkContext(GLFWwindow* window, bool enableValidation);
    VkContext(const VkContext&) = delete;
    VkContext& operator=(const VkContext&) = delete;

    // Recreate the swapchain at the window's current framebuffer size.
    // Caller must ensure the device is idle.
    void recreateSwapchain();

    // Fails loudly if a format lacks the features the G-buffer needs.
    void requireFormat(vk::Format f, vk::FormatFeatureFlags2 features, const char* what) const;

    // Record + submit + wait (setup-time uploads only).
    void submitOnce(const std::function<void(vk::CommandBuffer)>& record);

    GLFWwindow* window;
    vk::raii::Context context;
    vk::raii::Instance instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT messenger = nullptr;
    vk::raii::SurfaceKHR surface = nullptr;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;
    vk::raii::Queue queue = nullptr;  // graphics + present
    std::uint32_t queueFamily = 0;
    VmaAllocatorOwner allocator;
    Swapchain swapchain;

    bool validation = false;          // layer enabled on this instance
    std::string gpuName;
    float timestampPeriod = 0.0f;     // ns per timestamp tick
    float maxSamplerAnisotropy = 0.0f;  // 0 = samplerAnisotropy not enabled
    std::uint32_t timestampBits = 0;  // valid bits on the graphics queue; 0 = no GPU timing
};
