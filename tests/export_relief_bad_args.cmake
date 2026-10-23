# Invalid arguments of terrain_export_relief: each case must fail, with an error message on
# stderr, before anything is computed or written.
# Usage: cmake -DTOOL=<terrain_export_relief> -DOUT=<scratch-dir> -P export_relief_bad_args.cmake

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
expect_failure("unknown relief" 20261019 mars)
expect_failure("cell size not dividing the world" 20261019 default 10000)
expect_failure("cell size 0" 20261019 default 0)
expect_failure("15 zoom pixels" 20261019 default 16384 15)
expect_failure("negative threads" 20261019 default 16384 64 -1)
expect_failure("world not a multiple of 65,536 m" 20261019 default 16384 64 1 100000 1048576)
expect_failure("non-numeric world z" 20261019 default 16384 64 1 2097152 tall)
expect_failure("2 cells per side" 20261019 default 65536 64 1 131072 1048576)
expect_failure("too many arguments" 20261019 default 16384 64 1 2097152 1048576 extra)
expect_failure("place without its point" 20261019 default 16384 64 1 --at here 1)
expect_failure("place with an invalid name" --at a-b 1 2)
expect_failure("place with a non-numeric x" --at here east 2)
expect_failure("unknown step to switch off" --without rivers)
expect_failure("step to switch off missing" 20261019 --without)
