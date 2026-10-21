# Invalid arguments of terrain_export_horizon: each case must fail, with an error message on
# stderr, before anything is written.
# Usage: cmake -DTOOL=<terrain_export_horizon> -DOUT=<scratch-dir> -P export_horizon_bad_args.cmake

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

expect_failure("x without z" 20261019 1000)
expect_failure("non-numeric seed" world 0 0)
expect_failure("non-numeric x" 20261019 east 0)
expect_failure("x not a number" 20261019 nan 0)
expect_failure("infinite z" 20261019 0 inf)
expect_failure("cells not a multiple of 4" 20261019 0 0 30)
expect_failure("cells below 16" 20261019 0 0 12)
expect_failure("cells above 1024" 20261019 0 0 2048)
expect_failure("non-numeric cells" 20261019 0 0 many)
expect_failure("too many arguments" 20261019 0 0 64 1)
