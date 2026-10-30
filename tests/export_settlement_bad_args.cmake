# Invalid arguments of terrain_export_settlement: each case must fail, with an error message on
# stderr, before anything is written.
# Usage: cmake -DTOOL=<terrain_export_settlement> -DOUT=<scratch-dir> -DDATA=<data-dir>
#        -P export_settlement_bad_args.cmake

file(REMOVE_RECURSE "${OUT}")

function(expect_failure name)
    execute_process(COMMAND "${TOOL}" ${ARGN}
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

expect_failure("no output directory")
expect_failure("an option first" --seed 1)
expect_failure("unknown option" "${OUT}" --colour red)
expect_failure("non-numeric seed" "${OUT}" --seed world)
expect_failure("cell size 0" "${OUT}" --cell 0)
expect_failure("negative threads" "${OUT}" --threads -1)
expect_failure("world not a multiple of 65,536 m" "${OUT}" --world 100000 1048576)
expect_failure("cell size not dividing the world" "${OUT}" --world 2097152 1048576 --cell 10000)
expect_failure("missing data folder" "${OUT}" --data "${OUT}-no-data")
expect_failure("unknown preset" "${OUT}" --preset medieval)
expect_failure("zoom of a non-numeric size" "${OUT}" --zoom 0 0 wide 400)
expect_failure("zoom of size 0" "${OUT}" --zoom 0 0 0 400)
expect_failure("zoom missing a value" "${OUT}" --zoom 0 0 400)
