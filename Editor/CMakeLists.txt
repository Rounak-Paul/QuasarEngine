file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    src/**/*.cpp
    src/*.cpp
    src/**/*.h
    src/*.h
    src/**/*.inl
)

add_executable(Editor ${APP_SOURCES})

target_include_directories(Editor PUBLIC 
    src
    ${PROJECT_SOURCE_DIR}/Quasar/src/Include
)

target_link_libraries(Editor PUBLIC Quasar)