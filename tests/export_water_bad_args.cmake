# Invalid arguments of terrain_export_water: each case must fail, with an error message on
# stderr, before anything is computed or written.
# Usage: cmake -DTOOL=<terrain_export_water> -DOUT=<scratch-dir> -P export_water_bad_args.cmake

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
expect_failure("unknown option" --rivers)
expect_failure("world not a multiple of 65,536 m" --world 100000 1048576)
expect_failure("cell size not dividing the world" --cell 10000)
expect_failure("negative threads" --threads -1)
expect_failure("15 pixels wide" --width 15)
expect_failure("map without its z" --map here 1000)
expect_failure("view without its heading" --view here 1000 2000)
expect_failure("view with a non-numeric heading" --view here 1000 2000 north)
