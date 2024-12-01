#pragma once

// std
#pragma warning(push, 0)
#include <iostream>
#include <vector>
#include <math.h>
#include <cstring>
#include <cstdarg>
#include <assert.h>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <deque>
#include <utility>
#include <list>
#include <algorithm>
#include <span>
#include <cmath>
#include <random>
#include <array>
#pragma warning(pop)

// Vendor
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h> // This includes vulkan already
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <Defines.h>
#include <Core/Log.h>

// Customs Containers
#include <Containers/String.h>
#include <Containers/RingQueue.h>
#include <Containers/DynamicArray.h>
#include <Containers/FrameDynamicArray.h>
#include <Containers/Hashmap.h>
#include <Containers/XMLPaser.h>

// Custom 
#include <Memory/Memory.h>
#include <Platform/Thread.h>
#include <Platform/Platform.h>
#include <Core/SystemManager.h>

