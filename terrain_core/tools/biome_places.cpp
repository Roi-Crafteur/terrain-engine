// The places of the biomes of the land on the real planet (agent 49), for the views in Unreal: for
// every biome, the spot of a lattice where it holds all around (the columns 600 m around it of the
// same biome), the flattest; the borders of pairs of biomes; a river of a desert. The biomes are
// read on the relief alone (its ground and slopes at 8 m, the sea of the geography, the humidity
// of the planet level, the mask of the seas of dunes), fast: the courses of the rivers are only
// computed for the river of a desert.
//
// Usage: terrain_biome_places --planet <file> [--step <meters>] [--threads <n>]
// Prints one line per place: its name, x, z, and what it is.
#include "terrain/det_math.hpp"
#include "terrain/generation/biome.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/ergs.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/storage/planet_file.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using terrain::Biome;

constexpr double kSlopeStep = 8.0;
constexpr double kAround = 600.0;

struct Options {
    std::filesystem::path planet;
    double step = 40'000.0;
    int threads = 0;
};

std::optional<Options> parseOptions(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--planet" && i + 1 < argc) {
            o.planet = argv[++i];
        } else if (flag == "--step" && i + 1 < argc) {
            o.step = std::atof(argv[++i]);
        } else if (flag == "--threads" && i + 1 < argc) {
            o.threads = std::atoi(argv[++i]);
        } else {
            return std::nullopt;
        }
    }
    if (o.planet.empty() || !(o.step >= 1000.0)) {
        return std::nullopt;
    }
    return o;
}

class World {
public:
    explicit World(const std::filesystem::path& planet)
        : relief_(terrain::kDefaultWorldSeed),
          hydrology_(std::make_shared<const terrain::Hydrology>(
              terrain::kDefaultWorldSeed, relief_,
              std::make_shared<const terrain::PlanetLevel>(terrain::loadPlanetFile(
                  planet, terrain::kDefaultWorldSeed, terrain::PlanetParams{},
                  terrain::WorldSize{})))),
          tiles_(terrain::kDefaultWorldSeed, {}, {}, std::nullopt, {}, hydrology_, false) {}

    // The site of a column of the relief, and its climate and biome.
    [[nodiscard]] terrain::EcologySample at(double x, double z, terrain::SurfaceSite& site) const {
        const terrain::Heightfield& f = hydrology_->relief();
        const terrain::HeightSample s = f.sampleAt(x, z);
        site = terrain::heightfieldSite(
            s.height, s.continentalness,
            {f.heightAt(x + kSlopeStep, z), f.heightAt(x - kSlopeStep, z),
             f.heightAt(x, z + kSlopeStep), f.heightAt(x, z - kSlopeStep)},
            kSlopeStep);
        site.water = s.water;
        site.waterLevel = s.water == terrain::WaterKind::None ? s.height : s.waterLevel;
        const terrain::Ergs* ergs = hydrology_->ergs();
        if (ergs != nullptr && ergs->mayHold(x, z)) {
            site.erg = ergs->at(x, z, s.relief).mask;
        }
        const terrain::Ecology& ecology = tiles_.ecology();
        terrain::EcologySample e;
        e.climate = ecology.climate().sample(x, z, site);
        e.biome = terrain::classifyBiome(e.climate, site, ecology.params().biomes);
        return e;
    }
    [[nodiscard]] Biome biomeAt(double x, double z) const {
        terrain::SurfaceSite site;
        return at(x, z, site).biome;
    }
    [[nodiscard]] const terrain::Hydrology& hydrology() const { return *hydrology_; }
    [[nodiscard]] terrain::WorldSize world() const { return tiles_.world(); }

private:
    terrain::Heightfield relief_;
    std::shared_ptr<const terrain::Hydrology> hydrology_;
    terrain::TerrainTiles tiles_;
};

struct Spot {
    double x = 0.0;
    double z = 0.0;
    float slope = 1e9f;
    bool found = false;
};

// One lattice point: its biome, the best spot of that biome if it holds all around.
void visit(const World& w, double x, double z, std::array<Spot, terrain::kBiomeCount>& best,
           std::vector<std::array<double, 3>>& lattice) {
    terrain::SurfaceSite site;
    const terrain::EcologySample e = w.at(x, z, site);
    lattice.push_back({x, z, static_cast<double>(e.biome)});
    if (e.biome == Biome::Ocean || !site.dry()) {
        return;
    }
    const Spot& known = best[static_cast<std::size_t>(e.biome)];
    if (known.found && !(e.climate.slope < known.slope)) {
        return; // no flatter than the spot already kept
    }
    for (int a = 0; a < 8; ++a) {
        const double turns = a / 8.0;
        if (w.biomeAt(x + kAround * terrain::dmath::cosTurns(turns),
                      z + kAround * terrain::dmath::sinTurns(turns)) != e.biome) {
            return;
        }
    }
    Spot& s = best[static_cast<std::size_t>(e.biome)];
    if (!s.found || e.climate.slope < s.slope) {
        s = {x, z, e.climate.slope, true};
    }
}

// The border of two biomes between two points of them, by bisection.
std::array<double, 2> border(const World& w, std::array<double, 2> a, std::array<double, 2> b,
                             Biome first) {
    for (int n = 0; n < 24; ++n) {
        const std::array<double, 2> mid = {(a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0};
        (w.biomeAt(mid[0], mid[1]) == first ? a : b) = mid;
    }
    return a;
}

void printBorders(const World& w, const std::vector<std::array<double, 3>>& lattice, double step) {
    const std::array<std::array<Biome, 2>, 4> pairs = {{{Biome::Steppe, Biome::RockyDesert},
                                                        {Biome::Polar, Biome::IceDesert},
                                                        {Biome::DryForest, Biome::Savanna},
                                                        {Biome::Mediterranean,
                                                         Biome::TemperateForest}}};
    for (const auto& pair : pairs) {
        for (const auto& p : lattice) {
            if (static_cast<Biome>(p[2]) != pair[0]) {
                continue;
            }
            const double ex = p[0] + step;
            if (w.biomeAt(ex, p[1]) == pair[1]) {
                const auto at = border(w, {p[0], p[1]}, {ex, p[1]}, pair[0]);
                std::printf("lisiere_%s_%s %.0f %.0f border, %s to the west, %s to the east\n",
                            terrain::biomeName(pair[0]), terrain::biomeName(pair[1]), at[0],
                            at[1], terrain::biomeName(pair[0]), terrain::biomeName(pair[1]));
                break;
            }
        }
    }
}

// A river of a desert: the first desert point of the lattice with a river within 3 km, the point
// of the river nearest to it (the floor of its valley within 30 m).
void printDesertRiver(const World& w, const std::vector<std::array<double, 3>>& lattice) {
    for (const auto& p : lattice) {
        if (!terrain::isDesert(static_cast<Biome>(p[2]))) {
            continue;
        }
        const double d = w.hydrology().floorDistance(p[0], p[1], 3000.0);
        if (!(d < 3000.0)) {
            continue;
        }
        double bx = p[0];
        double bz = p[1];
        double bd = d;
        for (double r = 100.0; r <= 3200.0; r += 100.0) {
            for (int a = 0; a < 32; ++a) {
                const double x = p[0] + r * terrain::dmath::cosTurns(a / 32.0);
                const double z = p[1] + r * terrain::dmath::sinTurns(a / 32.0);
                const double here = w.hydrology().floorDistance(x, z, 3000.0);
                if (here < bd) {
                    bx = x;
                    bz = z;
                    bd = here;
                }
            }
        }
        std::printf("fleuve_desert %.0f %.0f a river of a %s (%.0f m from its floor)\n", bx, bz,
                    terrain::biomeName(static_cast<Biome>(p[2])), bd);
        return;
    }
    std::printf("fleuve_desert none found\n");
}

} // namespace

int main(int argc, char** argv) {
    const std::optional<Options> o = parseOptions(argc, argv);
    if (!o) {
        std::fprintf(stderr,
                     "usage: terrain_biome_places --planet <file> [--step <m>] [--threads <n>]\n");
        return 1;
    }
    const World w(o->planet);
    const terrain::WorldSize world = w.world();
    const auto columns = static_cast<int>(world.x / o->step);
    const auto rows = static_cast<int>(world.z / o->step);
    const int threads = o->threads > 0 ? o->threads
                                       : std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    std::vector<std::array<Spot, terrain::kBiomeCount>> best(static_cast<std::size_t>(threads));
    std::vector<std::vector<std::array<double, 3>>> lattices(static_cast<std::size_t>(threads));
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&, t] {
            for (int k = t; k < rows; k += threads) {
                for (int i = 0; i < columns; ++i) {
                    visit(w, (i + 0.5) * o->step - world.x / 2.0, (k + 0.5) * o->step - world.z / 2.0,
                          best[static_cast<std::size_t>(t)], lattices[static_cast<std::size_t>(t)]);
                }
            }
        });
    }
    for (std::thread& t : pool) {
        t.join();
    }
    std::array<Spot, terrain::kBiomeCount> spots{};
    std::vector<std::array<double, 3>> lattice;
    for (int t = 0; t < threads; ++t) { // the flattest spot; the first thread between equals
        for (int b = 0; b < terrain::kBiomeCount; ++b) {
            const Spot& s = best[static_cast<std::size_t>(t)][static_cast<std::size_t>(b)];
            Spot& into = spots[static_cast<std::size_t>(b)];
            if (s.found && (!into.found || s.slope < into.slope)) {
                into = s;
            }
        }
        lattice.insert(lattice.end(), lattices[static_cast<std::size_t>(t)].begin(),
                       lattices[static_cast<std::size_t>(t)].end());
    }
    std::sort(lattice.begin(), lattice.end());
    for (int b = 1; b < terrain::kBiomeCount; ++b) {
        const Spot& s = spots[static_cast<std::size_t>(b)];
        if (s.found) {
            std::printf("biome_%d %.0f %.0f %s all around, slope %.3f\n", b, s.x, s.z,
                        terrain::biomeName(static_cast<Biome>(b)), static_cast<double>(s.slope));
        } else {
            std::printf("biome_%d none %s\n", b, terrain::biomeName(static_cast<Biome>(b)));
        }
    }
    printBorders(w, lattice, o->step);
    printDesertRiver(w, lattice);
    return 0;
}
