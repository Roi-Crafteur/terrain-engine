#pragma once

// Golden checksums. Every client generates the world from the seed, so the same seed must give
// the same bits with MSVC, GCC, Clang and AppleClang, on x86-64 and ARM64: the checksums are
// asserted on every platform. One 64-bit FNV-1a checksum per part, over the raw bits of the
// values, so that a failure says what changed; the values obtained are printed in a form to
// paste back into the source when the world changes on purpose.
//
// Variants with experimental flags (the common model of the agents of wave 9, until R1 turns
// every new relief on together). A relief merged off behind an experimental flag keeps the
// goldens of main, bit for bit, and pins its own values in variants of them:
//   - a variant is the same executable run with --flags <name> (a second ctest entry, no other
//     build): add_test(NAME <test>_<name> COMMAND <test> --flags <name>);
//   - its main calls golden::readFlags(argc, argv) first: an unknown name fails the test;
//   - every world of a golden is built with golden::reliefParams(its params) (and its water
//     with golden::waterParams), so that the flags read set them;
//   - every value a flag changes reads golden::pick("<name>", value of main, value with it).
// An agent adds its name to the table of the experimental flags and these calls where its
// relief reaches, never helpers of its own. At R1 the values with the flags become those of
// main and the variants go.

#include "terrain/generation/speleothems.hpp"
#include "terrain/generation/vegetation.hpp"
#include "terrain/meshing/mesh.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include "terrain/generation/experimental_flags.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <bit>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
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

// The experimental flags of the variant this test runs as (--flags), none by default.
struct Flags {
    std::string text;
    std::vector<std::string> applied;
};

inline Flags& flags() {
    static Flags read;
    return read;
}

// Reads --flags <names> from the command line of the test; an unknown name fails it.
inline void readFlags(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--flags") {
            flags().text = argv[i + 1];
        }
    }
    terrain::WorldParams probe;
    const terrain::AppliedFlags read = terrain::applyExperimentalFlags(flags().text, probe);
    // The canonical text, "a,b": the names applied.
    flags().applied.clear();
    for (std::size_t at = 0; at < read.text.size();) {
        const std::size_t end = std::min(read.text.find(',', at), read.text.size());
        flags().applied.push_back(read.text.substr(at, end - at));
        at = end + 1;
    }
    for (const std::string& name : read.unknown) {
        std::printf("unknown experimental flag \"%s\"\n", name.c_str());
    }
    TEST_CHECK(read.unknown.empty());
    if (!flags().text.empty()) {
        std::printf("experimental flags: %s\n", flags().text.c_str());
    }
}

// params with the flags read applied: every world of the goldens is built with them.
[[nodiscard]] inline terrain::HeightfieldParams
reliefParams(terrain::HeightfieldParams params = {}) {
    terrain::WorldParams world;
    world.relief = params;
    static_cast<void>(terrain::applyExperimentalFlags(flags().text, world));
    return world.relief;
}

[[nodiscard]] inline terrain::HydrologyParams waterParams(terrain::HydrologyParams params = {}) {
    terrain::WorldParams world;
    world.water = params;
    static_cast<void>(terrain::applyExperimentalFlags(flags().text, world));
    return world.water;
}

// Whether the test runs with the flag name.
[[nodiscard]] inline bool flagOn(std::string_view name) {
    const std::vector<std::string>& on = flags().applied;
    return std::find(on.begin(), on.end(), name) != on.end();
}

// The value of main, or that with the flag name when the test runs with it.
[[nodiscard]] inline std::uint64_t pick(std::string_view name, std::uint64_t main,
                                        std::uint64_t withFlag) {
    return flagOn(name) ? withFlag : main;
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
