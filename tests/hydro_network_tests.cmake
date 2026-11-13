# The hydrographic network (agent 31, task 11C): the bands of the winds joined, the runoff and its
# infiltration, the routing of the planet level through the passes, the data of its network and the
# fine network of the streams, and their experimental flags. Included once from
# tests/CMakeLists.txt.
terrain_add_test(climate_bands_test terrain_core)
terrain_add_test(runoff_partition_test terrain_core)
terrain_add_test(planet_links_test terrain_core)
terrain_add_test(planet_network_test terrain_core)
terrain_add_test(drainage_network_test terrain_core)
terrain_add_test(network_flags_test terrain_core)
terrain_add_test(sphere_latitude_test terrain_core)
