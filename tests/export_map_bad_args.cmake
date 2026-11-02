# Invalid arguments of terrain_export_map: each case must fail, with an error message on stderr,
# before any planet file is read or built and without writing the image.
# Usage: cmake -DTOOL=<terrain_export_map> -DOUT=<scratch-dir> -P export_map_bad_args.cmake

file(REMOVE_RECURSE "${OUT}")
set(png "${OUT}/map.png")

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

expect_failure("nothing")
expect_failure("no place" --size 64 64 "${png}")
expect_failure("no output" --whole --size 64 64)
expect_failure("two places" --whole --box 0 0 100 100 --size 64 64 "${png}")
expect_failure("center without spacing" --center 0 0 --size 64 64 "${png}")
expect_failure("spacing of 0 m" --center 0 0 --mpp 0 --size 64 64 "${png}")
expect_failure("empty box" --box 0 0 0 100 --size 64 64 "${png}")
expect_failure("no size" --whole "${png}")
expect_failure("size and tiles" --center 0 0 --mpp 4 --size 64 64 --tiles 2 "${png}")
expect_failure("too many pixels" --whole --size 20000 64 "${png}")
expect_failure("tiles of 3 m" --center 0 0 --mpp 3 --tiles 2 "${png}")
expect_failure("tiles of a box" --box 0 0 100 100 --tiles 2 "${png}")
expect_failure("unknown layer" --whole --size 64 64 --layers relief,kingdoms "${png}")
expect_failure("max samples below 9" --whole --size 64 64 --max-samples 8 "${png}")
expect_failure("negative threads" --threads -2 --whole --size 64 64 "${png}")
expect_failure("non-numeric seed" --seed world --whole --size 64 64 "${png}")
expect_failure("unknown option" --rivers --whole --size 64 64 "${png}")
expect_failure("two outputs" --whole --size 64 64 "${png}" "${OUT}/other.png")
expect_failure("non-numeric center" --center east 0 --mpp 4 --size 64 64 --planet "${OUT}/p.bin"
               "${png}")
