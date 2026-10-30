// The expansion and the borders on hand-made grids (generation/settlement/expansion.hpp,
// borders.hpp): the border on a river, on a ridge, the edges along the coast and never the shore,
// the target area and the filling up to the next barrier, a land the people cannot live on never
// taken, a strait of 3 cells crossed but an arm of the sea of 6 never, nothing crossed when the sea
// is forbidden, the sea never held, the cleanup of the artifacts only, the same result twice.

#include "terrain/generation/settlement/borders.hpp"
#include "terrain/generation/settlement/expansion.hpp"

#include "test_assert.hpp"

#include <cstdio>
#include <memory>
#include <vector>

namespace st = terrain::settlement;

namespace {

// A grid of nx x nz cells of 8 km, the cells where land(i, j) holds being the land: every land cell
// costs 1, no river, no ridge, far from the sea unless near; the straits at most 2 cells from the
// land.
template <class Land>
st::ExpansionTerrain grid(int nx, int nz, Land land) {
    st::ExpansionTerrain t;
    t.grid = terrain::PlanetGrid{nx, nz, 8192.0};
    std::vector<std::uint32_t> cells;
    std::vector<std::uint32_t> straits;
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            bool near = false;
            for (int dj = -2; dj <= 2; ++dj) {
                for (int di = -2; di <= 2; ++di) {
                    near = near || (di * di + dj * dj <= 4 &&
                                    land(t.grid.wrapColumn(i + di), t.grid.wrapRow(j + dj)));
                }
            }
            const auto cell = static_cast<std::uint32_t>(t.grid.index(i, j));
            if (land(i, j)) {
                cells.push_back(cell);
            } else if (near) {
                straits.push_back(cell);
            }
        }
    }
    t.land = std::make_shared<const terrain::planet::LandIndex>(
        terrain::planet::LandIndex::ofCells(t.grid.cells(), cells));
    t.straits = std::make_shared<const terrain::planet::LandIndex>(
        terrain::planet::LandIndex::ofCells(t.grid.cells(), straits));
    t.cost.assign(cells.size(), 1.0f);
    t.mask.assign(cells.size(), 0);
    t.ford.assign(cells.size(), 0);
    t.coastKm.assign(cells.size(), 255);
    return t;
}

std::size_t slotAt(const st::ExpansionTerrain& t, int i, int j) {
    return t.land->slot(t.grid.index(i, j));
}

// Every land cell liked fully by one people.
st::SuitabilityMaps liked(const st::ExpansionTerrain& t) {
    st::SuitabilityMaps s;
    s.byPeople.emplace_back(t.land, std::uint8_t{255}, std::uint8_t{0});
    return s;
}

st::ExpansionRule rule() {
    st::ExpansionRule r;
    r.preferenceWeight = 0.0;
    return r;
}

std::vector<st::StatePlan> plans(std::initializer_list<double> targets) {
    std::vector<st::StatePlan> out;
    for (const double t : targets) {
        out.push_back({0, st::StateRank::Major, t});
    }
    return out;
}

std::uint16_t ownerAt(const st::ExpansionTerrain& t, const st::ExpansionResult& r, int i, int j) {
    return r.owner[slotAt(t, i, j)];
}

// The grids wrap: land between the columns 5 and 44 of 60, the sea around too wide to cross.
bool strip(int i, int) {
    return i >= 5 && i < 45;
}

// Two states from (13, 10) and (36, 10) meet on the river of column 25.
void testRiverBorder() {
    st::ExpansionTerrain t = grid(60, 20, strip);
    for (int j = 0; j < 20; ++j) {
        t.mask[slotAt(t, 25, j)] = 2; // a large river
    }
    const st::SuitabilityMaps s = liked(t);
    const st::ExpansionResult r = st::expand(t, s, rule(), plans({500, 500}),
                                             {t.grid.index(13, 10), t.grid.index(36, 10)}, 1e9);
    bool sides = true;
    for (int j = 0; j < 20; ++j) {
        for (int i = 5; i < 45; ++i) {
            sides = sides && (i == 25 || ownerAt(t, r, i, j) == (i < 25 ? 1 : 2));
        }
    }
    TEST_CHECK(sides);
    const st::BorderStats b = st::borderStats(t, r.owner, {}, st::BorderRule{});
    std::printf("river: %zu border edges, natural %.2f\n", b.edges, b.naturalShare());
    TEST_CHECK(b.edges > 0 && b.naturalShare() == 1.0 && b.river == b.edges);
}

// Without the ridge the two states would meet at column 23; the ridge between the columns 24 and
// 25 takes the border.
void testRidgeBorder() {
    st::ExpansionTerrain t = grid(60, 20, strip);
    for (int j = 0; j < 20; ++j) {
        t.mask[slotAt(t, 24, j)] |= st::kRidgeEastBit;
    }
    const st::SuitabilityMaps s = liked(t);
    const st::ExpansionResult r = st::expand(t, s, rule(), plans({500, 500}),
                                             {t.grid.index(13, 10), t.grid.index(33, 10)}, 1e9);
    bool onRidge = true;
    for (int j = 0; j < 20; ++j) {
        onRidge = onRidge && ownerAt(t, r, 24, j) == 1 && ownerAt(t, r, 25, j) == 2;
    }
    TEST_CHECK(onRidge);
    const st::BorderStats b = st::borderStats(t, r.owner, {}, st::BorderRule{});
    TEST_CHECK(b.betweenStates > 0 && b.naturalBetweenStates == b.betweenStates &&
               b.ridge == b.edges);
}

// An edge parallel to the shore follows the coast; an edge running to the shore does not; the
// shore itself is never a border.
void testCoastEdges() {
    st::ExpansionTerrain t = grid(20, 20, [](int i, int) { return i < 15; });
    for (int j = 0; j < 20; ++j) {
        for (int i = 0; i < 15; ++i) {
            t.coastKm[slotAt(t, i, j)] = static_cast<std::uint8_t>(8 * (15 - i));
        }
    }
    const st::BorderRule rule;
    TEST_CHECK(st::edgeKinds(t, slotAt(t, 13, 5), slotAt(t, 14, 5), true, rule) == st::kAlongCoast);
    TEST_CHECK(st::edgeKinds(t, slotAt(t, 14, 5), slotAt(t, 14, 6), false, rule) == 0);
    TEST_CHECK(st::edgeKinds(t, slotAt(t, 5, 5), slotAt(t, 6, 5), true, rule) == 0);
    std::vector<std::uint16_t> owner(t.mask.size(), 1); // one state up to the shore
    const st::BorderStats b = st::borderStats(t, owner, {}, rule);
    TEST_CHECK(b.edges == 0 && b.landEdges > 0);
}

// Alone on open land, a state stops at overflow times its target; in a ring of rivers enclosing
// a little more than its target, it fills up to the ring and keeps the rivers, nothing beyond.
void testTargetAndBarrier() {
    st::ExpansionTerrain open = grid(60, 60, [](int, int) { return true; });
    const st::SuitabilityMaps s = liked(open);
    const st::ExpansionResult a = st::expand(open, s, rule(), plans({100}), {open.grid.index(30, 30)}, 1e9);
    TEST_CHECK(a.cells[0] == 130);
    st::ExpansionTerrain ring = grid(60, 60, [](int, int) { return true; });
    for (int j = 24; j <= 36; ++j) {
        for (int i = 24; i <= 36; ++i) {
            if (i == 24 || i == 36 || j == 24 || j == 36) {
                ring.mask[slotAt(ring, i, j)] = 1; // a river around 11 x 11 cells
            }
        }
    }
    const st::ExpansionResult b = st::expand(ring, s, rule(), plans({100}), {ring.grid.index(30, 30)}, 1e9);
    std::printf("ring: %zu cells\n", b.cells[0]);
    TEST_CHECK(b.cells[0] >= 121 && b.cells[0] <= 130);
    TEST_CHECK(ownerAt(ring, b, 30, 37) == 0 && ownerAt(ring, b, 30, 36) == 1);
    // The landCap holds a state below its target.
    const st::ExpansionResult c = st::expand(open, s, rule(), plans({100}), {open.grid.index(30, 30)}, 40.0);
    TEST_CHECK(c.cells[0] == 40);
}

// A land the people cannot live on (below minSuitability) is never taken, even on the way.
void testUnlivable() {
    st::ExpansionTerrain t = grid(60, 20, strip);
    st::SuitabilityMaps s = liked(t);
    for (int j = 0; j < 20; ++j) {
        for (int i = 23; i < 27; ++i) {
            s.byPeople[0].values()[slotAt(t, i, j)] = 30; // about 0.12
        }
    }
    const st::ExpansionResult r = st::expand(t, s, rule(), plans({600}), {t.grid.index(13, 10)}, 1e9);
    bool beyond = false;
    for (int j = 0; j < 20; ++j) {
        for (int i = 23; i < 45; ++i) {
            beyond = beyond || ownerAt(t, r, i, j) != 0;
        }
    }
    TEST_CHECK(!beyond && r.cells[0] == 18 * 20);
}

// Two islands across a channel of width sea cells: the second is reached only over a strait.
std::uint16_t across(int width, st::SeaCrossing sea, std::size_t& passages) {
    const st::ExpansionTerrain t =
        grid(60, 20, [&](int i, int) { return (i >= 5 && i < 20) || (i >= 20 + width && i < 40 + width); });
    const st::SuitabilityMaps s = liked(t);
    st::ExpansionRule r = rule();
    r.sea = sea;
    const st::ExpansionTerrain& terrain = t;
    st::ExpansionTerrain closed = t;
    if (sea == st::SeaCrossing::Forbidden) {
        closed.straits = std::make_shared<const terrain::planet::LandIndex>(
            terrain::planet::LandIndex::ofCells(t.grid.cells(), {}));
    }
    const st::ExpansionResult e = st::expand(sea == st::SeaCrossing::Forbidden ? closed : terrain,
                                             s, r, plans({1000}), {t.grid.index(10, 10)}, 1e9);
    passages = 0;
    for (const std::uint16_t p : e.passage) {
        passages += p != 0 ? 1 : 0;
    }
    TEST_CHECK(e.owner.size() == t.land->landCells()); // the sea is never held
    return ownerAt(t, e, 25 + width, 10);
}

void testStraits() {
    std::size_t passages = 0;
    TEST_CHECK(across(3, st::SeaCrossing::Straits, passages) == 1 && passages > 0);
    TEST_CHECK(across(6, st::SeaCrossing::Straits, passages) == 0);
    TEST_CHECK(across(3, st::SeaCrossing::Forbidden, passages) == 0 && passages == 0);
}

// The cleanup takes the crumbs and the holes of a few cells, keeps the large enclaves and the
// capitals.
void testCleanup() {
    const st::ExpansionTerrain t = grid(30, 30, [](int, int) { return true; });
    std::vector<std::uint16_t> owner(t.mask.size(), 1);
    const auto set = [&](int i, int j, std::uint16_t o) { owner[slotAt(t, i, j)] = o; };
    set(5, 5, 2);
    set(6, 5, 2);
    set(5, 6, 2); // a crumb of 3 cells of the state 2
    set(20, 20, 0);
    set(21, 20, 0); // a hole of 2 cells
    for (int j = 10; j < 14; ++j) {
        for (int i = 10; i < 14; ++i) {
            set(i, j, 3); // an enclave of 16 cells
        }
    }
    set(25, 5, 4); // a capital alone
    const std::vector<std::uint16_t> clean =
        st::cleanOwners(t, owner, {t.grid.index(25, 5)}, st::BorderRule{});
    TEST_CHECK(clean[slotAt(t, 5, 5)] == 1 && clean[slotAt(t, 5, 6)] == 1);
    TEST_CHECK(clean[slotAt(t, 20, 20)] == 1 && clean[slotAt(t, 21, 20)] == 1);
    TEST_CHECK(clean[slotAt(t, 11, 11)] == 3 && clean[slotAt(t, 10, 10)] == 3);
    TEST_CHECK(clean[slotAt(t, 25, 5)] == 4);
}

void testDeterminism() {
    st::ExpansionTerrain t = grid(50, 40, [](int i, int j) { return (i * 7 + j * 3) % 23 != 0; });
    for (std::size_t k = 0; k < t.cost.size(); ++k) {
        t.cost[k] = 1.0f + static_cast<float>(k % 5);
    }
    const st::SuitabilityMaps s = liked(t);
    const auto run = [&] {
        return st::expand(t, s, rule(), plans({300, 300, 300}),
                          {t.grid.index(5, 5), t.grid.index(25, 20), t.grid.index(40, 35)}, 1e9);
    };
    const st::ExpansionResult a = run();
    const st::ExpansionResult b = run();
    TEST_CHECK(a.owner == b.owner && a.passage == b.passage && a.cells == b.cells);
}

} // namespace

int main() {
    testRiverBorder();
    testRidgeBorder();
    testCoastEdges();
    testTargetAndBarrier();
    testUnlivable();
    testStraits();
    testCleanup();
    testDeterminism();
    return test::result();
}
