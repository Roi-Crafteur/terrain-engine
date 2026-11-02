#pragma once

// The options, the report and the places of terrain_export_rivers (export_rivers.cpp).

#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace rivers_tool {

struct Place {
    std::string name;
    double x = 0.0;
    double z = 0.0;
    double heading = 0.0; // degrees, views only
    double above = 0.0;   // meters above the eye of a walker
    double tilt = 0.0;    // of the view
    std::string note;     // what the view shows, for the report
};

[[nodiscard]] inline Place placeAt(std::string name, double x, double z) {
    Place p;
    p.name = std::move(name);
    p.x = x;
    p.z = z;
    return p;
}

struct Options {
    std::filesystem::path directory;
    std::filesystem::path planet;
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    terrain::WorldSize world;
    int threads = 0;
    int width = 1600;
    int pixels = 1024; // of the maps
    bool coasts = true; // GeographyParams::coasts (--without coasts: the world before agent 33)
    std::vector<Place> maps;
    std::vector<Place> views;
};

[[nodiscard]] bool parseOptions(int argc, char** argv, Options& o);

// Printed and kept for rivers.txt.
class Report {
public:
    void add(const char* format, ...);
    [[nodiscard]] bool write(const std::filesystem::path& path) const;

private:
    std::string text_;
};

// The places: around (rx, rz) a river at the bottom of a carved valley under the mountains, a
// gorge through a crest and a waterfall; around (lx, lz) a lake of a high basin; a lowland river
// with meanders anywhere: added to the views of o.
void addPlaces(const terrain::Hydrology& hydrology, const terrain::Heightfield& field, double rx,
               double rz, double lx, double lz, Options& o, Report& r);

// A lowland river with meanders, anywhere: the point beside its channel, looking down it.
[[nodiscard]] bool pickPlain(const terrain::Hydrology& hydrology, Place& view);

// The deepest cuts of the reaches within 128 km of (cx, cz), and the lakes of the mountains of
// the planet level.
void reportMountains(const terrain::Hydrology& hydrology, const terrain::Heightfield& field,
                     double cx, double cz, Report& r);

// The windows of mountain_freeze_test on the default world: their points under the mountains,
// those the water changes (in the corridors of the rivers and under the lakes), and that every
// other one keeps every bit of the relief.
void reportFreeze(const terrain::Heightfield& relief, const terrain::Heightfield& field,
                  Report& r);

} // namespace rivers_tool
