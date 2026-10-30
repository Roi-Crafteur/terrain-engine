# The settlement V1 (agent 25, docs/PEUPLEMENT.md): its sources in terrain_core, kept in a file of
# their own, included once from terrain_core/CMakeLists.txt, so that the list of the library
# stays short and the settlement only touches one line of it. Data only: nothing graphic.
target_sources(terrain_core PRIVATE
    src/json/json_parse.cpp
    src/json/json_write.cpp
)
