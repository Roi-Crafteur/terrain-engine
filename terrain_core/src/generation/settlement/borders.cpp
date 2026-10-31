#include "terrain/generation/settlement/borders.hpp"

#include "terrain/generation/planet/planet_level.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

namespace terrain::settlement {

namespace {

constexpr int kSides[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
// A limit between two rows needs this many pairs of land cells to be read for a cut.
constexpr std::size_t kLeastPairs = 16;

std::size_t sideOf(const PlanetGrid& g, std::size_t cell, int k) {
    return g.index(g.wrapColumn(g.column(cell) + kSides[k][0]),
                   g.wrapRow(g.row(cell) + kSides[k][1]));
}

// The enclaves of at most holeCells cells (connected cells of one owner, a state or the wild, but
// no capital) whose land neighbors all belong to one other owner, given to it: the holes inside a
// state and the crumbs of a state inside another or in the wild. Read from owner alone.
std::vector<std::uint16_t> absorbEnclaves(const ExpansionTerrain& t,
                                          const std::vector<std::uint16_t>& owner,
                                          const std::vector<std::uint8_t>& capital,
                                          int holeCells) {
    std::vector<std::uint16_t> out = owner;
    std::vector<std::uint8_t> seen(owner.size(), 0);
    std::vector<std::size_t> part;
    t.land->forEachLand([&](std::size_t start, std::size_t first) {
        if (seen[first] != 0) {
            return;
        }
        part.assign(1, start);
        seen[first] = 1;
        std::set<std::uint16_t> around;
        bool keep = capital[first] != 0;
        for (std::size_t k = 0; k < part.size(); ++k) {
            for (int side = 0; side < 4; ++side) {
                const std::size_t n = sideOf(t.grid, part[k], side);
                if (!t.land->land(n)) {
                    continue;
                }
                const std::size_t s = t.land->slot(n);
                if (owner[s] != owner[first]) {
                    around.insert(owner[s]);
                } else if (seen[s] == 0) {
                    seen[s] = 1;
                    keep = keep || capital[s] != 0;
                    part.push_back(n);
                }
            }
        }
        if (!keep && static_cast<int>(part.size()) <= holeCells && around.size() == 1) {
            for (const std::size_t c : part) {
                out[t.land->slot(c)] = *around.begin();
            }
        }
    });
    return out;
}

// The cells of one state reaching into another owner's land by one cell, given to it.
std::vector<std::uint16_t> trimSpikes(const ExpansionTerrain& t,
                                      const std::vector<std::uint16_t>& owner,
                                      const std::vector<std::uint8_t>& capital) {
    std::vector<std::uint16_t> out = owner;
    t.land->forEachLand([&](std::size_t cell, std::size_t slot) {
        if (owner[slot] == 0 || capital[slot] != 0) {
            return;
        }
        std::uint16_t around[4];
        int landSides = 0;
        int own = 0;
        for (int side = 0; side < 4; ++side) {
            const std::size_t n = sideOf(t.grid, cell, side);
            if (t.land->land(n)) {
                around[landSides] = owner[t.land->slot(n)];
                own += around[landSides] == owner[slot] ? 1 : 0;
                ++landSides;
            }
        }
        if (landSides < 3 || own > 1) {
            return;
        }
        for (int k = 0; k < landSides; ++k) {
            const auto count = std::count(around, around + landSides, around[k]);
            if (around[k] != owner[slot] && count >= 3) {
                out[slot] = around[k];
                return;
            }
        }
    });
    return out;
}

} // namespace

std::vector<std::uint16_t> cleanOwners(const ExpansionTerrain& t,
                                       const std::vector<std::uint16_t>& owner,
                                       const std::vector<std::size_t>& capitals,
                                       const BorderRule& rule) {
    std::vector<std::uint8_t> capital(owner.size(), 0);
    for (const std::size_t c : capitals) {
        capital[t.land->slot(c)] = 1;
    }
    return trimSpikes(t, absorbEnclaves(t, owner, capital, rule.holeCells), capital);
}

std::vector<ClimateCut> climateCuts(const PlanetLevel& level, const planet::LandIndex& land,
                                    const BorderRule& rule) {
    const PlanetGrid& g = level.grid();
    std::vector<double> jump(static_cast<std::size_t>(std::max(g.nz - 1, 0)), -1.0);
    std::vector<double> read;
    for (int j = 0; j + 1 < g.nz; ++j) {
        double sum = 0.0;
        std::size_t pairs = 0;
        for (int i = 0; i < g.nx; ++i) {
            const std::size_t a = g.index(i, j);
            const std::size_t b = g.index(i, j + 1);
            if (land.land(a) && land.land(b)) {
                sum += std::abs(level.precipitation(a) - level.precipitation(b));
                ++pairs;
            }
        }
        if (pairs >= kLeastPairs) {
            jump[static_cast<std::size_t>(j)] = sum / static_cast<double>(pairs);
            read.push_back(jump[static_cast<std::size_t>(j)]);
        }
    }
    std::vector<ClimateCut> cuts;
    if (read.empty()) {
        return cuts;
    }
    std::nth_element(read.begin(), read.begin() + static_cast<long>(read.size() / 2), read.end());
    const double median = read[read.size() / 2];
    for (int j = 0; j + 1 < g.nz; ++j) {
        const double m = jump[static_cast<std::size_t>(j)];
        if (m >= rule.cutMinMm && m > rule.cutFactor * median) {
            const double z = -0.5 * g.periodZ() + (j + 1) * g.cellSize;
            cuts.push_back({j, -180.0 * z / g.periodZ(), m});
        }
    }
    return cuts;
}

std::uint8_t edgeKinds(const ExpansionTerrain& t, std::size_t a, std::size_t b, bool east,
                       const BorderRule& rule) {
    std::uint8_t kinds = 0;
    const auto wet = [&](std::size_t s) { return (t.mask[s] & (kRiverBits | kLakeBit)) != 0; };
    if (wet(a) || wet(b)) {
        kinds |= kAlongRiver;
    }
    if ((t.mask[a] & (east ? kRidgeEastBit : kRidgeSouthBit)) != 0) {
        kinds |= kAlongRidge;
    }
    const std::uint8_t ca = t.coastKm[a];
    const std::uint8_t cb = t.coastKm[b];
    if (std::min(ca, cb) <= rule.coastKm && ca != cb) {
        kinds |= kAlongCoast;
    }
    if (t.hostile[a] != t.hostile[b]) {
        kinds |= kAlongHostile;
    }
    return kinds;
}

double BorderStats::naturalShare() const {
    return edges > 0 ? static_cast<double>(natural) / static_cast<double>(edges) : 0.0;
}

double BorderStats::naturalShareBetweenStates() const {
    return betweenStates > 0
               ? static_cast<double>(naturalBetweenStates) / static_cast<double>(betweenStates)
               : 0.0;
}

BorderStats borderStats(const ExpansionTerrain& t, const std::vector<std::uint16_t>& owner,
                        const std::vector<ClimateCut>& cuts, const BorderRule& rule) {
    const PlanetGrid& g = t.grid;
    std::vector<std::uint8_t> nearRow(static_cast<std::size_t>(g.nz), 0);
    std::vector<std::uint8_t> cutBelow(static_cast<std::size_t>(g.nz), 0);
    for (const ClimateCut& c : cuts) {
        nearRow[static_cast<std::size_t>(c.row)] = 1;
        nearRow[static_cast<std::size_t>(g.wrapRow(c.row + 1))] = 1;
        cutBelow[static_cast<std::size_t>(c.row)] = 1;
    }
    BorderStats b;
    t.land->forEachLand([&](std::size_t cell, std::size_t a) {
        const int j = g.row(cell);
        for (const bool east : {true, false}) {
            const std::size_t n = sideOf(g, cell, east ? 0 : 2);
            if (!t.land->land(n)) {
                continue;
            }
            const std::size_t s = t.land->slot(n);
            const bool near = nearRow[static_cast<std::size_t>(j)] != 0 ||
                              nearRow[static_cast<std::size_t>(g.row(n))] != 0;
            const std::uint8_t kinds = edgeKinds(t, a, s, east, rule);
            ++b.landEdges;
            b.landEdgesNearCuts += near ? 1 : 0;
            b.landEdgesNatural += kinds != 0 ? 1 : 0;
            if (owner[a] == owner[s]) {
                continue;
            }
            const bool states = owner[a] != 0 && owner[s] != 0;
            ++b.edges;
            b.natural += kinds != 0 ? 1 : 0;
            const std::uint8_t first = kinds & static_cast<std::uint8_t>(-kinds); // lowest bit
            b.river += first == kAlongRiver ? 1 : 0;
            b.ridge += first == kAlongRidge ? 1 : 0;
            b.coast += first == kAlongCoast ? 1 : 0;
            b.hostile += first == kAlongHostile ? 1 : 0;
            b.betweenStates += states ? 1 : 0;
            b.naturalBetweenStates += states && kinds != 0 ? 1 : 0;
            b.nearCuts += near ? 1 : 0;
            b.alongCuts += !east && cutBelow[static_cast<std::size_t>(j)] != 0 ? 1 : 0;
        }
    });
    return b;
}

} // namespace terrain::settlement
