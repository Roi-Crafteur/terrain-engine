# Invalid arguments of terrain_probe_water: each case must fail, with an error message on stderr,
# before any planet file is read or built.
# Usage: cmake -DTOOL=<terrain_probe_water> -DOUT=<scratch-dir> -P probe_water_bad_args.cmake

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
    string(REGEX REPLACE "\n.*" "" first_line "${error}")
    message(STATUS "${name}: rejected (${first_line})")
endfunction()

expect_failure("no point")
expect_failure("no point but a planet" --planet "${OUT}/planet.bin")
expect_failure("a point without its z" 1000)
expect_failure("a non-numeric point" 1000 north --planet "${OUT}/planet.bin")
expect_failure("non-numeric seed" --seed world 0 0)
expect_failure("unknown option" --rivers 0 0)
expect_failure("world not a multiple of 65,536 m" --world 100000 1048576 0 0)
expect_failure("negative threads" --threads -1 0 0)
expect_failure("radius of 0 m" --radius 0 0 0)
expect_failure("negative falls radius" --falls -5 0 0)
