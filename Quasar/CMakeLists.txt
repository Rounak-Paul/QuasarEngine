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
set(FILAMENT_ENABLE_METAL OFF CACHE BOOL "Enable Metal backend" FORCE)
set(FILAMENT_SUPPORTS_METAL OFF CACHE BOOL "Support Metal backend" FORCE)
add_subdirectory(Vendor/FILAMENT)

target_precompile_headers(Quasar PRIVATE src/qspch.h)

# Include directories
target_include_directories(Quasar PRIVATE 
    src
    Vendor/FILAMENT/filament/include
    Vendor/FILAMENT/libs
    Vendor/FILAMENT/libs/filabridge/include
    Vendor/FILAMENT/libs/bluegl/include
    Vendor/FILAMENT/libs/utils/include
)

# Link core libraries
target_link_libraries(Quasar PRIVATE 
    filament
    filabridge
    backend
    bluegl
    geometry
    image
    utils
)

