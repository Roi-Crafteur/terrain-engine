// The seams of the ergs (agent 34): the share of the dunes the tiles and the horizon blend their
// materials with (SurfaceBlend::dunes) is smooth over the ground, so that two grids of other
// pitches (the tiles of two LODs, the horizon) read the same share where they meet. Read from
// the height of the dunes (c43746b) it jumped by half within a few meters: a straight line
// across the ripples, rectangles of tint along the edges of the tiles. And the normals of the
// vertices two tiles of a LOD share are the same (Marching Cubes took them one-sided on the sides
// of its grid: up to a degree apart on the dunes, a straight line at a low sun, c43746b).
#include "dunes_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/water/hydrology.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>

namespace {

void testSmoothShare() {
    terrain::HydrologyParams p;
    p.ergs = dunes_test::smallErgParams();
    const auto hydrology = std::make_shared<const terrain::Hydrology>(
        dunes_test::kSeed, dunes_test::small().relief, dunes_test::small().planet, p);
    const terrain::Heightfield field = dunes_test::small().relief.withHydrology(hydrology);
    const terrain::Ergs& ergs = *hydrology->ergs();
    double worst = 0.0;
    int lines = 0;
    for (double z = -0.5 * golden::kSmallPlanet.z; z < 0.5 * golden::kSmallPlanet.z && lines < 6;
         z += 8'192.0) {
        for (double x = -0.5 * golden::kSmallPlanet.x; x < 0.5 * golden::kSmallPlanet.x && lines < 6;
             x += 8'192.0) {
            if (!ergs.mayHold(x, z) ||
                !(ergs.at(x, z, dunes_test::small().relief.sampleAt(x, z).relief).mask > 0.0f) ||
                !(field.sampleAt(x, z).erg > 0.1f)) {
                continue;
            }
            ++lines;
            double last = -1.0;
            for (double d = 0.0; d <= 2'000.0; d += 4.0) {
                const terrain::HeightSample s = field.sampleAt(x + d, z);
                terrain::SurfaceSite site;
                site.dunes = s.dunes;
                site.erg = s.erg;
                const float share = terrain::blendSurface(terrain::ClimateSample{}, site,
                                                          terrain::BiomeParams{},
                                                          terrain::MaterialParams{},
                                                          terrain::BlendParams{}, 1.0f)
                                        .dunes;
                if (last >= 0.0) {
                    worst = std::max(worst, std::abs(static_cast<double>(share) - last));
                }
                last = static_cast<double>(share);
            }
        }
    }
    std::printf("dune share over %d lines of 2 km in the ergs: at most %.3f between samples 4 m "
                "apart\n",
                lines, worst);
    TEST_CHECK(lines >= 3);
    TEST_CHECK(worst <= 0.02);
}


// The angle in degrees between the normals two tiles of a LOD give the vertices they share.
double sideAngle(const terrain::TileData& a, const terrain::TileData& b, int& shared) {
    std::map<std::array<long long, 3>, terrain::Vec3f> normals;
    const auto key = [](double x, double y, double z) {
        return std::array<long long, 3>{std::llround(x * 64.0), std::llround(y * 64.0),
                                        std::llround(z * 64.0)};
    };
    for (std::size_t i = 0; i < a.mesh.vertices.size(); ++i) {
        const terrain::Vec3f& v = a.mesh.vertices[i];
        normals[key(a.originX + v.x, a.originY + v.y, a.originZ + v.z)] = a.mesh.normals[i];
    }
    double worst = 0.0;
    for (std::size_t i = 0; i < b.mesh.vertices.size(); ++i) {
        const terrain::Vec3f& v = b.mesh.vertices[i];
        const auto it = normals.find(key(b.originX + v.x, b.originY + v.y, b.originZ + v.z));
        if (it == normals.end()) {
            continue;
        }
        ++shared;
        const terrain::Vec3f& n = b.mesh.normals[i];
        const double dot = static_cast<double>(n.x * it->second.x + n.y * it->second.y +
                                               n.z * it->second.z);
        worst = std::max(worst, std::acos(std::min(1.0, dot)) * dunes_test::kDegrees);
    }
    return worst;
}

void testSideNormals() {
    terrain::HydrologyParams p;
    p.ergs = dunes_test::smallErgParams();
    const auto hydrology = std::make_shared<const terrain::Hydrology>(
        dunes_test::kSeed, dunes_test::small().relief, dunes_test::small().planet, p);
    const terrain::TerrainTiles tiles{dunes_test::kSeed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt,
                                      golden::kSmallPlanet, hydrology};
    // On the dunes of the crest of dunes_mesh_test, and beside it.
    double worst = 0.0;
    int shared = 0;
    for (const double x : {91'587.0, 91'900.0, 92'400.0}) {
        for (int lod = 0; lod <= 2; ++lod) {
            const terrain::ChunkCoord c{
                static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
                static_cast<std::int32_t>(std::floor(172'032.0 / terrain::kChunkSizeMeters))};
            const terrain::TileKey a = terrain::tileContaining(c, lod);
            terrain::TileKey east = a;
            east.origin.x += terrain::tileChunks(lod);
            terrain::TileKey south = a;
            south.origin.z += terrain::tileChunks(lod);
            const terrain::TileData ta = tiles.generate({a, 0});
            worst = std::max(worst, sideAngle(ta, tiles.generate({east, 0}), shared));
            worst = std::max(worst, sideAngle(ta, tiles.generate({south, 0}), shared));
        }
    }
    std::printf("normals two tiles share on the dunes: %d vertices, at most %.3f degrees apart\n",
                shared, worst);
    TEST_CHECK(shared >= 100);
    TEST_CHECK(worst <= 0.1);
}

} // namespace

int main() {
    testSmoothShare();
    testSideNormals();
    return test::result();
}
