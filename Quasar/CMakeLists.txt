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

target_precompile_headers(Quasar PUBLIC src/qspch.h)

# Include directories
target_include_directories(Quasar PUBLIC 
    src
)

# Link core libraries
target_link_libraries(Quasar PUBLIC 

)

