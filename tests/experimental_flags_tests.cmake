# The experimental flags of the world (agent 51): the relief of the wave merged switched off,
# switched on by name. Included once from tests/CMakeLists.txt.
# The table of the flags and the canonical text.
terrain_add_test(experimental_flags_test terrain_core)
# The planet file of version 2: the flags, then the level, then its sections (internal header).
terrain_add_test(planet_flags_test terrain_core)
target_include_directories(planet_flags_test PRIVATE ${PROJECT_SOURCE_DIR}/terrain_core/src/storage)
# The world without any flag, that of main at sea and on the coasts.
terrain_add_test(experimental_off_test terrain_core)
