add_library(GltfImporter SHARED
    src/vendor_impl.c
    src/gltf_loader.c
    src/plugin_main.c
)

if(MSVC)
    set_target_properties(GltfImporter PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

target_link_libraries(GltfImporter PRIVATE Quasar)

target_include_directories(GltfImporter PRIVATE
    ${CMAKE_SOURCE_DIR}/Quasar/vendors/cgltf
    ${CMAKE_SOURCE_DIR}/Quasar/vendors/stb
    src
)

set_target_properties(GltfImporter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins"
)

foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" CFG_UPPER)
    set_target_properties(GltfImporter PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_SOURCE_DIR}/bin/${cfg}/plugins"
        LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_SOURCE_DIR}/bin/${cfg}/plugins"
        ARCHIVE_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_SOURCE_DIR}/bin/${cfg}/plugins"
    )
endforeach()
