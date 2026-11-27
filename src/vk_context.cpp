#include "vk_context.h"

#include <cstdio>
#include <stdexcept>
#include <string>

#include <GLFW/glfw3.h>
#include <VkBootstrap.h>

namespace {

template <typename T>
T unwrap(vkb::Result<T> r, const char* what) {
    if (!r) throw std::runtime_error(std::string(what) + ": " + r.error().message());
    return r.value();
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    auto* stats = static_cast<ValidationStats*>(user);
    const char* label = "info";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        label = "ERROR";
        stats->errors.fetch_add(1);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        label = "warning";
        stats->warnings.fetch_add(1);
    }
    std::fprintf(stderr, "[vulkan %s] %s\n", label, data->pMessage);
    return VK_FALSE;
}

}  // namespace

ValidationStats& validationStats() {
    static ValidationStats stats;
    return stats;
}

bool validationLayerAvailable() {
    const auto info = vkb::SystemInfo::get_system_info();
    return info && info.value().validation_layers_available;
}

VkContext::VkContext(GLFWwindow* w, bool enableValidation) : window(w), validation(enableValidation) {
    // --- instance ---------------------------------------------------------
    std::uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    if (!glfwExts) throw std::runtime_error("GLFW: Vulkan surface extensions unavailable");

    vkb::InstanceBuilder ib;
    ib.set_app_name("sandbox " SANDBOX_VERSION)
        .set_app_version(SANDBOX_VERSION_MAJOR, SANDBOX_VERSION_MINOR, SANDBOX_VERSION_PATCH)
        .require_api_version(1, 3, 0)
        .enable_extensions(glfwExtCount, glfwExts);
    if (enableValidation) {
        ib.enable_validation_layers(true)
            .set_debug_callback(debugCallback)
            .set_debug_callback_user_data_pointer(&validationStats())
            .set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            .add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
    }
    vkb::Instance vkbInstance = unwrap(ib.build(), "instance");
    instance = vk::raii::Instance(context, vkbInstance.instance);
    if (vkbInstance.debug_messenger)
        messenger = vk::raii::DebugUtilsMessengerEXT(instance, vkbInstance.debug_messenger);

    // --- surface ----------------------------------------------------------
    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface) != VK_SUCCESS)
        throw std::runtime_error("glfwCreateWindowSurface failed");
    surface = vk::raii::SurfaceKHR(instance, rawSurface);

    // --- physical device + device ----------------------------------------
    VkPhysicalDeviceVulkan13Features f13{};
    f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;

    // Buffer device addresses: the mesh shaders pull packed quads and
    // appearance payloads through pointers in push constants (v0.2.5).
    VkPhysicalDeviceVulkan12Features f12{};
    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.bufferDeviceAddress = VK_TRUE;

    vkb::PhysicalDeviceSelector selector(vkbInstance, *surface);
    vkb::PhysicalDevice vkbPhys = unwrap(
        selector.set_minimum_version(1, 3).set_required_features_12(f12).set_required_features_13(f13).select(),
        "physical device");
    // Anisotropic filtering (block textures at grazing angles) where the
    // device has it: re-select with the feature required, which picks the
    // same device and enables it.
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(vkbPhys.physical_device, &supported);
    if (supported.samplerAnisotropy == VK_TRUE) {
        VkPhysicalDeviceFeatures want{};
        want.samplerAnisotropy = VK_TRUE;
        vkbPhys = unwrap(selector.set_required_features(want).select(), "physical device (anisotropy)");
        maxSamplerAnisotropy = vkbPhys.properties.limits.maxSamplerAnisotropy;
    }
    gpuName = vkbPhys.properties.deviceName;
    std::fprintf(stderr, "[sandbox] GPU: %s\n", gpuName.c_str());

    vkb::Device vkbDevice = unwrap(vkb::DeviceBuilder(vkbPhys).build(), "device");
    queueFamily = unwrap(vkbDevice.get_queue_index(vkb::QueueType::graphics), "graphics queue index");

    physicalDevice = vk::raii::PhysicalDevice(instance, vkbPhys.physical_device);
    device = vk::raii::Device(physicalDevice, vkbDevice.device);
    if (!physicalDevice.getSurfaceSupportKHR(queueFamily, *surface))
        throw std::runtime_error("graphics queue family cannot present to this surface");
    queue = device.getQueue(queueFamily, 0);
    timestampPeriod = vkbPhys.properties.limits.timestampPeriod;
    timestampBits = physicalDevice.getQueueFamilyProperties()[queueFamily].timestampValidBits;

    // --- VMA --------------------------------------------------------------
    VmaAllocatorCreateInfo aci{};
    aci.vulkanApiVersion = VK_API_VERSION_1_3;
    aci.instance = *instance;
    aci.physicalDevice = *physicalDevice;
    aci.device = *device;
    aci.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    VmaAllocator a = VK_NULL_HANDLE;
    if (vmaCreateAllocator(&aci, &a) != VK_SUCCESS) throw std::runtime_error("vmaCreateAllocator failed");
    allocator = VmaAllocatorOwner(a);

    recreateSwapchain();
}

void VkContext::recreateSwapchain() {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);

    vkb::SwapchainBuilder sb(*physicalDevice, *device, *surface, queueFamily, queueFamily);
    // sRGB swapchain: the hardware does the linear -> sRGB encode on write.
    sb.set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .add_fallback_format({VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h))
        // Transfer source: --capture reads the final image back.
        .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        .set_old_swapchain(*swapchain.handle);
    vkb::Swapchain vkbSwap = unwrap(sb.build(), "swapchain");

    swapchain.views.clear();
    swapchain.handle = vk::raii::SwapchainKHR(device, vkbSwap.swapchain);  // destroys the old one
    swapchain.format = static_cast<vk::Format>(vkbSwap.image_format);
    swapchain.extent = vkbSwap.extent;
    swapchain.images = swapchain.handle.getImages();
    for (vk::Image img : swapchain.images) {
        swapchain.views.emplace_back(device, vk::ImageViewCreateInfo{
            .image = img,
            .viewType = vk::ImageViewType::e2D,
            .format = swapchain.format,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        });
    }
    if (swapchain.format != vk::Format::eB8G8R8A8Srgb && swapchain.format != vk::Format::eR8G8B8A8Srgb)
        std::fprintf(stderr, "[sandbox] warning: non-sRGB swapchain format %s\n",
                     vk::to_string(swapchain.format).c_str());
}

void VkContext::requireFormat(vk::Format f, vk::FormatFeatureFlags2 features, const char* what) const {
    auto chain = physicalDevice.getFormatProperties2<vk::FormatProperties2, vk::FormatProperties3>(f);
    const auto have = chain.get<vk::FormatProperties3>().optimalTilingFeatures;
    if ((have & features) != features)
        throw std::runtime_error(std::string("format ") + vk::to_string(f) + " lacks required features for " + what);
}

void VkContext::submitOnce(const std::function<void(vk::CommandBuffer)>& record) {
    vk::raii::CommandPool pool(device, vk::CommandPoolCreateInfo{
        .flags = vk::CommandPoolCreateFlagBits::eTransient, .queueFamilyIndex = queueFamily});
    vk::raii::CommandBuffers cmds(device, vk::CommandBufferAllocateInfo{
        .commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1});
    vk::CommandBuffer cmd = *cmds[0];
    cmd.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    record(cmd);
    cmd.end();
    vk::raii::Fence fence(device, vk::FenceCreateInfo{});
    const vk::CommandBufferSubmitInfo cmdInfo{.commandBuffer = cmd};
    queue.submit2(vk::SubmitInfo2{.commandBufferInfoCount = 1, .pCommandBufferInfos = &cmdInfo}, *fence);
    if (device.waitForFences(*fence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
        throw std::runtime_error("submitOnce: fence wait failed");
}
