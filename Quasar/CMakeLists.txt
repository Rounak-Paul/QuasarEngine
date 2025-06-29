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

target_precompile_headers(Quasar PUBLIC src/qspch.h)

# Include directories
target_include_directories(Quasar PUBLIC 
    src
    ${VULKAN_PATH}/include
)

# Link directories
target_link_directories(Quasar PUBLIC ${VULKAN_PATH}/lib)

# Link core libraries
target_link_libraries(Quasar PRIVATE 
    
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

