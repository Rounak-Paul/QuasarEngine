add_library(SkyNode SHARED
    src/plugin_main.c
    src/sky_node.c
)

target_link_libraries(SkyNode PRIVATE Quasar)

set_target_properties(SkyNode PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins"
)

foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" CFG_UPPER)
    set_target_properties(SkyNode PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_SOURCE_DIR}/bin/${cfg}/plugins"
        LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_SOURCE_DIR}/bin/${cfg}/plugins"
        ARCHIVE_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_SOURCE_DIR}/bin/${cfg}/plugins"
    )
endforeach()
