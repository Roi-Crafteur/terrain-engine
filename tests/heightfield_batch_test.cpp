// The batch functions of the Heightfield (samplesAt, heightsAt) give the bits of sampleAt at each
// point whatever the sequence: empty, one point, the same point again and again, a row and the
// same row reversed, points alternating between both ends of the world, rows 256 m and 1 km apart
// through the walls of the highest chain and through a plain (the cells of the memos change at
// every point, as for the far horizon); and they refuse spans of different sizes. frozen_reference_test compares them with the frozen reference on rows and
// scattered points.
#include "terrain/generation/heightfield.hpp"

#include "bit_exact_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {

using bit_exact::Point;
using bit_exact::same;

void checkSequence(const terrain::Heightfield& field, const std::vector<Point>& points) {
    std::vector<double> xs;
    std::vector<double> zs;
    for (const Point& p : points) {
        xs.push_back(p.x);
        zs.push_back(p.z);
    }
    std::vector<terrain::HeightSample> samples(points.size());
    std::vector<float> heights(points.size());
    field.samplesAt(xs, zs, samples);
    field.heightsAt(xs, zs, heights);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const terrain::HeightSample expected = field.sampleAt(points[i].x, points[i].z);
        TEST_CHECK(same(samples[i], expected));
        TEST_CHECK(bit_exact::sameWater(samples[i], expected));
        TEST_CHECK(same(heights[i], expected.height));
    }
}

void checkSequences(const terrain::Heightfield& field) {
    const terrain::WorldSize& world = field.world();
    checkSequence(field, {});
    checkSequence(field, {{-1245184.5, -8500224.25}});
    checkSequence(field, std::vector<Point>(50, Point{0.5 * world.x - 1.0, 3.0}));
    std::vector<Point> row = bit_exact::rowPoints(world, 64);
    checkSequence(field, row);
    std::reverse(row.begin(), row.end());
    checkSequence(field, row);
    std::vector<Point> alternating;
    for (int i = 0; i < 64; ++i) {
        const double side = (i % 2 == 0 ? 0.5 : -0.5) * world.x;
        alternating.push_back({side - (i % 2) * 1.0 + 0.25 * i, -0.5 * world.z + i});
    }
    checkSequence(field, alternating);
}

// Rows along x and z from (x, z), points step meters apart.
void checkCoarseRows(const terrain::Heightfield& field, double x, double z) {
    for (const double step : {256.0, 1024.0}) {
        std::vector<Point> alongX;
        std::vector<Point> alongZ;
        for (int i = 0; i < 64; ++i) {
            alongX.push_back({x + step * i, z});
            alongZ.push_back({x, z - step * i});
        }
        checkSequence(field, alongX);
        checkSequence(field, alongZ);
    }
}

void checkSizes(const terrain::Heightfield& field) {
    const std::vector<double> two = {1.0, 2.0};
    const std::vector<double> three = {1.0, 2.0, 3.0};
    std::vector<float> heights(2);
    std::vector<terrain::HeightSample> samples(3);
    bool threw = false;
    try {
        field.heightsAt(two, three, heights);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_CHECK(threw);
    threw = false;
    try {
        field.samplesAt(two, two, samples);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_CHECK(threw);
}

} // namespace

int main() {
    const terrain::Heightfield field(20261019);
    checkSequences(field);
    checkCoarseRows(field, 400'000.0, -11'412'000.0); // the walls below the highest summit
    checkCoarseRows(field, -1'286'144.0, -8'541'184.0); // a plain of the benchmark
    checkSizes(field);
    terrain::HeightfieldParams planet;
    planet.macroContinent = 0.25f;
    planet.plates.amplitude = 0.5f;
    checkSequences(terrain::Heightfield(20261019, planet));
    checkSequences(terrain::Heightfield(20261019, {}, bit_exact::kWorlds[1]));
    return test::result();
}
