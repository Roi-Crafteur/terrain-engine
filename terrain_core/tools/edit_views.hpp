#pragma once

// Views of the player edits from far away (agent 41): a hole of one block and a wall one block
// thick on the ground, seen from the west by an eye 1.8 m over the ground, through the tiles a
// player there streams (selectTiles), drawn by renderMeshView. A control of the coarse tiles
// (edit_coarse.hpp): how the edit looks at each LOD, how much it changes when its tile switches
// from LOD 0 to 1, how it fades out up to LOD 3. Not a renderer: the check in Unreal stays.

#include "terrain/chunk_coord.hpp"
#include "terrain/io/png.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <tuple>

namespace edit_views {

enum class Edit { Hole, Wall };

// A world with the edit at (x, z) and the same world without it, their tiles kept.
class Scene {
public:
    Scene(Edit edit, int x, int z);

    [[nodiscard]] int x() const { return x_; }
    [[nodiscard]] int z() const { return z_; }
    [[nodiscard]] double groundAt(double x, double z) const;
    [[nodiscard]] const terrain::TerrainTiles& tiles(bool edited) const {
        return edited ? *edited_ : *plain_;
    }
    // A tile of the world with or without the edit, generated once.
    const terrain::TileData& tile(bool edited, const terrain::TileRequest& request);

private:
    int x_;
    int z_;
    std::unique_ptr<terrain::TerrainTiles> plain_;
    std::unique_ptr<terrain::TerrainTiles> edited_;
    std::map<std::tuple<bool, int, int, int, unsigned>, terrain::TileData> tiles_;
};

struct View {
    double distance = 64.0; // meters west of the edit
    double sunSine = 0.5;   // the sine of the elevation of the sun (0.1: a low sun)
    int width = 640;
    int height = 360;
    // The chunk whose player the tiles are selected for; the chunk of the eye by default.
    std::optional<terrain::ChunkCoord> player;
};

// The edit seen from the west, with it or without it.
[[nodiscard]] terrain::Image render(Scene& scene, const View& view, bool edited);

// The pixels where the edit shows: those whose color differs between the views with and without
// it by more than 8 / 255 on a channel.
[[nodiscard]] std::size_t editPixels(const terrain::Image& with, const terrain::Image& without);

// The pixels where the edit shows differently in two pairs of views (with and without it): the
// change of its look between two selections of tiles from the same eye.
[[nodiscard]] std::size_t changedPixels(const terrain::Image& withA, const terrain::Image& withoutA,
                                        const terrain::Image& withB,
                                        const terrain::Image& withoutB);

// How the edit looks in two pairs of views from the same eye (with and without it, A and B: the
// tiles of two LODs): the pixels where it shows in each, those of each within reach pixels of a
// pixel of the other (its outline kept), and the mean luminance of each over them (0 to 255).
struct Switch {
    std::size_t shownA = 0;
    std::size_t shownB = 0;
    std::size_t nearA = 0;
    std::size_t nearB = 0;
    double lumaA = 0.0;
    double lumaB = 0.0;
};
[[nodiscard]] Switch compareSwitch(const terrain::Image& withA, const terrain::Image& withoutA,
                                   const terrain::Image& withB, const terrain::Image& withoutB,
                                   int reach);

// The pixels a meter across spans at the distance of the view (its center).
[[nodiscard]] double pixelsPerMeter(const View& view);

// The LOD of the tile holding the edit for a player at the eye of a view.
[[nodiscard]] int editLod(const Scene& scene, const View& view);

} // namespace edit_views
