add_library(BuiltinRendererPBR SHARED
    src/plugin_main.c
    src/pbr_renderer.c
    src/pbr_forward.c
)

# Match the engine's MSVC runtime library.
if(MSVC)
    set_target_properties(BuiltinRendererPBR PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Plugins link against Quasar which re-exports causality.
# This gives access to both the engine API and the Causality UI API
# for implementing on_editor_ui and on_editor_menu callbacks.
target_link_libraries(BuiltinRendererPBR PRIVATE Quasar)

# ── Output directories ────────────────────────────────────────────
# Mirror the root output convention: single-config → bin/plugins/,
# multi-config (MSVC) → bin/<Config>/plugins/.

set_target_properties(BuiltinRendererPBR PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/plugins"
)

foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" CFG_UPPER)
    set_target_properties(BuiltinRendererPBR PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_SOURCE_DIR}/bin/${cfg}/plugins"
        LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_SOURCE_DIR}/bin/${cfg}/plugins"
        ARCHIVE_OUTPUT_DIRECTORY_${CFG_UPPER} "${CMAKE_SOURCE_DIR}/bin/${cfg}/plugins"
    )
endforeach()
