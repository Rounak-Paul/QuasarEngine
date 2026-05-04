file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS src/*.c)

add_executable(QuasarEditor ${EDITOR_SOURCES})

target_include_directories(QuasarEditor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/Quasar/vendors/cjson
)

target_link_libraries(QuasarEditor PRIVATE Quasar causality)

if(MSVC)
    target_compile_options(QuasarEditor PRIVATE /experimental:c11atomics)
endif()
