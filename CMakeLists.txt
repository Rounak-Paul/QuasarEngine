cmake_minimum_required(VERSION 3.20)
set (CMAKE_CXX_STANDARD 20)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDebug")
else()
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

project(QuasarEngine)

add_compile_definitions($<$<CONFIG:Debug>:QS_DEBUG>)

if(APPLE)
    add_definitions(-DQS_PLATFORM_APPLE)

elseif(WIN32)
    add_definitions(-DQS_PLATFORM_WINDOWS)

elseif(UNIX)
    add_definitions(-DQS_PLATFORM_LINUX)

else()
set(CMAKE_CXX_FLAGS_RELEASE "")
set(CMAKE_C_FLAGS_RELEASE "")
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_subdirectory(Quasar)
add_subdirectory(Editor)