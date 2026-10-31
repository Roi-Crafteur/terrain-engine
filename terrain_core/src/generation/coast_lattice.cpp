#include "coast_lattice.hpp"

#include "terrain/det_math.hpp"
#include "terrain/world_size.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>

namespace terrain::coast_lattice {

namespace {

using coast_inlets::Draws;
using coast_inlets::Inlet;
using coast_inlets::inlandOf;
using coast_inlets::inletOf;
using coast_inlets::kSignalPerMeter;
using coast_inlets::Lattice;
using coast_inlets::rootOf;
using coast_inlets::Shape;
using coast_inlets::shapeOf;
using coast_inlets::Site;
using coast_inlets::World;

// The candidate of a cell is drawn kEdge to 1 - kEdge of the way across it, and kept within
// kNearSea meters of coast signal at sea to kNearLand inland.
constexpr double kEdge = 0.15;
constexpr double kNearSea = 4'000.0;
constexpr double kNearLand = 6'000.0;
// The root of an inlet lies kRoot meters of signal out at sea (by lattice), within kRootSlack
// meters of its cell.
constexpr std::array<double, 2> kRoot{1'200.0, 300.0};
constexpr double kRootSlack = 500.0;
// How often a cell holds an inlet where its kind of coast weighs fully: the fjords and the rias
// on the large lattice; the calanques, the coves (more in strata) and else the stacks on the
// small one.
constexpr double kFjordRate = 0.9;
constexpr double kRiaRate = 0.8;
constexpr double kCalanqueRate = 0.9;
constexpr double kCoveRate = 0.6;
constexpr double kStackRate = 0.35;
// The stacks: kStackLow to kStackHigh meters of radius, a dome kStackDome of it high, kStackGap
// meters of sea at least between them and the shore of the geography.
constexpr double kStackLow = 12.0;
constexpr double kStackHigh = 45.0;
constexpr double kStackDome = 0.3;
constexpr double kStackGap = 120.0;

double between(double low, double high, double t) {
    return low + (high - low) * t;
}

bool inCell(double x, double z, double x0, double z0, double width, double height) {
    return x > x0 - kRootSlack && x < x0 + width + kRootSlack && z > z0 - kRootSlack &&
           z < z0 + height + kRootSlack;
}

// The side branches of a ria from its main arm: one to three, from 30 to 70 % of the way up it,
// 40 to 70 degrees off it on either side, 30 to 50 % of its length, 70 % of its width there.
void addBranches(const World& w, const Inlet& main, const Shape& shape, double turns, Draws& d,
                 Cell& cell) {
    const int count = 1 + static_cast<int>(3.0 * d.next());
    std::size_t mouth = 0;
    while (mouth < main.axis.size() && w.coast(main.axis[mouth].x, main.axis[mouth].z) < 0.0) {
        ++mouth;
    }
    for (int b = 0; b < count; ++b) {
        const double at = between(0.3, 0.7, d.next());
        const double off = between(0.11, 0.19, d.next()) * (b % 2 == 0 ? 1.0 : -1.0);
        const double length = between(0.3, 0.5, d.next());
        const auto k = mouth + static_cast<std::size_t>(
                                   at * static_cast<double>(main.axis.size() - 1 - mouth));
        if (mouth >= main.axis.size() || k >= main.axis.size()) {
            return;
        }
        Shape branch = shape;
        branch.half = 0.7 * static_cast<double>(main.axis[k].half);
        branch.depth = 0.8 * static_cast<double>(main.axis[k].depth);
        branch.inland = length * shape.inland;
        branch.phase = d.next();
        Inlet inlet = inletOf(w, Lattice::Large, InletKind::Ria, branch, main.axis[k].x,
                              main.axis[k].z, turns + off, 200.0);
        if (!inlet.axis.empty() && branch.half > 20.0) {
            inlet.site.harbor = false;
            cell.inlets.push_back(std::move(inlet));
        }
    }
}

// The inlet of a cell from its candidate (x, z): its root out at sea, then up the coast signal.
void addInlet(const World& w, Lattice lattice, InletKind kind, Draws& d, double x, double z,
              const std::array<double, 4>& bounds, Cell& cell) {
    const Shape shape = shapeOf(kind, d);
    if (!rootOf(w, -kRoot[static_cast<std::size_t>(lattice)], x, z) ||
        !inCell(x, z, bounds[0], bounds[1], bounds[2], bounds[3])) {
        return;
    }
    const double turns = inlandOf(w, x, z);
    constexpr std::array<double, kInletKindCount> kLeast{1'000.0, 600.0, 150.0, 60.0};
    Inlet inlet = inletOf(w, lattice, kind, shape, x, z, turns,
                          kLeast[static_cast<std::size_t>(kind)]);
    if (inlet.axis.empty()) {
        return;
    }
    cell.inlets.push_back(std::move(inlet));
    if (kind == InletKind::Ria) {
        const Inlet main = cell.inlets.back();
        addBranches(w, main, shape, turns, d, cell);
    }
}

// A stack of a cell from its candidate (x, z), out at sea off the shore.
void addStack(const World& w, Draws& d, double x, double z, const std::array<double, 4>& bounds,
              Cell& cell) {
    const double radius = between(kStackLow, kStackHigh, d.next());
    if (!rootOf(w, -kSignalPerMeter * (radius + kStackGap), x, z) ||
        !inCell(x, z, bounds[0], bounds[1], bounds[2], bounds[3])) {
        return;
    }
    cell.stacks.push_back({wrapCoordinate(x, w.size.x), wrapCoordinate(z, w.size.z),
                           static_cast<float>(radius), static_cast<float>(kStackDome * radius)});
}

std::uint64_t keyOf(int lattice, std::int64_t i, std::int64_t j) {
    return (static_cast<std::uint64_t>(lattice) << 62) |
           (static_cast<std::uint64_t>(i & 0x7FFFFFFF) << 31) |
           static_cast<std::uint64_t>(j & 0x7FFFFFFF);
}

// A total order of the sites: by x, then z, then everything else (a branch of a ria may share its
// site with the arm it leaves).
bool siteBefore(const CoastSite& a, const CoastSite& b) {
    return std::tie(a.x, a.z, a.inlet, a.length, a.opening, a.size, a.depth, a.shelter, a.coast) <
           std::tie(b.x, b.z, b.inlet, b.length, b.opening, b.size, b.depth, b.shelter, b.coast);
}

std::int64_t wrapIndex(std::int64_t i, std::int64_t n) {
    return ((i % n) + n) % n;
}

} // namespace

Cell cellOf(const World& w, Lattice lattice, std::int64_t i, std::int64_t j, double x0, double z0,
            double width, double height) {
    Cell cell;
    Draws d(w, lattice, i, j);
    const double x = x0 + width * between(kEdge, 1.0 - kEdge, d.next());
    const double z = z0 + height * between(kEdge, 1.0 - kEdge, d.next());
    const double pick = d.next();
    const double kind = d.next();
    const std::array<double, 4> bounds{x0, z0, width, height};
    const Site site = w.site(x, z);
    if (!(site.coast > -kNearSea && site.coast < kNearLand)) {
        return cell;
    }
    const auto weight = [&site](CoastKind k) {
        return static_cast<double>(site.weights[static_cast<std::size_t>(k)]);
    };
    if (lattice == Lattice::Large) {
        const double fjord = weight(CoastKind::Fjord);
        const double ria = weight(CoastKind::Ria);
        if (pick < kFjordRate * fjord + kRiaRate * ria) {
            addInlet(w, lattice, kind * (fjord + ria) < fjord ? InletKind::Fjord : InletKind::Ria,
                     d, x, z, bounds, cell);
        }
        return cell;
    }
    const double calanque = kCalanqueRate * weight(CoastKind::Calanque);
    const double cove = kCoveRate * (weight(CoastKind::Rocky) + weight(CoastKind::Cliff)) *
                        (0.4 + 0.6 * static_cast<double>(site.strata));
    if (pick < calanque + cove) {
        addInlet(w, lattice, kind * (calanque + cove) < calanque ? InletKind::Calanque
                                                                  : InletKind::Cove,
                 d, x, z, bounds, cell);
    } else if (pick < calanque + cove + kStackRate * (weight(CoastKind::Rocky) +
                                                      weight(CoastKind::Cliff) +
                                                      weight(CoastKind::Calanque))) {
        addStack(w, d, x, z, bounds, cell);
    }
    return cell;
}

Lattices::Lattices(coast_inlets::World world) : world_(std::move(world)) {
    for (std::size_t l = 0; l < grids_.size(); ++l) {
        Grid& g = grids_[l];
        g.nx = std::max<std::int64_t>(1, static_cast<std::int64_t>(world_.size.x / kCellMeters[l]));
        g.nz = std::max<std::int64_t>(1, static_cast<std::int64_t>(world_.size.z / kCellMeters[l]));
        g.width = world_.size.x / static_cast<double>(g.nx);
        g.height = world_.size.z / static_cast<double>(g.nz);
    }
}

std::int64_t Lattices::column(int lattice, double x) const {
    const Grid& g = grids_[static_cast<std::size_t>(lattice)];
    const auto i = static_cast<std::int64_t>(std::floor((x + 0.5 * world_.size.x) / g.width));
    return std::clamp<std::int64_t>(i, 0, g.nx - 1);
}

std::int64_t Lattices::row(int lattice, double z) const {
    const Grid& g = grids_[static_cast<std::size_t>(lattice)];
    const auto j = static_cast<std::int64_t>(std::floor((z + 0.5 * world_.size.z) / g.height));
    return std::clamp<std::int64_t>(j, 0, g.nz - 1);
}

const Cell& Lattices::cell(int lattice, std::int64_t i, std::int64_t j) const {
    return cells_.get(keyOf(lattice, i, j), [&] {
        const Grid& g = grids_[static_cast<std::size_t>(lattice)];
        return cellOf(world_, static_cast<Lattice>(lattice), i, j,
                      -0.5 * world_.size.x + static_cast<double>(i) * g.width,
                      -0.5 * world_.size.z + static_cast<double>(j) * g.height, g.width,
                      g.height);
    });
}

const Lattices::Near& Lattices::near(int lattice, std::int64_t i, std::int64_t j) const {
    return nears_.get(keyOf(lattice, i, j), [&] {
        const Grid& g = grids_[static_cast<std::size_t>(lattice)];
        Near n;
        std::set<std::pair<std::int64_t, std::int64_t>> seen;
        for (std::int64_t dj = -1; dj <= 1; ++dj) {
            for (std::int64_t di = -1; di <= 1; ++di) {
                const std::int64_t ci = wrapIndex(i + di, g.nx);
                const std::int64_t cj = wrapIndex(j + dj, g.nz);
                if (!seen.insert({ci, cj}).second) {
                    continue;
                }
                const Cell& c = cell(lattice, ci, cj);
                for (const Inlet& inlet : c.inlets) {
                    n.inlets.push_back(&inlet);
                }
                for (const coast_inlets::Stack& stack : c.stacks) {
                    n.stacks.push_back(&stack);
                }
            }
        }
        return n;
    });
}

coast_inlets::Effect Lattices::effectAt(double x, double z) const {
    coast_inlets::Effect e;
    for (int l = 0; l < 2; ++l) {
        const Near& n = near(l, column(l, x), row(l, z));
        for (const Inlet* inlet : n.inlets) {
            coast_inlets::addInlet(*inlet, x, z, world_.size, e);
        }
        for (const coast_inlets::Stack* stack : n.stacks) {
            coast_inlets::addStack(*stack, x, z, world_.size, e);
        }
    }
    return e;
}

std::vector<CoastSite> Lattices::sitesIn(double minX, double minZ, double maxX,
                                         double maxZ) const {
    std::vector<CoastSite> out;
    for (int l = 0; l < 2; ++l) {
        const Grid& g = grids_[static_cast<std::size_t>(l)];
        std::set<std::pair<std::int64_t, std::int64_t>> seen;
        const std::int64_t i0 = column(l, minX) - 1;
        const std::int64_t i1 = column(l, maxX) + 1;
        const std::int64_t j0 = row(l, minZ) - 1;
        const std::int64_t j1 = row(l, maxZ) + 1;
        for (std::int64_t j = j0; j <= j1; ++j) {
            for (std::int64_t i = i0; i <= i1; ++i) {
                const std::int64_t ci = wrapIndex(i, g.nx);
                const std::int64_t cj = wrapIndex(j, g.nz);
                if (!seen.insert({ci, cj}).second) {
                    continue;
                }
                for (const Inlet& inlet : cell(l, ci, cj).inlets) {
                    const CoastSite& s = inlet.site;
                    if (s.x >= minX && s.x < maxX && s.z >= minZ && s.z < maxZ) {
                        out.push_back(s);
                    }
                }
            }
        }
    }
    std::sort(out.begin(), out.end(), siteBefore);
    return out;
}

std::size_t Lattices::cellCount() const {
    return cells_.size();
}

} // namespace terrain::coast_lattice
