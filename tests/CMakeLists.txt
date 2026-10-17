# Characterization tests for src/common.
#
# common/ is compiled per tool (vec_t is double with DOUBLEVEC_T, float otherwise, and
# some code is #ifdef'd per tool), so the tests are built twice:
#   common_tests_f64  as hlcsg sees common/ (HLCSG, DOUBLEVEC_T)
#   common_tests_f32  as hlrad sees common/ (HLRAD, float vec_t)

list(TRANSFORM COMMON_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE VHLT_COMMON_SOURCES)

set(VHLT_TEST_SOURCES
    unit/test_main.cpp
    unit/test_bspfile.cpp
    unit/test_cmdlib.cpp
    unit/test_entity.cpp
    unit/test_loader.cpp
    unit/test_math.cpp
    unit/test_threads.cpp
    unit/test_tokenizer.cpp
    unit/test_winding.cpp
)

# Stock BSPs used for the load/write round-trip test; the test is skipped if the folder is missing.
set(VHLT_TEST_BSP_DIR "$ENV{HOME}/.steam/steam/steamapps/common/Half-Life/cstrike/maps"
    CACHE PATH "Folder of .bsp files for the round-trip test")

# bsp_probe: LoadBSPFile/WriteBSPFile driver. Error() exits the process, so the loader's
# rejection paths are tested by running it as a subprocess (test_loader.cpp).
add_executable(bsp_probe probe/bsp_probe.cpp ${VHLT_COMMON_SOURCES})
target_include_directories(bsp_probe PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(bsp_probe PRIVATE HLRAD)

function(vhlt_common_tests name)
    add_executable(${name} ${VHLT_TEST_SOURCES} ${VHLT_COMMON_SOURCES})
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    target_compile_definitions(${name} PRIVATE ${ARGN} VHLT_TEST_BSP_DIR="${VHLT_TEST_BSP_DIR}"
                               VHLT_BSP_PROBE="$<TARGET_FILE:bsp_probe>")
    add_dependencies(${name} bsp_probe)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

vhlt_common_tests(common_tests_f64 HLCSG DOUBLEVEC_T)
vhlt_common_tests(common_tests_f32 HLRAD)
