#pragma once

// Golden checksums. Every client generates the world from the seed, so the same seed must give
// the same bits with MSVC, GCC, Clang and AppleClang, on x86-64 and ARM64: the checksums are
// asserted on every platform. One 64-bit FNV-1a checksum per part, over the raw bits of the
// values, so that a failure says what changed; the values obtained are printed in a form to
// paste back into the source when the world changes on purpose.

#include "terrain/generation/speleothems.hpp"
#include "terrain/generation/vegetation.hpp"
#include "terrain/meshing/mesh.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include "test_assert.hpp"

#include <bit>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace golden {

class Checksum {
public:
    void add(std::uint64_t value, int bytes = 8) {
        for (int i = 0; i < bytes; ++i) {
            hash_ = (hash_ ^ ((value >> (8 * i)) & 0xFFU)) * 0x100000001B3ULL;
        }
    }
    void add(float value) { add(std::bit_cast<std::uint32_t>(value), 4); }
    void add(double value) { add(std::bit_cast<std::uint64_t>(value)); }
    void add(int value) { add(static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)), 4); }
    void add(bool value) { add(static_cast<std::uint64_t>(value ? 1 : 0), 1); }
    void add(const terrain::Vec3f& v) {
        add(v.x);
        add(v.y);
        add(v.z);
    }
    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 0xCBF29CE484222325ULL;
};

inline void addMesh(Checksum& sum, const terrain::Mesh& mesh) {
    sum.add(static_cast<std::uint64_t>(mesh.vertices.size()));
    for (const terrain::Vec3f& v : mesh.vertices) {
        sum.add(v);
    }
    for (const terrain::Vec3f& n : mesh.normals) {
        sum.add(n);
    }
    for (const std::uint32_t index : mesh.indices) {
        sum.add(static_cast<std::uint64_t>(index), 4);
    }
}

inline void addPlant(Checksum& sum, const terrain::Plant& p) {
    sum.add(static_cast<std::uint64_t>(p.type), 1);
    sum.add(p.x);
    sum.add(p.z);
    sum.add(p.y);
    sum.add(p.scale);
    sum.add(p.rotation);
}

inline void addSpeleothem(Checksum& sum, const terrain::Speleothem& s) {
    sum.add(static_cast<std::uint64_t>(s.kind), 1);
    sum.add(s.x);
    sum.add(s.z);
    sum.add(s.y);
    sum.add(s.length);
    sum.add(s.radius);
    sum.add(s.ice);
}

inline void addMaterials(Checksum& sum, const std::vector<terrain::CaveMaterial>& materials) {
    for (const terrain::CaveMaterial m : materials) {
        sum.add(static_cast<std::uint64_t>(m), 1);
    }
}

// Everything a tile holds.
inline void addTile(Checksum& sum, const terrain::TileData& data) {
    sum.add(data.originX);
    sum.add(data.originY);
    sum.add(data.originZ);
    sum.add(data.minHeight);
    sum.add(data.maxHeight);
    addMesh(sum, data.mesh);
    sum.add(static_cast<std::uint64_t>(data.plants.size()));
    for (const terrain::Plant& p : data.plants) {
        addPlant(sum, p);
    }
    addMaterials(sum, data.materials);
    addMesh(sum, data.water);
    addMaterials(sum, data.waterMaterials);
    sum.add(static_cast<std::uint64_t>(data.speleothems.size()));
    for (const terrain::Speleothem& s : data.speleothems) {
        addSpeleothem(sum, s);
    }
    addMesh(sum, data.lava);
}

struct Part {
    const char* name;
    std::uint64_t actual;
    std::uint64_t expected;
};

// Prints the parts, ready to paste back, and checks each one.
inline void report(const char* title, const std::vector<Part>& parts) {
    std::printf("golden checksums, %s:\n", title);
    for (const Part& part : parts) {
        std::printf("  {\"%s\", 0x%016" PRIX64 "ULL},%s\n", part.name, part.actual,
                    part.actual == part.expected ? "" : "  (differs)");
        TEST_CHECK(part.actual == part.expected);
    }
}

} // namespace golden
