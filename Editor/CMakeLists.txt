file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS src/*.c)

add_executable(QuasarEditor ${EDITOR_SOURCES})

target_include_directories(QuasarEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(QuasarEditor PRIVATE Quasar)
