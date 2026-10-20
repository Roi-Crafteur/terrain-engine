#pragma once

// Helpers shared by the tests of the aquifers, the speleothems and the cave materials: grounds
// with a constant environment (biome, temperature and humidity of the surface above), to
// control what the caves read from the climate.

#include "terrain/generation/caves.hpp"

namespace caves_test {

inline terrain::CaveEnvironment environmentOf(terrain::Biome biome, float temperature,
                                              float humidity) {
    terrain::CaveEnvironment environment;
    environment.biome = biome;
    environment.temperature = temperature;
    environment.humidity = humidity;
    return environment;
}

// Humid temperate forest, dry desert, and a cold humid taiga.
inline terrain::CaveEnvironment humidEnvironment() {
    return environmentOf(terrain::Biome::TemperateForest, 12.0f, 0.9f);
}
inline terrain::CaveEnvironment dryEnvironment() {
    return environmentOf(terrain::Biome::Desert, 12.0f, 0.05f);
}
inline terrain::CaveEnvironment coldEnvironment() {
    return environmentOf(terrain::Biome::Taiga, -10.0f, 0.9f);
}

// The ground with the same environment everywhere.
inline terrain::CaveGround withEnvironment(terrain::CaveGround ground,
                                           const terrain::CaveEnvironment& environment) {
    ground.environment = [environment](double, double) { return environment; };
    return ground;
}

} // namespace caves_test
