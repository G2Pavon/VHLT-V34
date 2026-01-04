cmake_minimum_required(VERSION 3.20)

project(VHLT
    VERSION 3.4
    LANGUAGES CXX
)

message("Configuring ${PROJECT_NAME}")

# ================================
# Output
# ================================

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ================================
# Global (Windows / MSVC)
# ================================

if (MSVC)
    add_compile_options(/MT /O2 /GL /Gy)
    add_link_options(/LTCG)
    add_compile_definitions(
        WIN32
        _CONSOLE
        SYSTEM_WIN32
        STDC_HEADERS
    )
endif()

# ================================
# Directories
# ================================

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(COMMON_DIR ${SRC_DIR}/common)
set(TEMPLATE_DIR ${SRC_DIR}/template)

include_directories(
    ${COMMON_DIR}
    ${TEMPLATE_DIR}
)

# ================================
# Common sources
# ================================

set(COMMON_SOURCES
    ${COMMON_DIR}/blockmem.cpp
    ${COMMON_DIR}/bspfile.cpp
    ${COMMON_DIR}/cmdlib.cpp
    ${COMMON_DIR}/cmdlinecfg.cpp
    ${COMMON_DIR}/filelib.cpp
    ${COMMON_DIR}/log.cpp
    ${COMMON_DIR}/mathlib.cpp
    ${COMMON_DIR}/messages.cpp
    ${COMMON_DIR}/scriplib.cpp
    ${COMMON_DIR}/threads.cpp
    ${COMMON_DIR}/winding.cpp
)

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_compile_definitions(VERSION_64BIT)
else()
    add_compile_definitions(VERSION_32BIT)
endif()

# ================================
# hlbsp
# ================================

add_executable(hlbsp
    ${COMMON_SOURCES}
    ${SRC_DIR}/hlbsp/brink.cpp
    ${SRC_DIR}/hlbsp/merge.cpp
    ${SRC_DIR}/hlbsp/outside.cpp
    ${SRC_DIR}/hlbsp/portals.cpp
    ${SRC_DIR}/hlbsp/qbsp.cpp
    ${SRC_DIR}/hlbsp/solidbsp.cpp
    ${SRC_DIR}/hlbsp/surfaces.cpp
    ${SRC_DIR}/hlbsp/tjunc.cpp
    ${SRC_DIR}/hlbsp/writebsp.cpp
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
    ${SRC_DIR}/hlcsg/brushunion.cpp
    ${SRC_DIR}/hlcsg/hullfile.cpp
    ${SRC_DIR}/hlcsg/map.cpp
    ${SRC_DIR}/hlcsg/properties.cpp
    ${SRC_DIR}/hlcsg/qcsg.cpp
    ${SRC_DIR}/hlcsg/textures.cpp
    ${SRC_DIR}/hlcsg/wadcfg.cpp
    ${SRC_DIR}/hlcsg/wadpath.cpp
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
    ${SRC_DIR}/hlrad/qrad.cpp
    ${SRC_DIR}/hlrad/qradutil.cpp
    ${SRC_DIR}/hlrad/sparse.cpp
    ${SRC_DIR}/hlrad/trace.cpp
    ${SRC_DIR}/hlrad/transfers.cpp
    ${SRC_DIR}/hlrad/transparency.cpp
    ${SRC_DIR}/hlrad/vismatrix.cpp
    ${SRC_DIR}/hlrad/vismatrixutil.cpp
)

target_compile_definitions(hlrad PRIVATE HLRAD)

# ================================
# hlvis
# ================================

add_executable(hlvis
    ${COMMON_SOURCES}
    ${SRC_DIR}/hlvis/flow.cpp
    ${SRC_DIR}/hlvis/vis.cpp
    ${SRC_DIR}/hlvis/zones.cpp
)

target_compile_definitions(hlvis PRIVATE HLVIS)

# ================================
# ripent
# ================================

add_executable(ripent
    ${COMMON_SOURCES}
    ${SRC_DIR}/ripent/ripent.cpp
)

target_compile_definitions(ripent PRIVATE RIPENT)
