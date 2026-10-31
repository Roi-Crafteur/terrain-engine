# The probe of the core and the verification launcher (agent 37). Included by one line at the end
# of tests/CMakeLists.txt.

# terrain_probe on the small world with its water: the values of the core bit for bit, the lines
# exact, the frozen schema of its JSON, the same bytes whatever the threads.
terrain_add_test(probe_test terrain_probe_lib)

# Invalid arguments are rejected before any planet file is read.
add_test(NAME probe_bad_args
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_probe>
            -DOUT=${CMAKE_BINARY_DIR}/probe_bad_args
            -P ${CMAKE_CURRENT_SOURCE_DIR}/probe_bad_args.cmake)
# The planet file of the small world built, read back, checked without ever being written.
add_test(NAME probe_planet_check
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:terrain_probe>
            -DOUT=${CMAKE_BINARY_DIR}/probe_planet_check
            -P ${CMAKE_CURRENT_SOURCE_DIR}/probe_planet_check.cmake)
# A line of the small world without its planet, as text.
add_test(NAME probe_line_smoke
    COMMAND terrain_probe --world 2097152 1048576 --line -2000 500 2000 500 400)
set_tests_properties(probe_line_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "distance +x +z +relief\\.relief")
add_dependencies(check terrain_probe)

# The verification launcher (scripts/verify/), without Unreal: one test per file of its unit tests.
find_package(Python COMPONENTS Interpreter QUIET)
if(Python_Interpreter_FOUND)
    set(verify_tests places png pixels sheet walks logs machine workspace planet probe session driver)
    foreach(name IN LISTS verify_tests)
        add_test(NAME verify_${name}_test
            COMMAND ${Python_EXECUTABLE} -B -m unittest test_${name}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/scripts/verify/tests)
    endforeach()
endif()
