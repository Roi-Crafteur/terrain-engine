# The tests of the settlement V1 (agent 25, docs/PEUPLEMENT.md): data files, analysis maps,
# peoples, kingdoms. The tests of a task live in a file of their own, included once from
# tests/CMakeLists.txt, so that the list stays short.

# The JSON of the data files: strict reading, numbers to the bit, canonical writing.
terrain_add_test(json_test terrain_core)
