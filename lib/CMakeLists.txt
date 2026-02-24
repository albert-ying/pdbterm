# -------- sources --------
set(LIB_SOURCES       
    ${CMAKE_CURRENT_SOURCE_DIR}/lodepng/lodepng.cpp  
)

set(LIB_HEADERS
    ${CMAKE_SOURCE_DIR}/include/simd/simd.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lodepng/lodepng.h
)

add_library(pdbterm_utils STATIC
    ${LIB_SOURCES}
    ${LIB_HEADERS}
)

set_target_properties(pdbterm_utils PROPERTIES POSITION_INDEPENDENT_CODE ON)

# include paths
target_include_directories(pdbterm_utils
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include/simd
        ${CMAKE_CURRENT_SOURCE_DIR}/lodepng
        ${CMAKE_CURRENT_SOURCE_DIR}/gemmi/include
)

# external deps
target_link_libraries(pdbterm_utils
    PUBLIC
        gemmi::gemmi_cpp
)
