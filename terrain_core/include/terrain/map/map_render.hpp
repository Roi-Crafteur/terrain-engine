#pragma once

#include "terrain/map/map_frame.hpp"
#include "terrain/map/map_layers.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace terrain {

// Counters of a sampling (MapSampler::sample), summed over its threads.
struct MapSampleStats {
    std::size_t samples = 0;        // samples of the grid: the pixels and their border
    std::size_t reliefSamples = 0;  // samples of a Heightfield
    std::size_t planetSamples = 0;  // samples of the planet level
    std::size_t hydrologyCalls = 0; // queries of the courses of the Hydrology

    friend bool operator==(const MapSampleStats&, const MapSampleStats&) = default;
};

// The samples of a frame: its pixels and a border of one pixel all around, (width + 2) x
// (height + 2) row by row from the north; pixel (i, j), i in [-1, width] and j in [-1, height],
// at index mapGridIndex(frame, i, j).
[[nodiscard]] inline std::size_t mapGridIndex(const MapFrame& frame, int i, int j) {
    return static_cast<std::size_t>(j + 1) * static_cast<std::size_t>(frame.width() + 2) +
           static_cast<std::size_t>(i + 1);
}
[[nodiscard]] inline std::size_t mapGridSize(const MapFrame& frame) {
    return static_cast<std::size_t>(frame.width() + 2) * static_cast<std::size_t>(frame.height() + 2);
}

// Where a map reads the world.
class MapSampler {
public:
    virtual ~MapSampler() = default;
    // Fills out (mapGridSize(frame) samples) with the samples of the pixels of frame and of its
    // border, on up to threads threads. Each sample depends on its pixel center (frame.pixelX,
    // pixelZ) and on the spacing of the pixels only: the same in every frame of this spacing that
    // has this center, whatever the number of threads. Throws std::invalid_argument if out is of
    // another size.
    virtual void sample(const MapFrame& frame, std::span<MapSample> out, int threads,
                        MapSampleStats& stats) const = 0;
};

// The cost cap of an image: at most this many samples are read for it (1024 x 1024).
inline constexpr std::size_t kMapMaxSamples = std::size_t{1} << 20;

struct MapRenderParams {
    int threads = 1; // 0: the hardware concurrency
    // Above this many samples (pixels and border), the image is sampled every stride pixels
    // along each axis and each sample covers a stride x stride block (mapStride). At least 9.
    std::size_t maxSamples = kMapMaxSamples;
};

// An image of the map: 4 bytes per pixel (r, g, b, a), row by row from the north.
struct MapImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    int stride = 1; // pixels per sample along each axis
    MapSampleStats stats;

    [[nodiscard]] MapColor at(int i, int j) const;
};

// The smallest stride s at which (ceil(width / s) + 2) (ceil(height / s) + 2) samples fit in
// maxSamples. Throws std::invalid_argument for maxSamples below 9.
[[nodiscard]] int mapStride(int width, int height, std::size_t maxSamples);
// The frame of the samples of frame at a stride: the same box on ceil(width / stride) x
// ceil(height / stride) pixels (frame itself at stride 1).
[[nodiscard]] MapFrame strideFrame(const MapFrame& frame, int stride);

// The image of frame: the samples of sampler, then over each pixel the enabled layers in the
// order of layers. A pixel no layer covers stays transparent black. The same bytes whatever the
// number of threads. Throws std::invalid_argument for a name of enabled not in layers.
[[nodiscard]] MapImage renderMap(const MapSampler& sampler, const MapFrame& frame,
                                 const MapLayers& layers, const std::vector<std::string>& enabled,
                                 const MapRenderParams& params = {});

} // namespace terrain
