file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    src/**/*.cpp
    src/*.cpp
    src/**/*.h
    src/*.h
    src/**/*.inl
)
add_library(Quasar SHARED ${SOURCES})
set_target_properties(Quasar PROPERTIES LINKER_LANGUAGE CXX)
target_compile_definitions(Quasar PRIVATE QS_BUILD_DLL)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-anon-enum-enum-conversion")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
if(APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework CoreVideo")
endif()
add_subdirectory(Vendor/FILAMENT)
add_subdirectory(Vendor/GLFW)

target_precompile_headers(Quasar PUBLIC src/qspch.h)

# Include directories
target_include_directories(Quasar PUBLIC 
    src
    Vendor/FILAMENT/include
    Vendor/FILAMENT/libs
    Vendor/FILAMENT/libs/filabridge/include
    Vendor/FILAMENT/libs/bluegl/include
    Vendor/FILAMENT/libs/utils/include
    Vendor/GLFW/include
)

# Link core libraries
target_link_libraries(Quasar PUBLIC 
    filament
    filabridge
    backend
    bluegl
    geometry
    image
    utils
    glfw
)

