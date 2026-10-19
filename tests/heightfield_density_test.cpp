#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/heightfield_density.hpp"
#include "terrain/meshing/density_grid.hpp"
#include "terrain/meshing/marching_cubes.hpp"
#include "terrain/meshing/mesh.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

using terrain::ChunkColumns;
using terrain::DensityGrid;
using terrain::Heightfield;
using terrain::HeightfieldDensity;
using terrain::kChunkSizeMeters;
using terrain::kGridSamples;

struct Point {
    double x;
    double z;
};

// Origins of chunks in coastal plains and on a mountain slope of the default world.
Point findPoint(const Heightfield& field, bool mountain) {
    for (int j = -40; j <= 40; ++j) {
        for (int i = -40; i <= 40; ++i) {
            const double x = i * 1024.0;
            const double z = j * 1024.0;
            const terrain::HeightSample s = field.sampleAt(x, z);
            const bool plain = s.mountainMask == 0.0f && s.height > 20.0f && s.height < 200.0f;
            if (mountain ? s.mountain > 1000.0f : plain) {
                return {x, z};
            }
        }
    }
    return {0.0, 0.0};
}

// Origin of the chunk that contains the lowest ground of the column range.
double lowestChunkY(const ChunkColumns& columns) {
    return std::floor(columns.minHeight / kChunkSizeMeters) * kChunkSizeMeters;
}

void testDensitySign() {
    const Heightfield field(terrain::kDefaultWorldSeed);
    const HeightfieldDensity density(field);
    for (const Point p : {Point{0.0, 0.0}, Point{-1234.5, 678.9}, Point{4.0e4, -2.5e4}}) {
        const double h = field.heightAt(p.x, p.z);
        TEST_CHECK(density(p.x, h, p.z) == 0.0);
        TEST_CHECK(density(p.x, h - 0.5, p.z) < 0.0); // solid below the ground
        TEST_CHECK(density(p.x, h + 0.5, p.z) > 0.0); // air above it
    }
}

void testColumns() {
    const Heightfield field(terrain::kDefaultWorldSeed);
    const Point origin = findPoint(field, true);
    const ChunkColumns columns = terrain::sampleChunkColumns(field, origin.x, origin.z);
    bool matches = true;
    float low = columns.at(0, 0);
    float high = low;
    for (int z = 0; z < kGridSamples; ++z) {
        for (int x = 0; x < kGridSamples; ++x) {
            const float h = columns.at(x, z);
            matches = matches && h == field.heightAt(origin.x + x * terrain::kCellSizeMeters,
                                                      origin.z + z * terrain::kCellSizeMeters);
            low = std::min(low, h);
            high = std::max(high, h);
        }
    }
    TEST_CHECK(matches);
    TEST_CHECK(columns.minHeight == low);
    TEST_CHECK(columns.maxHeight == high);
}

// The column shortcut gives exactly the grid of sampleDensity(HeightfieldDensity).
void testDensityFromColumnsMatchesSampleDensity() {
    const Heightfield field(terrain::kDefaultWorldSeed);
    for (const bool mountain : {false, true}) {
        const Point origin = findPoint(field, mountain);
        const ChunkColumns columns = terrain::sampleChunkColumns(field, origin.x, origin.z);
        const double originY = lowestChunkY(columns);
        const DensityGrid fast = terrain::densityFromColumns(columns, originY);
        const DensityGrid reference =
            terrain::sampleDensity(HeightfieldDensity(field), origin.x, originY, origin.z);
        bool identical = true;
        for (int z = 0; z < kGridSamples; ++z) {
            for (int y = 0; y < kGridSamples; ++y) {
                for (int x = 0; x < kGridSamples; ++x) {
                    identical = identical && fast.at(x, y, z) == reference.at(x, y, z);
                }
            }
        }
        TEST_CHECK(identical);
    }
}

// Marching Cubes vertices of a plains chunk lie on the heightfield surface, and its
// normals point up (toward the air).
void testMeshFollowsHeightfield() {
    const Heightfield field(terrain::kDefaultWorldSeed);
    const Point origin = findPoint(field, false);
    const ChunkColumns columns = terrain::sampleChunkColumns(field, origin.x, origin.z);
    TEST_CHECK(columns.maxHeight - columns.minHeight < static_cast<float>(kChunkSizeMeters));
    const double originY = lowestChunkY(columns);
    const terrain::Mesh mesh =
        terrain::marchingCubes(terrain::densityFromColumns(columns, originY));
    TEST_CHECK(!mesh.indices.empty());

    constexpr double kTolerance = 0.05; // linear interpolation along horizontal cell edges
    bool onSurface = true;
    bool upward = true;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const terrain::Vec3f v = mesh.vertices[i];
        const double h = field.heightAt(origin.x + v.x, origin.z + v.z);
        onSurface = onSurface && std::abs(originY + v.y - h) <= kTolerance;
        upward = upward && mesh.normals[i].y > 0.0f;
    }
    TEST_CHECK(onSurface);
    TEST_CHECK(upward);
}

} // namespace

int main() {
    testDensitySign();
    testColumns();
    testDensityFromColumnsMatchesSampleDensity();
    testMeshFollowsHeightfield();
    return test::result();
}
