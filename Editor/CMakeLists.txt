file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS src/*.c)

add_executable(QuasarEditor ${EDITOR_SOURCES})

target_link_libraries(QuasarEditor PRIVATE Quasar)
