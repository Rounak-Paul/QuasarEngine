file(GLOB IMGUI_SOURCES CONFIGURE_DEPENDS
    imgui/*.cpp
    imgui/*.h
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_glfw.h
    imgui/backends/imgui_impl_vulkan.cpp
    imgui/backends/imgui_impl_vulkan.h
)

add_library(ImGui ${IMGUI_SOURCES})
set_target_properties(ImGui PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(ImGui PUBLIC
    imgui
    ${PROJECT_SOURCE_DIR}/Quasar/Vendor/GLFW/include
    ${VULKAN_PATH}/include
)

target_link_libraries(ImGui PUBLIC
    glfw
)

if(APPLE)
    target_link_libraries(ImGui PUBLIC 
        ${VULKAN_PATH}/lib/libvulkan.1.dylib
        ${VULKAN_PATH}/lib/libvulkan.1.3.275.dylib
    )
elseif(WIN32)
    target_link_libraries(ImGui PUBLIC 
        ${VULKAN_PATH}/lib/vulkan-1.lib
    )
endif()