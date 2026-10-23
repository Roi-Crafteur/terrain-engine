# Invalid arguments of terrain_export_blend: each case must fail, with an error message on
# stderr, before anything is computed or written.
# Usage: cmake -DTOOL=<terrain_export_blend> -DOUT=<scratch-dir> -P export_blend_bad_args.cmake

file(REMOVE_RECURSE "${OUT}")

function(expect_failure name)
    execute_process(COMMAND "${TOOL}" ${ARGN}
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
    message(STATUS "${name}: rejected")
endfunction()

expect_failure("no mode")
expect_failure("unknown mode" view "${OUT}")
expect_failure("maps without a place" maps "${OUT}")
expect_failure("place without its z" maps "${OUT}" --at plain 1000)
expect_failure("place with a non-numeric x" maps "${OUT}" --at plain east 2)
expect_failure("size below 16" maps "${OUT}" --size 8 --at plain 0 0)
expect_failure("no threads" maps "${OUT}" --threads 0 --at plain 0 0)
expect_failure("non-numeric seed" maps "${OUT}" --seed world --at plain 0 0)
expect_failure("edge without its z" edge "${OUT}" 1000)
expect_failure("edge with a non-numeric x" edge "${OUT}" east 2)
expect_failure("unknown flag" edge "${OUT}" 0 0 --at plain 0 0)
