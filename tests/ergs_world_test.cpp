// The seas of dunes in the world (agent 34, ergs.hpp) on the small world, the same code with the
// ergs enabled and disabled: outside the ergs every height, water and tile is the same to the bit;
// inside, the dunes rise; no dune under the water of a river or a lake; the sand of the dunes is
// the material of their columns; the cost of a sample in an erg and outside it.
#include "dunes_support.hpp"
#include "cost_ratio_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace {

using terrain::HeightSample;
using terrain::WaterKind;

std::shared_ptr<const terrain::Hydrology> hydrologyOf(bool ergs) {
    terrain::HydrologyParams p;
    p.ergs = dunes_test::smallErgParams();
    p.ergs.enabled = ergs;
    return std::make_shared<const terrain::Hydrology>(dunes_test::kSeed, dunes_test::small().relief,
                                                      dunes_test::small().planet, p);
}

struct Worlds {
    std::shared_ptr<const terrain::Hydrology> on = hydrologyOf(true);
    std::shared_ptr<const terrain::Hydrology> off = hydrologyOf(false);
    terrain::TerrainTiles withErgs{dunes_test::kSeed, terrain::HeightfieldParams{},
                                   terrain::EcologyParams{}, std::nullopt, golden::kSmallPlanet, on};
    terrain::TerrainTiles without{dunes_test::kSeed, terrain::HeightfieldParams{},
                                  terrain::EcologyParams{}, std::nullopt, golden::kSmallPlanet, off};
};

const Worlds& worlds() {
    static const Worlds w;
    return w;
}

bool same(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// Points of the full ergs of the small world (lattice of 8 km).
std::vector<std::pair<double, double>> ergSites() {
    std::vector<std::pair<float, std::pair<double, double>>> all;
    const terrain::Ergs& ergs = *worlds().on->ergs();
    for (double z = -0.5 * golden::kSmallPlanet.z; z < 0.5 * golden::kSmallPlanet.z; z += 8192.0) {
        for (double x = -0.5 * golden::kSmallPlanet.x; x < 0.5 * golden::kSmallPlanet.x;
             x += 8192.0) {
            const float mask = ergs.mayHold(x, z)
                                   ? ergs.at(x, z, dunes_test::small().relief.sampleAt(x, z).relief).mask
                                   : 0.0f;
            if (mask > 0.0f) {
                all.push_back({mask, {x, z}});
            }
        }
    }
    // The fullest, the first met among equals.
    std::stable_sort(all.begin(), all.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::pair<double, double>> out;
    for (std::size_t k = 0; k < all.size() && k < 6; ++k) {
        out.push_back(all[k].second);
    }
    std::printf("the fullest erg point: mask %.2f\n", all.empty() ? 0.0 : static_cast<double>(all[0].first));
    return out;
}

// The samples of a window of side meters around (x, z), every step meters, through the batch.
std::vector<HeightSample> window(const terrain::TerrainTiles& t, double x, double z, double side,
                                 double step) {
    std::vector<double> xs;
    std::vector<double> zs;
    for (double v = -0.5 * side; v < 0.5 * side; v += step) {
        for (double u = -0.5 * side; u < 0.5 * side; u += step) {
            xs.push_back(terrain::wrapCoordinate(x + u, golden::kSmallPlanet.x));
            zs.push_back(terrain::wrapCoordinate(z + v, golden::kSmallPlanet.z));
        }
    }
    std::vector<HeightSample> out(xs.size());
    t.heightfield().samplesAt(xs, zs, out);
    return out;
}

// Every sample outside the ergs keeps its bits (around the ergs and far from them); inside, the
// dunes rise; under water, none.
using Places = std::vector<std::pair<double, double>>;

// Returns points of the dunes over 8 m, one per window at most.
Places testSamples(const Places& sites) {
    std::vector<std::pair<double, double>> dunes;
    std::size_t outside = 0;
    std::size_t changed = 0;
    std::size_t inside = 0;
    std::size_t wetDunes = 0;
    double highest = 0.0;
    std::vector<std::pair<double, double>> places = sites;
    for (int k = 0; k < 6; ++k) {
        places.emplace_back(-900'000.0 + 297'131.0 * k, -450'000.0 + 151'777.0 * k);
    }
    for (const auto& [x, z] : places) {
        bool taken = false;
        // A window across the edge of the erg too: 40 km at 160 m.
        const std::vector<HeightSample> a = window(worlds().withErgs, x, z, 40'000.0, 160.0);
        const std::vector<HeightSample> b = window(worlds().without, x, z, 40'000.0, 160.0);
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].dunes == 0.0f) {
                ++outside;
                changed += same(a[i].height, b[i].height) && a[i].water == b[i].water &&
                                   same(a[i].waterLevel, b[i].waterLevel)
                               ? 0
                               : 1;
                continue;
            }
            ++inside;
            highest = std::max(highest, static_cast<double>(a[i].dunes));
            if (a[i].dunes > 8.0f && dunes.size() < 6 && !taken) {
                taken = true;
                const std::size_t side = static_cast<std::size_t>(40'000.0 / 160.0);
                dunes.emplace_back(x - 20'000.0 + 160.0 * static_cast<double>(i % side),
                                   z - 20'000.0 + 160.0 * static_cast<double>(i / side));
            }
            wetDunes += a[i].water != WaterKind::None ? 1 : 0;
        }
    }
    std::printf("samples: %zu outside the ergs (%zu changed), %zu inside (highest dune %.0f m, %zu "
                "under water)\n",
                outside, changed, inside, highest, wetDunes);
    TEST_CHECK(changed == 0);
    TEST_CHECK(inside > 5'000);
    TEST_CHECK(highest >= 10.0 && highest <= 300.0);
    TEST_CHECK(wetDunes == 0);
    return dunes;
}

terrain::TileData tileAt(const terrain::TerrainTiles& tiles, double x, double z, int lod) {
    const terrain::ChunkCoord chunk{
        static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
    return tiles.generate({terrain::tileContaining(chunk, lod), 0});
}

bool sameMesh(const terrain::TileData& a, const terrain::TileData& b) {
    if (a.mesh.vertices.size() != b.mesh.vertices.size() ||
        a.mesh.indices.size() != b.mesh.indices.size()) {
        return false;
    }
    for (std::size_t n = 0; n < a.mesh.vertices.size(); ++n) {
        const terrain::Vec3f u = a.mesh.vertices[n];
        const terrain::Vec3f v = b.mesh.vertices[n];
        if (!same(u.x, v.x) || !same(u.y, v.y) || !same(u.z, v.z)) {
            return false;
        }
    }
    return a.mesh.indices == b.mesh.indices && a.surfaceBlend == b.surfaceBlend &&
           a.waterColumns.size() == b.waterColumns.size();
}

// The tiles of every level far from the ergs are the same to the bit; those on the dunes differ,
// on dunes of more than 8 m (one per erg met).
void testTiles(const std::vector<std::pair<double, double>>& sites) {
    std::size_t far = 0;
    std::size_t farSame = 0;
    std::size_t near = 0;
    std::size_t nearChanged = 0;
    for (int lod = 0; lod <= 3; ++lod) {
        for (int k = 0; k < 6; ++k) {
            const double x = -900'000.0 + 331'017.0 * k;
            const double z = -400'000.0 + 137'731.0 * k;
            if (worlds().on->ergs()->mayHold(terrain::wrapCoordinate(x, golden::kSmallPlanet.x),
                                             terrain::wrapCoordinate(z, golden::kSmallPlanet.z))) {
                continue;
            }
            ++far;
            farSame += sameMesh(tileAt(worlds().withErgs, x, z, lod), tileAt(worlds().without, x, z, lod))
                           ? 1
                           : 0;
        }
        for (std::size_t k = 0; k < sites.size() && k < 3; ++k) {
            ++near;
            nearChanged += sameMesh(tileAt(worlds().withErgs, sites[k].first, sites[k].second, lod),
                                    tileAt(worlds().without, sites[k].first, sites[k].second, lod))
                               ? 0
                               : 1;
        }
    }
    std::printf("tiles: %zu far from the ergs (%zu the same), %zu on the dunes (%zu changed)\n", far,
                farSame, near, nearChanged);
    TEST_CHECK(far >= 8 && farSame == far);
    TEST_CHECK(near >= 4 && nearChanged == near);
}

// The cost of the samples of a row with the ergs over that without them, in an erg and away.
void testCost(const std::vector<std::pair<double, double>>& sites) {
    if (sites.empty()) {
        TEST_CHECK(false);
        return;
    }
    const auto timeRows = [&](double x0, double z0, double most, const char* where) {
        const cost_test::TimedRun run = [&](cost_test::Side side, std::size_t item) {
            const terrain::TerrainTiles& t =
                side == cost_test::Side::With ? worlds().withErgs : worlds().without;
            std::vector<double> xs;
            std::vector<double> zs;
            for (int i = 0; i < 4096; ++i) {
                xs.push_back(x0 + 4.0 * i);
                zs.push_back(z0 + 50.0 * static_cast<double>(item));
            }
            std::vector<HeightSample> out(xs.size());
            return cost_test::secondsOf([&] { t.heightfield().samplesAt(xs, zs, out); });
        };
        const cost_test::Ratio ratio = cost_test::ratioAtMost(6, run, most);
        std::printf("cost %-14s %s\n", where, cost_test::ratioText(ratio).c_str());
        return ratio;
    };
    const cost_test::Ratio away = timeRows(-1'000'000.0, -300'000.0, 1.3, "away from ergs");
    TEST_CHECK(away.passed());
    const cost_test::Ratio inside = timeRows(sites[0].first - 8000.0, sites[0].second, 1e9, "in an erg");
    TEST_CHECK(inside.passed());
}

} // namespace

int main() {
    const std::vector<std::pair<double, double>> sites = ergSites();
    std::printf("%zu sites in the ergs\n", sites.size());
    TEST_CHECK(sites.size() >= 3);
    const std::vector<std::pair<double, double>> dunes = testSamples(sites);
    testTiles(dunes);
    testCost(sites);
    return test::result();
}
