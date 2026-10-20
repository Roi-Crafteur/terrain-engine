# Invalid arguments of terrain_export_cave_regions: each case must fail, with an error message on
# stderr, before anything is written.
# Usage: cmake -DTOOL=<terrain_export_cave_regions> -DOUT=<scratch-dir>
#              -P export_cave_regions_bad_args.cmake

file(REMOVE_RECURSE "${OUT}")

function(expect_failure name)
    execute_process(COMMAND "${TOOL}" "${OUT}" ${ARGN}
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0)
        message(FATAL_ERROR "${name}: expected a failure, got success:\n${output}")
    endif()
    if(NOT error MATCHES "Error: .*\nUsage: ")
        message(FATAL_ERROR "${name}: no clear error message (exit ${result}), stderr:\n${error}")
    endif()
    if(EXISTS "${OUT}")
        message(FATAL_ERROR "${name}: ${OUT} was written despite the error")
    endif()
    string(REGEX REPLACE "\n.*" "" first_line "${error}")
    message(STATUS "${name}: rejected (${first_line})")
endfunction()

expect_failure("non-numeric seed" world)
expect_failure("negative seed" -1)
expect_failure("0 m/px" 20261019 0)
expect_failure("negative m/px" 20261019 -100)
expect_failure("m/px not a number" 20261019 nan)
expect_failure("infinite m/px" 20261019 inf)
expect_failure("size 1" 20261019 100 1)
expect_failure("size too large" 20261019 100 100000)
expect_failure("non-numeric size" 20261019 100 big)
expect_failure("non-numeric cx" 20261019 100 64 east)
expect_failure("infinite cx" 20261019 100 64 inf)
expect_failure("non-numeric cz" 20261019 100 64 0 south)
expect_failure("too many arguments" 20261019 100 64 0 0 1)
