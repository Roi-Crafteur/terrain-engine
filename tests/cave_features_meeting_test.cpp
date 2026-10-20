// The meeting of two cave region types that holds a preferred type (the preview of the cave
// regions export is centered on one with the Rivers or Volcanic type, which hold the underground
// rivers and the lava): the preferred meeting, the fall back to the nearest of all, and the
// export that follows it.
#include "terrain/generation/aquifers.hpp"
#include "terrain/io/cave_regions_png.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "caves_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace {

using terrain::AquiferCache;
using terrain::CaveDistrictType;
using terrain::CaveRegionsMeeting;
using terrain::DistrictWeights;

constexpr std::uint64_t kSeed = terrain::kDefaultWorldSeed;
constexpr std::array<CaveDistrictType, 2> kFluids = {CaveDistrictType::Rivers,
                                                     CaveDistrictType::Volcanic};

terrain::CaveParams districtParams() {
    terrain::CaveParams params;
    params.districts = terrain::CaveDistrictParams{};
    return params;
}

const terrain::TerrainTiles& world() {
    static const terrain::TerrainTiles tiles(kSeed, {}, {}, districtParams());
    return tiles;
}

const terrain::CaveField& field() {
    return *world().caves();
}

// Total weight of the types at a point.
double weightOf(const DistrictWeights& weights, std::span<const CaveDistrictType> types) {
    double total = 0.0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(weights.count); ++i) {
        if (std::find(types.begin(), types.end(), weights.districts[i].type) != types.end()) {
            total += static_cast<double>(weights.weights[i]);
        }
    }
    return total;
}

double distanceTo(const CaveRegionsMeeting& meeting, double x, double z) {
    return std::hypot(meeting.x - x, meeting.z - z);
}

bool same(const CaveRegionsMeeting& a, const CaveRegionsMeeting& b) {
    return a.found == b.found && a.x == b.x && a.z == b.z;
}

// A type that no district of the 13 x 13 cells around the point has, if there is one.
std::optional<CaveDistrictType> absentType(const terrain::CaveGround& ground, double x, double z) {
    std::array<bool, terrain::kCaveDistrictTypeCount> present{};
    const double cell = field().districts()->params().cellSize;
    const int homeX = static_cast<int>(std::floor(x / cell));
    const int homeZ = static_cast<int>(std::floor(z / cell));
    for (int cellZ = homeZ - 6; cellZ <= homeZ + 6; ++cellZ) {
        for (int cellX = homeX - 6; cellX <= homeX + 6; ++cellX) {
            present[static_cast<std::size_t>(field().district(cellX, cellZ, ground).type)] = true;
        }
    }
    for (std::size_t t = 0; t < present.size(); ++t) {
        if (!present[t]) {
            return static_cast<CaveDistrictType>(t);
        }
    }
    return std::nullopt;
}

// The nearest meeting that holds a preferred type is one of Rivers or Volcanic with a weight of
// its own at its midpoint, never nearer than the nearest of all, and the same when asked again.
void testPreferredMeeting() {
    const terrain::CaveGround& ground = world().caveGround();
    const CaveRegionsMeeting plain = terrain::findCaveRegionsMeeting(field(), ground, 0.0, 0.0);
    const CaveRegionsMeeting wanted =
        terrain::findCaveRegionsMeeting(field(), ground, 0.0, 0.0, kFluids);
    const CaveRegionsMeeting again =
        terrain::findCaveRegionsMeeting(field(), ground, 0.0, 0.0, kFluids);
    TEST_CHECK(plain.found && wanted.found && same(wanted, again));
    AquiferCache cache(field(), ground);
    TEST_CHECK(weightOf(cache.districtWeights(wanted.x, wanted.z), kFluids) >= 0.25);
    TEST_CHECK(distanceTo(wanted, 0.0, 0.0) >= distanceTo(plain, 0.0, 0.0));
    // It is the nearest of its kind: asked from itself, it answers itself.
    const CaveRegionsMeeting from =
        terrain::findCaveRegionsMeeting(field(), ground, wanted.x, wanted.z, kFluids);
    TEST_CHECK(from.found && from.x == wanted.x && from.z == wanted.z);
    // No preference changes nothing, nor does preferring every type.
    TEST_CHECK(same(terrain::findCaveRegionsMeeting(field(), ground, 0.0, 0.0, {}), plain));
    std::vector<CaveDistrictType> every;
    for (int t = 0; t < terrain::kCaveDistrictTypeCount; ++t) {
        every.push_back(static_cast<CaveDistrictType>(t));
    }
    TEST_CHECK(same(terrain::findCaveRegionsMeeting(field(), ground, 0.0, 0.0, every), plain));
}

// Without a meeting that holds a preferred type, the nearest of all answers; without land, the
// point itself. On warm flat ground no district is a glacier: preferring glaciers changes nothing.
void testFallBack() {
    const terrain::CaveGround warm = caves_test::flatGround(100.0f);
    const std::optional<CaveDistrictType> absent = absentType(warm, 0.0, 0.0);
    TEST_CHECK(absent.has_value());
    if (absent) {
        const std::array<CaveDistrictType, 1> preferred = {*absent};
        const CaveRegionsMeeting plain = terrain::findCaveRegionsMeeting(field(), warm, 0.0, 0.0);
        TEST_CHECK(plain.found);
        TEST_CHECK(same(terrain::findCaveRegionsMeeting(field(), warm, 0.0, 0.0, preferred),
                        plain));
    }
    const terrain::CaveGround sea = caves_test::flatGround(-500.0f);
    const CaveRegionsMeeting none =
        terrain::findCaveRegionsMeeting(field(), sea, 123.0, -456.0, kFluids);
    TEST_CHECK(!none.found && none.x == 123.0 && none.z == -456.0);
}

// The export centers its preview on the preferred meeting it is asked for, and on the nearest of
// all without a preference.
void testExportFollowsPreference() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "terrain_cave_features_meeting_test";
    std::filesystem::remove_all(directory);
    terrain::CaveRegionsExport request;
    request.map = terrain::HeightmapRegion{0.0, 0.0, 100.0, 8};
    request.previewMetersPerPixel = 40.0;
    request.previewSize = 8;
    terrain::CaveRegionsStats plainStats;
    TEST_CHECK(terrain::exportCaveRegionsPngs(request, directory, &plainStats));
    request.preferredTypes = {kFluids.begin(), kFluids.end()};
    terrain::CaveRegionsStats wantedStats;
    TEST_CHECK(terrain::exportCaveRegionsPngs(request, directory, &wantedStats));
    std::filesystem::remove_all(directory);

    const terrain::CaveGround& ground = world().caveGround();
    const CaveRegionsMeeting plain = terrain::findCaveRegionsMeeting(field(), ground, 0.0, 0.0);
    const CaveRegionsMeeting wanted =
        terrain::findCaveRegionsMeeting(field(), ground, 0.0, 0.0, kFluids);
    TEST_CHECK(plainStats.meetingFound && plainStats.previewArea.centerX == plain.x &&
               plainStats.previewArea.centerZ == plain.z);
    TEST_CHECK(wantedStats.meetingFound && wantedStats.previewArea.centerX == wanted.x &&
               wantedStats.previewArea.centerZ == wanted.z);
}

} // namespace

int main() {
    testPreferredMeeting();
    testFallBack();
    testExportFollowsPreference();
    return test::result();
}
