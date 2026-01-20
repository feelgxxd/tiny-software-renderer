cmake_minimum_required(VERSION 3.15)
project(SoftwareRenderer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SoftwareRenderer
    src/main.cpp
    src/platform/sdl_window.cpp
    src/core/framebuffer.cpp
    src/core/rasterizer.cpp
)

set(SDL2_DIR "${CMAKE_SOURCE_DIR}/external/SDL2/cmake")
find_package(SDL2 CONFIG REQUIRED)

target_include_directories(SoftwareRenderer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(SoftwareRenderer
    SDL2::SDL2
    SDL2::SDL2main
)