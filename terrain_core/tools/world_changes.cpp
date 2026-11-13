// What a world of experimental flags changes for the other agents (task 11C, agent 31): the
// seas of dunes and the places of the views. Builds the planet level of the default world in
// memory with the flags (experimental_flags.hpp), its hydrology and its relief with the water, then
// prints (and writes to changes.txt, one line each, to compare two runs):
// - the ergs (agent 34): the share of the area of each character (realistic, waves, giants), the
//   share of the points of the ergs on grassland and on savanna, and at the wall of the giant
//   waves and the field of stars their mask, their kind of dune and the rain of their cell;
// - each place of --places (lines "name x z"): the ground, the kind and level of the water at the
//   place and the most the ground moves within 100 m (a cross of 9 points), the planet cell (its
//   river, its lake, its discharge), the biome.
// Usage: terrain_world_changes <output-dir> [--flags <a,b>] [--places <file>] [--step <cells>]

#include "tool_flags.hpp"

#include "terrain/generation/ecology.hpp"
#include "terrain/generation/ergs.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/terrain_surface.hpp"
#include "terrain/generation/water/hydrology.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Place {
    std::string name;
    double x = 0.0;
    double z = 0.0;
};

struct Options {
    std::filesystem::path directory;
    std::string flags;
    std::vector<Place> places;
    int step = 8;
};

std::vector<Place> readPlaces(const std::string& file) {
    std::vector<Place> places;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        Place p;
        if (words >> p.name >> p.x >> p.z) {
            places.push_back(p);
        }
    }
    return places;
}

bool parse(int argc, char** argv, Options& o) {
    if (argc < 2) {
        return false;
    }
    o.directory = argv[1];
    for (int a = 2; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "--flags" && a + 1 < argc) {
            o.flags = argv[++a];
        } else if (arg == "--places" && a + 1 < argc) {
            o.places = readPlaces(argv[++a]);
        } else if (arg == "--step" && a + 1 < argc) {
            o.step = std::atoi(argv[++a]);
        } else {
            return false;
        }
    }
    return o.step > 0;
}

class LevelHumidity final : public terrain::HumiditySource {
public:
    explicit LevelHumidity(std::shared_ptr<const terrain::PlanetLevel> level)
        : level_(std::move(level)) {}
    [[nodiscard]] float humidityAt(double x, double z) const override {
        return level_->humidityAt(x, z);
    }

private:
    std::shared_ptr<const terrain::PlanetLevel> level_;
};

struct World {
    std::shared_ptr<const terrain::PlanetLevel> level;
    std::shared_ptr<const terrain::Hydrology> hydrology;
    std::unique_ptr<terrain::Heightfield> field; // with its water (the valleys: at the places only)
    std::unique_ptr<terrain::Heightfield> bare;  // without (the ergs read its relief and rifts)
    std::unique_ptr<terrain::Ecology> ecology;
};

terrain::Biome biomeAt(const World& w, const terrain::Heightfield& field, double x, double z) {
    constexpr double kStep = 64.0;
    const terrain::HeightSample h = field.sampleAt(x, z);
    const std::array<float, 4> around = {
        field.heightAt(x + kStep, z), field.heightAt(x - kStep, z),
        field.heightAt(x, z + kStep), field.heightAt(x, z - kStep)};
    terrain::SurfaceSite site = terrain::heightfieldSite(h.height, h.continentalness, around, kStep);
    site.water = h.water;
    site.waterLevel = h.waterLevel;
    return terrain::classifyBiome(w.ecology->climate().sample(x, z, site), site,
                                  w.ecology->params().biomes);
}

void ergLines(const World& w, int step, std::FILE* out) {
    const terrain::Ergs* ergs = w.hydrology->ergs();
    if (ergs == nullptr) {
        std::fprintf(out, "ergs: none\n");
        return;
    }
    std::array<double, 3> area{};
    for (const terrain::ErgSummary& e : ergs->ergs()) {
        area[static_cast<std::size_t>(e.character)] += e.area;
    }
    const double total = area[0] + area[1] + area[2];
    std::fprintf(out, "ergs: %zu, realistic %.1f %%, waves %.1f %%, giants %.1f %% of their area\n",
                 ergs->ergs().size(), 100.0 * area[0] / total, 100.0 * area[1] / total,
                 100.0 * area[2] / total);
    const terrain::PlanetGrid& g = w.level->grid();
    std::size_t points = 0;
    std::size_t grass = 0;
    std::size_t savanna = 0;
    for (int j = 0; j < g.nz; j += step) {
        for (int i = 0; i < g.nx; i += step) {
            const double x = g.centerX(i);
            const double z = g.centerZ(j);
            if (w.level->cells().ocean(g.index(i, j)) || !ergs->mayHold(x, z)) {
                continue;
            }
            const terrain::HeightSample s = w.bare->sampleAt(x, z);
            if (ergs->at(x, z, s.relief, s.rift).mask < 0.5f) {
                continue;
            }
            ++points;
            const terrain::Biome b = biomeAt(w, *w.bare, x, z);
            grass += b == terrain::Biome::Grassland ? 1 : 0;
            savanna += b == terrain::Biome::Savanna ? 1 : 0;
        }
    }
    std::fprintf(out, "ergs on grassland %.1f %%, on savanna %.1f %% (%zu points)\n",
                 points > 0 ? 100.0 * static_cast<double>(grass) / static_cast<double>(points) : 0.0,
                 points > 0 ? 100.0 * static_cast<double>(savanna) / static_cast<double>(points) : 0.0,
                 points);
    const std::array<Place, 2> named = {Place{"giant_wall", -323'111.0, 3'952'778.0},
                                        Place{"star_field", -26'543'080.0, 5'119'222.0}};
    for (const Place& p : named) {
        const terrain::HeightSample s = w.bare->sampleAt(p.x, p.z);
        const terrain::ErgPoint e = ergs->at(p.x, p.z, s.relief, s.rift);
        std::fprintf(out, "%s: mask %.2f, kind %d, rain %.0f mm/yr\n", p.name.c_str(),
                     static_cast<double>(e.mask), static_cast<int>(ergs->kindOf(e)),
                     static_cast<double>(w.level->sampleAt(p.x, p.z).precipitation));
    }
}

void placeLine(const World& w, const Place& p, std::FILE* out) {
    const terrain::HeightSample h = w.field->sampleAt(p.x, p.z);
    float moves = 0.0f;
    for (int dj = -1; dj <= 1; ++dj) {
        for (int di = -1; di <= 1; ++di) {
            moves = std::max(moves, std::abs(w.field->heightAt(p.x + 100.0 * di, p.z + 100.0 * dj) -
                                             h.height));
        }
    }
    const terrain::PlanetSample cell = w.level->sampleAt(p.x, p.z);
    std::fprintf(out,
                 "place %s: ground %.2f, water %d level %.2f, relief within 100 m %.1f, planet "
                 "river %d lake %d discharge %.1f, biome %s\n",
                 p.name.c_str(), static_cast<double>(h.height), static_cast<int>(h.water),
                 h.water == terrain::WaterKind::None ? 0.0 : static_cast<double>(h.waterLevel),
                 static_cast<double>(moves), cell.discharge > 0.0f ? 1 : 0, cell.lake,
                 static_cast<double>(cell.discharge),
                 terrain::biomeName(biomeAt(w, *w.field, p.x, p.z)));
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        std::fprintf(stderr, "usage: terrain_world_changes <output-dir> [--flags <a,b>] "
                             "[--places <file>] [--step <cells>]\n");
        return 1;
    }
    terrain::WorldParams params;
    std::string canonical;
    if (!tool_flags::apply(o.flags, params, canonical)) {
        return 1;
    }
    const std::uint64_t seed = terrain::kDefaultWorldSeed;
    World w;
    w.level = std::make_shared<const terrain::PlanetLevel>(seed, params.relief, params.planet,
                                                           terrain::WorldSize{});
    const terrain::Heightfield relief(seed, params.relief);
    w.hydrology = std::make_shared<const terrain::Hydrology>(seed, relief, w.level, params.water);
    w.field = std::make_unique<terrain::Heightfield>(relief.withHydrology(w.hydrology));
    w.bare = std::make_unique<terrain::Heightfield>(relief);
    w.ecology = std::make_unique<terrain::Ecology>(seed, params.ecology, relief.world(),
                                                   std::make_shared<const LevelHumidity>(w.level));
    std::filesystem::create_directories(o.directory);
    const std::filesystem::path path = o.directory / "changes.txt";
    std::FILE* file = std::fopen(path.string().c_str(), "w");
    if (file == nullptr) {
        std::fprintf(stderr, "cannot write %s\n", path.string().c_str());
        return 1;
    }
    std::fprintf(file, "flags \"%s\", planet checksum %016llx\n", canonical.c_str(),
                 static_cast<unsigned long long>(w.level->checksum()));
    ergLines(w, o.step, file);
    for (const Place& p : o.places) {
        placeLine(w, p, file);
    }
    std::fclose(file);
    std::ifstream back(path);
    std::string line;
    while (std::getline(back, line)) {
        std::printf("%s\n", line.c_str());
    }
    return 0;
}
