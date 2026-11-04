// The volcanoes on the far horizon and across the LODs (agent 45). Their summits show on every level
// of the horizon however coarse, wherever its grid falls (Heightfield::seenAt: the highest top of
// the volcanoes within the cell of a sample); seen from 30 and 100 km through the clipmap of the
// horizon, the silhouette of a cone follows the true one where it is the volcano, its peak
// included; the roughness of their lava gives way to its mean where the footprint of a sample is
// coarse; the summit of a cone stands at the same height in the tiles of LOD 0 to 3.
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/volcanoes.hpp"
#include "terrain/io/horizon_panorama.hpp"
#include "terrain/streaming/horizon_epoch.hpp"
#include "terrain/streaming/horizon_sampler.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "horizon_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <vector>

namespace {

using terrain::Heightfield;
using terrain::VolcanoForm;
using terrain::VolcanoSite;

double degrees(double tangent) {
    return std::atan(tangent) * 180.0 / std::numbers::pi;
}

// The true top of a volcano: the highest ground within 1.3 of its crater (or 300 m) around its
// summit, every 8 m.
struct TrueTop {
    double x = 0.0;
    double z = 0.0;
    double height = -1e30;
};

TrueTop trueTop(const Heightfield& field, const VolcanoSite& v) {
    const terrain::VolcanoOffset top = terrain::volcanoSummit(v);
    const double r = std::max(300.0, 1.3 * static_cast<double>(v.crater));
    TrueTop best;
    for (double dz = -r; dz <= r; dz += 8.0) {
        std::vector<double> xs;
        std::vector<double> zs;
        for (double dx = -r; dx <= r; dx += 8.0) {
            xs.push_back(v.x + top.dx + dx);
            zs.push_back(v.z + top.dz + dz);
        }
        std::vector<float> h(xs.size());
        field.heightsAt(xs, zs, h);
        for (std::size_t i = 0; i < h.size(); ++i) {
            if (h[i] > best.height) {
                best = {xs[i], zs[i], static_cast<double>(h[i])};
            }
        }
    }
    return best;
}

std::vector<const VolcanoSite*> cones(const Heightfield& field, int count) {
    std::vector<const VolcanoSite*> out;
    for (const VolcanoSite& v : field.volcanoes()->sites()) {
        const bool cone = v.form == VolcanoForm::Strato || v.form == VolcanoForm::IslandStrato ||
                          v.form == VolcanoForm::Shield || v.form == VolcanoForm::IslandShield;
        if (cone && static_cast<int>(out.size()) < count) {
            out.push_back(&v);
        }
    }
    return out;
}

// On the levels of 256 m to 4 km (where a cone would shrink between their samples; the finer ones
// draw it, their crests prefiltered), wherever the grid falls, the sample whose cell holds the
// true top shows it (within 2 % of the height of the volcano and 2 m).
void testTops(const Heightfield& field) {
    int checks = 0;
    int missed = 0;
    double worst = 0.0;
    for (const VolcanoSite* v : cones(field, 12)) {
        const TrueTop t = trueTop(field, *v);
        for (double pitch = 256.0; pitch <= 4096.0; pitch *= 2.0) {
            for (const double shift : {0.0, 0.3, 0.7}) {
                // The center of the cell of a grid offset by shift of a pitch that holds the top.
                const double cx = (std::floor(t.x / pitch - shift) + shift + 0.5) * pitch;
                const double cz = (std::floor(t.z / pitch - shift) + shift + 0.5) * pitch;
                const double seen = static_cast<double>(field.seenAt(cx, cz, pitch).height);
                const double below = t.height - seen;
                worst = std::max(worst, below);
                missed += below > 0.02 * static_cast<double>(v->height) + 2.0 ? 1 : 0;
                ++checks;
            }
        }
    }
    std::printf("tops: %d cells holding the summit of a cone (256 m to 4 km, 3 offsets), %d miss "
                "it, %.1f m below it at worst\n",
                checks, missed, worst);
    TEST_CHECK(checks >= 150 && missed == 0);
}

struct Error {
    int columns = 0;
    double sum = 0.0;
    double peak = -90.0;
    double truePeak = -90.0;
};

// The silhouette of a volcano seen from dist meters along azimuth az, over the columns where the
// true silhouette lies on it, through the clipmap of the horizon and over the true ground.
Error silhouetteOf(const terrain::TerrainTiles& tiles, const terrain::HorizonSampler& sampler,
                   const VolcanoSite& v, double dist, double az) {
    const Heightfield& f = tiles.heightfield();
    const terrain::VolcanoOffset top = terrain::volcanoSummit(v);
    const double sx = v.x + top.dx;
    const double sz = v.z + top.dz;
    terrain::PanoramaParams eye;
    eye.eyeX = sx + dist * std::cos(az);
    eye.eyeZ = sz + dist * std::sin(az);
    eye.eyeHeight = std::max(0.0, static_cast<double>(f.heightAt(eye.eyeX, eye.eyeZ))) + 2.0;
    eye.width = 2400; // 0.15 degree per column
    eye.minDistance = 0.3 * dist;
    terrain::HorizonClipmap clipmap(sampler.params());
    horizon_test::settle(clipmap, sampler, eye.eyeX, eye.eyeZ);
    const terrain::GroundHeight levels = terrain::clipmapGround(clipmap);
    const terrain::GroundHeight truth = [&f](double x, double z) -> std::optional<float> {
        return f.heightAt(x, z);
    };
    const double turns = std::atan2(sx - eye.eyeX, -(sz - eye.eyeZ)) / (2.0 * std::numbers::pi);
    const double half =
        std::atan2(static_cast<double>(v.radius), dist) / (2.0 * std::numbers::pi) * eye.width;
    const int first = static_cast<int>(std::floor(turns * eye.width - half));
    const int columns = 2 * static_cast<int>(half) + 1;
    const terrain::Silhouette a =
        terrain::traceSilhouette(levels, eye, first, columns, 1.6 * dist, 256.0);
    const terrain::Silhouette b =
        terrain::traceSilhouette(truth, eye, first, columns, 1.6 * dist, 256.0);
    Error e;
    for (std::size_t c = 0; c < a.tangent.size(); ++c) {
        if (!std::isfinite(a.tangent[c]) || !std::isfinite(b.tangent[c]) ||
            !(std::abs(b.distance[c] - dist) < 0.8 * static_cast<double>(v.radius))) {
            continue;
        }
        ++e.columns;
        e.sum += std::abs(degrees(a.tangent[c]) - degrees(b.tangent[c]));
        e.peak = std::max(e.peak, degrees(a.tangent[c]));
        e.truePeak = std::max(e.truePeak, degrees(b.tangent[c]));
    }
    return e;
}

// Two stratovolcanoes from 30 km, the second a large one from 100 km too, on three sides (where
// another relief hides it, no column).
void testSilhouettes(const terrain::TerrainTiles& tiles) {
    const Heightfield& f = tiles.heightfield();
    const terrain::HorizonSampler sampler(f, tiles.ecology(), horizon_test::smallParams());
    std::vector<const VolcanoSite*> picked;
    for (const VolcanoSite& v : f.volcanoes()->sites()) {
        if (v.form == VolcanoForm::Strato && !v.atSea && picked.size() < 2 &&
            (picked.empty() ? !v.large : v.large)) {
            picked.push_back(&v);
        }
    }
    int views = 0;
    int bad = 0;
    for (const VolcanoSite* v : picked) {
        for (const double dist : {30000.0, 100000.0}) {
            if (!v->large && dist > 50000.0) {
                continue; // under 2.5 km, a strato is a sliver from 100 km
            }
            for (const double az : {0.5, 2.6, 4.4}) {
                const Error e = silhouetteOf(tiles, sampler, *v, dist, az);
                if (e.columns < 5) {
                    continue; // another relief hides it from there
                }
                ++views;
                const double mean = e.sum / e.columns;
                const bool ok = mean < 0.25 && e.peak > e.truePeak - 0.1;
                bad += ok ? 0 : 1;
                std::printf("strato %.0f m high from %3.0f km: %3d columns, %.3f degrees off on "
                            "average, peak %.3f for %.3f%s\n",
                            static_cast<double>(v->height), dist / 1000.0, e.columns, mean, e.peak,
                            e.truePeak, ok ? "" : "  WRONG");
            }
        }
    }
    TEST_CHECK(views >= 4 && bad == 0);
}

// The roughness of a sheet of lava (the mean departure of the ground from the mean of its two
// neighbors 8 m away) at a footprint of 0 and of 128 m: it fades.
void testFootprint(const Heightfield& field) {
    const Heightfield coarse = field.forFootprint(128.0);
    const auto roughness = [](const Heightfield& f, const VolcanoSite& v) {
        double sum = 0.0;
        int n = 0;
        for (int k = 0; k < 400; ++k) {
            const double a = 2.39996323 * k;
            const double r = 0.4 * static_cast<double>(v.radius) * std::sqrt((k + 0.5) / 400.0);
            const double x = v.x + r * std::cos(a);
            const double z = v.z + r * std::sin(a);
            const std::vector<double> xs = {x - 8.0, x, x + 8.0};
            const std::vector<double> zs = {z, z, z};
            std::vector<float> h(3);
            f.heightsAt(xs, zs, h);
            sum += std::abs(static_cast<double>(h[1]) - 0.5 * (h[0] + h[2]));
            ++n;
        }
        return sum / n;
    };
    double fine = 0.0;
    double far = 0.0;
    int fields = 0;
    for (const VolcanoSite& v : field.volcanoes()->sites()) {
        if (v.form == VolcanoForm::LavaField && !v.atSea && fields < 3) {
            ++fields;
            fine += roughness(field, v);
            far += roughness(coarse, v);
        }
    }
    std::printf("lava fields: %.3f m of roughness at points, %.3f m at a footprint of 128 m\n",
                fine / fields, far / fields);
    TEST_CHECK(fields == 3 && far < 0.5 * fine);
}

// The summit of a cone in the tiles of LOD 0 to 3: the highest vertex within 40 m of the true top
// stands at the same height whatever the LOD, and at the true top.
void testLods(const terrain::TerrainTiles& tiles) {
    const Heightfield& f = tiles.heightfield();
    double spread = 0.0;
    double below = 0.0;
    int checked = 0;
    for (const VolcanoSite* v : cones(f, 6)) {
        const TrueTop t = trueTop(f, *v);
        const terrain::ChunkCoord chunk{
            static_cast<std::int32_t>(std::floor(t.x / terrain::kChunkSizeMeters)),
            static_cast<std::int32_t>(std::floor(t.z / terrain::kChunkSizeMeters))};
        double low = 1e30;
        double high = -1e30;
        for (int lod = 0; lod <= 3; ++lod) {
            const terrain::TileData tile = tiles.generate({terrain::tileContaining(chunk, lod), 0});
            double top = -1e30;
            for (const terrain::Vec3f& p : tile.mesh.vertices) {
                const double x = tile.originX + static_cast<double>(p.x);
                const double z = tile.originZ + static_cast<double>(p.z);
                if (std::hypot(x - t.x, z - t.z) < 40.0) {
                    top = std::max(top, tile.originY + static_cast<double>(p.y));
                }
            }
            low = std::min(low, top);
            high = std::max(high, top);
        }
        spread = std::max(spread, high - low);
        below = std::max(below, t.height - low);
        ++checked;
    }
    std::printf("the summits of %d cones in the tiles of LOD 0 to 3: %.2f m apart at most, %.2f m "
                "under the true top at most\n",
                checked, spread, below);
    TEST_CHECK(checked == 6 && spread < 6.0 && below < 8.0);
}

} // namespace

int main() {
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    testTops(tiles.heightfield());
    testSilhouettes(tiles);
    testFootprint(tiles.heightfield());
    testLods(tiles);
    return test::result();
}
