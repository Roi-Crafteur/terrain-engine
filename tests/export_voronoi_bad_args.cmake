# Invalid arguments of terrain_export_voronoi: each case must fail, with an error message on
# stderr, before anything is written.
# Usage: cmake -DTOOL=<terrain_export_voronoi> -DOUT=<scratch-dir> -P export_voronoi_bad_args.cmake

file(REMOVE_RECURSE "${OUT}")

function(expect_failure name)
    execute_process(COMMAND "${TOOL}" "${OUT}" ${ARGN}
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0)
        message(FATAL_ERROR "${name}: expected a failure, got success:\n${output}")
    endif()
    if(NOT error MATCHES "(Invalid|Expected) .*\nUsage: ")
        message(FATAL_ERROR "${name}: no clear error message (exit ${result}), stderr:\n${error}")
    endif()
    if(EXISTS "${OUT}")
        message(FATAL_ERROR "${name}: ${OUT} was written despite the error")
    endif()
    string(REGEX REPLACE "\n.*" "" first_line "${error}")
    message(STATUS "${name}: rejected (${first_line})")
endfunction()

expect_failure("non-numeric seed" world 64000 32 0 0)
expect_failure("0 m/px" 20261019 0 32 0 0)
expect_failure("size 1" 20261019 64000 1 0 0)
expect_failure("non-numeric center z" 20261019 64000 32 0 south)
expect_failure("plates 0" 20261019 64000 32 0 0 0)
expect_failure("negative plates" 20261019 64000 32 0 0 -0.5)
expect_failure("plates not a number" 20261019 64000 32 0 0 nan)
expect_failure("negative macro" 20261019 64000 32 0 0 0.5 -1)
expect_failure("infinite macro" 20261019 64000 32 0 0 0.5 inf)
expect_failure("world size 1" 20261019 64000 32 0 0 0.5 0.25 1)
expect_failure("non-numeric world size" 20261019 64000 32 0 0 0.5 0.25 large)
expect_failure("too many arguments" 20261019 64000 32 0 0 0.5 0.25 64 1)
