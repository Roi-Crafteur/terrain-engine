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
// --find: the forms to keep somewhere in such a world: the best field of stars (the erg cell with
// the most cells of stars around it, then its highest star dunes), the best wall of giant waves
// (the highest giant dunes), the lakes of the mountains most like lac_montagne (its water at
// 1,725 m, summits of 3,345 m within 3 km). --pole: the mean temperature at the poles of the
// planet level and of the tiles, Celsius, after the flags.
// Usage: terrain_world_changes <output-dir> [--flags <a,b>] [--places <file>] [--step <cells>]
//        [--find] [--pole <celsius>]

#include "tool_flags.hpp"

#include "terrain/det_math.hpp"
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
#include <functional>
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
    bool find = false;
    bool pole = false;
    float poleTemperature = 0.0f;
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
        } else if (arg == "--find") {
            o.find = true;
        } else if (arg == "--pole" && a + 1 < argc) {
            o.pole = true;
            o.poleTemperature = std::strtof(argv[++a], nullptr);
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

struct Found {
    double x = 0.0;
    double z = 0.0;
    float dunes = -1.0f; // meters of sand
    float mask = 0.0f;
};

// The highest dunes of kind around (x, z): a grid of step meters, half meters to each side.
Found highestAround(const World& w, const terrain::Ergs& ergs, double x, double z, double half,
                    double step, terrain::DuneKind kind) {
    Found best;
    const terrain::WorldSize& world = w.bare->world();
    for (double dz = -half; dz <= half; dz += step) {
        for (double dx = -half; dx <= half; dx += step) {
            const double px = terrain::wrapCoordinate(x + dx, world.x);
            const double pz = terrain::wrapCoordinate(z + dz, world.z);
            const terrain::HeightSample s = w.bare->sampleAt(px, pz);
            const terrain::ErgPoint e = ergs.at(px, pz, s.relief, s.rift);
            if (e.mask < 0.5f || ergs.kindOf(e) != kind) {
                continue;
            }
            const float d = ergs.dunesAt(px, pz, s);
            if (d > best.dunes) {
                best = {px, pz, d, e.mask};
            }
        }
    }
    return best;
}

void printFound(std::FILE* out, const World& w, const char* name, const Found& f, int around) {
    std::fprintf(out, "%s: x %.0f z %.0f, dunes %.0f m, mask %.2f, rain %.0f mm/yr, %d cells of "
                      "its kind within 16 km\n",
                 name, f.x, f.z, static_cast<double>(f.dunes), static_cast<double>(f.mask),
                 static_cast<double>(w.level->sampleAt(f.x, f.z).precipitation), around);
}

// The best field of stars and the best wall of giant waves of the ergs.
void findDunes(const World& w, std::FILE* out) {
    const terrain::Ergs* ergs = w.hydrology->ergs();
    const terrain::PlanetGrid& g = w.level->grid();
    if (ergs == nullptr) {
        return;
    }
    std::vector<std::uint8_t> kind(g.cells(), 0);
    for (std::size_t c = 0; c < g.cells(); ++c) {
        const double x = g.centerX(g.column(c));
        const double z = g.centerZ(g.row(c));
        if (w.level->cells().ocean(c) || !ergs->mayHold(x, z)) {
            continue;
        }
        const terrain::HeightSample s = w.bare->sampleAt(x, z);
        const terrain::ErgPoint e = ergs->at(x, z, s.relief, s.rift);
        kind[c] = e.mask < 0.5f ? 0 : static_cast<std::uint8_t>(ergs->kindOf(e));
    }
    const auto around = [&g, &kind](std::size_t c, std::uint8_t k) {
        int n = 0;
        for (int dj = -2; dj <= 2; ++dj) {
            for (int di = -2; di <= 2; ++di) {
                n += kind[g.index(g.wrapColumn(g.column(c) + di), g.wrapRow(g.row(c) + dj))] == k;
            }
        }
        return n;
    };
    for (const terrain::DuneKind k : {terrain::DuneKind::Star, terrain::DuneKind::Giants}) {
        const auto code = static_cast<std::uint8_t>(k);
        std::vector<std::pair<int, std::size_t>> cells;
        for (std::size_t c = 0; c < g.cells(); ++c) {
            if (kind[c] == code) {
                cells.emplace_back(around(c, code), c);
            }
        }
        std::sort(cells.begin(), cells.end(), std::greater<>());
        Found best;
        int bestAround = 0;
        for (std::size_t r = 0; r < std::min<std::size_t>(cells.size(), 12); ++r) {
            const std::size_t c = cells[r].second;
            const Found f = highestAround(w, *ergs, g.centerX(g.column(c)), g.centerZ(g.row(c)),
                                          4096.0, 512.0, k);
            if (f.dunes > best.dunes) {
                best = f;
                bestAround = cells[r].first;
            }
        }
        printFound(out, w, k == terrain::DuneKind::Star ? "best star field" : "best giant wall",
                   best, bestAround);
    }
}

// The lakes of the mountains of the planet level most like lac_montagne.
void findLakes(const World& w, std::FILE* out) {
    const terrain::PlanetGrid& g = w.level->grid();
    struct Lake {
        double score;
        double x;
        double z;
        float level;
        float peaks;
    };
    std::vector<Lake> lakes;
    for (const terrain::PlanetLake& l : w.level->lakes()) {
        if (l.frozen || l.playa || l.level < 1000.0f || l.pit == terrain::planet::kNoCell) {
            continue;
        }
        const double x = g.centerX(g.column(l.pit));
        const double z = g.centerZ(g.row(l.pit));
        if (w.bare->sampleAt(x, z).mountainMask < 0.3f) {
            continue;
        }
        float peaks = l.level;
        for (int k = 0; k < 16; ++k) {
            const double a = static_cast<double>(k) / 16.0;
            peaks = std::max(peaks, w.bare->heightAt(x + 3000.0 * terrain::dmath::cosTurns(a),
                                                     z + 3000.0 * terrain::dmath::sinTurns(a)));
        }
        if (peaks - l.level >= 800.0f) {
            lakes.push_back({std::abs(l.level - 1725.0f) + 0.5 * std::abs(peaks - 3345.0f), x, z,
                             l.level, peaks});
        }
    }
    std::sort(lakes.begin(), lakes.end(),
              [](const Lake& a, const Lake& b) { return a.score < b.score; });
    for (std::size_t k = 0; k < std::min<std::size_t>(lakes.size(), 6); ++k) {
        const Lake& l = lakes[k];
        const terrain::HeightSample s = w.field->sampleAt(l.x, l.z);
        std::fprintf(out, "mountain lake: x %.0f z %.0f, level %.0f m, summits %.0f m within 3 "
                          "km, water there %d at %.1f m\n",
                     l.x, l.z, static_cast<double>(l.level), static_cast<double>(l.peaks),
                     static_cast<int>(s.water), static_cast<double>(s.waterLevel));
    }
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        std::fprintf(stderr, "usage: terrain_world_changes <output-dir> [--flags <a,b>] "
                             "[--places <file>] [--step <cells>] [--find] [--pole <celsius>]\n");
        return 1;
    }
    terrain::WorldParams params;
    std::string canonical;
    if (!tool_flags::apply(o.flags, params, canonical)) {
        return 1;
    }
    if (o.pole) {
        params.planet.climate.poleTemperature = o.poleTemperature;
        params.ecology.climate.poleTemperature = o.poleTemperature;
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
    if (o.find) {
        findDunes(w, file);
        findLakes(w, file);
    }
    std::fclose(file);
    std::ifstream back(path);
    std::string line;
    while (std::getline(back, line)) {
        std::printf("%s\n", line.c_str());
    }
    return 0;
}
