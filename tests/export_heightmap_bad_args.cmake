# Invalid arguments of terrain_export_heightmap: each case must fail, with an error message on
# stderr, before anything is written.
# Usage: cmake -DTOOL=<terrain_export_heightmap> -DOUT=<scratch-dir> -P export_heightmap_bad_args.cmake

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

expect_failure("size 1" 20261019 250 1 0 0)
expect_failure("size 16385" 20261019 250 16385 0 0)
expect_failure("non-numeric size" 20261019 250 big 0 0)
expect_failure("non-numeric center x" 20261019 250 64 east 0)
expect_failure("trailing text in center z" 20261019 250 64 0 12x)
expect_failure("center x not a number" 20261019 250 64 nan 0)
expect_failure("0 m/px" 20261019 0 64 0 0)
expect_failure("non-numeric seed" world 250 64 0 0)
expect_failure("too many arguments" 20261019 250 64 0 0 1)
