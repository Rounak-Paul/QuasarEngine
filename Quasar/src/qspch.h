#pragma once

#include "defines.h"

#include <string>
#include <iostream>
#include <span>

// Vendor
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

// Custom
#include <Core/Logger.h>
#include <Core/Platform.h>
#include <Containers/DeletionQueue.h>