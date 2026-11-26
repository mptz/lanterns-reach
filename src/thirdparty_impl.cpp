// Single translation unit for header-only third-party implementations, built
// without our warning flags. (stb_image lives in stb_impl.cpp, which the
// CPU-only core links without Vulkan.)
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
