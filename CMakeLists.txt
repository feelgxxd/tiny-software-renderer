cmake_minimum_required(VERSION 3.20)
project(TinySoftwareRenderer VERSION 1.0.0 LANGUAGES C CXX)

option(SR_BUILD_VIEWER "Build the SDL2 interactive viewer" ON)
option(SR_BUILD_TESTS "Build the test suite" ON)
option(SR_NATIVE_ARCH "Optimize for the build machine's CPU (-march=native / AVX2)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Put every executable next to each other (and next to SDL2.dll on Windows).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# Core library: everything except platform/window code.
add_library(sr_core STATIC
    src/core/framebuffer.cpp
    src/core/image_io.cpp
    src/core/renderer.cpp
    src/core/texture.cpp
    src/core/thread_pool.cpp
    src/geometry/mesh.cpp
    src/render/procedural.cpp
    src/render/scene.cpp
    src/render/shading.cpp
)
target_include_directories(sr_core PUBLIC src)
target_include_directories(sr_core SYSTEM PUBLIC external/stb)
target_compile_definitions(sr_core PUBLIC SR_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")

find_package(Threads REQUIRED)
target_link_libraries(sr_core PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(sr_core PUBLIC /W4 /permissive- /utf-8 $<$<CONFIG:Release,RelWithDebInfo>:/O2 /Oi /GS->)
    target_compile_definitions(sr_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
    if(SR_NATIVE_ARCH)
        target_compile_options(sr_core PUBLIC /arch:AVX2)
    endif()
else()
    target_compile_options(sr_core PUBLIC -Wall -Wextra -Wpedantic $<$<CONFIG:Release,RelWithDebInfo>:-O3>)
    if(SR_NATIVE_ARCH)
        target_compile_options(sr_core PUBLIC -march=native)
    endif()
endif()

# ---------------------------------------------------------------------------
# Headless renderer CLI.
add_executable(sr_render apps/sr_render.cpp)
target_link_libraries(sr_render PRIVATE sr_core)

# ---------------------------------------------------------------------------
# Interactive viewer (SDL2 only presents the finished frame).
if(SR_BUILD_VIEWER)
    find_package(SDL2 CONFIG QUIET)
    if(NOT SDL2_FOUND)
        message(STATUS "SDL2 not found on the system, fetching it")
        include(FetchContent)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-2.30.9
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(SDL2)
    endif()

    add_executable(sr_viewer apps/sr_viewer.cpp)
    if(TARGET SDL2::SDL2-static)
        target_link_libraries(sr_viewer PRIVATE sr_core SDL2::SDL2-static)
    else()
        target_link_libraries(sr_viewer PRIVATE sr_core SDL2::SDL2)
    endif()
endif()

# ---------------------------------------------------------------------------
if(SR_BUILD_TESTS)
    enable_testing()
    add_executable(sr_tests tests/test_main.cpp tests/test_math.cpp tests/test_raster.cpp)
    target_link_libraries(sr_tests PRIVATE sr_core)
    target_compile_definitions(sr_tests PRIVATE SR_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")
    add_test(NAME sr_tests COMMAND sr_tests WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
