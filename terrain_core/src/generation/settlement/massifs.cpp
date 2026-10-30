#include "terrain/generation/settlement/borders.hpp"

#include <cstdint>
#include <vector>

namespace terrain::settlement {

namespace {

constexpr std::uint32_t kNone = 0xFFFFFFFF;
constexpr int kSides[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

bool mountain(const ExpansionTerrain& t, std::size_t slot) {
    return (t.mask[slot] & kMountainBit) != 0 && t.hostile[slot] == 0;
}

// The massif of every land cell (kNone outside the mountains), and the cells of each massif.
std::vector<std::uint32_t> label(const ExpansionTerrain& t, std::vector<std::size_t>& cells) {
    std::vector<std::uint32_t> id(t.mask.size(), kNone);
    const PlanetGrid& g = t.grid;
    std::vector<std::size_t> queue;
    t.land->forEachLand([&](std::size_t start, std::size_t first) {
        if (id[first] != kNone || !mountain(t, first)) {
            return;
        }
        const auto massif = static_cast<std::uint32_t>(cells.size());
        id[first] = massif;
        queue.assign(1, start);
        for (std::size_t k = 0; k < queue.size(); ++k) {
            for (const auto& side : kSides) {
                const std::size_t b = g.index(g.wrapColumn(g.column(queue[k]) + side[0]),
                                              g.wrapRow(g.row(queue[k]) + side[1]));
                if (!t.land->land(b)) {
                    continue;
                }
                const std::size_t s = t.land->slot(b);
                if (id[s] == kNone && mountain(t, s)) {
                    id[s] = massif;
                    queue.push_back(b);
                }
            }
        }
        cells.push_back(queue.size());
    });
    return id;
}

// What each massif holds: its cells held, its first state, a second state, a meeting on a ridge.
struct Holding {
    std::size_t held = 0;
    std::uint16_t first = 0;
    bool shared = false;
    bool ridgeMeet = false;
};

} // namespace

MassifStats massifStats(const ExpansionTerrain& t, const std::vector<std::uint16_t>& owner,
                        const BorderRule& rule, int leastCells) {
    std::vector<std::size_t> cells;
    const std::vector<std::uint32_t> id = label(t, cells);
    const auto big = [&](std::uint32_t m) {
        return m != kNone && cells[m] >= static_cast<std::size_t>(leastCells);
    };
    std::vector<Holding> h(cells.size());
    MassifStats st;
    const PlanetGrid& g = t.grid;
    t.land->forEachLand([&](std::size_t cell, std::size_t a) {
        if (big(id[a]) && owner[a] != 0) {
            Holding& m = h[id[a]];
            ++m.held;
            m.shared = m.shared || (m.first != 0 && m.first != owner[a]);
            m.first = m.first == 0 ? owner[a] : m.first;
        }
        for (const bool east : {true, false}) {
            const std::size_t n = east ? g.index(g.wrapColumn(g.column(cell) + 1), g.row(cell))
                                       : g.index(g.column(cell), g.wrapRow(g.row(cell) + 1));
            if (!t.land->land(n) || owner[a] == owner[t.land->slot(n)]) {
                continue;
            }
            const std::size_t b = t.land->slot(n);
            const bool ridge = (t.mask[a] & (east ? kRidgeEastBit : kRidgeSouthBit)) != 0;
            if (owner[a] != 0 && owner[b] != 0 && ridge) {
                for (const std::uint32_t m : {id[a], id[b]}) {
                    if (big(m)) {
                        h[m].ridgeMeet = true;
                    }
                }
            }
            const std::size_t wild = owner[a] == 0 ? a : b;
            const std::size_t held = owner[a] == 0 ? b : a;
            if ((owner[a] == 0 || owner[b] == 0) && big(id[wild]) &&
                (t.mask[held] & kMountainBit) == 0) {
                ++st.footEdges;
                st.footNatural += edgeKinds(t, a, b, east, rule) != 0 ? 1 : 0;
            }
        }
    });
    for (std::size_t m = 0; m < cells.size(); ++m) {
        if (!big(static_cast<std::uint32_t>(m))) {
            continue;
        }
        ++st.massifs;
        st.cells += cells[m];
        st.held += h[m].held;
        st.shared += h[m].shared ? 1 : 0;
        st.sharedCells += h[m].shared ? cells[m] : 0;
        st.ridgeMeet += h[m].shared && h[m].ridgeMeet ? 1 : 0;
        st.ridgeMeetCells += h[m].shared && h[m].ridgeMeet ? cells[m] : 0;
    }
    return st;
}

} // namespace terrain::settlement
