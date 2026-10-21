// Climate: latitude, base temperature, humidity, altitude correction, exposure and slope.
#include "terrain/generation/climate.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

using terrain::ClimateField;
using terrain::ClimateParams;
using terrain::ClimateSample;
using terrain::SurfaceSite;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;

// Deterministic sequence of points spread over a few thousand kilometers.
struct Points {
    std::uint64_t state = 0x1234567;

    double next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(state >> 11U) * 0x1.0p-53;
    }
    double coordinate() { return (next() - 0.5) * 4.0e6; }
    // Anywhere along one side of the world.
    double anywhere(double side) { return (next() - 0.5) * side; }
};

void testSlope() {
    TEST_CHECK(terrain::slopeOf(0.0f, 0.0f) == 0.0f);
    TEST_CHECK(std::abs(terrain::slopeOf(3.0f, -4.0f) - 5.0f) < 1e-6f);
    TEST_CHECK(terrain::slopeOf(-0.5f, 0.0f) == 0.5f);
}

void testExposure() {
    TEST_CHECK(terrain::exposureOf(0.0f, 0.0f) == 0.0f);
    // Ground going down toward +Z faces the sun, which stands to the south.
    TEST_CHECK(std::abs(terrain::exposureOf(0.0f, -1.0f) - std::sqrt(0.5f)) < 1e-6f);
    TEST_CHECK(terrain::exposureOf(0.0f, 1.0f) == -terrain::exposureOf(0.0f, -1.0f));
    // Slopes facing east or west are neither warmed nor cooled.
    TEST_CHECK(terrain::exposureOf(0.8f, 0.0f) == 0.0f);
    TEST_CHECK(terrain::exposureOf(-0.8f, 0.0f) == 0.0f);
    // Steeper south faces are more exposed, never beyond 1.
    TEST_CHECK(terrain::exposureOf(0.0f, -0.2f) < terrain::exposureOf(0.0f, -0.5f));
    TEST_CHECK(terrain::exposureOf(0.0f, -1000.0f) < 1.0f);
    TEST_CHECK(terrain::exposureOf(0.0f, -1000.0f) > 0.99f);
}

void testDeterminism() {
    const ClimateField a(kSeed);
    const ClimateField b(kSeed);
    const ClimateField other(kSeed + 1);
    Points points;
    int differences = 0;
    for (int i = 0; i < 200; ++i) {
        const double x = points.coordinate();
        const double z = points.coordinate();
        TEST_CHECK(a.baseTemperatureAt(x, z) == b.baseTemperatureAt(x, z));
        TEST_CHECK(a.humidityAt(x, z, 0.3f) == b.humidityAt(x, z, 0.3f));
        if (a.baseTemperatureAt(x, z) != other.baseTemperatureAt(x, z) &&
            a.humidityAt(x, z, 0.3f) != other.humidityAt(x, z, 0.3f)) {
            ++differences;
        }
    }
    TEST_CHECK(differences > 190);
}

// Base temperatures stay within mean +- amplitude, humidities within [0, 1], and both span a
// wide range: hot and cold, wet and dry climates.
void testRanges() {
    const ClimateField field(kSeed);
    const ClimateParams& params = field.params();
    Points points;
    float minTemperature = 1e9f;
    float maxTemperature = -1e9f;
    float minHumidity = 1.0f;
    float maxHumidity = 0.0f;
    const terrain::WorldSize world;
    for (int i = 0; i < 4000; ++i) {
        const double x = points.anywhere(world.x);
        const double z = points.anywhere(world.z);
        const float temperature = field.baseTemperatureAt(x, z);
        const float humidity = field.humidityAt(x, z, static_cast<float>(points.next()));
        TEST_CHECK(std::abs(temperature - field.latitudeTemperatureAt(z)) <=
                   params.temperatureAmplitude);
        TEST_CHECK(humidity >= 0.0f && humidity <= 1.0f);
        minTemperature = std::min(minTemperature, temperature);
        maxTemperature = std::max(maxTemperature, temperature);
        minHumidity = std::min(minHumidity, humidity);
        maxHumidity = std::max(maxHumidity, humidity);
    }
    TEST_CHECK(minTemperature < -6.0f && maxTemperature > 25.0f);
    TEST_CHECK(minHumidity < 0.1f && maxHumidity > 0.9f);
}

// The latitude: warm on the equator (z = 0), cold at both poles (z = +-world.z / 2, the
// north-south seam), colder toward the poles, the same in both hemispheres, periodic, and flat
// at the poles: no jump and no kink at the seam.
void testLatitude() {
    const ClimateField field(kSeed);
    const ClimateParams& p = field.params();
    const terrain::WorldSize world;
    const double half = 0.5 * world.z;
    TEST_CHECK(field.latitudeTemperatureAt(0.0) == p.equatorTemperature);
    TEST_CHECK(field.latitudeTemperatureAt(half) == p.poleTemperature);
    TEST_CHECK(field.latitudeTemperatureAt(-half) == p.poleTemperature);
    // About 9 degrees at 45 degrees of latitude.
    TEST_CHECK(std::abs(field.latitudeTemperatureAt(0.5 * half) - 9.3f) < 0.5f);
    bool colder = true;
    bool symmetric = true;
    bool periodic = true;
    float previous = field.latitudeTemperatureAt(0.0);
    for (int i = 1; i <= 1000; ++i) {
        const double z = half * i / 1000.0;
        const float t = field.latitudeTemperatureAt(z);
        colder = colder && t <= previous;
        symmetric = symmetric && t == field.latitudeTemperatureAt(-z);
        periodic = periodic && t == field.latitudeTemperatureAt(z + world.z) &&
                   t == field.latitudeTemperatureAt(z - 3.0 * world.z);
        previous = t;
    }
    TEST_CHECK(colder && symmetric && periodic);
    // Across the seam, 2 km apart: within a hundredth of a degree (the slope vanishes there).
    const float north = field.latitudeTemperatureAt(-half + 1000.0);
    const float south = field.latitudeTemperatureAt(half - 1000.0);
    TEST_CHECK(std::abs(north - south) < 1.0e-6f);
    TEST_CHECK(std::abs(north - p.poleTemperature) < 0.01f);
    // The base temperature too: polar at both edges, the fbm continuous across the seam.
    Points points;
    bool cold = true;
    bool continuous = true;
    for (int i = 0; i < 200; ++i) {
        const double x = points.anywhere(world.x);
        cold = cold && field.baseTemperatureAt(x, half) < 0.0f &&
               field.baseTemperatureAt(x, -half) < 0.0f;
        continuous = continuous && std::abs(field.baseTemperatureAt(x, half - 0.5) -
                                            field.baseTemperatureAt(x, -half + 0.5)) < 0.01f;
    }
    TEST_CHECK(cold && continuous);
}

// The temperature drops by exactly lapseRate per meter above sea level, never below it.
void testAltitudeCorrection() {
    const ClimateField field(kSeed);
    const float lapse = field.params().lapseRate;
    TEST_CHECK(field.temperatureAt(15.0f, 0.0f, 0.0f) == 15.0f);
    TEST_CHECK(field.temperatureAt(15.0f, -300.0f, 0.0f) == 15.0f);
    TEST_CHECK(std::abs(field.temperatureAt(15.0f, 1000.0f, 0.0f) - (15.0f - 1000.0f * lapse)) <
               1e-5f);
    TEST_CHECK(std::abs(lapse - 0.0065f) < 1e-9f); // 6.5 degrees per kilometer
    float previous = field.temperatureAt(10.0f, 0.0f, 0.0f);
    bool decreasing = true;
    for (float altitude = 10.0f; altitude <= 6000.0f; altitude += 10.0f) {
        const float temperature = field.temperatureAt(10.0f, altitude, 0.0f);
        decreasing = decreasing && temperature < previous;
        previous = temperature;
    }
    TEST_CHECK(decreasing);
}

SurfaceSite slopedSite(float ground, float gradZ, terrain::WaterKind water) {
    SurfaceSite site;
    site.ground = ground;
    site.waterLevel = water == terrain::WaterKind::Ocean ? terrain::kSeaLevel : ground;
    site.water = water;
    site.gradZ = gradZ;
    site.continentalness = 0.2f;
    return site;
}

// sample() combines the layers: base temperature and humidity of the position, slope and
// exposure of the ground, temperature at the top of the column. The sun warms south faces of
// dry ground only.
void testSample() {
    const ClimateField field(kSeed);
    const double x = 12345.0;
    const double z = -6789.0;
    const ClimateSample south =
        field.sample(x, z, slopedSite(800.0f, -0.6f, terrain::WaterKind::None));
    const ClimateSample north =
        field.sample(x, z, slopedSite(800.0f, 0.6f, terrain::WaterKind::None));
    const ClimateSample flat =
        field.sample(x, z, slopedSite(800.0f, 0.0f, terrain::WaterKind::None));
    TEST_CHECK(south.baseTemperature == field.baseTemperatureAt(x, z));
    TEST_CHECK(south.humidity == field.humidityAt(x, z, 0.2f));
    TEST_CHECK(std::abs(south.slope - 0.6f) < 1e-6f);
    TEST_CHECK(south.exposure > 0.0f && north.exposure < 0.0f && flat.exposure == 0.0f);
    TEST_CHECK(south.temperature > flat.temperature && flat.temperature > north.temperature);
    TEST_CHECK(flat.temperature == field.temperatureAt(flat.baseTemperature, 800.0f, 0.0f));
    TEST_CHECK(south.temperature - flat.temperature <= field.params().exposureWarming);

    // Over the ocean: the temperature of the sea surface, whatever the slope of the floor.
    const ClimateSample sea =
        field.sample(x, z, slopedSite(-200.0f, -0.6f, terrain::WaterKind::Ocean));
    TEST_CHECK(sea.temperature == sea.baseTemperature);
    // A river holds its water at the ground height: corrected like the land around it.
    const ClimateSample river =
        field.sample(x, z, slopedSite(800.0f, 0.0f, terrain::WaterKind::River));
    TEST_CHECK(river.temperature == flat.temperature);
}

// Continental interiors are drier than the coasts, by at most inlandDrying.
void testInlandDrying() {
    const ClimateField field(kSeed);
    Points points;
    bool drier = true;
    bool bounded = true;
    for (int i = 0; i < 500; ++i) {
        const double x = points.coordinate();
        const double z = points.coordinate();
        const float coast = field.humidityAt(x, z, 0.0f);
        const float inland = field.humidityAt(x, z, 0.9f);
        drier = drier && inland <= coast;
        bounded = bounded && coast - inland <= field.params().inlandDrying + 1e-6f;
    }
    TEST_CHECK(drier && bounded);
}

// The climate is continuous: neighbors one meter apart have nearly the same climate, so the
// climate has no seam anywhere (it does not depend on a region).
void testContinuity() {
    const ClimateField field(kSeed);
    Points points;
    float maxTemperatureStep = 0.0f;
    float maxHumidityStep = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        const double x = points.coordinate();
        const double z = points.coordinate();
        maxTemperatureStep =
            std::max(maxTemperatureStep,
                     std::abs(field.baseTemperatureAt(x + 1.0, z) - field.baseTemperatureAt(x, z)));
        maxHumidityStep = std::max(maxHumidityStep, std::abs(field.humidityAt(x, z + 1.0, 0.3f) -
                                                             field.humidityAt(x, z, 0.3f)));
    }
    TEST_CHECK(maxTemperatureStep < 0.01f);
    TEST_CHECK(maxHumidityStep < 0.001f);
}

} // namespace

int main() {
    testSlope();
    testExposure();
    testDeterminism();
    testRanges();
    testLatitude();
    testAltitudeCorrection();
    testSample();
    testInlandDrying();
    testContinuity();
    return test::result();
}
