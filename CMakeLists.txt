cmake_minimum_required(VERSION 3.20)

project(VHLT
    VERSION 3.4
    LANGUAGES CXX
)

message("Configuring ${PROJECT_NAME}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if (MSVC)
    add_compile_definitions(
        WIN32
        _CONSOLE
        SYSTEM_WIN32
    )
endif()

# ================================
# Directories
# ================================

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(COMMON_DIR ${SRC_DIR}/common)

include_directories(
    ${SRC_DIR}
    ${COMMON_DIR}
)

# ================================
# Common sources
# ================================

set(COMMON_SOURCES
    ${COMMON_DIR}/blockmem.cpp
    ${COMMON_DIR}/bspfile.cpp
    ${COMMON_DIR}/cmdlib.cpp
    ${COMMON_DIR}/cmdlinecfg.cpp
    ${COMMON_DIR}/entity.cpp
    ${COMMON_DIR}/filelib.cpp
    ${COMMON_DIR}/log.cpp
    ${COMMON_DIR}/messages.cpp
    ${COMMON_DIR}/scriplib.cpp
    ${COMMON_DIR}/stringlib.cpp
    ${COMMON_DIR}/threads.cpp
    ${COMMON_DIR}/winding.cpp
)

# ================================
# hlbsp
# ================================

add_executable(hlbsp
    ${COMMON_SOURCES}
    ${SRC_DIR}/hlbsp/brink.cpp
    ${SRC_DIR}/hlbsp/merge.cpp
    ${SRC_DIR}/hlbsp/outside.cpp
    ${SRC_DIR}/hlbsp/portals.cpp
    ${SRC_DIR}/hlbsp/hlbsp.cpp
    ${SRC_DIR}/hlbsp/solidbsp.cpp
    ${SRC_DIR}/hlbsp/surfaces.cpp
    ${SRC_DIR}/hlbsp/tjunc.cpp
    ${SRC_DIR}/hlbsp/writebsp.cpp
)

target_include_directories(hlbsp PRIVATE
    ${SRC_DIR}/hlbsp
)

target_compile_definitions(hlbsp PRIVATE
    HLBSP
    DOUBLEVEC_T
)

# ================================
# hlcsg
# ================================

add_executable(hlcsg
    ${COMMON_SOURCES}
    ${SRC_DIR}/hlcsg/ansitoutf8.cpp
    ${SRC_DIR}/hlcsg/brush.cpp
    ${SRC_DIR}/hlcsg/hullfile.cpp
    ${SRC_DIR}/hlcsg/map.cpp
    ${SRC_DIR}/hlcsg/hlcsg.cpp
    ${SRC_DIR}/hlcsg/textures.cpp
    ${SRC_DIR}/hlcsg/wadpath.cpp
)

target_include_directories(hlcsg PRIVATE
    ${SRC_DIR}/hlcsg
)

target_compile_definitions(hlcsg PRIVATE
    HLCSG
    DOUBLEVEC_T
)

# ================================
# hlrad
# ================================

add_executable(hlrad
    ${COMMON_SOURCES}
    ${SRC_DIR}/hlrad/compress.cpp
    ${SRC_DIR}/hlrad/lerp.cpp
    ${SRC_DIR}/hlrad/lightmap.cpp
    ${SRC_DIR}/hlrad/loadtextures.cpp
    ${SRC_DIR}/hlrad/mathutil.cpp
    ${SRC_DIR}/hlrad/nomatrix.cpp
    ${SRC_DIR}/hlrad/hlrad.cpp
    ${SRC_DIR}/hlrad/hlradutil.cpp
    ${SRC_DIR}/hlrad/sparse.cpp
    ${SRC_DIR}/hlrad/trace.cpp
    ${SRC_DIR}/hlrad/transparency.cpp
    ${SRC_DIR}/hlrad/vismatrix.cpp
    ${SRC_DIR}/hlrad/vismatrixutil.cpp
)

target_include_directories(hlrad PRIVATE
    ${SRC_DIR}/hlrad
)

target_compile_definitions(hlrad PRIVATE HLRAD)

# ================================
# hlvis
# ================================

add_executable(hlvis
    ${COMMON_SOURCES}
    ${SRC_DIR}/hlvis/flow.cpp
    ${SRC_DIR}/hlvis/hlvis.cpp
)

target_include_directories(hlvis PRIVATE
    ${SRC_DIR}/hlvis
)

target_compile_definitions(hlvis PRIVATE HLVIS)

# ================================
# ripent
# ================================

add_executable(ripent
    ${COMMON_SOURCES}
    ${SRC_DIR}/ripent/ripent.cpp
)

target_include_directories(ripent PRIVATE
    ${SRC_DIR}/ripent
)

target_compile_definitions(ripent PRIVATE RIPENT)