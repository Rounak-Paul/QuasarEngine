#pragma once
#include <qspch.h>

#include"VulkanTypes.h"

namespace Quasar::Renderer { 
b8 vulkan_object_shader_create(vulkan_context* context, vulkan_object_shader* out_shader);
void vulkan_object_shader_destroy(vulkan_context* context, struct vulkan_object_shader* shader);
void vulkan_object_shader_use(vulkan_context* context, struct vulkan_object_shader* shader);
void vulkan_object_shader_update_global_state(vulkan_context* context, struct vulkan_object_shader* shader);
}