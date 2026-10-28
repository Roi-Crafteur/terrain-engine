#pragma once

#include "terrain/generation/planet/planet_build.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

// Internal to the planet level: what the two builds share (planet::buildPlanet keeps every array,
// planet::buildLevelParts frees each one after its last use).
namespace terrain::planet {

// The seconds of each stage, each stage told to the probe.
class Stopwatch {
public:
    explicit Stopwatch(const PlanetStageProbe& probe) : probe_(probe) { tell("field"); }

    double lap(const char* stage) {
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - start_).count();
        tell(stage);
        start_ = std::chrono::steady_clock::now();
        return seconds;
    }
    void tell(const char* stage) const {
        if (probe_) {
            probe_(stage);
        }
    }

private:
    const PlanetStageProbe& probe_;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

// The points of the rivers moved from the centers of their cells to the floors (floor(cell), the
// images kept: a polyline never jumps by a period), and the lengths of the segments measured
// again.
void moveToFloors(RiverGraph& graph, const PlanetGrid& grid,
                  const std::function<std::uint8_t(std::size_t)>& floor, int floorSamples);

} // namespace terrain::planet
