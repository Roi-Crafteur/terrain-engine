# Invalid arguments of terrain_export_lows (agent 52): each case must fail, with an error message
# on stderr, before anything is computed or written.
# Usage: cmake -DTOOL=<terrain_export_lows> -DOUT=<scratch-dir> -P export_lows_bad_args.cmake

file(REMOVE_RECURSE "${OUT}")

function(expect_failure name)
    execute_process(COMMAND "${TOOL}" "${OUT}" ${ARGN}
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0)
        message(FATAL_ERROR "${name}: expected a failure, got success:\n${output}")
    endif()
    if(NOT error MATCHES "Invalid .*\nUsage: ")
        message(FATAL_ERROR "${name}: no clear error message (exit ${result}), stderr:\n${error}")
    endif()
    if(EXISTS "${OUT}")
        message(FATAL_ERROR "${name}: ${OUT} was written despite the error")
    endif()
    string(REGEX REPLACE "\n.*" "" first_line "${error}")
    message(STATUS "${name}: rejected (${first_line})")
endfunction()

expect_failure("non-numeric seed" --seed world)
expect_failure("cell 0" --cell 0)
expect_failure("cell not dividing the world" --cell 10000)
expect_failure("negative threads" --threads -1)
expect_failure("width 0" --width 0)
expect_failure("world not a multiple of 65,536 m" --world 100000 1048576)
expect_failure("unknown option" --planets file)
expect_failure("missing value" --cell)
expect_failure("unknown flag" --flags lows,nonsense)
