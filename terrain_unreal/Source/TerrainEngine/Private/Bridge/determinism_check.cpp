#include "determinism_check.hpp"

#include <array>
#include <bit>
#include <cstdio>

namespace terrain::unreal {

namespace {

// Produced by the core built by CMake (GCC 13, Release) and checked by
// unreal_determinism_check_test on every ctest run, on every platform of the CI.
constexpr std::array<HeightProbe, 10> kHeightProbes{{
    {0.0, 0.0, 0xBEE0B883U},                 // -0.44 m: the origin lies on a coast
    {1234.5, -6789.25, 0xC185AC74U},         // -16.71 m: the shallow sea by that coast
    {162047.75, 188158.5, 0x45233E31U},      // 2611.89 m: a mountain
    {-147456.25, 57344.5, 0x42179410U},      // 37.89 m: a plain
    {400000.0, 0.25, 0xC3A5B480U},           // -331.41 m: the continental slope
    {163296.25, -11327312.5, 0x45AF5DC1U},   // 5611.72 m: the summit of a belt
    {-12345678.9, 7654321.5, 0x435E0D42U},   // 222.05 m: far inland
    {30015487.5, 17498111.75, 0xC58FC0D3U},  // -4600.10 m: by the corner of the world
    {-30015488.0, -17498112.0, 0xC58FC0D5U}, // -4600.10 m: the corner itself
    {60032210.5, -35003013.25, 0xC185AC74U}, // the second probe, one world away
}};

// Produced and checked the same way.
constexpr std::array<TileProbe, 5> kTileProbes{{
    {{{{0, 0}, 0}, 0}, 0x8B9B1FB5558A2F1DULL},                      // the origin, a coast
    {{{{10128, 11760}, 0}, 0}, 0xD44368150476C663ULL},              // a mountain
    {{{{-9216, 3584}, 0}, 0}, 0x29BE34450E519420ULL},               // a plain
    {{{{-2, 3}, 0}, kSeamNegX | kSeamPosZ}, 0xA833FDBD33E86263ULL}, // the coast, with seams
    {{{{1875967, 1093631}, 0}, 0}, 0x9D9F183BDB302F4EULL},          // the corner of the world
}};

class Fnv1a {
public:
    void add(std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            hash_ ^= (value >> (8 * i)) & 0xFFU;
            hash_ *= 1099511628211ULL;
        }
    }
    void add(float value) { add(std::bit_cast<std::uint32_t>(value), 4); }
    void add(double value) { add(std::bit_cast<std::uint64_t>(value), 8); }
    void add(const Vec3f& v) {
        add(v.x);
        add(v.y);
        add(v.z);
    }
    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 14695981039346656037ULL;
};

DeterminismReport heightMismatch(const HeightProbe& probe, std::uint32_t bits) {
    DeterminismReport report;
    report.same = false;
    report.height = probe;
    report.actualBits = bits;
    return report;
}

DeterminismReport tileMismatch(const TileProbe& probe, std::uint64_t fingerprint) {
    DeterminismReport report;
    report.same = false;
    report.tile = true;
    report.tileProbe = probe;
    report.actualFingerprint = fingerprint;
    return report;
}

std::string describeMismatch(const DeterminismReport& report) {
    char text[320];
    if (report.tile) {
        const TileKey& key = report.tileProbe.request.key;
        std::snprintf(text, sizeof(text),
                      "this build does not generate the same world: the mesh of the tile (%d, %d) "
                      "LOD %d has the fingerprint 0x%016llX instead of 0x%016llX",
                      key.origin.x, key.origin.z, key.lod,
                      static_cast<unsigned long long>(report.actualFingerprint),
                      static_cast<unsigned long long>(report.tileProbe.fingerprint));
    } else {
        std::snprintf(text, sizeof(text),
                      "this build does not generate the same world: heightAt(%.4f, %.4f) = %.9g m "
                      "(0x%08X) instead of %.9g m (0x%08X)",
                      report.height.x, report.height.z,
                      static_cast<double>(std::bit_cast<float>(report.actualBits)),
                      static_cast<unsigned>(report.actualBits),
                      static_cast<double>(std::bit_cast<float>(report.height.bits)),
                      static_cast<unsigned>(report.height.bits));
    }
    return text;
}

} // namespace

std::span<const HeightProbe> heightProbes() {
    return kHeightProbes;
}

std::span<const TileProbe> tileProbes() {
    return kTileProbes;
}

std::uint64_t tileFingerprint(const TileData& data) {
    Fnv1a hash;
    hash.add(data.originX);
    hash.add(data.originY);
    hash.add(data.originZ);
    hash.add(data.mesh.vertices.size(), 8);
    for (const Vec3f& vertex : data.mesh.vertices) {
        hash.add(vertex);
    }
    for (const Vec3f& normal : data.mesh.normals) {
        hash.add(normal);
    }
    hash.add(data.mesh.indices.size(), 8);
    for (const std::uint32_t index : data.mesh.indices) {
        hash.add(index, 4);
    }
    return hash.value();
}

DeterminismReport checkHeights(const Heightfield& field) {
    for (const HeightProbe& probe : kHeightProbes) {
        const auto bits = std::bit_cast<std::uint32_t>(field.heightAt(probe.x, probe.z));
        if (bits != probe.bits) {
            return heightMismatch(probe, bits);
        }
    }
    return {};
}

DeterminismReport checkTiles(const TerrainTiles& tiles) {
    for (const TileProbe& probe : kTileProbes) {
        const std::uint64_t fingerprint = tileFingerprint(tiles.generate(probe.request));
        if (fingerprint != probe.fingerprint) {
            return tileMismatch(probe, fingerprint);
        }
    }
    return {};
}

DeterminismReport checkDefaultWorld() {
    const TerrainTiles tiles(kDefaultWorldSeed);
    const DeterminismReport heights = checkHeights(tiles.heightfield());
    return heights.same ? checkTiles(tiles) : heights;
}

std::string describe(const DeterminismReport& report) {
    if (report.same) {
        return "the heights and tiles of the default world match terrain_core bit for bit";
    }
    return describeMismatch(report);
}

} // namespace terrain::unreal
