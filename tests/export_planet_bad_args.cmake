# Invalid arguments of terrain_export_planet: each case must fail, with an error message on
# stderr, before anything is computed or written.
# Usage: cmake -DTOOL=<terrain_export_planet> -DOUT=<scratch-dir> -P export_planet_bad_args.cmake

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

expect_failure("non-numeric seed" world)
expect_failure("cell size not dividing the world" 20261019 10000)
expect_failure("cell size 0" 20261019 0)
expect_failure("cell size not a number" 20261019 nan)
expect_failure("unknown relief" 20261019 8192 mars)
expect_failure("0 samples" 20261019 8192 default 0)
expect_failure("9 samples" 20261019 8192 default 9)
expect_failure("negative threads" 20261019 8192 default 1 -1)
expect_failure("world not a multiple of 65,536 m" 20261019 8192 default 1 1 100000 1048576)
expect_failure("non-numeric world z" 20261019 8192 default 1 1 2097152 tall)
expect_failure("2 cells per side" 20261019 65536 default 1 1 131072 1048576)
expect_failure("too many arguments" 20261019 8192 default 1 1 2097152 1048576 extra)
