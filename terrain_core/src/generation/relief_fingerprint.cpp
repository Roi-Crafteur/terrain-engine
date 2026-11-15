// The fingerprint of a relief (relief_fingerprint.hpp).
#include "terrain/generation/relief_fingerprint.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace terrain {

// A field added to one of these structures must be written below, in the function of its
// structure; then the size here follows (the same on MSVC, GCC and Clang, 64 bits).
static_assert(sizeof(VoronoiParams) == 144, "VoronoiParams changed: write its new field below");
static_assert(sizeof(GeographyParams) == 688, "GeographyParams changed: write its new field below");
static_assert(sizeof(HeightfieldParams) == 944,
              "HeightfieldParams changed: write its new field below");

namespace {

// The values of the fields, little endian, in the order written.
class Bytes {
public:
    void add(double v) { put(std::bit_cast<std::uint64_t>(v), 8); }
    void add(float v) { put(std::bit_cast<std::uint32_t>(v), 4); }
    void add(int v) { put(static_cast<std::uint32_t>(v), 4); }
    void add(bool v) { bytes_.push_back(v ? 1 : 0); }
    [[nodiscard]] std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    void put(std::uint64_t v, int count) {
        for (int n = 0; n < count; ++n) {
            bytes_.push_back(static_cast<std::uint8_t>(v >> (8 * n)));
        }
    }
    std::vector<std::uint8_t> bytes_;
};

void addVoronoi(Bytes& b, const VoronoiParams& v) {
    b.add(v.amplitude);
    b.add(v.minPlates);
    b.add(v.maxPlates);
    b.add(v.lloydIterations);
    b.add(v.warpWavelength);
    b.add(v.warpAmplitude);
    b.add(v.detailWarpWavelength);
    b.add(v.detailWarpAmplitude);
    b.add(v.continentLevel);
    b.add(v.oceanLevel);
    b.add(v.inlandSeaLevel);
    b.add(v.shelfLevel);
    b.add(v.coastWidth);
    b.add(v.coastNoiseWavelength);
    b.add(v.coastRoughness);
    b.add(v.collisionWidth);
    b.add(v.collisionUplift);
    b.add(v.collisionMountainBoost);
    b.add(v.riftWidth);
    b.add(v.riftDepth);
    b.add(v.riftFlankWidth);
    b.add(v.riftFlankHeight);
    b.add(v.oceanRidgeHeight);
    b.add(v.junctionFade);
}

// The crust, the continents and the coasts of the geography.
void addGeographyCrust(Bytes& b, const GeographyParams& g) {
    b.add(g.enabled);
    b.add(g.cellSize);
    b.add(g.crustWavelength);
    b.add(g.crustOctaves);
    b.add(g.crustRoughness);
    b.add(g.crustWarpWavelength);
    b.add(g.crustWarpAmplitude);
    b.add(g.landShare);
    b.add(g.polarBias);
    b.add(g.polarStart);
    b.add(g.coreLatitudes);
    b.add(g.crustCandidates);
    b.add(g.minContinents);
    b.add(g.maxContinents);
    b.add(g.continentArea);
    b.add(g.largestShare);
    b.add(g.inlandSeaArea);
    b.add(g.coastWavelength);
    b.add(g.coastAmplitude);
    b.add(g.coastBand);
    b.add(g.detailCoastWavelength);
    b.add(g.detailCoastAmplitude);
    b.add(g.detailCoastBand);
    b.add(g.fineCoastWavelength);
    b.add(g.fineCoastAmplitude);
    b.add(g.fineCoastBand);
    b.add(g.ruggedWavelength);
    b.add(g.smoothRugged);
    b.add(g.shelfDepth);
    b.add(g.shelfWidth);
    b.add(g.shelfSlope);
    b.add(g.abyssDepth);
    b.add(g.coastRamp);
    b.add(g.drainSlope);
    b.add(g.inlandRise);
    b.add(g.drainJitter);
}

// The provinces, the plates, the belts and the plateaus of the geography.
void addGeographyBelts(Bytes& b, const GeographyParams& g) {
    b.add(g.provinceSize);
    b.add(g.provinceWarp);
    b.add(g.forelandDistance);
    b.add(g.provinceBlend);
    addVoronoi(b, g.plates);
    b.add(g.beltWidth);
    b.add(g.beltOffset);
    b.add(g.beltUplift);
    b.add(g.chainlessShare);
    b.add(g.plateauShare);
    b.add(g.plateauLength);
    b.add(g.plateauUplift);
    b.add(g.plateauWidth);
    b.add(g.beltReach);
    b.add(g.reliefFloor);
    b.add(g.reliefVariation);
    b.add(g.plateauRelief);
    b.add(g.massifRelief);
    b.add(g.spurRelief);
    b.add(g.spurWavelength);
}

// The steps of the relief, the mountains, the detail, the sea floors and the rifts.
void addGeographyRelief(Bytes& b, const GeographyParams& g) {
    for (const bool step : {g.ravines, g.livelyPlains, g.valleys, g.summits, g.walls, g.plainForms,
                            g.depressions, g.coasts}) {
        b.add(step);
    }
    b.add(g.mountainHeight);
    b.add(g.mountainWavelength);
    b.add(g.mountainSharpness);
    b.add(g.mountainGain);
    b.add(g.mountainValleyDetail);
    b.add(g.rockRelief);
    b.add(g.rockWavelength);
    b.add(g.rockOctaves);
    b.add(g.detailAmplitude);
    b.add(g.arcLift);
    b.add(g.arcWavelength);
    b.add(g.trenchDepth);
    b.add(g.trenchWidth);
    b.add(g.ridgeLift);
    b.add(g.ridgeWidth);
    b.add(g.riftShare);
    b.add(g.riftDepthLow);
    b.add(g.riftDepthHigh);
    b.add(g.riftWidth);
    b.add(g.riftShoulder);
    b.add(g.landMin);
}

void addHeightfield(Bytes& b, const HeightfieldParams& p) {
    b.add(p.continentWavelength);
    b.add(p.coastContinentalness);
    b.add(p.macroContinent);
    b.add(p.macroContinentWavelength);
    b.add(p.macroContinentWidth);
    addVoronoi(b, p.plates);
    b.add(p.maskWavelength);
    b.add(p.maskLow);
    b.add(p.maskHigh);
    b.add(p.warpWavelength);
    b.add(p.warpAmplitude);
    b.add(p.ridgeWavelength);
    b.add(p.ridgeOctaves);
    b.add(p.ridgeSharpness);
    b.add(p.mountainHeight);
    b.add(p.detailWavelength);
    b.add(p.detailAmplitude);
    addGeographyCrust(b, p.geography);
    addGeographyBelts(b, p.geography);
    addGeographyRelief(b, p.geography);
}

class Fnv {
public:
    void add(std::uint8_t byte) {
        hash_ ^= byte;
        hash_ *= 0x100000001B3ULL;
    }
    void add(std::uint64_t v, int count) {
        for (int n = 0; n < count; ++n) {
            add(static_cast<std::uint8_t>(v >> (8 * n)));
        }
    }
    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 0xCBF29CE484222325ULL;
};

} // namespace

std::vector<std::uint8_t> reliefParamsBytes(const HeightfieldParams& params) {
    Bytes b;
    addHeightfield(b, params);
    return b.take();
}

std::uint64_t reliefFingerprint(const Heightfield& relief) {
    Fnv f;
    for (const std::uint8_t byte : reliefParamsBytes(relief.params())) {
        f.add(byte);
    }
    const WorldSize& world = relief.world();
    f.add(std::bit_cast<std::uint64_t>(world.x), 8);
    f.add(std::bit_cast<std::uint64_t>(world.z), 8);
    const Heightfield bare = relief.withoutCoasts();
    for (int k = 0; k < kFingerprintSide; ++k) {
        for (int i = 0; i < kFingerprintSide; ++i) {
            const double x = world.x * ((i + 0.5) / kFingerprintSide - 0.5);
            const double z = world.z * ((k + 0.5) / kFingerprintSide - 0.5);
            f.add(std::bit_cast<std::uint32_t>(bare.heightAt(x, z)), 4);
        }
    }
    return f.value();
}

} // namespace terrain
