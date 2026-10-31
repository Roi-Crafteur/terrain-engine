# terrain_probe and the planet file, on the small world: the first probe builds the file, the
# second reads it and says the same; --planet-check accepts it with the checksum of the probes,
# refuses a missing file, the file of another world and a damaged one (exit code 2), and never
# writes a file, the one it checks least of all.
# Usage: cmake -DTOOL=<terrain_probe> -DOUT=<scratch-dir> -P probe_planet_check.cmake

set(world --world 2097152 1048576)
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
set(planet "${OUT}/planet.bin")

function(run expected_result output_var)
    execute_process(COMMAND "${TOOL}" ${ARGN}
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL expected_result)
        message(FATAL_ERROR "terrain_probe ${ARGN}: exit ${result}, expected ${expected_result}\n"
                            "${output}\n${error}")
    endif()
    set(${output_var} "${output}" PARENT_SCOPE)
endfunction()

function(expect text pattern what)
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${what}: no match for '${pattern}' in:\n${text}")
    endif()
endfunction()

run(2 missing --planet-check "${planet}" ${world} --json)
expect("${missing}" "\"status\": \"missing\"" "a missing file")
if(EXISTS "${planet}")
    message(FATAL_ERROR "--planet-check wrote ${planet}")
endif()

run(0 first --planet "${planet}" ${world} --json 1000 1000 -50000 20000)
expect("${first}" "\"status\": \"built\"" "the first probe")
string(REGEX MATCH "\"checksum\": \"(0x[0-9a-f]+)\"" found "${first}")
set(checksum "${CMAKE_MATCH_1}")
run(0 second --planet "${planet}" ${world} --json 1000 1000 -50000 20000)
string(REPLACE "\"status\": \"built\"" "\"status\": \"read\"" first_read "${first}")
if(NOT first_read STREQUAL second)
    message(FATAL_ERROR "the probe of the planet read back differs from the one built:\n"
                        "${first}\n---\n${second}")
endif()
message(STATUS "probes built then read the same, level checksum ${checksum}")

file(SHA256 "${planet}" before)
run(0 ok --planet-check "${planet}" ${world} --json)
expect("${ok}" "\"status\": \"ok\"" "the file of the probes")
expect("${ok}" "\"checksum\": \"${checksum}\"" "the checksum of the file")
run(0 ok_text --planet-check "${planet}" ${world})
expect("${ok_text}" "status: ok" "the text of a check")
run(2 stale --planet-check "${planet}" --world 4194304 1048576 --json)
expect("${stale}" "\"status\": \"stale\"" "the file of another world")
run(2 other_seed --planet-check "${planet}" ${world} --seed 7 --json)
expect("${other_seed}" "\"status\": \"stale\"" "the file of another seed")
file(SHA256 "${planet}" after)
if(NOT before STREQUAL after)
    message(FATAL_ERROR "--planet-check changed ${planet}")
endif()

file(WRITE "${OUT}/damaged.bin" "not a planet file")
run(2 damaged --planet-check "${OUT}/damaged.bin" ${world} --json)
expect("${damaged}" "\"status\": \"damaged\"" "a damaged file")
file(READ "${OUT}/damaged.bin" damaged_bytes)
if(NOT damaged_bytes STREQUAL "not a planet file")
    message(FATAL_ERROR "--planet-check wrote over a damaged file")
endif()
message(STATUS "--planet-check: ok, missing, stale (world, seed) and damaged, nothing written")
