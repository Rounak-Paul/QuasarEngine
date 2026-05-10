add_library(BloomNode SHARED
    src/plugin_main.c
    src/bloom_node.c
)

target_link_libraries(BloomNode PRIVATE Quasar)

set_target_properties(BloomNode PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins"
)

foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" CFG_UPPER)
    set_target_properties(BloomNode PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_SOURCE_DIR}/bin/${cfg}/plugins"
        LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_SOURCE_DIR}/bin/${cfg}/plugins"
        ARCHIVE_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_SOURCE_DIR}/bin/${cfg}/plugins"
    )
endforeach()
