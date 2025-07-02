file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    src/**/*.cpp
    src/*.cpp
    src/**/*.h
    src/*.h
    src/**/*.inl
)
add_library(Quasar SHARED ${SOURCES})
set_target_properties(Quasar PROPERTIES LINKER_LANGUAGE CXX)
target_compile_definitions(Quasar PRIVATE QS_BUILD_DLL)

add_subdirectory(Vendor/GLFW)
add_subdirectory(Vendor/GLM)
add_subdirectory(Vendor/VMA)
add_subdirectory(Vendor/IMGUI)

target_precompile_headers(Quasar PUBLIC src/qspch.h)
target_precompile_headers(Quasar PUBLIC src/defines.h)

# Include directories
target_include_directories(Quasar PUBLIC 
    src
    ${VULKAN_PATH}/include
    vendor/GLFW/include
    vendor/GLM
    vendor/VMA/include
    Vendor/IMGUI/imgui
)

# Link directories
target_link_directories(Quasar PUBLIC ${VULKAN_PATH}/lib)

# Link core libraries
target_link_libraries(Quasar PRIVATE 
    glfw
    glm
    ImGui
)

if(APPLE)
    target_link_libraries(Quasar PUBLIC 
        ${VULKAN_PATH}/lib/libvulkan.1.dylib
        ${VULKAN_PATH}/lib/libvulkan.1.4.313.dylib
        ${VULKAN_PATH}/lib/libshaderc_shared.dylib
        ${VULKAN_PATH}/lib/libspirv-cross-c-shared.dylib
    )
elseif(WIN32)
    target_link_libraries(Quasar PUBLIC 
        ${VULKAN_PATH}/lib/vulkan-1.lib
        ${VULKAN_PATH}/lib/shaderc_shared.lib
    )
endif()

