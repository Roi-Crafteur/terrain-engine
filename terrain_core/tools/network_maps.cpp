// The layers of the maps of terrain_export_network: the network of the planet level, its basins
// and the fine network.
#include "network_draw.hpp"

#include "terrain/generation/water/drainage_network.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace network_tool {

namespace {

constexpr Rgb kSea{20, 40, 110};
constexpr Rgb kTerminal{40, 200, 230};
constexpr Rgb kDry{140, 90, 40};
constexpr Rgb kIce{255, 255, 255};
constexpr Rgb kSource{0, 0, 0};
constexpr Rgb kConfluence{255, 255, 255};
constexpr std::uint32_t kShownCells = 16;
constexpr double kShownDischarge = 10.0;
constexpr double kWetLeast = 0.5;
constexpr std::uint32_t kDryShownCells = 64;

std::array<double, 2> centerNear(const terrain::PlanetGrid& g, std::size_t cell, double x,
                                 double z) {
    return {terrain::nearestImage(g.centerX(g.column(cell)), x, g.periodX()),
            terrain::nearestImage(g.centerZ(g.row(cell)), z, g.periodZ())};
}

void endMark(const terrain::PlanetNetwork& net, const Canvas& c, std::size_t cell,
             const std::array<double, 2>& at) {
    switch (net.end(cell)) {
    case terrain::StreamEnd::Sea:
        square(c, at[0], at[1], 2.0, kSea);
        break;
    case terrain::StreamEnd::TerminalLake:
        disc(c, at[0], at[1], 3.0, kTerminal);
        break;
    case terrain::StreamEnd::DryBasin:
        disc(c, at[0], at[1], 3.0, kDry);
        break;
    case terrain::StreamEnd::Lake:
        ring(c, at[0], at[1], 3.0, kTerminal);
        break;
    default:
        break;
    }
}

std::uint64_t basinMix(std::uint64_t v) {
    v ^= v >> 33;
    v *= 0xFF51AFD7ED558CCDULL;
    v ^= v >> 33;
    return v;
}

} // namespace

void planetMap(const terrain::PlanetNetwork& net, const Canvas& c, double half) {
    const terrain::PlanetLevel& level = net.level();
    const terrain::PlanetGrid& g = level.grid();
    const double x = c.x0 + half;
    const double z = c.z0 + half;
    const int reach = static_cast<int>(std::ceil(half / g.cellSize)) + 1;
    const int i0 = static_cast<int>(std::floor((terrain::wrapCoordinate(x, g.periodX()) +
                                                0.5 * g.periodX()) / g.cellSize));
    const int j0 = static_cast<int>(std::floor((terrain::wrapCoordinate(z, g.periodZ()) +
                                                0.5 * g.periodZ()) / g.cellSize));
    for (int dj = -reach; dj <= reach; ++dj) {
        for (int di = -reach; di <= reach; ++di) {
            const std::size_t cell = g.index(g.wrapColumn(i0 + di), g.wrapRow(j0 + dj));
            // The cells under a lake drain to its outlet: the lake is drawn by the relief.
            // Shown: the streams of 0.5 m^3/s at least, the large dry ones (wadis) dashed.
            const bool dry = net.discharge(cell) < kWetLeast;
            if (!net.land(cell) || (level.cells().bits[cell] & terrain::PlanetCells::kLakeBit) != 0 ||
                (net.area(cell) < kShownCells && net.discharge(cell) < kShownDischarge) ||
                (dry && net.area(cell) < kDryShownCells)) {
                continue;
            }
            const std::array<double, 2> a = centerNear(g, cell, x, z);
            const std::int64_t d = net.down(cell);
            if (d < 0) {
                endMark(net, c, cell, a);
                continue;
            }
            if (net.end(cell) == terrain::StreamEnd::Lake) {
                endMark(net, c, cell, a);
                continue;
            }
            const std::array<double, 2> b = centerNear(g, static_cast<std::size_t>(d), x, z);
            line(c, a[0], a[1], b[0], b[1], dry ? 0.5 : 0.8 * halfWidth(net.discharge(cell)),
                 dry ? kDry : orderColor(net.order(cell)), dry);
            if (!level.water(cell).frozen && level.water(static_cast<std::size_t>(d)).frozen) {
                disc(c, b[0], b[1], 2.5, kIce);
            }
        }
    }
}

void basinsMap(const terrain::PlanetNetwork& net, terrain::Image& image, double x0, double z0,
               double spacing) {
    const terrain::PlanetGrid& g = net.level().grid();
    std::vector<std::int64_t> ids(static_cast<std::size_t>(image.width) *
                                  static_cast<std::size_t>(image.height));
    for (int pz = 0; pz < image.height; ++pz) {
        for (int px = 0; px < image.width; ++px) {
            const double x = terrain::wrapCoordinate(x0 + (px + 0.5) * spacing, g.periodX());
            const double z = terrain::wrapCoordinate(z0 + (pz + 0.5) * spacing, g.periodZ());
            const std::size_t cell =
                g.index(g.wrapColumn(static_cast<int>((x + 0.5 * g.periodX()) / g.cellSize)),
                        g.wrapRow(static_cast<int>((z + 0.5 * g.periodZ()) / g.cellSize)));
            ids[static_cast<std::size_t>(pz * image.width + px)] =
                net.land(cell) ? static_cast<std::int64_t>(net.basin(cell)) : -1;
        }
    }
    for (int pz = 0; pz < image.height; ++pz) {
        for (int px = 0; px < image.width; ++px) {
            const std::int64_t id = ids[static_cast<std::size_t>(pz * image.width + px)];
            const auto k = static_cast<std::size_t>((pz * image.width + px) * 3);
            const bool edge =
                (px + 1 < image.width && ids[static_cast<std::size_t>(pz * image.width + px + 1)] != id) ||
                (pz + 1 < image.height && ids[static_cast<std::size_t>((pz + 1) * image.width + px)] != id);
            const std::uint64_t h = basinMix(static_cast<std::uint64_t>(id) + 1);
            for (std::size_t ch = 0; ch < 3; ++ch) {
                const auto tint = static_cast<std::uint8_t>(110 + ((h >> (8 * ch)) & 0x7F));
                image.pixels[k + ch] = id < 0 ? kSea[ch] : (edge ? 0 : tint);
            }
        }
    }
}

void fineMap(const terrain::DrainageNetwork& net, const Canvas& c, double half, bool all,
             std::FILE* report) {
    const double x = c.x0 + half;
    const double z = c.z0 + half;
    const std::vector<terrain::StreamPiece> pieces =
        net.piecesIn(x - half, z - half, x + half, z + half);
    std::array<double, 13> length{};
    std::size_t drawn = 0;
    for (const terrain::StreamPiece& p : pieces) {
        if (!all && !p.trunk && p.order < 3) {
            continue;
        }
        const double l = std::hypot(p.x1 - p.x0, p.z1 - p.z0);
        length[static_cast<std::size_t>(std::min<int>(p.order, 12))] += l;
        // The rills (order 1) thin, the streams by their discharge.
        const double radius = p.order <= 1 && !p.trunk ? 0.45 : halfWidth(p.discharge);
        arc(c, p.x0, p.z0, p.cx, p.cz, p.x1, p.z1, radius, orderColor(p.order), !p.wet);
        ++drawn;
    }
    if (all) {
        for (const terrain::StreamMark& m : net.marksIn(x - half, z - half, x + half, z + half)) {
            if (m.kind == terrain::StreamMarkKind::Source) {
                if (m.wet) { // a spring; the heads of the dry ravines are not marked
                    disc(c, m.x, m.z, 1.5, kSource);
                }
            } else if (m.kind == terrain::StreamMarkKind::Confluence) {
                ring(c, m.x, m.z, 2.0, kConfluence);
            } else {
                square(c, m.x, m.z, 2.0, m.end == terrain::StreamEnd::Sea ? kSea : kTerminal);
            }
        }
    }
    if (report != nullptr) {
        const double area = (2.0 * half) * (2.0 * half) * 1e-6;
        std::fprintf(report, "  %zu pieces drawn; length by order (km):", drawn);
        double total = 0.0;
        for (std::size_t k = 1; k < length.size(); ++k) {
            std::fprintf(report, " %zu: %.1f", k, length[k] * 1e-3);
            total += length[k] * 1e-3;
        }
        std::fprintf(report, "; density %.2f km/km^2\n", total / area);
    }
}

} // namespace network_tool
