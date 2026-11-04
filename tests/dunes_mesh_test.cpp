// The crests of the dunes in the tiles (agent 34): followed along a crest, the mesh of the tiles
// (Marching Cubes) keeps the height of the core within 4 % of its cell, without teeth: a
// sharp crest between the samples of the grid drew regular triangles along it (seen at a low sun
// in Unreal on 6d2ae20), a rounded one (dune_forms::kCrestRound) none.
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace {

struct World {
    std::shared_ptr<const terrain::Hydrology> hydrology = [] {
        terrain::HydrologyParams p;
        p.ergs = dunes_test::smallErgParams();
        return std::make_shared<const terrain::Hydrology>(dunes_test::kSeed, dunes_test::small().relief,
                                                          dunes_test::small().planet, p);
    }();
    terrain::TerrainTiles tiles{dunes_test::kSeed, terrain::HeightfieldParams{},
                                terrain::EcologyParams{}, std::nullopt, golden::kSmallPlanet, hydrology};
};

// The tallest dune of the small world on a lattice of 8 km, then the crest from it along the wind:
// at each meter the highest point within 20 m across.
std::vector<std::array<double, 3>> crest(const World& w) {
    const terrain::Heightfield& f = w.tiles.heightfield();
    double bx = 0.0;
    double bz = 0.0;
    float best = 0.0f;
    // Searched on the dunes alone (Ergs::dunesOf over the relief without water: cheap).
    const terrain::Ergs& ergs = *w.hydrology->ergs();
    for (double z = -0.5 * golden::kSmallPlanet.z; z < 0.5 * golden::kSmallPlanet.z; z += 8192.0) {
        for (double x = -0.5 * golden::kSmallPlanet.x; x < 0.5 * golden::kSmallPlanet.x; x += 8192.0) {
            if (!ergs.mayHold(x, z)) {
                continue;
            }
            const terrain::ErgPoint e = ergs.at(x, z, dunes_test::small().relief.sampleAt(x, z).relief);
            if (!(e.mask > 0.0f)) {
                continue;
            }
            for (double d = 0.0; d < 2'000.0; d += 25.0) {
                const float h = ergs.dunesOf(x + d, z, e);
                if (h > best) {
                    best = h;
                    bx = x + d;
                    bz = z;
                }
            }
        }
    }
    const std::array<double, 2> wind = w.hydrology->ergs()->windAt(bx, bz);
    std::vector<std::array<double, 3>> out;
    double x = bx;
    double z = bz;
    for (int k = 0; k < 200; ++k) {
        double top = -1e9;
        double off = 0.0;
        for (double o = -20.0; o <= 20.0; o += 0.5) {
            const double h = f.heightAt(x - o * wind[1], z + o * wind[0]);
            if (h > top) {
                top = h;
                off = o;
            }
        }
        x -= off * wind[1];
        z += off * wind[0];
        out.push_back({x, z, top});
        x += wind[0];
        z += wind[1];
    }
    std::printf("crest from a dune of %.0f m at (%.0f, %.0f)\n", static_cast<double>(best), bx, bz);
    return out;
}

// The highest point of the mesh of the tiles of LOD lod over (x, z).
double meshAt(const std::vector<std::array<double, 9>>& tris, double x, double z) {
    double best = -1e9;
    for (const std::array<double, 9>& t : tris) {
        const double det = (t[5] - t[8]) * (t[0] - t[6]) + (t[6] - t[3]) * (t[2] - t[8]);
        if (std::abs(det) < 1e-12) {
            continue;
        }
        const double a = ((t[5] - t[8]) * (x - t[6]) + (t[6] - t[3]) * (z - t[8])) / det;
        const double b = ((t[8] - t[2]) * (x - t[6]) + (t[0] - t[6]) * (z - t[8])) / det;
        if (a >= -1e-6 && b >= -1e-6 && 1.0 - a - b >= -1e-6) {
            best = std::max(best, a * t[1] + b * t[4] + (1.0 - a - b) * t[7]);
        }
    }
    return best;
}

std::vector<std::array<double, 9>> trianglesAlong(const World& w,
                                                  const std::vector<std::array<double, 3>>& c,
                                                  int lod) {
    std::vector<terrain::TileKey> done;
    std::vector<std::array<double, 9>> out;
    for (const std::array<double, 3>& p : c) {
        const terrain::ChunkCoord chunk{
            static_cast<std::int32_t>(std::floor(p[0] / terrain::kChunkSizeMeters)),
            static_cast<std::int32_t>(std::floor(p[1] / terrain::kChunkSizeMeters))};
        const terrain::TileKey key = terrain::tileContaining(chunk, lod);
        if (std::find(done.begin(), done.end(), key) != done.end()) {
            continue;
        }
        done.push_back(key);
        const terrain::TileData d = w.tiles.generate({key, 0});
        const auto near = [&](double x, double z) {
            for (std::size_t i = 0; i < c.size(); i += 2) {
                if (std::abs(c[i][0] - x) < 16.0 + 2.0 * terrain::tileCellMeters(lod) &&
                    std::abs(c[i][1] - z) < 16.0 + 2.0 * terrain::tileCellMeters(lod)) {
                    return true;
                }
            }
            return false;
        };
        for (std::size_t k = 0; k + 2 < d.mesh.indices.size(); k += 3) {
            const terrain::Vec3f& first = d.mesh.vertices[d.mesh.indices[k]];
            if (!near(d.originX + static_cast<double>(first.x),
                      d.originZ + static_cast<double>(first.z))) {
                continue;
            }
            std::array<double, 9> t{};
            for (int v = 0; v < 3; ++v) {
                const terrain::Vec3f& q = d.mesh.vertices[d.mesh.indices[k + static_cast<std::size_t>(v)]];
                const auto n = static_cast<std::size_t>(3 * v);
                t[n] = d.originX + q.x;
                t[n + 1] = d.originY + q.y;
                t[n + 2] = d.originZ + q.z;
            }
            out.push_back(t);
        }
    }
    return out;
}

void testNoTeeth() {
    const World w;
    const std::vector<std::array<double, 3>> c = crest(w);
    for (int lod = 0; lod <= 2; ++lod) {
        const std::vector<std::array<double, 9>> tris = trianglesAlong(w, c, lod);
        std::vector<double> e;
        int missed = 0;
        for (const std::array<double, 3>& p : c) {
            const double m = meshAt(tris, p[0], p[1]);
            missed += m < -1e8 ? 1 : 0;
            e.push_back(m < -1e8 ? 0.0 : m - p[2]);
        }
        TEST_CHECK(missed <= 2); // a point exactly on an edge between two tiles

        // What the mesh adds along the crest beyond its slow part (15 m), peak to peak.
        double low = 1e9;
        double high = -1e9;
        for (std::size_t i = 10; i + 10 < e.size(); ++i) {
            double mean = 0.0;
            for (std::size_t k = i - 7; k <= i + 7; ++k) {
                mean += e[k] / 15.0;
            }
            low = std::min(low, e[i] - mean);
            high = std::max(high, e[i] - mean);
        }
        const double cell = terrain::tileCellMeters(lod);
        std::printf("LOD %d (cells of %.0f m): teeth along the crest %.2f m peak to peak\n", lod,
                    cell, high - low);
        TEST_CHECK(high - low <= 0.04 * cell);
    }
}

} // namespace

int main() {
    testNoTeeth();
    return test::result();
}
