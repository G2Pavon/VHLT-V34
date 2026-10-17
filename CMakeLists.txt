cmake_minimum_required(VERSION 3.20)

project(VHLT
    VERSION 3.4
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Output must be bit-identical to the reference tools, so float evaluation is pinned:
# no fused multiply-add contraction (Clang's default is on) and no fast-math.
if(MSVC)
    add_compile_options(/fp:precise) # contraction stays off unless /fp:contract is given (VS 2022+)
else()
    add_compile_options(-ffp-contract=off -fno-fast-math)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if (MSVC)
    add_compile_definitions(WIN32 _CONSOLE SYSTEM_WIN32)
endif()

set(COMMON_SOURCES
    src/common/blockmem.cpp
    src/common/bspfile.cpp
    src/common/cmdlib.cpp
    src/common/entity.cpp
    src/common/filelib.cpp
    src/common/log.cpp
    src/common/messages.cpp
    src/common/tokenizer.cpp
    src/common/stringlib.cpp
    src/common/threads.cpp
    src/common/winding.cpp
)

# common/ is built once per precision: vec_t is double for hlcsg and hlbsp (DOUBLEVEC_T), float
# for hlvis and hlrad. DOUBLEVEC_T is PUBLIC so a tool can only be linked with the matching build.
add_library(common_f64 OBJECT ${COMMON_SOURCES})
target_include_directories(common_f64 PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(common_f64 PUBLIC DOUBLEVEC_T)

add_library(common_f32 OBJECT ${COMMON_SOURCES})
target_include_directories(common_f32 PUBLIC ${CMAKE_SOURCE_DIR}/src)

# =====================================================================================
# hlcsg
# =====================================================================================
add_executable(hlcsg
    src/hlcsg/ansitoutf8.cpp
    src/hlcsg/brush.cpp
    src/hlcsg/hullfile.cpp
    src/hlcsg/map.cpp
    src/hlcsg/hlcsg.cpp
    src/hlcsg/textures.cpp
    src/hlcsg/wadpath.cpp
)
target_link_libraries(hlcsg PRIVATE common_f64)
target_compile_definitions(hlcsg PRIVATE HLCSG)

# =====================================================================================
# hlbsp
# =====================================================================================
add_executable(hlbsp
    src/hlbsp/brink.cpp
    src/hlbsp/merge.cpp
    src/hlbsp/outside.cpp
    src/hlbsp/portals.cpp
    src/hlbsp/hlbsp.cpp
    src/hlbsp/solidbsp.cpp
    src/hlbsp/surfaces.cpp
    src/hlbsp/tjunc.cpp
    src/hlbsp/writebsp.cpp
)
target_link_libraries(hlbsp PRIVATE common_f64)
target_compile_definitions(hlbsp PRIVATE HLBSP)

# =====================================================================================
# hlvis
# =====================================================================================
add_executable(hlvis
    src/hlvis/flow.cpp
    src/hlvis/hlvis.cpp
)
target_link_libraries(hlvis PRIVATE common_f32)
target_compile_definitions(hlvis PRIVATE HLVIS)

# =====================================================================================
# hlrad
# =====================================================================================
add_executable(hlrad
    src/hlrad/compress.cpp
    src/hlrad/lerp.cpp
    src/hlrad/lightmap.cpp
    src/hlrad/loadtextures.cpp
    src/hlrad/mathutil.cpp
    src/hlrad/nomatrix.cpp
    src/hlrad/hlrad.cpp
    src/hlrad/hlradutil.cpp
    src/hlrad/sparse.cpp
    src/hlrad/trace.cpp
    src/hlrad/transparency.cpp
    src/hlrad/vismatrix.cpp
    src/hlrad/vismatrixutil.cpp
)
target_link_libraries(hlrad PRIVATE common_f32)
target_compile_definitions(hlrad PRIVATE HLRAD)

# =====================================================================================
# tests (doctest characterization tests for src/common; parity harness in tests/parity)
# =====================================================================================
option(VHLT_BUILD_TESTS "Build the unit tests" ON)
if(VHLT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
