cmake_minimum_required(VERSION 3.24)

project(SerenaDE
    VERSION 0.1.0
    DESCRIPTION "Run the Serenity OS desktop environment on Linux/BSD via X11"
    LANGUAGES C CXX
)

# Match Lagom's language mode for OUR targets. Lagom sets these in its own
# directory scope (Meta/CMake/common_compile_options.cmake), which does not
# propagate to src/; compiling our translation units against AK headers with a
# different standard or exception model is unsafe.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-fno-exceptions)

# ---------------------------------------------------------------------------
# Serenity source location
#
# Preferred during development: point at a local checkout so in-tree patches
# can be iterated on without re-fetching:
#   cmake -B Build -G Ninja -DSERENITY_SOURCE_DIR=~/git/serenity
#
# Otherwise a pinned revision is fetched (see cmake/FetchSerenity.cmake).
# ---------------------------------------------------------------------------
set(SERENITY_SOURCE_DIR "" CACHE PATH "Path to a SerenityOS/serenity checkout")

if(NOT SERENITY_SOURCE_DIR)
    include(cmake/FetchSerenity.cmake)
endif()

message(STATUS "SerenaDE: building against Serenity at ${SERENITY_SOURCE_DIR}")

# Apply the tracked (minimal) patch set. Configure fails if any patch does not
# apply cleanly -- that means the pin moved or a patch was upstreamed and not
# dropped. See patches/README.md.
include(cmake/ApplyPatches.cmake)

# ---------------------------------------------------------------------------
# Lagom: Serenity's official host-build system for its userland libraries.
# Built in-tree from the pinned Serenity source.
# ---------------------------------------------------------------------------
set(BUILD_LAGOM ON CACHE BOOL "" FORCE)
# LibWeb/Ladybird are not needed for the desktop environment and are expensive.
set(ENABLE_LAGOM_LIBWEB OFF CACHE BOOL "" FORCE)
add_subdirectory(${SERENITY_SOURCE_DIR}/Meta/Lagom ${CMAKE_BINARY_DIR}/lagom)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
