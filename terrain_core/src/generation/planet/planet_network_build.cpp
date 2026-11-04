// The passes of the network of the planet level (planet_network.hpp): downstream once (the cells in
// an order where every cell comes after the cells draining to it: orders, areas, main donors), then
// upstream once (basins, watercourses, kinds, banks).
#include "terrain/generation/planet/planet_network.hpp"

#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <limits>

namespace terrain {

namespace {

constexpr std::uint32_t kNoDonor = std::numeric_limits<std::uint32_t>::max();

struct NetworkInputs {
    std::uint8_t most = 0;
    std::uint8_t count = 0;
    void add(std::uint8_t order) {
        if (order > most) {
            most = order;
            count = 1;
        } else if (order == most && order > 0 && count < 2) {
            ++count;
        }
    }
    [[nodiscard]] std::uint8_t order() const {
        return static_cast<std::uint8_t>(count >= 2 && most < 255 ? most + 1 : most);
    }
};

// The offset from cell b to its neighbor a, in cells (each in -1, 0, 1), across the seams.
std::array<int, 2> offsetOf(const PlanetGrid& g, std::size_t a, std::size_t b) {
    const auto wrap = [](int d, int n) { return d > n / 2 ? d - n : (d < -n / 2 ? d + n : d); };
    return {wrap(g.column(a) - g.column(b), g.nx), wrap(g.row(a) - g.row(b), g.nz)};
}

// The bank a tributary c joins the watercourse of d on, looking downstream along it (from its
// main donor, or else toward where d drains).
StreamBank bankOf(const PlanetGrid& g, std::size_t c, std::size_t d, std::uint32_t mainDonor,
                  std::int64_t next) {
    std::array<int, 2> along{0, 0};
    if (mainDonor != kNoDonor) {
        const std::array<int, 2> m = offsetOf(g, mainDonor, d);
        along = {-m[0], -m[1]};
    } else if (next >= 0) {
        along = offsetOf(g, static_cast<std::size_t>(next), d);
    }
    const std::array<int, 2> o = offsetOf(g, c, d);
    // Looking along (x, z), x east and z south, the right hand points to (-z, x).
    const int side = o[0] * -along[1] + o[1] * along[0];
    return side >= 0 ? StreamBank::Right : StreamBank::Left;
}

} // namespace

void PlanetNetwork::build() {
    const std::size_t count = land_->landCells();
    const PlanetGrid& grid = level_->grid();
    std::vector<std::uint32_t> pending(count, 0);
    land_->forEachLand([&](std::size_t cell, std::size_t) {
        if (const std::int64_t d = down(cell); d >= 0) {
            ++pending[slot(static_cast<std::size_t>(d))];
        }
    });
    std::vector<NetworkInputs> inputs(count);
    std::vector<std::uint32_t> area(count, 0);
    std::vector<std::uint32_t> best(count, kNoDonor);
    std::vector<std::uint32_t> sequence;
    sequence.reserve(count);
    order_.assign(count, 0);
    std::deque<std::uint32_t> ready;
    land_->forEachLand([&](std::size_t cell, std::size_t s) {
        if (pending[s] == 0) {
            ready.push_back(static_cast<std::uint32_t>(cell));
        }
    });
    while (!ready.empty()) {
        const std::uint32_t c = ready.front();
        ready.pop_front();
        const std::size_t s = slot(c);
        sequence.push_back(c);
        inputs[s].add(seed(c));
        order_[s] = inputs[s].order();
        area[s] += 1;
        const std::int64_t d = down(c);
        if (d < 0) {
            continue;
        }
        const std::size_t ds = slot(static_cast<std::size_t>(d));
        inputs[ds].add(order_[s]);
        area[ds] += area[s];
        if (ownEnd(c) == StreamEnd::Confluence &&
            (best[ds] == kNoDonor || discharge(c) > discharge(best[ds]))) {
            best[ds] = c;
        }
        if (--pending[ds] == 0) {
            ready.push_back(static_cast<std::uint32_t>(d));
        }
    }
    planet::freeVector(pending);
    planet::freeVector(inputs);
    area_.resize(count);
    for (std::size_t s = 0; s < count; ++s) {
        area_[s] = static_cast<std::uint16_t>(std::min<std::uint32_t>(area[s], 65535));
    }
    planet::freeVector(area);
    // Upstream: the basins and the watercourses, from their ends.
    basin_.assign(count, 0);
    flags_.assign(count, 0);
    std::vector<float> endFlow(count, 0.0f);
    std::vector<std::uint8_t> endKind(count, 0);
    for (auto it = sequence.rbegin(); it != sequence.rend(); ++it) {
        const std::uint32_t c = *it;
        const std::size_t s = slot(c);
        const std::int64_t d = down(c);
        const StreamEnd own = ownEnd(c);
        const std::size_t ds = d >= 0 ? slot(static_cast<std::size_t>(d)) : s;
        const bool main = d >= 0 && own == StreamEnd::Confluence && best[ds] == c;
        basin_[s] = d >= 0 ? basin_[ds] : c;
        endFlow[s] = main ? endFlow[ds] : static_cast<float>(discharge(c));
        endKind[s] = main ? endKind[ds] : static_cast<std::uint8_t>(own);
        const auto ending = static_cast<StreamEnd>(endKind[s]);
        StreamKind kind = StreamKind::Brook;
        if (ending == StreamEnd::Sea && endFlow[s] >= params_.fleuveLeast) {
            kind = StreamKind::Fleuve;
        } else if (endFlow[s] >= params_.riverLeast) {
            kind = StreamKind::River;
        }
        StreamBank bank = StreamBank::None;
        if (d >= 0 && !main && own == StreamEnd::Confluence) {
            bank = bankOf(grid, c, static_cast<std::size_t>(d), best[ds],
                          down(static_cast<std::size_t>(d)));
        }
        flags_[s] = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(kind) | (main ? 0x04 : 0x00) |
            (static_cast<std::uint8_t>(bank) << 3) | (static_cast<std::uint8_t>(own) << 5));
    }
}

} // namespace terrain
