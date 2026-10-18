# Characterization tests for src/common, built against both builds of common/:
#   common_tests_f64  links common_f64 (vec_t double, as hlcsg and hlbsp use it)
#   common_tests_f32  links common_f32 (vec_t float, as hlvis and hlrad use it)

set(VHLT_TEST_SOURCES
    unit/test_main.cpp
    unit/test_bspfile.cpp
    unit/test_cmdlib.cpp
    unit/test_cmdline.cpp
    unit/test_entity.cpp
    unit/test_filelib.cpp
    unit/test_loader.cpp
    unit/test_math.cpp
    unit/test_planeindex.cpp
    unit/test_threads.cpp
    unit/test_tokenizer.cpp
    unit/test_winding.cpp
)

# Stock BSPs used for the load/write round-trip test; the test is skipped if the folder is missing.
set(VHLT_TEST_BSP_DIR "$ENV{HOME}/.steam/steam/steamapps/common/Half-Life/cstrike/maps"
    CACHE PATH "Folder of .bsp files for the round-trip test")

# bsp_probe: LoadBSPFile/WriteBSPFile driver. Error() exits the process, so the loader's
# rejection paths are tested by running it as a subprocess (test_loader.cpp).
add_executable(bsp_probe probe/bsp_probe.cpp)
target_link_libraries(bsp_probe PRIVATE common_f32)

function(vhlt_common_tests name common)
    add_executable(${name} ${VHLT_TEST_SOURCES})
    target_link_libraries(${name} PRIVATE ${common})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    target_compile_definitions(${name} PRIVATE VHLT_TEST_BSP_DIR="${VHLT_TEST_BSP_DIR}"
                               VHLT_BSP_PROBE="$<TARGET_FILE:bsp_probe>")
    add_dependencies(${name} bsp_probe)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 300) # a deadlock fails instead of hanging
endfunction()

vhlt_common_tests(common_tests_f64 common_f64)
# hlcsg pieces that stand alone (hlcsg uses the double build)
target_sources(common_tests_f64 PRIVATE unit/test_hlcsg.cpp
               ${CMAKE_SOURCE_DIR}/src/hlcsg/faces.cpp ${CMAKE_SOURCE_DIR}/src/hlcsg/ansitoutf8.cpp)
vhlt_common_tests(common_tests_f32 common_f32)
# hlrad's transfer compression (header-only; compress.cpp holds its tables)
target_sources(common_tests_f32 PRIVATE unit/test_compress.cpp ${CMAKE_SOURCE_DIR}/src/hlrad/compress.cpp)
