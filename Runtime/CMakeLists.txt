file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS src/*.c)

add_executable(QuasarRuntime ${RUNTIME_SOURCES})

target_link_libraries(QuasarRuntime PRIVATE Quasar)
