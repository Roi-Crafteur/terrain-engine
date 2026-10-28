#pragma once

// The textures of the GPU horizon as Unreal holds them, for the tests of the edits: the texels of
// the samples and the detail normals of every level, by slot, written by the updates of a
// HorizonGpuStreamer (the detail normals created pending, as the renderer creates them).

#include "horizon_gpu_streamer.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace horizon_test {

struct TextureMirror {
    std::map<int, std::vector<float>> texels;
    std::map<int, std::vector<std::uint16_t>> normals;
    int side = 129; // samples per side of a window (K + 1)
    int ratio = 4;  // detail texels per sample along each axis

    void apply(const terrain::unreal::HorizonGpuUpdate& update) {
        using terrain::unreal::kNormalHalves;
        using terrain::unreal::kTexelFloats;
        for (const terrain::unreal::TexelRegion& r : update.regions) {
            std::vector<float>& t = texels[r.level];
            t.resize(static_cast<std::size_t>(side * side * kTexelFloats));
            for (int row = 0; row < r.height; ++row) {
                for (int c = 0; c < r.width * kTexelFloats; ++c) {
                    t[static_cast<std::size_t>(((r.z + row) * side + r.x) * kTexelFloats + c)] =
                        r.data[static_cast<std::size_t>(row * r.width * kTexelFloats + c)];
                }
            }
        }
        const int texture = side * ratio;
        for (const terrain::unreal::NormalRegion& r : update.normalRegions) {
            std::vector<std::uint16_t>& t = normals[r.level];
            t.resize(static_cast<std::size_t>(texture * texture * kNormalHalves),
                     terrain::unreal::kNormalPendingHalf);
            for (int row = 0; row < r.height; ++row) {
                for (int c = 0; c < r.width * kNormalHalves; ++c) {
                    t[static_cast<std::size_t>(((r.z + row) * texture + r.x) * kNormalHalves + c)] =
                        r.data[static_cast<std::size_t>(row * r.width * kNormalHalves + c)];
                }
            }
        }
    }

    // The two halves of the detail texel (tx, tz) of a level, wrapped as the slots.
    [[nodiscard]] const std::uint16_t* normalAt(int level, int tx, int tz) const {
        const int texture = side * ratio;
        const int x = ((tx % texture) + texture) % texture;
        const int z = ((tz % texture) + texture) % texture;
        return &normals.at(
            level)[static_cast<std::size_t>((z * texture + x) * terrain::unreal::kNormalHalves)];
    }
};

} // namespace horizon_test
