cmake_minimum_required(VERSION 3.15)

project(pdbterm LANGUAGES C CXX)

include(FetchContent)

# ----------------------------------------------------------
# External libraries: gemmi + lodepng
# ----------------------------------------------------------

# gemmi
FetchContent_Declare(
    gemmi
    GIT_REPOSITORY https://github.com/project-gemmi/gemmi.git
    GIT_TAG        v0.7.4
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/lib/gemmi
)

# lodepng
FetchContent_Declare(
    lodepng
    GIT_REPOSITORY https://github.com/lvandeve/lodepng.git
    GIT_TAG        master
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/lib/lodepng
)

FetchContent_MakeAvailable(gemmi lodepng)

# ----------------------------------------------------------
# Build config
# ----------------------------------------------------------

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------
# Subdirectories
# ----------------------------------------------------------
add_subdirectory(lib)
add_subdirectory(src)

add_executable(pdbterm main.cpp)

target_link_libraries(pdbterm
    PRIVATE
        pdbterm_core
        pdbterm_utils
)
