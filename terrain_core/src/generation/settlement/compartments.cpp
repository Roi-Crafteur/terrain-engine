#include "terrain/generation/settlement/expansion.hpp"

#include <cstdint>
#include <vector>

namespace terrain::settlement {

namespace {

constexpr int kSides[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

// Whether the edge from cell a to its side neighbor b (offset dx, dz) is a ridge.
bool ridgeBetween(const ExpansionTerrain& t, std::size_t a, std::size_t b, int dx, int dz) {
    const std::size_t holder = dx < 0 || dz < 0 ? b : a; // the cell holding the bit of the edge
    const std::uint8_t bit = dx != 0 ? kRidgeEastBit : kRidgeSouthBit;
    return (t.mask[t.land->slot(holder)] & bit) != 0;
}

bool inside(const ExpansionTerrain& t, std::size_t slot) {
    return (t.mask[slot] & (kRiverBits | kLakeBit)) == 0 && t.hostile[slot] == 0;
}

} // namespace

std::size_t Compartments::memoryBytes() const {
    return (id.capacity() + cells.capacity()) * sizeof(std::uint32_t);
}

Compartments computeCompartments(const ExpansionTerrain& t) {
    Compartments c;
    c.id.assign(t.mask.size(), kNoCompartment);
    const PlanetGrid& g = t.grid;
    std::vector<std::size_t> queue;
    t.land->forEachLand([&](std::size_t start, std::size_t first) {
        if (c.id[first] != kNoCompartment || !inside(t, first)) {
            return;
        }
        const auto label = static_cast<std::uint32_t>(c.cells.size());
        c.id[first] = label;
        queue.assign(1, start);
        for (std::size_t k = 0; k < queue.size(); ++k) {
            const std::size_t a = queue[k];
            for (const auto& side : kSides) {
                const std::size_t b =
                    g.index(g.wrapColumn(g.column(a) + side[0]), g.wrapRow(g.row(a) + side[1]));
                if (!t.land->land(b)) {
                    continue;
                }
                const std::size_t s = t.land->slot(b);
                if (c.id[s] == kNoCompartment && inside(t, s) &&
                    !ridgeBetween(t, a, b, side[0], side[1])) {
                    c.id[s] = label;
                    queue.push_back(b);
                }
            }
        }
        c.cells.push_back(static_cast<std::uint32_t>(queue.size()));
    });
    return c;
}

} // namespace terrain::settlement
