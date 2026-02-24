file(GLOB_RECURSE STRUCT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/structure/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/visualization/*.cpp"
)

file(GLOB_RECURSE STRUCT_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/structure/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/visualization/*.hpp"
)

add_library(pdbterm_core STATIC
    ${STRUCT_SOURCES}
    ${STRUCT_HEADERS}
)

target_include_directories(pdbterm_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/structure
        ${CMAKE_CURRENT_SOURCE_DIR}/visualization
)

target_link_libraries(pdbterm_core
    PUBLIC
        pdbterm_utils   # gemmi + lodepng
)
