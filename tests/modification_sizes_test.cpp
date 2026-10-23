// The criteria of phase 15 on the sizes of the region files (format 2): a hole of 10 m about
// 1 KiB, in the middle of a section or across 8 of them; a mountain of 1 km x 1 km x 500 m razed
// by a box, then by a box and 47,000 explosions on its floor and walls, at most about 1 MiB. The
// edits are geometric: no terrain is needed. terrain_modification_sizes measures the same cases
// on the terrain, with the times, the memory and the comparison with Minecraft.
#include "terrain/storage/region_file.hpp"
#include "terrain/storage/volume_edit.hpp"

#include "test_assert.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::EditMode;
using terrain::EditShape;
using terrain::ModificationStore;
using terrain::VolumeEdit;

const terrain::WorldSize kWorld{};
constexpr std::size_t kKiB = 1024;

VolumeEdit sphere(EditShape shape, BlockCoord center, double radius) {
    return {shape, EditMode::Remove, center, {}, static_cast<std::uint16_t>(radius * 16.0)};
}

// Bytes of every region file of the store.
std::size_t fileBytes(const ModificationStore& store) {
    std::size_t bytes = 0;
    for (const terrain::RegionCoord region : terrain::regionsOf(store)) {
        bytes += terrain::serializeRegion(store, region, 7).size();
    }
    return bytes;
}

std::size_t applied(const std::vector<VolumeEdit>& edits, const char* name) {
    ModificationStore store;
    const auto start = std::chrono::steady_clock::now();
    for (const VolumeEdit& edit : edits) {
        terrain::applyVolumeEdit(store, edit, kWorld);
    }
    const std::chrono::duration<double> applying = std::chrono::steady_clock::now() - start;
    const std::size_t bytes = fileBytes(store);
    std::printf("%s: %zu edits applied in %.2f s, %zu blocks, %zu bytes of region files\n", name,
                edits.size(), applying.count(), store.blockCount(), bytes);
    return bytes;
}

void testHoles() {
    TEST_CHECK(applied({sphere(EditShape::Sphere, {-1240, 136, 3304}, 5.0)},
                       "hole of 10 m in a section") <= kKiB);
    TEST_CHECK(applied({sphere(EditShape::Sphere, {-1248, 144, 3312}, 5.0)},
                       "hole of 10 m on 8 sections") <= kKiB);
}

// The floor and walls of the box dug by explosions of 7 m every 8 m.
std::vector<VolumeEdit> razedByExplosions(BlockCoord corner, int height) {
    const VolumeEdit box{EditShape::Box, EditMode::Remove, corner, {1000, 500, 1000}, 0};
    std::vector<VolumeEdit> edits = {box};
    for (int a = 0; a < 1000; a += 8) {
        for (int b = 0; b < 1000; b += 8) {
            edits.push_back(sphere(EditShape::Explosion, {corner.x + a, corner.y, corner.z + b},
                                   7.0));
        }
        for (int y = corner.y; y < corner.y + height; y += 8) {
            edits.push_back(sphere(EditShape::Explosion, {corner.x, y, corner.z + a}, 7.0));
            edits.push_back(sphere(EditShape::Explosion, {corner.x + 999, y, corner.z + a}, 7.0));
            edits.push_back(sphere(EditShape::Explosion, {corner.x + a, y, corner.z}, 7.0));
            edits.push_back(sphere(EditShape::Explosion, {corner.x + a, y, corner.z + 999}, 7.0));
        }
    }
    return edits;
}

void testRazedMountain() {
    const BlockCoord corner{-1245179, 1290, -8499705};
    const VolumeEdit box{EditShape::Box, EditMode::Remove, corner, {1000, 500, 1000}, 0};
    TEST_CHECK(applied({box}, "mountain razed by a box") <= kKiB * kKiB);
    TEST_CHECK(applied(razedByExplosions(corner, 500), "mountain razed by explosions") <=
               kKiB * kKiB);
}

} // namespace

int main() {
    testHoles();
    testRazedMountain();
    return test::result();
}
