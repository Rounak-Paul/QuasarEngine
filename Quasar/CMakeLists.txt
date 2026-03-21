# Causality UI library (static)
add_subdirectory(vendors/causality)

# Quasar engine sources
file(GLOB_RECURSE QUASAR_SOURCES CONFIGURE_DEPENDS src/*.c)

add_library(Quasar STATIC ${QUASAR_SOURCES})

target_include_directories(Quasar
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(Quasar
    PUBLIC
        causality
)
