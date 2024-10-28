file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/*.h
)

add_executable(Editor ${APP_SOURCES})

target_include_directories(Editor PUBLIC ${PROJECT_SOURCE_DIR}/Quasar/Include)
target_link_libraries(Editor PUBLIC Quasar)