// Transitions between the biomes and the materials on the default world: the dominant biome and
// material of every sample are those of the ecology (the same bits) on maps of a mountain, a
// coast, a plain and an eroded region with its rivers; the weights change continuously over 1 m
// along transects across bands; the widths of the bands by pair, measured along lines across
// their borders (a few km between temperate forest and grassland, hundreds of meters at the
// edge of the mountains, a few meters at the edge of the rock); every image of a point gets the
// same bits; maps computed on 4 threads equal maps computed on one.

#include "terrain/generation/biome_blend.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/eroded_region.hpp"
#include "terrain/generation/terrain_surface.hpp"

#include "default_world_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

namespace {

using default_world::Place;
using terrain::Biome;
using terrain::BlendWeights;
using terrain::Ecology;
using terrain::SurfaceBlend;
using terrain::SurfaceMaterial;

const Ecology& ecology() {
    static const Ecology e(default_world::kSeed);
    return e;
}

// The blend of the column at (x, z) on the site of a surface of the Heightfield whose samples
// are step meters apart (heightfieldSite), and its slope.
struct Sample {
    SurfaceBlend blend;
    float slope = 0.0f;
    float temperature = 0.0f;
};

Sample sampleAt(double x, double z, double step) {
    const terrain::Heightfield& field = default_world::field();
    const terrain::HeightSample sample = field.sampleAt(x, z);
    const terrain::SurfaceSite site =
        terrain::heightfieldSite(sample.height, sample.continentalness,
                                 {field.heightAt(x + step, z), field.heightAt(x - step, z),
                                  field.heightAt(x, z + step), field.heightAt(x, z - step)},
                                 step);
    const terrain::ClimateSample climate = ecology().climate().sample(x, z, site);
    return {ecology().blendAt(x, z, site, climate), climate.slope, climate.temperature};
}

SurfaceBlend blendAt(double x, double z, double step) {
    return sampleAt(x, z, step).blend;
}

BlendWeights weightsAt(double x, double z, double step) {
    BlendWeights w;
    terrain::addBlend(w, blendAt(x, z, step), 1.0f);
    return w;
}

float largestChange(const BlendWeights& a, const BlendWeights& b) {
    float change = 0.0f;
    for (std::size_t i = 0; i < a.biomes.size(); ++i) {
        change = std::max(change, std::abs(a.biomes[i] - b.biomes[i]));
    }
    for (std::size_t i = 0; i < a.materials.size(); ++i) {
        change = std::max(change, std::abs(a.materials[i] - b.materials[i]));
    }
    return change;
}

bool sameDominants(const terrain::TerrainSurface& surface) {
    const terrain::EcologyMap map = ecology().map(surface);
    const std::vector<SurfaceBlend> blends = ecology().blendMap(surface, map);
    bool same = blends.size() == map.samples.size();
    for (std::size_t i = 0; same && i < blends.size(); ++i) {
        same = blends[i].biomes[0] == map.samples[i].biome &&
               blends[i].materials[0] == map.samples[i].material;
    }
    return same;
}

void testDominantIsTheEcology(const Place& mountain, const Place& coast, const Place& plain) {
    const terrain::Heightfield& field = default_world::field();
    for (const auto& [place, step] : {std::pair{mountain, 50.0}, std::pair{mountain, 2.0},
                                      std::pair{coast, 20.0}, std::pair{plain, 400.0}}) {
        TEST_CHECK(
            sameDominants(terrain::surfaceFromHeightfield(field, {place.x, place.z, step, 96})));
    }
    const terrain::ErodedRegion eroded(default_world::kSeed, {mountain.x, mountain.z, 100.0, 96});
    TEST_CHECK(sameDominants(terrain::surfaceFromEroded(eroded)));
}

bool wet(double x, double z, double step) {
    return blendAt(x, z, step).materials[0] == SurfaceMaterial::Water;
}

// Along a transect of 1 m steps from (x0, z) eastward (sites of 4 m): the largest change of a
// weight over 1 m away from the water (the coast stays sharp), cut into 16 steps: a jump would
// keep its size in one of them.
void checkTransect(double x0, double z, int meters, float& largestStep, float& largestShare) {
    constexpr double kSite = 4.0;
    BlendWeights previous = weightsAt(x0, z, kSite);
    float largest = 0.0f;
    int where = 1;
    for (int m = 1; m <= meters; ++m) {
        const BlendWeights next = weightsAt(x0 + m, z, kSite);
        const float change = largestChange(previous, next);
        if (wet(x0 + m - 1, z, kSite) || wet(x0 + m, z, kSite)) {
            previous = next;
            continue;
        }
        if (change > largest) {
            largest = change;
            where = m;
        }
        previous = next;
    }
    float cut = 0.0f;
    BlendWeights before = weightsAt(x0 + where - 1, z, kSite);
    for (int k = 1; k <= 16; ++k) {
        const BlendWeights after = weightsAt(x0 + where - 1 + k / 16.0, z, kSite);
        cut = std::max(cut, largestChange(before, after));
        before = after;
    }
    largestStep = std::max(largestStep, largest);
    if (largest > 1.0e-3f) {
        largestShare = std::max(largestShare, cut / largest);
    }
}

// Transects of 1 km across the first band of biomes found eastward of each place (every 100 m),
// and of 200 m at each place (the materials of the relief, a few meters wide).
void testContinuousOverOneMeter(const std::vector<Place>& places) {
    float largestStep = 0.0f;
    float largestShare = 0.0f;
    int banded = 0;
    for (const Place& place : places) {
        for (int k = 0; k < 2'000; ++k) {
            const double x = place.x + k * 100.0;
            if (blendAt(x, place.z, 100.0).biomeWeights[1] > 0.2f) {
                checkTransect(x - 500.0, place.z, 1'000, largestStep, largestShare);
                ++banded;
                break;
            }
        }
        checkTransect(place.x, place.z + 37.0, 200, largestStep, largestShare);
    }
    std::printf("over 1 m: largest change %.3f, largest of its 16 cuts %.3f of it, %d bands\n",
                static_cast<double>(largestStep), static_cast<double>(largestShare), banded);
    TEST_CHECK(banded >= 2);
    TEST_CHECK(largestStep < 0.9f);
    TEST_CHECK(largestShare < 0.5f);
}

// What a band measures along a line: the side its dominant id belongs to (0 or 1, -1 for
// neither) and the weight of the other side.
struct Sides {
    std::function<int(const SurfaceBlend&)> side;
    std::function<float(const SurfaceBlend&, int)> otherWeight;
};

// Widths of the bands along a line of count samples step meters apart from (x0, z) eastward
// (sites at the same step): at each change of the dominant side, the run around it where the
// other side weighs more than 2 %, when it ends on both sides within the line and no slope of
// the run exceeds maxSlope.
void bandWidths(double x0, double z, double step, int count, const Sides& s, float maxSlope,
                std::vector<double>& widths) {
    std::vector<Sample> line;
    line.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        line.push_back(sampleAt(x0 + i * step, z, step));
    }
    const auto at = [&line](int i) -> const Sample& { return line[static_cast<std::size_t>(i)]; };
    const auto side = [&](int i) { return s.side(at(i).blend); };
    for (int i = 0; i + 1 < count; ++i) {
        const int a = side(i);
        const int b = side(i + 1);
        if (a < 0 || b < 0 || a == b) {
            continue;
        }
        int left = i;
        while (left > 0 && side(left) == a && s.otherWeight(at(left).blend, a) > 0.02f) {
            --left;
        }
        int right = i + 1;
        while (right + 1 < count && side(right) == b && s.otherWeight(at(right).blend, b) > 0.02f) {
            ++right;
        }
        bool gentle = true;
        for (int k = left; k <= right; ++k) {
            gentle = gentle && at(k).slope <= maxSlope;
        }
        if (gentle && left > 0 && right + 1 < count && side(left) == a && side(right) == b) {
            widths.push_back((right - left - 1) * step);
        }
    }
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

float biomeWeight(const SurfaceBlend& blend, const std::function<bool(Biome)>& in) {
    float w = 0.0f;
    for (std::size_t s = 0; s < terrain::kBlendSlots; ++s) {
        w += in(blend.biomes[s]) ? blend.biomeWeights[s] : 0.0f;
    }
    return w;
}

float materialWeight(const SurfaceBlend& blend, const std::function<bool(SurfaceMaterial)>& in) {
    float w = 0.0f;
    for (std::size_t s = 0; s < terrain::kBlendSlots; ++s) {
        w += in(blend.materials[s]) ? blend.materialWeights[s] : 0.0f;
    }
    return w;
}

// Sides of a pair of sets of biomes.
Sides biomeSides(std::function<bool(Biome)> first, std::function<bool(Biome)> second) {
    return {[=](const SurfaceBlend& b) {
                return first(b.biomes[0]) ? 0 : second(b.biomes[0]) ? 1 : -1;
            },
            [=](const SurfaceBlend& b, int side) {
                return biomeWeight(b, side == 0 ? second : first);
            }};
}

// Forest and grassland (temperate or tropical, the humidity decides) on lines of 120 km across
// the plain; the edge of the mountains on gentle slopes (the tree line: the temperature decides,
// not the slope) on lines of 10 km across the tree line; the edge of the rock (the slope decides)
// on lines of 800 m across the mountain on the site of the LOD 0 tiles (1 m).
void testWidthsByPair(const Place& mountain, const Place& plain, const Place& snowLine) {
    std::vector<double> forest;
    const Sides woods = biomeSides(
        [](Biome b) { return b == Biome::Grassland || b == Biome::Savanna; },
        [](Biome b) { return b == Biome::TemperateForest || b == Biome::TropicalForest; });
    for (int k = -6; k <= 6; ++k) {
        bandWidths(plain.x - 60'000.0, plain.z + k * 8'000.0, 50.0, 2'400, woods, 1.0f, forest);
    }
    std::vector<double> snow;
    const Sides snowEdge =
        biomeSides([](Biome b) { return b == Biome::Snow; },
                   [](Biome b) { return b != Biome::Snow && b != Biome::Ocean; });
    for (int k = -10; k <= 10; ++k) {
        bandWidths(snowLine.x - 2'500.0, snowLine.z + k * 500.0, 5.0, 1'000, snowEdge, 1.0e9f,
                   snow);
    }
    std::vector<double> rock;
    const auto isRock = [](SurfaceMaterial m) { return m == SurfaceMaterial::Rock; };
    const auto isGround = [](SurfaceMaterial m) {
        return m != SurfaceMaterial::Rock && m != SurfaceMaterial::Water;
    };
    const Sides rockEdge{[=](const SurfaceBlend& b) {
                             return isRock(b.materials[0]) ? 0 : isGround(b.materials[0]) ? 1 : -1;
                         },
                         [=](const SurfaceBlend& b, int side) {
                             return side == 0 ? materialWeight(b, isGround)
                                              : materialWeight(b, isRock);
                         }};
    for (int k = -20; k <= 20; ++k) {
        bandWidths(mountain.x - 400.0, mountain.z + k * 50.0, 1.0, 800, rockEdge, 1.0e9f, rock);
    }
    std::printf("median widths: forest and grassland %.0f m (%zu bands), snow %.0f m (%zu), rock "
                "%.1f m (%zu)\n",
                median(forest), forest.size(), median(snow), snow.size(), median(rock),
                rock.size());
    TEST_CHECK(forest.size() >= 5 && median(forest) >= 1'000.0 && median(forest) <= 10'000.0);
    TEST_CHECK(snow.size() >= 5 && median(snow) >= 10.0 && median(snow) <= 1'500.0);
    TEST_CHECK(rock.size() >= 20 && median(rock) >= 1.0 && median(rock) <= 12.0);
}

// The snow line on gentle ground: land at a temperature within half a degree of the snow on a
// slope below 0.2 (lattice of 64 km, out to 12,800 km).
std::optional<Place> snowLine() {
    const terrain::BiomeParams params;
    return default_world::nearest(65536.0, 200, [&params](double x, double z) {
        if (default_world::field().heightAt(x, z) < terrain::kSeaLevel) {
            return false;
        }
        const Sample s = sampleAt(x, z, 50.0);
        return s.slope < 0.2f && std::abs(s.temperature - params.snowTemperature) < 0.5f;
    });
}

void testWrapped(const Place& mountain) {
    const terrain::WorldSize world;
    bool same = true;
    for (int k = 0; k < 64; ++k) {
        const double x = mountain.x + k * 37.5;
        const double z = mountain.z - k * 21.25;
        const SurfaceBlend blend = blendAt(x, z, 1.0);
        same = same && blend == blendAt(x + world.x, z, 1.0) &&
               blend == blendAt(x, z - world.z, 1.0) &&
               blend == blendAt(x - world.x, z + world.z, 1.0);
    }
    TEST_CHECK(same);
    // Across the corner of the world, where its four edges meet.
    const SurfaceBlend corner = blendAt(world.x / 2.0, world.z / 2.0, 4.0);
    TEST_CHECK(corner == blendAt(-world.x / 2.0, -world.z / 2.0, 4.0));
}

void testThreads(const std::vector<Place>& places) {
    const auto mapOf = [](const Place& p) {
        const terrain::TerrainSurface surface =
            terrain::surfaceFromHeightfield(default_world::field(), {p.x, p.z, 25.0, 64});
        return ecology().blendMap(surface, ecology().map(surface));
    };
    std::vector<std::vector<SurfaceBlend>> serial;
    for (const Place& p : places) {
        serial.push_back(mapOf(p));
    }
    std::vector<std::vector<SurfaceBlend>> parallel(places.size());
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < places.size(); ++i) {
        threads.emplace_back([&, i] { parallel[i] = mapOf(places[i]); });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    TEST_CHECK(parallel == serial);
}

} // namespace

int main() {
    const std::optional<Place> mountain = default_world::mountain();
    const std::optional<Place> coast = default_world::coast();
    const std::optional<Place> plain = default_world::plain();
    TEST_CHECK(mountain && coast && plain);
    if (!mountain || !coast || !plain) {
        return test::result();
    }
    testDominantIsTheEcology(*mountain, *coast, *plain);
    testContinuousOverOneMeter({*mountain, *coast, *plain});
    const std::optional<Place> snow = snowLine();
    TEST_CHECK(snow.has_value());
    testWidthsByPair(*mountain, *plain, snow.value_or(*mountain));
    testWrapped(*mountain);
    testThreads({*mountain, *coast, *plain});
    return test::result();
}
