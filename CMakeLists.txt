cmake_minimum_required(VERSION 3.16)
project(DeadArrival C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- raylib 6.2 via FetchContent (pinned to the exact release tag) ----
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        6.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)

# ---- Dead Arrival ----
file(GLOB_RECURSE DA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_executable(dead_arrival ${DA_SOURCES})
target_include_directories(dead_arrival PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(dead_arrival PRIVATE /W4)
else()
    target_compile_options(dead_arrival PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

target_link_libraries(dead_arrival PRIVATE raylib)

# Run from the repository root so ./assets is found (see README).
