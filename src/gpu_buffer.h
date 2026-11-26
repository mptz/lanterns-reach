#pragma once
// Host-visible, persistently-mapped VMA buffer. v0.1's buffers are tiny
// (cube mesh, upload staging), so no device-local copy path.

#include <cstring>
#include <stdexcept>
#include <utility>

#include "vk_context.h"

class HostBuffer {
public:
    HostBuffer(VkContext& ctx, vk::DeviceSize size, vk::BufferUsageFlags usage) : allocator_(ctx.allocator.get()) {
        const VkBufferCreateInfo bci = vk::BufferCreateInfo{.size = size, .usage = usage};
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        // RANDOM access: these buffers are also used for readbacks.
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        VkBuffer raw = VK_NULL_HANDLE;
        if (vmaCreateBuffer(allocator_, &bci, &aci, &raw, &allocation_, &info) != VK_SUCCESS)
            throw std::runtime_error("vmaCreateBuffer failed");
        buffer_ = raw;
        mapped_ = info.pMappedData;
    }
    HostBuffer(const HostBuffer&) = delete;
    HostBuffer& operator=(const HostBuffer&) = delete;
    HostBuffer(HostBuffer&& o) noexcept
        : allocator_(std::exchange(o.allocator_, VK_NULL_HANDLE)), buffer_(std::exchange(o.buffer_, {})),
          allocation_(std::exchange(o.allocation_, VK_NULL_HANDLE)), mapped_(std::exchange(o.mapped_, nullptr)) {}
    ~HostBuffer() {
        if (allocator_ && buffer_) vmaDestroyBuffer(allocator_, buffer_, allocation_);
    }

    void write(const void* data, std::size_t bytes) {
        std::memcpy(mapped_, data, bytes);
        vmaFlushAllocation(allocator_, allocation_, 0, bytes);  // no-op on coherent memory
    }
    // Host reads of device writes (readbacks): call after the writing
    // submission's fence has signalled.
    void read(void* out, std::size_t bytes) {
        vmaInvalidateAllocation(allocator_, allocation_, 0, bytes);  // no-op on coherent memory
        std::memcpy(out, mapped_, bytes);
    }
    vk::Buffer get() const { return buffer_; }

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    vk::Buffer buffer_;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
};
