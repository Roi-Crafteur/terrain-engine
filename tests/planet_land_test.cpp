// The compact arrays of the planet level (planet_land, phase 11D): the slots of the land cells
// across the words of 64 bits, grids without land or without sea, the values of the land read
// by cell, the values of a few cells.
#include "terrain/generation/planet/planet_land.hpp"

#include "terrain/generation/noise.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include "test_assert.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace {

namespace planet = terrain::planet;
using planet::LandIndex;

std::vector<std::uint8_t> mask(std::size_t cells, std::uint64_t seed) {
    std::vector<std::uint8_t> ocean(cells);
    for (std::size_t c = 0; c < cells; ++c) {
        const std::uint64_t bits = terrain::noise::deriveSeed(seed, c);
        ocean[c] = (bits & 3U) == 0 ? planet::kLand
                                    : ((bits & 4U) != 0 ? planet::kOcean : planet::kForcedOutlet);
    }
    return ocean;
}

void testSlots(std::size_t cells, std::uint64_t seed) {
    const std::vector<std::uint8_t> ocean = mask(cells, seed);
    const LandIndex land(ocean);
    std::size_t slots = 0;
    bool ok = land.cells() == cells;
    for (std::size_t c = 0; c < cells; ++c) {
        const bool isLand = ocean[c] == planet::kLand;
        ok = ok && land.land(c) == isLand && land.slot(c) == slots && land.seaSlot(c) == c - slots;
        slots += isLand ? 1U : 0U;
    }
    TEST_CHECK(ok && land.landCells() == slots);
    std::size_t visited = 0;
    bool inOrder = true;
    std::size_t last = 0;
    land.forEachLand([&](std::size_t cell, std::size_t slot) {
        inOrder = inOrder && slot == visited && ocean[cell] == planet::kLand &&
                  (visited == 0 || cell > last);
        last = cell;
        ++visited;
    });
    TEST_CHECK(inOrder && visited == slots);
}

void testEmpty() {
    const LandIndex sea(std::vector<std::uint8_t>(130, planet::kOcean));
    TEST_CHECK(sea.landCells() == 0 && !sea.land(0) && !sea.land(129) && sea.seaSlot(129) == 129);
    const LandIndex land(std::vector<std::uint8_t>(130, planet::kLand));
    TEST_CHECK(land.landCells() == 130 && land.slot(64) == 64 && land.slot(129) == 129);
    std::size_t count = 0;
    sea.forEachLand([&](std::size_t, std::size_t) { ++count; });
    TEST_CHECK(count == 0);
}

void testValues() {
    const std::vector<std::uint8_t> ocean = mask(200, 3);
    auto index = std::make_shared<const LandIndex>(ocean);
    planet::LandValues<std::int32_t> values(index, 7, -1);
    bool ok = values.values().size() == index->landCells();
    for (std::size_t c = 0; c < ocean.size(); ++c) {
        if (index->land(c)) {
            values.at(c) = static_cast<std::int32_t>(c);
        }
    }
    const std::vector<std::int32_t> all = values.expand();
    for (std::size_t c = 0; c < ocean.size(); ++c) {
        const std::int32_t expected = index->land(c) ? static_cast<std::int32_t>(c) : -1;
        ok = ok && values[c] == expected && all[c] == expected;
    }
    TEST_CHECK(ok);
    values.release();
    TEST_CHECK(values.empty());
}

void testSparse() {
    planet::SparseCells<std::int32_t> sparse(150, -1);
    for (const std::size_t c : {149U, 3U, 64U, 63U, 100U}) {
        sparse.add(c, static_cast<std::int32_t>(2 * c));
    }
    sparse.finish();
    bool ok = sparse.entries().size() == 5 && sparse.entries().front().cell == 3 &&
              sparse.entries().back().cell == 149;
    const std::vector<std::int32_t> all = sparse.expand();
    for (std::size_t c = 0; c < 150; ++c) {
        const bool set = c == 3 || c == 63 || c == 64 || c == 100 || c == 149;
        const std::int32_t expected = set ? static_cast<std::int32_t>(2 * c) : -1;
        ok = ok && sparse.has(c) == set && sparse[c] == expected && all[c] == expected;
    }
    TEST_CHECK(ok && all.size() == 150);
}

} // namespace

int main() {
    for (const std::size_t cells : {1U, 63U, 64U, 65U, 128U, 1000U, 4099U}) {
        testSlots(cells, cells);
    }
    testEmpty();
    testValues();
    testSparse();
    return test::result();
}
