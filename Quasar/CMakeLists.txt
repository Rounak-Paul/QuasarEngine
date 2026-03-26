# ── Vendor libraries ─────────────────────────────────────────────

# Causality UI library (static)
add_subdirectory(vendors/causality)

# OpenAL-soft (static, no extras)
set(LIBTYPE "STATIC" CACHE STRING "" FORCE)
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_CONFIG OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_HRTF_DATA OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_ENABLE_MODULES OFF CACHE BOOL "" FORCE)
add_subdirectory(vendors/openal-soft)

# Suppress -Werror=function-effects from OpenAL's CoreAudio backend (vendor code)
if(TARGET OpenAL AND NOT MSVC)
    target_compile_options(OpenAL PRIVATE -Wno-error=function-effects)
endif()

# meshoptimizer (static)
set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
add_subdirectory(vendors/meshoptimizer)

# cJSON (static, no tests)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "" FORCE)
set(CJSON_OVERRIDE_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(CJSON_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendors/cjson)

# ── Quasar engine ───────────────────────────────────────────────

file(GLOB_RECURSE QUASAR_SOURCES CONFIGURE_DEPENDS src/*.c)

add_library(Quasar STATIC ${QUASAR_SOURCES})

# Precompiled header
target_precompile_headers(Quasar PRIVATE src/qs_pch.h)

target_include_directories(Quasar
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/vendors/cgltf
        ${CMAKE_CURRENT_SOURCE_DIR}/vendors/stb
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vendors/cjson
)

target_link_libraries(Quasar
    PUBLIC
        causality
        OpenAL::OpenAL
        meshoptimizer
        cjson
)
