#include "edit_views.hpp"

#include "terrain/det_math.hpp"
#include "terrain/io/mesh_view.hpp"
#include "terrain/streaming/lod.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace edit_views {

namespace {

constexpr double kEye = 1.8;          // meters over the ground
constexpr double kFieldDegrees = 30.0; // 640 pixels: 21 pixels a degree, as 1920 over 90 degrees
constexpr double kSunLeft = 0.3;

int groundBlock(const terrain::TerrainTiles& tiles, int x, int z) {
    return static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z)));
}

// The sky behind the tiles: one color.
terrain::Image sky(int width, int height) {
    terrain::Image image{width, height, 3, {}};
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
    for (std::size_t p = 0; p < image.pixels.size(); p += 3) {
        image.pixels[p] = 170;
        image.pixels[p + 1] = 200;
        image.pixels[p + 2] = 235;
    }
    return image;
}

terrain::LodParams lodFor(double distance) {
    terrain::LodParams lod;
    lod.viewDistance = static_cast<int>(std::ceil(distance / terrain::kChunkSizeMeters)) + 6;
    return lod;
}

terrain::ChunkCoord playerOf(const View& view, double eyeX, double eyeZ) {
    return view.player.value_or(terrain::worldToChunk(eyeX, eyeZ));
}

} // namespace

Scene::Scene(Edit edit, int x, int z)
    : x_(x), z_(z), plain_(std::make_unique<terrain::TerrainTiles>(terrain::kDefaultWorldSeed)),
      edited_(std::make_unique<terrain::TerrainTiles>(terrain::kDefaultWorldSeed)) {
    const int base = groundBlock(*edited_, x, z);
    if (edit == Edit::Hole) {
        // The block the tool breaks: its center half a meter under the ground.
        edited_->setBlock({x, base, z}, terrain::BlockEdit::Removed);
        return;
    }
    // A wall one block thick, 3 high and 8 long, across the view.
    for (int dz = -4; dz < 4; ++dz) {
        const int foot = groundBlock(*edited_, x, z + dz);
        for (int y = foot + 1; y <= foot + 3; ++y) {
            edited_->setBlock({x, y, z + dz}, terrain::BlockEdit::Added);
        }
    }
}

double Scene::groundAt(double x, double z) const {
    return plain_->heightfield().heightAt(x, z);
}

const terrain::TileData& Scene::tile(bool edited, const terrain::TileRequest& request) {
    const auto key = std::tuple{edited, request.key.lod, request.key.origin.x,
                                request.key.origin.z, request.seams};
    auto it = tiles_.find(key);
    if (it == tiles_.end()) {
        it = tiles_.emplace(key, tiles(edited).generate(request)).first;
    }
    return it->second;
}

terrain::Image render(Scene& scene, const View& view, bool edited) {
    terrain::ViewParams p;
    p.eyeX = scene.x() - view.distance;
    p.eyeZ = scene.z() + 0.3;
    p.eyeHeight = scene.groundAt(p.eyeX, p.eyeZ) + kEye;
    p.heading = 0.25; // east
    p.tilt = (scene.groundAt(scene.x(), scene.z()) + 1.0 - p.eyeHeight) / view.distance;
    p.fieldOfView = kFieldDegrees / 360.0;
    p.width = view.width;
    p.height = view.height;
    p.minDistance = 1.0;
    const std::vector<terrain::TileRequest> requests = terrain::tileRequests(
        terrain::selectTiles(playerOf(view, p.eyeX, p.eyeZ), lodFor(view.distance)));
    std::vector<terrain::ViewMesh> meshes;
    meshes.reserve(requests.size());
    for (const terrain::TileRequest& request : requests) {
        const terrain::TileData& t = scene.tile(edited, request);
        meshes.push_back({&t.mesh, t.originX, t.originY, t.originZ, {}, false, {}});
    }
    return terrain::renderMeshView(meshes, sky(view.width, view.height), p, 0,
                                   {kSunLeft, view.sunSine, false});
}

namespace {

// Per pixel, whether the edit shows: the colors with and without it differ by more than 8 / 255
// on a channel.
std::vector<bool> footprint(const terrain::Image& with, const terrain::Image& without) {
    std::vector<bool> shown(with.pixels.size() / 3, false);
    for (std::size_t p = 0; p + 2 < with.pixels.size(); p += 3) {
        bool differs = false;
        for (std::size_t c = 0; c < 3; ++c) {
            differs = differs || std::abs(static_cast<int>(with.pixels[p + c]) -
                                          static_cast<int>(without.pixels[p + c])) > 8;
        }
        shown[p / 3] = differs;
    }
    return shown;
}

double luma(const terrain::Image& image, std::size_t pixel) {
    const std::size_t p = pixel * 3;
    return 0.3 * image.pixels[p] + 0.59 * image.pixels[p + 1] + 0.11 * image.pixels[p + 2];
}

// The pixels of a footprint within reach pixels (square) of a pixel of another.
std::size_t near(const std::vector<bool>& a, const std::vector<bool>& b, int width, int height,
                 int reach) {
    std::size_t count = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!a[static_cast<std::size_t>(y * width + x)]) {
                continue;
            }
            bool found = false;
            for (int v = std::max(0, y - reach); v <= std::min(height - 1, y + reach) && !found;
                 ++v) {
                for (int u = std::max(0, x - reach); u <= std::min(width - 1, x + reach); ++u) {
                    if (b[static_cast<std::size_t>(v * width + u)]) {
                        found = true;
                        break;
                    }
                }
            }
            count += found ? 1 : 0;
        }
    }
    return count;
}

} // namespace

std::size_t editPixels(const terrain::Image& with, const terrain::Image& without) {
    const std::vector<bool> shown = footprint(with, without);
    return static_cast<std::size_t>(std::count(shown.begin(), shown.end(), true));
}

std::size_t changedPixels(const terrain::Image& withA, const terrain::Image& withoutA,
                          const terrain::Image& withB, const terrain::Image& withoutB) {
    std::size_t count = 0;
    for (std::size_t p = 0; p + 2 < withA.pixels.size(); p += 3) {
        bool differs = false;
        for (std::size_t c = 0; c < 3; ++c) {
            const int a = static_cast<int>(withA.pixels[p + c]) -
                          static_cast<int>(withoutA.pixels[p + c]);
            const int b = static_cast<int>(withB.pixels[p + c]) -
                          static_cast<int>(withoutB.pixels[p + c]);
            differs = differs || std::abs(a - b) > 8;
        }
        count += differs ? 1 : 0;
    }
    return count;
}

Switch compareSwitch(const terrain::Image& withA, const terrain::Image& withoutA,
                     const terrain::Image& withB, const terrain::Image& withoutB, int reach) {
    const std::vector<bool> a = footprint(withA, withoutA);
    const std::vector<bool> b = footprint(withB, withoutB);
    Switch s;
    double lumaA = 0.0;
    double lumaB = 0.0;
    for (std::size_t p = 0; p < a.size(); ++p) {
        if (a[p]) {
            ++s.shownA;
            lumaA += luma(withA, p);
        }
        if (b[p]) {
            ++s.shownB;
            lumaB += luma(withB, p);
        }
    }
    s.lumaA = s.shownA > 0 ? lumaA / static_cast<double>(s.shownA) : 0.0;
    s.lumaB = s.shownB > 0 ? lumaB / static_cast<double>(s.shownB) : 0.0;
    s.nearA = near(a, b, withA.width, withA.height, reach);
    s.nearB = near(b, a, withA.width, withA.height, reach);
    return s;
}

double pixelsPerMeter(const View& view) {
    const double half = 0.5 * kFieldDegrees / 360.0;
    const double focal =
        0.5 * view.width * terrain::dmath::cosTurns(half) / terrain::dmath::sinTurns(half);
    return focal / view.distance;
}

int editLod(const Scene& scene, const View& view) {
    const double eyeX = scene.x() - view.distance;
    const double eyeZ = scene.z() + 0.3;
    const terrain::ChunkCoord edit = terrain::worldToChunk(scene.x(), scene.z());
    for (const terrain::TileKey& key :
         terrain::selectTiles(playerOf(view, eyeX, eyeZ), lodFor(view.distance))) {
        if (terrain::tileContains(key, edit)) {
            return key.lod;
        }
    }
    return -1;
}

} // namespace edit_views
