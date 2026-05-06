cmake_minimum_required(VERSION 3.20)

project(VHLT
    VERSION 3.4
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
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

# =====================================================================================
# hlcsg
# =====================================================================================
add_executable(hlcsg
    ${COMMON_SOURCES}
    src/hlcsg/ansitoutf8.cpp
    src/hlcsg/brush.cpp
    src/hlcsg/hullfile.cpp
    src/hlcsg/map.cpp
    src/hlcsg/hlcsg.cpp
    src/hlcsg/textures.cpp
    src/hlcsg/wadpath.cpp
)
target_include_directories(hlcsg PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(hlcsg PRIVATE HLCSG DOUBLEVEC_T)

# =====================================================================================
# hlbsp
# =====================================================================================
add_executable(hlbsp
    ${COMMON_SOURCES}
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
target_include_directories(hlbsp PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(hlbsp PRIVATE HLBSP DOUBLEVEC_T)

# =====================================================================================
# hlvis
# =====================================================================================
add_executable(hlvis
    ${COMMON_SOURCES}
    src/hlvis/flow.cpp
    src/hlvis/hlvis.cpp
)
target_include_directories(hlvis PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(hlvis PRIVATE HLVIS)

# =====================================================================================
# hlrad
# =====================================================================================
add_executable(hlrad
    ${COMMON_SOURCES}
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
target_include_directories(hlrad PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(hlrad PRIVATE HLRAD)