# terrain_export_lows with a planet file and the experimental flags (agent 52): on a small world,
# --flags lows builds the planet level of the lows into its file, then reads it back (the same
# checksum); without the flags the same file is refused (PlanetFlagsError), never read nor
# replaced, and still read with them afterwards.
# Usage: cmake -DTOOL=<terrain_export_lows> -DOUT=<scratch-dir> -P export_lows_planet.cmake

file(REMOVE_RECURSE "${OUT}")
set(planet "${OUT}/planet-lows.bin")
set(small --world 4194304 2097152 --cell 65536 --width 32 --readers 4 --threads 2)

function(run_tool out_var result_var)
    execute_process(COMMAND "${TOOL}" "${OUT}/lows" --planet "${planet}" ${small} ${ARGN}
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    set(${out_var} "${output}${error}" PARENT_SCOPE)
    set(${result_var} ${result} PARENT_SCOPE)
endfunction()

function(checksum_of text out_var)
    if(NOT text MATCHES "planet level (built into|read from) [^\n]*, checksum ([0-9a-f]+)")
        message(FATAL_ERROR "no planet level in the output:\n${text}")
    endif()
    set(${out_var} "${CMAKE_MATCH_1} ${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

run_tool(built result --flags lows)
if(NOT result EQUAL 0 OR NOT built MATCHES "experimental flags: lows\n")
    message(FATAL_ERROR "--flags lows, a new file: exit ${result}\n${built}")
endif()
checksum_of("${built}" first)
if(NOT first MATCHES "^built into ")
    message(FATAL_ERROR "--flags lows, a new file: not built (${first})")
endif()

run_tool(read result --flags lows)
checksum_of("${read}" second)
string(REGEX REPLACE "^[a-z ]+ " "" sum "${first}")
if(NOT result EQUAL 0 OR NOT second STREQUAL "read from ${sum}")
    message(FATAL_ERROR "--flags lows, the same file: exit ${result}, ${second}, not read from "
                        "${sum}\n${read}")
endif()

file(SHA256 "${planet}" before)
run_tool(refused result)
if(result EQUAL 0 OR NOT refused MATCHES "experimental flags \"lows\", not \"\" as asked")
    message(FATAL_ERROR "no flags, a file of the lows: not refused (exit ${result})\n${refused}")
endif()
file(SHA256 "${planet}" after)
if(NOT before STREQUAL after)
    message(FATAL_ERROR "no flags, a file of the lows: the file was replaced")
endif()

run_tool(again result --flags lows)
checksum_of("${again}" third)
if(NOT result EQUAL 0 OR NOT third STREQUAL "read from ${sum}")
    message(FATAL_ERROR "--flags lows after the refusal: exit ${result}, ${third}\n${again}")
endif()
message(STATUS "planet of the lows ${sum}: built, read back, refused without the flags")
