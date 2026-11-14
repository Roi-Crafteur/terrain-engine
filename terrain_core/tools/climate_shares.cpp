// The shares of the climates of the land of whole worlds (task 11C, after the audit; the flag
// sphere_latitude): for each seed, its planet level built in memory with the experimental flags
// (experimental_flags.hpp), then the land of every step-th cell along each axis read as the tiles
// read it (the relief with its coasts, the ecology over the humidity of the planet level) and
// classified by the biomes of the tiles (classifyBiome). Prints per seed, and writes to
// shares.txt: the shares of the land of snow, tundra, snow + tundra, desert, of the arid land
// (aridity index P / PET under 0.5 on the planet level: hyper-arid, arid and semi-arid in the
// classes of UNEP) and of the frozen land (PlanetLevel::water), then of every biome.
// The dry biomes: the desert, and the steppe where the biomes have one (their names). --pole: the
// mean temperature at the poles of the planet level and of the tiles, Celsius, after the flags.
// --ergs: the number of ergs (agent 34), their area, and the wall of the giant waves and the field
// of stars (their mask, their kind of dune, the rain of their cell).
// --frost: the one threshold of the frost (the ice of the planet level and the snow of the
// biomes), Celsius, after the flags.
// Usage: terrain_climate_shares <output-dir> [--seeds <a,b,...>] [--flags <a,b>]
//        [--frost <celsius>] [--pole <celsius>] [--ergs] [--step <cells>] [--threads <n>]

#include "tool_flags.hpp"

#include "terrain/generation/ecology.hpp"
#include "terrain/generation/ergs.hpp"
#include "terrain/generation/planet/planet_climate.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::filesystem::path directory;
    std::vector<std::uint64_t> seeds{terrain::kDefaultWorldSeed};
    std::string flags;
    bool frost = false;
    float frostTemperature = 0.0f;
    bool pole = false;
    float poleTemperature = 0.0f;
    bool ergs = false;
    int step = 4;
    int threads = 4;
};

std::vector<std::uint64_t> seedsOf(const std::string& text) {
    std::vector<std::uint64_t> seeds;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = text.find(',', at);
        seeds.push_back(std::strtoull(text.substr(at, end - at).c_str(), nullptr, 10));
        at = end == std::string::npos ? text.size() : end + 1;
    }
    return seeds;
}

bool parse(int argc, char** argv, Options& o) {
    if (argc < 2) {
        return false;
    }
    o.directory = argv[1];
    for (int a = 2; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "--seeds" && a + 1 < argc) {
            o.seeds = seedsOf(argv[++a]);
        } else if (arg == "--flags" && a + 1 < argc) {
            o.flags = argv[++a];
        } else if (arg == "--frost" && a + 1 < argc) {
            o.frost = true;
            o.frostTemperature = std::strtof(argv[++a], nullptr);
        } else if (arg == "--pole" && a + 1 < argc) {
            o.pole = true;
            o.poleTemperature = std::strtof(argv[++a], nullptr);
        } else if (arg == "--ergs") {
            o.ergs = true;
        } else if (arg == "--step" && a + 1 < argc) {
            o.step = std::atoi(argv[++a]);
        } else if (arg == "--threads" && a + 1 < argc) {
            o.threads = std::atoi(argv[++a]);
        } else {
            return false;
        }
    }
    return !o.seeds.empty() && o.step > 0 && o.threads > 0;
}

// The humidity of a planet level, as the tiles read it (planetHumidity of the hydrology).
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

struct Counts {
    std::array<std::uint64_t, terrain::kBiomeCount> biomes{};
    std::uint64_t land = 0;   // the cells of the land read on the planet level
    std::uint64_t arid = 0;   // of them, an aridity index under 0.5
    std::uint64_t frozen = 0; // of them, frozen
    void add(const Counts& o) {
        for (std::size_t b = 0; b < biomes.size(); ++b) {
            biomes[b] += o.biomes[b];
        }
        land += o.land;
        arid += o.arid;
        frozen += o.frozen;
    }
};

constexpr double kSiteStep = 64.0; // meters to the neighbors of a sample, as the dev map probe

// The rows j = first, first + threads, ... of the sampled cells.
Counts countRows(const terrain::PlanetLevel& level, const terrain::Heightfield& field,
                 const terrain::Ecology& ecology, int step, int first, int stride) {
    Counts n;
    const terrain::PlanetGrid& g = level.grid();
    const terrain::PlanetClimateParams& climate = level.params().climate;
    for (int j = first * step; j < g.nz; j += stride * step) {
        for (int i = 0; i < g.nx; i += step) {
            const std::size_t c = g.index(i, j);
            if (level.cells().ocean(c) ||
                (level.cells().bits[c] & terrain::PlanetCells::kLakeBit) != 0) {
                continue;
            }
            ++n.land;
            const double pet = terrain::planet::potentialEvapotranspiration(
                level.temperature(c), climate);
            n.arid += level.precipitation(c) < 0.5 * pet ? 1 : 0;
            n.frozen += level.water(c).frozen ? 1 : 0;
            const double x = g.centerX(i);
            const double z = g.centerZ(j);
            const terrain::HeightSample h = field.sampleAt(x, z);
            if (h.water == terrain::WaterKind::Ocean) {
                continue; // the sea of the tiles in a cell of land: not counted as a biome
            }
            const std::array<float, 4> around = {
                field.heightAt(x + kSiteStep, z), field.heightAt(x - kSiteStep, z),
                field.heightAt(x, z + kSiteStep), field.heightAt(x, z - kSiteStep)};
            terrain::SurfaceSite site =
                terrain::heightfieldSite(h.height, h.continentalness, around, kSiteStep);
            site.water = h.water;
            site.waterLevel = h.waterLevel;
            const terrain::ClimateSample sample = ecology.climate().sample(x, z, site);
            ++n.biomes[static_cast<std::size_t>(
                terrain::classifyBiome(sample, site, ecology.params().biomes))];
        }
    }
    return n;
}

double share(std::uint64_t part, std::uint64_t whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

// The ergs of a level (Ergs, as the hydrology builds them), and the two places of agent 34.
std::string ergLine(std::uint64_t seed, const terrain::PlanetLevel& level,
                    const terrain::Heightfield& field, const terrain::WorldParams& world) {
    const terrain::Ergs ergs(seed, level, {}, world.water.ergs);
    double area = 0.0;
    for (const terrain::ErgSummary& e : ergs.ergs()) {
        area += e.area;
    }
    std::string line = "  ergs: " + std::to_string(ergs.ergs().size()) + ", area " +
                       std::to_string(static_cast<long long>(area)) + " blocks;";
    const std::array<std::array<double, 2>, 2> places = {
        std::array<double, 2>{-323'111.0, 3'952'778.0}, std::array<double, 2>{-26'543'080.0,
                                                                             5'119'222.0}};
    const std::array<const char*, 2> names = {"giant_wall", "star_field"};
    for (std::size_t k = 0; k < places.size(); ++k) {
        const terrain::HeightSample s = field.sampleAt(places[k][0], places[k][1]);
        const terrain::ErgPoint e = ergs.at(places[k][0], places[k][1], s.relief, s.rift);
        char text[160];
        std::snprintf(text, sizeof(text), " %s mask %.2f kind %d rain %.0f mm/yr;", names[k],
                      static_cast<double>(e.mask), static_cast<int>(ergs.kindOf(e)),
                      static_cast<double>(level.sampleAt(places[k][0], places[k][1]).precipitation));
        line += text;
    }
    return line;
}

void report(std::FILE* out, std::uint64_t seed, const std::string& flags,
            const terrain::WorldParams& world, const Counts& n, double seconds) {
    std::uint64_t read = 0;
    for (const std::uint64_t b : n.biomes) {
        read += b;
    }
    const auto biome = [&n](terrain::Biome b) { return n.biomes[static_cast<std::size_t>(b)]; };
    const std::uint64_t snow = biome(terrain::Biome::Snow);
    const std::uint64_t tundra = biome(terrain::Biome::Tundra);
    std::uint64_t dry = 0; // the desert, and the steppe where the biomes have one
    for (std::size_t b = 0; b < n.biomes.size(); ++b) {
        const std::string name = terrain::biomeName(static_cast<terrain::Biome>(b));
        dry += name == "desert" || name == "steppe" ? n.biomes[b] : 0;
    }
    std::fprintf(out,
                 "seed %llu flags \"%s\" frost %.1f pole %.1f: snow %.1f %%, tundra %.1f %%, snow "
                 "+ tundra %.1f %%, desert %.1f %%, dry biomes %.1f %%, arid (P/PET < 0.5) %.1f "
                 "%%, frozen %.1f %% (%llu samples, %llu land cells, %.0f s)\n",
                 static_cast<unsigned long long>(seed), flags.c_str(),
                 static_cast<double>(world.planet.climate.iceTemperature),
                 static_cast<double>(world.planet.climate.poleTemperature), share(snow, read),
                 share(tundra, read), share(snow + tundra, read),
                 share(biome(terrain::Biome::Desert), read), share(dry, read), share(n.arid, n.land),
                 share(n.frozen, n.land), static_cast<unsigned long long>(read),
                 static_cast<unsigned long long>(n.land), seconds);
    std::fprintf(out, "  biomes:");
    for (std::size_t b = 0; b < n.biomes.size(); ++b) {
        std::fprintf(out, " %s %.1f;", terrain::biomeName(static_cast<terrain::Biome>(b)),
                     share(n.biomes[b], read));
    }
    std::fprintf(out, "\n");
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        std::fprintf(stderr, "usage: terrain_climate_shares <output-dir> [--seeds <a,b,...>] "
                             "[--flags <a,b>] [--frost <celsius>] [--pole <celsius>] [--ergs] "
                             "[--step <cells>] [--threads <n>]\n");
        return 1;
    }
    terrain::WorldParams world;
    std::string canonical;
    if (!tool_flags::apply(o.flags, world, canonical)) {
        return 1;
    }
    if (o.frost) {
        world.planet.climate.iceTemperature = o.frostTemperature;
        world.ecology.biomes.snowTemperature = o.frostTemperature;
    }
    if (o.pole) {
        world.planet.climate.poleTemperature = o.poleTemperature;
        world.ecology.climate.poleTemperature = o.poleTemperature;
    }
    world.planet.threads = o.threads;
    std::filesystem::create_directories(o.directory);
    std::FILE* file = std::fopen((o.directory / "shares.txt").string().c_str(), "a");
    for (const std::uint64_t seed : o.seeds) {
        const auto start = std::chrono::steady_clock::now();
        const auto level = std::make_shared<const terrain::PlanetLevel>(
            seed, world.relief, world.planet, terrain::WorldSize{});
        const terrain::Heightfield field(seed, world.relief);
        const terrain::Ecology ecology(seed, world.ecology, field.world(),
                                       std::make_shared<const LevelHumidity>(level));
        std::vector<Counts> parts(static_cast<std::size_t>(o.threads));
        std::vector<std::thread> workers;
        for (int t = 0; t < o.threads; ++t) {
            workers.emplace_back([&, t] {
                parts[static_cast<std::size_t>(t)] =
                    countRows(*level, field, ecology, o.step, t, o.threads);
            });
        }
        Counts n;
        for (int t = 0; t < o.threads; ++t) {
            workers[static_cast<std::size_t>(t)].join();
            n.add(parts[static_cast<std::size_t>(t)]);
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const std::string ergs = o.ergs ? ergLine(seed, *level, field, world) + "\n" : "";
        for (std::FILE* out : {stdout, file}) {
            if (out != nullptr) {
                report(out, seed, canonical, world, n, seconds);
                std::fputs(ergs.c_str(), out);
                std::fflush(out);
            }
        }
    }
    if (file != nullptr) {
        std::fclose(file);
    }
    return 0;
}
