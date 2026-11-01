#include "coast_lattice.hpp"

#include "coast_reefs.hpp"

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
// on the large lattice; the calanques and the coves (more in strata) on the small one.
constexpr double kFjordRate = 0.9;
constexpr double kRiaRate = 0.8;
constexpr double kCalanqueRate = 0.9;
constexpr double kCoveRate = 0.6;
// A reef (coast_reefs) off kCapeReef of the capes between two coves.
constexpr double kCapeReef = 0.3;
// The coves only where the faces of the shore rise kCoveTop meters at least; the coves of a
// group kCoveSpacing apart (see addCoves), the second and third kCoveSecond and kCoveThird of the
// time, kCoveLeast meters inland at least, their roots and the reefs off their capes within
// kNearSlack meters of the cell.
constexpr double kCoveTop = 8.0;
constexpr double kCoveSpacingMost = 650.0;
constexpr double kCoveSecond = 0.7;
constexpr double kCoveThird = 0.4;
constexpr double kCoveLeast = 60.0;
constexpr double kNearSlack = 100.0;
constexpr double kCapeLeast = 30.0;
// A fjord between massifs (betweenMassifs): the land read kFjordSide meters beyond its water,
// kFjordMassif meters high somewhere on either side, kFjordBank meters at every place read. Where
// the candidate of a cell gives none, others kFjordAlong meters along the shore where the fjords
// weigh kFjordWeight at least.
constexpr std::array<double, 3> kFjordSide{1'200.0, 2'000.0, 3'000.0};
constexpr double kFjordMassif = 700.0;
constexpr double kFjordBank = 300.0;
constexpr std::array<double, 4> kFjordAlong{2'000.0, -2'000.0, 4'000.0, -4'000.0};
constexpr float kFjordWeight = 0.3f;

double between(double low, double high, double t) {
    return low + (high - low) * t;
}

bool inCell(double x, double z, const std::array<double, 4>& b, double slack = kRootSlack) {
    return x > b[0] - slack && x < b[0] + b[2] + slack && z > b[1] - slack &&
           z < b[1] + b[3] + slack;
}

// The index of the mouth of an inlet: its first point on the land of the geography.
std::size_t mouthOf(const World& w, const Inlet& inlet) {
    std::size_t k = 0;
    while (k + 1 < inlet.axis.size() && w.coast(inlet.axis[k].x, inlet.axis[k].z) < 0.0) {
        ++k;
    }
    return k;
}

// A fjord lies between massifs: read 1.2 to 3 km beyond its water (World::height, kFjordSide
// meters beyond its shore) at a third, two thirds and the head of the fjord, the land rises
// kFjordMassif meters at least somewhere on either side, and kFjordBank meters at least on either
// side at each of those places. No fjord along a coastal plain, nor with low land or the sea on
// one side.
bool betweenMassifs(const World& w, const Inlet& fjord) {
    const std::size_t mouth = mouthOf(w, fjord);
    const std::size_t last = fjord.axis.size() - 1;
    if (last < mouth + 3) {
        return false;
    }
    std::array<double, 2> massif{-kFjordMassif, -kFjordMassif};
    for (const double at : {1.0 / 3.0, 2.0 / 3.0, 1.0}) {
        std::array<double, 2> highest{-kFjordMassif, -kFjordMassif};
        const auto up = static_cast<std::size_t>(at * static_cast<double>(last - mouth - 2));
        const std::size_t k = std::min(last - 1, mouth + 1 + up);
        const coast_inlets::AxisPoint& a = fjord.axis[k - 1];
        const coast_inlets::AxisPoint& b = fjord.axis[k + 1];
        const double length = dmath::hypot(b.x - a.x, b.z - a.z);
        const double nx = -(b.z - a.z) / length;
        const double nz = (b.x - a.x) / length;
        for (const double far : kFjordSide) {
            const double away = static_cast<double>(fjord.axis[k].half) + far;
            for (std::size_t side = 0; side < 2; ++side) {
                const double sign = side == 0 ? away : -away;
                const double h = w.height(fjord.axis[k].x + sign * nx, fjord.axis[k].z + sign * nz);
                highest[side] = std::max(highest[side], h);
            }
        }
        if (highest[0] < kFjordBank || highest[1] < kFjordBank) {
            return false;
        }
        massif = {std::max(massif[0], highest[0]), std::max(massif[1], highest[1])};
    }
    return massif[0] >= kFjordMassif && massif[1] >= kFjordMassif;
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
        !inCell(x, z, bounds)) {
        return;
    }
    const double turns = inlandOf(w, x, z);
    constexpr std::array<double, kInletKindCount> kLeast{1'000.0, 600.0, 150.0, kCoveLeast};
    Inlet inlet = inletOf(w, lattice, kind, shape, x, z, turns,
                          kLeast[static_cast<std::size_t>(kind)]);
    if (inlet.axis.empty() || (kind == InletKind::Fjord && !betweenMassifs(w, inlet))) {
        return;
    }
    cell.inlets.push_back(std::move(inlet));
    if (kind == InletKind::Ria) {
        const Inlet main = cell.inlets.back();
        addBranches(w, main, shape, turns, d, cell);
    }
}

// The fjord of a cell from its candidate (x, z), else from the first of the places kFjordAlong
// meters along the shore from it that gives one.
void addFjord(const World& w, Draws& d, double x, double z, const std::array<double, 4>& bounds,
              Cell& cell) {
    addInlet(w, Lattice::Large, InletKind::Fjord, d, x, z, bounds, cell);
    const dmath::SineCosine inland = dmath::sinCosTurns(inlandOf(w, x, z));
    for (std::size_t k = 0; k < kFjordAlong.size() && cell.inlets.empty(); ++k) {
        const double cx = x - inland.sine * kFjordAlong[k];
        const double cz = z + inland.cosine * kFjordAlong[k];
        const double coast = w.coast(cx, cz);
        if (coast > -kNearSea && coast < kNearLand &&
            w.site(cx, cz).weights[static_cast<std::size_t>(CoastKind::Fjord)] >= kFjordWeight) {
            addInlet(w, Lattice::Large, InletKind::Fjord, d, cx, cz, bounds, cell);
        }
    }
}

// A reef of a cell off the cape at (x, z), its middle within slack meters of the cell.
void addReef(const World& w, Draws& d, double x, double z, const std::array<double, 4>& bounds,
             double slack, Cell& cell) {
    const auto inside = [&bounds, slack](double px, double pz) {
        return inCell(px, pz, bounds, slack);
    };
    for (const coast_inlets::Stack& rock : coast_reefs::reefOf(w, d, x, z, inside)) {
        cell.stacks.push_back(rock);
    }
}

// Whether the shore rises kCoveTop meters at least at the mouth of a cove (coast_shape::shoreTop).
bool rises(const World& w, const Inlet& cove) {
    const coast_inlets::AxisPoint& m = cove.axis[mouthOf(w, cove)];
    return static_cast<double>(w.site(m.x, m.z).top) >= kCoveTop;
}

// Whether a cove keeps kCapeLeast meters of land at least from every other inlet of the cell, from
// shore to shore along their axes inland of their mouths (a cape between them, never crossing;
// out at sea their flared roots may meet).
bool apart(const World& w, const Inlet& cove, const Cell& cell) {
    for (const Inlet& other : cell.inlets) {
        for (std::size_t i = mouthOf(w, cove); i < cove.axis.size(); ++i) {
            const coast_inlets::AxisPoint& a = cove.axis[i];
            for (std::size_t k = mouthOf(w, other); k < other.axis.size(); ++k) {
                const coast_inlets::AxisPoint& b = other.axis[k];
                const double gap = static_cast<double>(a.half) + static_cast<double>(b.half) +
                                   kCapeLeast;
                if (dmath::hypot(a.x - b.x, a.z - b.z) < gap) {
                    return false;
                }
            }
        }
    }
    return true;
}

// The coves of a cell: the first from its candidate (x, z), then one on either side along the
// shore (kCoveSecond and kCoveThird of the time), kCoveSpacing times the mouth of the first apart
// (at most kCoveSpacingMost meters), where the shore rises kCoveTop meters too; capes between them,
// a reef off kCapeReef of the capes.
void addCoves(const World& w, Draws& d, double x, double z, const std::array<double, 4>& bounds,
              Cell& cell) {
    const std::size_t first = cell.inlets.size();
    addInlet(w, Lattice::Small, InletKind::Cove, d, x, z, bounds, cell);
    if (cell.inlets.size() == first || !rises(w, cell.inlets[first])) {
        cell.inlets.resize(first);
        return;
    }
    const coast_inlets::AxisPoint root = cell.inlets[first].axis.front();
    const dmath::SineCosine inland = dmath::sinCosTurns(inlandOf(w, root.x, root.z));
    const double spacing =
        std::min(kCoveSpacingMost, between(1.8, 2.6, d.next()) *
                                       static_cast<double>(cell.inlets[first].site.opening));
    for (const double side : {1.0, -1.0}) {
        if (d.next() >= (side > 0.0 ? kCoveSecond : kCoveThird)) {
            continue;
        }
        double cx = root.x - inland.sine * spacing * side;
        double cz = root.z + inland.cosine * spacing * side;
        if (!rootOf(w, -kRoot[1], cx, cz) || !inCell(cx, cz, bounds, kNearSlack)) {
            continue;
        }
        const Shape shape = shapeOf(InletKind::Cove, d);
        Inlet cove = inletOf(w, Lattice::Small, InletKind::Cove, shape, cx, cz,
                             inlandOf(w, cx, cz), kCoveLeast);
        if (cove.axis.empty() || !rises(w, cove) || !apart(w, cove, cell)) {
            continue;
        }
        cell.inlets.push_back(std::move(cove));
        if (d.next() < kCapeReef) {
            addReef(w, d, 0.5 * (root.x + cx), 0.5 * (root.z + cz), bounds, kNearSlack, cell);
        }
    }
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
    const double coast = w.coast(x, z);
    if (!(coast > -kNearSea && coast < kNearLand)) {
        return cell; // most cells: one sample of the geography, no kinds read
    }
    const Site site = w.site(x, z);
    const auto weight = [&site](CoastKind k) {
        return static_cast<double>(site.weights[static_cast<std::size_t>(k)]);
    };
    if (lattice == Lattice::Large) {
        const double fjord = weight(CoastKind::Fjord);
        const double ria = weight(CoastKind::Ria);
        if (pick < kFjordRate * fjord + kRiaRate * ria) {
            if (kind * (fjord + ria) < fjord) {
                addFjord(w, d, x, z, bounds, cell);
            } else {
                addInlet(w, lattice, InletKind::Ria, d, x, z, bounds, cell);
            }
        }
        return cell;
    }
    const double calanque = kCalanqueRate * weight(CoastKind::Calanque);
    const double cove = kCoveRate * (weight(CoastKind::Rocky) + weight(CoastKind::Cliff)) *
                        (0.4 + 0.6 * static_cast<double>(site.strata));
    // The calanques as drawn before; the coves kept to shores with relief.
    if (pick < calanque + cove) {
        if (kind * (calanque + cove) < calanque) {
            addInlet(w, lattice, InletKind::Calanque, d, x, z, bounds, cell);
        } else if (static_cast<double>(site.top) >= kCoveTop) {
            addCoves(w, d, x, z, bounds, cell);
        }
    }
    return cell;
}

Lattices::Lattices(coast_inlets::World world)
    : world_(std::move(world)), cut_(coast_inlets::makeCut(world_.seed, world_.size)) {
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
            coast_inlets::addInlet(*inlet, x, z, world_.size, cut_, e);
        }
        for (const coast_inlets::Stack* stack : n.stacks) {
            coast_reefs::addRock(*stack, x, z, world_.size, e);
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

std::vector<CoastStack> Lattices::stacksIn(double minX, double minZ, double maxX,
                                           double maxZ) const {
    std::vector<CoastStack> out;
    const int l = static_cast<int>(Lattice::Small);
    const Grid& g = grids_[static_cast<std::size_t>(l)];
    std::set<std::pair<std::int64_t, std::int64_t>> seen;
    for (std::int64_t j = row(l, minZ) - 1; j <= row(l, maxZ) + 1; ++j) {
        for (std::int64_t i = column(l, minX) - 1; i <= column(l, maxX) + 1; ++i) {
            const std::int64_t ci = wrapIndex(i, g.nx);
            const std::int64_t cj = wrapIndex(j, g.nz);
            if (!seen.insert({ci, cj}).second) {
                continue;
            }
            for (const coast_inlets::Stack& s : cell(l, ci, cj).stacks) {
                if (s.x >= minX && s.x < maxX && s.z >= minZ && s.z < maxZ) {
                    out.push_back({s.x, s.z, s.radius, s.height});
                }
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const CoastStack& a, const CoastStack& b) {
        return std::tie(a.x, a.z, a.radius, a.height) < std::tie(b.x, b.z, b.radius, b.height);
    });
    return out;
}

std::size_t Lattices::cellCount() const {
    return cells_.size();
}

} // namespace terrain::coast_lattice
