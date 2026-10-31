// Walks on the collision of the tiles (phase 12A), the LOD 0 tiles a client takes its collision
// from: a walker finds its floor every kStep meters, never a hole, never a step up or down of more
// than kStepMost.
//   - The K2: from the collision point of the second stop (415 839.2, -11 414 503.0), heading
//     191.3 degrees, over a ground almost flat, under the lip of a cap, to the vertical face about
//     40.6 m ahead: no opening in its tiles, at least kHeadroom of air over the walker, and it
//     meets the face (a rock rising more than kStepMost ahead) after kLeastWalk meters.
//   - The banks of the slots of the falls of the small world: across their corridors every
//     kBankEvery meters along them, on each side, from kBankFrom meters beyond the rim of the slot
//     out to kBankFar meters from its axis, kCliffMargin meters off the walls of the relief (more
//     than kWallSlope meters per meter) and off its rock in three dimensions. Besides, the slope
//     of the triangle under the walker is that of the relief there (the length of its gradient)
//     within kSlopeOff at kSlopeShare of the places at least, every kSlopeEvery meters: the
//     collision follows the relief, it adds no slope of its own. The share of them steeper than
//     45 degrees is reported (Unreal lets a walker slide there: the paths that will cross them
//     are another task).
#include "falls_support.hpp"

#include "terrain/io/mesh_checks.hpp"
#include "terrain/streaming/lod.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace {

constexpr double kStartX = 415'839.2;
constexpr double kStartZ = -11'414'503.0;
constexpr double kHeading = 191.3; // degrees clockwise from the north (-Z)
constexpr double kStep = 0.1;
constexpr double kStepMost = 0.45;
constexpr double kHeadroom = 2.0;
constexpr double kLeastWalk = 38.0;
constexpr double kMostWalk = 43.0;

// A surface of a tile over a point: its height and the slope of its triangle (the length of the
// gradient of its plane).
struct Surface {
    double y = 0.0;
    double slope = 0.0;
};

// The surfaces of tile t over (x, z): the triangles a vertical line crosses, appended to out.
void surfacesOn(const terrain::TileData& t, double x, double z, std::vector<Surface>& out) {
    const terrain::Mesh& m = t.mesh;
    const double px = x - t.originX;
    const double pz = z - t.originZ;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const terrain::Vec3f& a = m.vertices[m.indices[i]];
        const terrain::Vec3f& b = m.vertices[m.indices[i + 1]];
        const terrain::Vec3f& c = m.vertices[m.indices[i + 2]];
        const double d0 = (b.x - a.x) * (pz - a.z) - (b.z - a.z) * (px - a.x);
        const double d1 = (c.x - b.x) * (pz - b.z) - (c.z - b.z) * (px - b.x);
        const double d2 = (a.x - c.x) * (pz - c.z) - (a.z - c.z) * (px - c.x);
        const bool inside =
            (d0 >= 0.0 && d1 >= 0.0 && d2 >= 0.0) || (d0 <= 0.0 && d1 <= 0.0 && d2 <= 0.0);
        const double area = d0 + d1 + d2;
        if (!inside || !(std::abs(area) > 1e-12)) {
            continue;
        }
        // The normal of its plane: (b - a) x (c - a).
        const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        const double nx = uy * vz - uz * vy;
        const double ny = uz * vx - ux * vz;
        const double nz = ux * vy - uy * vx;
        out.push_back({t.originY + (d1 * a.y + d2 * b.y + d0 * c.y) / area,
                       std::abs(ny) > 1e-12 ? std::hypot(nx, nz) / std::abs(ny) : 1e9});
    }
}

std::vector<Surface> surfacesAt(const std::vector<terrain::TileData>& tiles, double x, double z) {
    std::vector<Surface> out;
    for (const terrain::TileData& t : tiles) {
        surfacesOn(t, x, z, out);
    }
    std::sort(out.begin(), out.end(), [](const Surface& a, const Surface& b) { return a.y < b.y; });
    return out;
}

// The floor a walker standing on floor finds over surfaces: the highest of them kStepMost above
// it at most (null when none), and the lowest above that (the air over the walker).
const Surface* floorOf(const std::vector<Surface>& surfaces, double floor, double& above) {
    const Surface* next = nullptr;
    above = 1e9;
    for (const Surface& s : surfaces) {
        if (s.y <= floor + kStepMost) {
            next = next == nullptr || s.y > next->y ? &s : next;
        } else {
            above = std::min(above, s.y);
        }
    }
    return next;
}

void walkK2() {
    const terrain::TerrainTiles tiles(terrain::kDefaultWorldSeed);
    std::vector<terrain::TileData> data;
    std::size_t openEdges = 0;
    for (std::int32_t z = -713'408; z <= -713'403; ++z) {
        for (std::int32_t x = 25'988; x <= 25'990; ++x) {
            data.push_back(tiles.generate({{{x, z}, 0}, 0}));
            openEdges += terrain::checkTileMesh(data.back().mesh,
                                                static_cast<float>(terrain::tileSizeMeters(0)))
                             .openEdges;
        }
    }
    const double heading = kHeading * std::numbers::pi / 180.0;
    const double dx = std::sin(heading);
    const double dz = -std::cos(heading);
    double floor = tiles.groundAt(kStartX, kStartZ);
    double steepest = 0.0;
    double lowestHeadroom = 1e9;
    double walked = 0.0;
    bool faced = false; // stopped by rock rising more than kStepMost ahead
    for (double t = kStep; t <= kMostWalk; t += kStep) {
        const std::vector<Surface> surfaces = surfacesAt(data, kStartX + dx * t, kStartZ + dz * t);
        double above = 0.0;
        const Surface* next = floorOf(surfaces, floor, above);
        if (next == nullptr) {
            faced = above < 1e8;
            break;
        }
        steepest = std::max(steepest, std::abs(next->y - floor));
        lowestHeadroom = std::min(lowestHeadroom, above - next->y);
        floor = next->y;
        walked = t;
    }
    std::printf("the walk to the foot of the K2: %zu open edges in its tiles; %.1f m walked to the "
                "face, steps of %.2f m at most every %.1f m, %.1f m of air over the walker at "
                "least\n",
                openEdges, walked, steepest, kStep, lowestHeadroom);
    TEST_CHECK(openEdges == 0);
    TEST_CHECK(faced && walked >= kLeastWalk && walked < kMostWalk);
    TEST_CHECK(steepest <= kStepMost);
    TEST_CHECK(lowestHeadroom >= kHeadroom);
}

constexpr double kBankEvery = 150.0;
constexpr double kBankFrom = 3.0;
constexpr double kBankFar = 60.0;
constexpr double kSlopeOff = 0.05;
constexpr double kSlopeShare = 0.97;
constexpr double kSlopeEvery = 1.0;
constexpr double kGradient = 0.25;
constexpr double kSlotLeast = 2.0;
constexpr double kBankWalked = 2000.0;
constexpr double kCliffMargin = 1.5;
constexpr double kWallSlope = 2.0;

// The LOD 0 tiles of the small world, each built once.
class Tiles {
public:
    explicit Tiles(const terrain::TerrainTiles& tiles) : tiles_(tiles) {}

    const terrain::TileData& at(double& x, double& z) {
        x = terrain::wrapCoordinate(x, golden::kSmallPlanet.x);
        z = terrain::wrapCoordinate(z, golden::kSmallPlanet.z);
        const terrain::ChunkCoord chunk{
            static_cast<std::int32_t>(std::floor(x / terrain::kChunkSizeMeters)),
            static_cast<std::int32_t>(std::floor(z / terrain::kChunkSizeMeters))};
        const terrain::TileKey key = terrain::tileContaining(chunk, 0);
        const std::pair<std::int32_t, std::int32_t> k{key.origin.x, key.origin.z};
        auto it = built_.find(k);
        if (it == built_.end()) {
            it = built_.emplace(k, tiles_.generate({key, 0})).first;
        }
        return it->second;
    }

private:
    const terrain::TerrainTiles& tiles_;
    std::map<std::pair<std::int32_t, std::int32_t>, terrain::TileData> built_;
};

struct BankWalk {
    std::size_t lines = 0;
    double walked = 0.0;
    std::size_t holes = 0;
    double steepest = 0.0;  // the largest step
    std::size_t places = 0; // where the slopes are compared
    std::size_t off = 0;    // the slope of the triangle off that of the relief by kSlopeOff
    double offMost = 0.0;
    std::size_t steep = 0; // the relief steeper than 45 degrees
};

// The length of the gradient of the relief of tiles at (x, z).
double gradientAt(const terrain::Heightfield& h, double x, double z) {
    const double gx =
        static_cast<double>(h.heightAt(x + kGradient, z) - h.heightAt(x - kGradient, z));
    const double gz =
        static_cast<double>(h.heightAt(x, z + kGradient) - h.heightAt(x, z - kGradient));
    return std::hypot(gx, gz) / (2.0 * kGradient);
}

// A line across a bank: from (x, z) along (dx, dz), a sample every kStep meters.
struct Line {
    double x = 0.0;
    double z = 0.0;
    double dx = 0.0;
    double dz = 0.0;

    [[nodiscard]] double xAt(std::size_t k) const {
        return x + dx * kStep * static_cast<double>(k);
    }
    [[nodiscard]] double zAt(std::size_t k) const {
        return z + dz * kStep * static_cast<double>(k);
    }
};

// The samples of the line on its bank: kCliffMargin meters at least from a wall of the relief
// (rising or coming down more than kWallSlope meters per meter between two samples: a cliff, or
// a slope no walker stands on) and from a column of rock in three dimensions (its faces).
std::vector<char> bankOf(const terrain::Heightfield& relief, const Line& line, std::size_t count) {
    const auto margin = static_cast<std::size_t>(std::lround(kCliffMargin / kStep));
    std::vector<char> rough(count, 0);
    double previous = 0.0;
    for (std::size_t k = 0; k < count; ++k) {
        const double h = static_cast<double>(relief.heightAt(line.xAt(k), line.zAt(k)));
        rough[k] = (k > 0 && std::abs(h - previous) > kWallSlope * kStep) ||
                   relief.rockAt(line.xAt(k), line.zAt(k)).faceCount > 0;
        previous = h;
    }
    std::vector<char> bank(count, 1);
    for (std::size_t k = 0; k < count; ++k) {
        if (rough[k] != 0) {
            const std::size_t from = k > margin ? k - margin : 0;
            std::fill(bank.begin() + static_cast<std::ptrdiff_t>(from),
                      bank.begin() + static_cast<std::ptrdiff_t>(std::min(count, k + margin + 1)),
                      0);
        }
    }
    return bank;
}

// The walk along the samples first to last of the line, all of them on its bank.
void walkStretch(Tiles& tiles, const terrain::Heightfield& relief, const Line& line,
                 std::size_t first, std::size_t last, BankWalk& b) {
    double floor = static_cast<double>(relief.heightAt(line.xAt(first), line.zAt(first)));
    for (std::size_t k = first; k <= last; ++k) {
        double px = line.xAt(k);
        double pz = line.zAt(k);
        std::vector<Surface> surfaces;
        surfacesOn(tiles.at(px, pz), px, pz, surfaces);
        double above = 0.0;
        const Surface* next = k == first ? nullptr : floorOf(surfaces, floor, above);
        if (k == first) {
            // Its first floor: the surface nearest the relief.
            for (const Surface& s : surfaces) {
                next = next == nullptr || std::abs(s.y - floor) < std::abs(next->y - floor) ? &s
                                                                                            : next;
            }
        }
        if (next == nullptr) {
            b.holes += surfaces.empty() ? 1 : 0;
            b.steepest = std::max(b.steepest, surfaces.empty() ? 0.0 : surfaces.back().y - floor);
            return;
        }
        b.steepest = k > first ? std::max(b.steepest, std::abs(next->y - floor)) : b.steepest;
        floor = next->y;
        b.walked += k > first ? kStep : 0.0;
        if (k % static_cast<std::size_t>(std::lround(kSlopeEvery / kStep)) == 0) {
            const double gradient = gradientAt(relief, px, pz);
            const double off = std::abs(next->slope - gradient);
            ++b.places;
            b.off += off > kSlopeOff ? 1 : 0;
            b.offMost = std::max(b.offMost, off);
            b.steep += gradient > 1.0 ? 1 : 0;
        }
    }
}

// The walks along the line over length meters, on each stretch of its bank.
void walkBank(Tiles& tiles, const terrain::Heightfield& relief, const Line& line, double length,
              BankWalk& b) {
    ++b.lines;
    const auto count = static_cast<std::size_t>(length / kStep) + 1;
    const std::vector<char> bank = bankOf(relief, line, count);
    for (std::size_t k = 0; k < count; ++k) {
        if (bank[k] == 0) {
            continue;
        }
        std::size_t last = k;
        while (last + 1 < count && bank[last + 1] != 0) {
            ++last;
        }
        walkStretch(tiles, relief, line, k, last, b);
        k = last;
    }
}

void walkBanks() {
    const falls::World w = falls::makeWorld();
    const terrain::TerrainTiles field(terrain::kDefaultWorldSeed, terrain::HeightfieldParams{},
                                      terrain::EcologyParams{}, std::nullopt, golden::kSmallPlanet,
                                      w.hydrology);
    Tiles tiles(field);
    falls::Reaches reaches(*w.hydrology);
    BankWalk b;
    for (const std::size_t r : falls::steepReaches(w, reaches)) {
        const terrain::RiverReach& reach = reaches[r];
        const auto& p = reach.points;
        double next = 0.0;
        double s = 0.0;
        for (std::size_t i = 1; i + 1 < p.size(); ++i) {
            s += std::hypot(p[i].x - p[i - 1].x, p[i].z - p[i - 1].z);
            if (s < next || !(falls::depthAt(reach, i) >= kSlotLeast)) {
                continue;
            }
            next = s + kBankEvery;
            const std::array<double, 2> rims = falls::rimsAt(w, reach, i);
            const double length = std::hypot(p[i + 1].x - p[i - 1].x, p[i + 1].z - p[i - 1].z);
            const double ux = -(p[i + 1].z - p[i - 1].z) / length;
            const double uz = (p[i + 1].x - p[i - 1].x) / length;
            for (int side = 0; side < 2; ++side) {
                const double sign = side == 0 ? 1.0 : -1.0;
                const double from = rims[static_cast<std::size_t>(side)] + kBankFrom;
                if (rims[static_cast<std::size_t>(side)] > 0.0 && from < kBankFar) {
                    const Line line{p[i].x + sign * from * ux, p[i].z + sign * from * uz, sign * ux,
                                    sign * uz};
                    walkBank(tiles, field.heightfield(), line, kBankFar - from, b);
                }
            }
        }
    }
    const double offShare =
        static_cast<double>(b.off) / static_cast<double>(std::max<std::size_t>(1, b.places));
    std::printf("the banks of the slots of the falls: %zu walks, %.0f m walked, %zu holes, steps "
                "of %.2f m at most every %.1f m; at %zu places the slope of the collision %.0f%% "
                "of them within %.2f of that of the relief (%.2f at most), the relief steeper than "
                "45 degrees at %.1f%% of them\n",
                b.lines, b.walked, b.holes, b.steepest, kStep, b.places, 100.0 * (1.0 - offShare),
                kSlopeOff, b.offMost,
                100.0 * static_cast<double>(b.steep) /
                    static_cast<double>(std::max<std::size_t>(1, b.places)));
    TEST_CHECK(b.lines >= 40 && b.walked >= kBankWalked);
    TEST_CHECK(b.holes == 0 && b.steepest <= kStepMost);
    TEST_CHECK(1.0 - offShare >= kSlopeShare);
}

} // namespace

int main() {
    walkK2();
    walkBanks();
    return test::result();
}
