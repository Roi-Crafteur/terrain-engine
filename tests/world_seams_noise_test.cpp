// The surface across the edges of the wrapped world (WorldSize): the heightfield and each of its
// layers (with and without the macro and plate layers), the climate and the ecology give the same
// bits at every image of a point, and none of them jumps across the edges x = +-world.x / 2 (at a
// mid-latitude), z = +-world.z / 2 (the poles) and the corner. The noises themselves:
// noise_periodic_test.cpp; the plates: voronoi_test.cpp; the latitude: climate_test.cpp.
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"

#include "test_assert.hpp"
#include "world_seams_support.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using seams_test::kWorld;
using seams_test::Point;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

std::vector<float> layersOf(const terrain::HeightSample& s) {
    return {s.continentalness,
            s.macroContinentalness,
            s.plateContinentalness,
            s.plateCollision,
            s.mountainMask,
            s.base,
            s.mountain,
            s.detail,
            s.height};
}

terrain::Heightfield planetField() {
    terrain::HeightfieldParams params;
    params.macroContinent = 0.25f;
    params.plates.amplitude = 0.5f;
    return terrain::Heightfield(kSeed, params);
}

// Every layer of the heightfield, bit for bit at every image, and continuous across the edges.
void testHeightfield(const terrain::Heightfield& field, const char* name) {
    bool same = true;
    for (const Point p : seams_test::testPoints(300, 1)) {
        const std::vector<float> layers = layersOf(field.sampleAt(p.x, p.z));
        for (const auto& k : seams_test::kImages) {
            const Point q = seams_test::imageOf(p, k);
            same = same && layersOf(field.sampleAt(q.x, q.z)) == layers &&
                   field.heightAt(q.x, q.z) == layers.back();
        }
    }
    std::printf("%s: every layer the same at every image of 300 points\n", name);
    TEST_CHECK(same);
    for (const seams_test::Walk& walk : seams_test::kWalks) {
        TEST_CHECK(seams_test::continuousAcross(
            walk, [&field](double x, double z) { return layersOf(field.sampleAt(x, z)); },
            "layers"));
    }
}

std::vector<float> ecologyOf(const terrain::EcologySample& s) {
    std::vector<float> values = {s.climate.baseTemperature,
                                 s.climate.humidity,
                                 s.climate.temperature,
                                 s.climate.slope,
                                 s.climate.exposure,
                                 static_cast<float>(s.biome),
                                 static_cast<float>(s.material)};
    values.insert(values.end(), s.densities.begin(), s.densities.end());
    return values;
}

// The climate (base temperature, humidity) and the ecology of a site (climate, biome, material,
// plant densities): bit for bit at every image; the climate continuous across the edges.
void testClimateAndEcology() {
    const terrain::Ecology ecology(kSeed);
    const terrain::ClimateField& climate = ecology.climate();
    terrain::SurfaceSite site;
    site.ground = 420.0f;
    site.waterLevel = 420.0f;
    site.gradX = 0.12f;
    site.gradZ = -0.05f;
    site.steepness = 0.13f;
    site.continentalness = 0.3f;
    bool same = true;
    for (const Point p : seams_test::testPoints(300, 2)) {
        const float base = climate.baseTemperatureAt(p.x, p.z);
        const float humidity = climate.humidityAt(p.x, p.z, 0.3f);
        const std::vector<float> sample = ecologyOf(ecology.sampleAt(p.x, p.z, site));
        for (const auto& k : seams_test::kImages) {
            const Point q = seams_test::imageOf(p, k);
            same = same && climate.baseTemperatureAt(q.x, q.z) == base &&
                   climate.humidityAt(q.x, q.z, 0.3f) == humidity &&
                   ecologyOf(ecology.sampleAt(q.x, q.z, site)) == sample;
        }
    }
    std::printf("climate and ecology: the same at every image of 300 points\n");
    TEST_CHECK(same);
    for (const seams_test::Walk& walk : seams_test::kWalks) {
        TEST_CHECK(seams_test::continuousAcross(
            walk,
            [&climate](double x, double z) {
                return std::vector<float>{climate.baseTemperatureAt(x, z),
                                          climate.humidityAt(x, z, 0.3f)};
            },
            "climate"));
    }
}

} // namespace

int main() {
    testHeightfield(terrain::Heightfield(kSeed), "heightfield");
    testHeightfield(planetField(), "heightfield with the macro and plate layers");
    testClimateAndEcology();
    return test::result();
}
