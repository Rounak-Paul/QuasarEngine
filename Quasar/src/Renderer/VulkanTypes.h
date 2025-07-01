#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#define VK_CHECK(x)                                                                     \
    do {                                                                               \
        VkResult err = x;                                                              \
        if (x) {                                                                     \
            LOG_ERROR("Detected Vulkan error: {}", string_VkResult(err));\
            abort();                                                                   \
        }                                                                              \
    } while (0)

namespace Quasar {


}