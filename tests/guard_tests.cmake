# The guards of the goldens and of the tests (agent 42): the world as the game plays it pinned bit
# for bit, the checksum of its planet level checked, not only printed. Included once, at the end
# of tests/CMakeLists.txt (after coast_tests.cmake: the fixture coast_planet and its file).

# The planet level of the default world, as the fixture coast_planet_file reads or builds it
# (terrain_probe --planet): its checksum is the one pinned here. Pinned at ab574e3 (agent 42):
# the planet level of kGeneratorVersion 9, kReliefGeneratorVersion 4. With a pass expression,
# ctest decides on the checksum and no longer on the exit code of terrain_probe: the golden of the
# played world (below) reads the same file and checks the same checksum with its exit code.
set_tests_properties(coast_planet_file PROPERTIES
    PASS_REGULAR_EXPRESSION "\"checksum\": \"0x26626564570b0d9e\"")

