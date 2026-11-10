// Out of the volcanoes the world keeps every bit (agent 45): the same code, the volcanoes on and
// off (VolcanoParams::enabled), on the default world. The samples of points drawn anywhere and
// just beyond the reach of the volcanoes, the tiles of LOD 0 to 3 and the cells of the levels 0
// to 5 of the far horizon whose every read lies beyond their reach: the same bits. Within their
// reach, the samples differ; a batch gives the bits of the points one by one.
// The relief only: the planet level is built over this relief, so that its rivers and lakes find
// the volcanoes, and change with them downstream (its checksum changes: planet.bin is made again).
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/volcanoes.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"
#include "volcano_on_support.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::HeightSample;

bool same(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

bool sameSample(const HeightSample& a, const HeightSample& b) {
    return same(a.continentalness, b.continentalness) && same(a.mountainMask, b.mountainMask) &&
           same(a.relief, b.relief) && same(a.rift, b.rift) && same(a.base, b.base) &&
           same(a.regional, b.regional) && same(a.mountain, b.mountain) &&
           same(a.detail, b.detail) && same(a.height, b.height) && a.water == b.water &&
           a.volcanic == b.volcanic && same(a.waterLevel, b.waterLevel) &&
           same(a.dunes, b.dunes) && same(a.erg, b.erg) && same(a.ergFloor, b.ergFloor) &&
           same(a.rock, b.rock);
}

terrain::HeightfieldParams without() {
    terrain::HeightfieldParams p;
    p.volcanoes.enabled = false;
    return p;
}

// Whether every point of the square [x - half, x + half]^2, read every step meters, lies beyond
// the reach of every volcano by margin meters.
bool squareOutside(const terrain::Volcanoes& v, double x, double z, double half, double step,
                   double margin) {
    const terrain::WorldSize& w = v.world();
    for (double dz = -half; dz <= half + 0.5 * step; dz += step) {
        for (double dx = -half; dx <= half + 0.5 * step; dx += step) {
            const double px = terrain::wrapCoordinate(x + dx, w.x);
            const double pz = terrain::wrapCoordinate(z + dz, w.z);
            for (const std::uint32_t i : v.near(px, pz)) {
                const terrain::VolcanoSite& s = v.sites()[i];
                const terrain::VolcanoOffset o = terrain::volcanoOffset(w, s.x, s.z, px, pz);
                if (std::hypot(o.dx, o.dz) < static_cast<double>(s.reach) + margin) {
                    return false;
                }
            }
        }
    }
    return true;
}

class Draws {
public:
    double next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return static_cast<double>(state_ >> 11) * 0x1.0p-53;
    }

private:
    std::uint64_t state_ = 0x5EED45ULL;
};

struct Place {
    double x = 0.0;
    double z = 0.0;
};

// Places just beyond the reach of every few volcanoes and anywhere in the world.
std::vector<Place> placesBeyond(const terrain::Volcanoes& v, Draws& draws, double beyond) {
    std::vector<Place> out;
    const terrain::WorldSize& w = v.world();
    for (std::size_t i = 0; i < v.sites().size(); i += 37) {
        const terrain::VolcanoSite& s = v.sites()[i];
        const double angle = 2.0 * 3.14159265358979323846 * draws.next();
        const double r = static_cast<double>(s.reach) + beyond;
        out.push_back({terrain::wrapCoordinate(s.x + r * std::cos(angle), w.x),
                       terrain::wrapCoordinate(s.z + r * std::sin(angle), w.z)});
    }
    for (int k = 0; k < 60; ++k) {
        out.push_back({(draws.next() - 0.5) * w.x, (draws.next() - 0.5) * w.z});
    }
    return out;
}

void testSamples(const terrain::Heightfield& on, const terrain::Heightfield& off) {
    const terrain::Volcanoes& v = *on.volcanoes();
    const terrain::WorldSize& w = on.world();
    Draws draws;
    int outside = 0;
    int differ = 0;
    int inside = 0;
    int changed = 0;
    for (int k = 0; k < 6000; ++k) {
        const bool around = k % 2 == 0;
        const terrain::VolcanoSite& s = v.sites()[static_cast<std::size_t>(k) % v.sites().size()];
        // Around a volcano, from its heart to a little beyond its reach; or anywhere.
        const double angle = 2.0 * 3.14159265358979323846 * draws.next();
        const double r = (0.2 + 1.0 * draws.next()) * static_cast<double>(s.reach);
        const double x = around ? terrain::wrapCoordinate(s.x + r * std::cos(angle), w.x)
                                : (draws.next() - 0.5) * w.x;
        const double z = around ? terrain::wrapCoordinate(s.z + r * std::sin(angle), w.z)
                                : (draws.next() - 0.5) * w.z;
        const bool same = sameSample(on.sampleAt(x, z), off.sampleAt(x, z));
        if (squareOutside(v, x, z, 0.0, 1.0, 0.0)) {
            ++outside;
            differ += same ? 0 : 1;
        } else {
            ++inside;
            changed += same ? 0 : 1;
        }
    }
    std::printf("samples: %d beyond the reach of the volcanoes (%d differ), %d within it (%d "
                "changed)\n",
                outside, differ, inside, changed);
    TEST_CHECK(outside > 2000 && differ == 0);
    TEST_CHECK(inside > 1000 && changed > inside / 3);
}

// Rows across volcanoes: a batch, the points one by one.
void testBatches(const terrain::Heightfield& on) {
    const terrain::Volcanoes& v = *on.volcanoes();
    int rows = 0;
    int differ = 0;
    for (std::size_t i = 0; i < v.sites().size(); i += 151) {
        const terrain::VolcanoSite& s = v.sites()[i];
        std::vector<double> xs(256);
        std::vector<double> zs(256);
        for (std::size_t k = 0; k < xs.size(); ++k) {
            xs[k] = s.x + (static_cast<double>(k) - 128.0) * static_cast<double>(s.reach) / 100.0;
            zs[k] = s.z + 0.1 * static_cast<double>(s.radius);
        }
        std::vector<HeightSample> batch(xs.size());
        on.samplesAt(xs, zs, batch);
        for (std::size_t k = 0; k < xs.size(); ++k) {
            differ += sameSample(batch[k], on.sampleAt(xs[k], zs[k])) ? 0 : 1;
        }
        ++rows;
    }
    std::printf("batches: %d rows across volcanoes, %d samples unlike their points\n", rows,
                differ);
    TEST_CHECK(rows >= 20 && differ == 0);
}

bool sameMesh(const terrain::TileData& a, const terrain::TileData& b) {
    if (a.mesh.vertices.size() != b.mesh.vertices.size() || a.mesh.indices != b.mesh.indices) {
        return false;
    }
    for (std::size_t n = 0; n < a.mesh.vertices.size(); ++n) {
        const terrain::Vec3f u = a.mesh.vertices[n];
        const terrain::Vec3f w = b.mesh.vertices[n];
        if (!same(u.x, w.x) || !same(u.y, w.y) || !same(u.z, w.z)) {
            return false;
        }
    }
    return a.surfaceBlend == b.surfaceBlend && a.waterLevels.size() == b.waterLevels.size();
}

terrain::TileKey keyAt(double x, double z, int lod) {
    const terrain::ChunkCoord chunk{
        static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
        static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
    return terrain::tileContaining(chunk, lod);
}

void testTiles(const terrain::TerrainTiles& on, const terrain::TerrainTiles& off) {
    const terrain::Volcanoes& v = *on.heightfield().volcanoes();
    Draws draws;
    int far = 0;
    int same = 0;
    int near = 0;
    int changed = 0;
    for (int lod = 0; lod <= 3; ++lod) {
        const double side = terrain::tileSizeMeters(lod);
        for (const Place& p : placesBeyond(v, draws, 2.0 * side + 64.0)) {
            const terrain::TileKey key = keyAt(p.x, p.z, lod);
            const double x0 = key.origin.x * terrain::kChunkSizeMeters;
            const double z0 = key.origin.z * terrain::kChunkSizeMeters;
            if (far >= 12 * (lod + 1) ||
                !squareOutside(v, x0 + 0.5 * side, z0 + 0.5 * side, 0.5 * side + 64.0, 16.0,
                               0.0)) {
                continue;
            }
            ++far;
            same += sameMesh(on.generate({key, 0}), off.generate({key, 0})) ? 1 : 0;
        }
        for (std::size_t i = 0; i < v.sites().size() && near < 3 * (lod + 1); i += 211) {
            const terrain::VolcanoSite& s = v.sites()[i];
            if (s.atSea) {
                continue;
            }
            const terrain::VolcanoOffset top = terrain::volcanoSummit(s);
            const terrain::TileKey key = keyAt(s.x + top.dx + 0.3 * s.crater, s.z + top.dz, lod);
            ++near;
            changed += sameMesh(on.generate({key, 0}), off.generate({key, 0})) ? 0 : 1;
        }
    }
    std::printf("tiles of LOD 0 to 3: %d beyond the volcanoes (%d the same), %d on them (%d "
                "changed)\n",
                far, same, near, changed);
    TEST_CHECK(far >= 40 && same == far);
    TEST_CHECK(near >= 10 && changed == near);
}

void testHorizon(const terrain::TerrainTiles& on, const terrain::TerrainTiles& off) {
    terrain::HorizonParams params;
    params.levels = 6;
    params.cellsPerSide = 64;
    const terrain::HorizonSampler a(on.heightfield(), on.ecology(), params);
    const terrain::HorizonSampler b(off.heightfield(), off.ecology(), params);
    const terrain::Volcanoes& v = *on.heightfield().volcanoes();
    Draws draws;
    int windows = 0;
    int differ = 0;
    for (int level = 0; level < params.levels; ++level) {
        const double pitch = terrain::levelPitch(params, level);
        constexpr int kCells = 8;
        int here = 0;
        for (const Place& p : placesBeyond(v, draws, (kCells / 2 + 3) * pitch)) {
            const terrain::HorizonIndex c = terrain::canonicalCenter(params, level, p.x, p.z);
            const double cx = terrain::samplePosition(c.x, static_cast<int>(pitch));
            const double cz = terrain::samplePosition(c.z, static_cast<int>(pitch));
            if (here >= 6 ||
                !squareOutside(v, cx, cz, (kCells / 2 + 2) * pitch, std::max(8.0, pitch / 4.0),
                               0.0)) {
                continue;
            }
            ++here;
            const terrain::HorizonRect rect{c.x - kCells / 2, c.z - kCells / 2, c.x + kCells / 2,
                                            c.z + kCells / 2};
            const std::vector<terrain::HorizonCell> ca = a.sample(level, rect);
            const std::vector<terrain::HorizonCell> cb = b.sample(level, rect);
            for (std::size_t k = 0; k < ca.size(); ++k) {
                differ += same(ca[k].height, cb[k].height) && same(ca[k].gradX, cb[k].gradX) &&
                                  same(ca[k].gradZ, cb[k].gradZ) &&
                                  ca[k].material == cb[k].material &&
                                  ca[k].biome == cb[k].biome && ca[k].flags == cb[k].flags &&
                                  same(ca[k].waterLevel, cb[k].waterLevel)
                              ? 0
                              : 1;
            }
        }
        windows += here;
    }
    std::printf("horizon: %d windows of levels 0 to 5 beyond the volcanoes, %d cells differ\n",
                windows, differ);
    TEST_CHECK(windows >= 24 && differ == 0);
}

} // namespace

int main() {
    const terrain::TerrainTiles on(terrain::kDefaultWorldSeed, volcano_test::on());
    const terrain::TerrainTiles off(terrain::kDefaultWorldSeed, without());
    testSamples(on.heightfield(), off.heightfield());
    testBatches(on.heightfield());
    testTiles(on, off);
    testHorizon(on, off);
    return test::result();
}
