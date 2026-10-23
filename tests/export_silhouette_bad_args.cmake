# Invalid arguments of terrain_export_silhouette: each case must fail, with an error message on
# stderr, before anything is written.
# Usage: cmake -DTOOL=<terrain_export_silhouette> -DOUT=<scratch-dir> -P export_silhouette_bad_args.cmake

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

expect_failure("cells not a multiple of 4" 30)
expect_failure("cells below 16" 12)
expect_failure("cells above 1024" 2048)
expect_failure("non-numeric cells" many)
expect_failure("timings after the cells" 64 --timings)
expect_failure("no column per degree" 64 0)
expect_failure("too many columns per degree" 64 21)
expect_failure("non-numeric columns per degree" 64 ten)
expect_failure("ratio not a power of two" 64 1 3)
expect_failure("ratio below 2" 64 1 1)
expect_failure("ratio above 16" 64 1 32)
expect_failure("non-numeric ratio" 64 1 4 eight)
