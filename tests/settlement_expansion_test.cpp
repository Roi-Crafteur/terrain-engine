// The expansion and the borders on hand-made grids (generation/settlement/expansion.hpp,
// borders.hpp): the border on a river, on a ridge, the edges along the coast and never the shore,
// the target area and the filling up to the next barrier, a land the people cannot live on never
// taken, a strait of 3 cells crossed but an arm of the sea of 6 never, nothing crossed when the sea
// is forbidden, the sea never held, the cleanup of the artifacts only, the compartments and the
// capital of a minor state in one of its size, the massifs of mountains (climbed up to their crest
// while filling), the minor states along their valleys, the same result twice.

#include "terrain/generation/settlement/borders.hpp"
#include "terrain/generation/settlement/capitals.hpp"
#include "terrain/generation/settlement/expansion.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
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
    t.hostile.assign(cells.size(), 0);
    t.drainage.assign(cells.size(), 0);
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

// One state of that target from the cell (i, j).
st::ExpansionResult alone(const st::ExpansionTerrain& t, const st::SuitabilityMaps& s,
                          double target, int i, int j, double landCap = 1e9) {
    return st::expand(t, s, rule(), plans({target}), {t.grid.index(i, j)}, landCap);
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
    // The edge of a hostile land is natural; inside it, or between two lands merely less liked,
    // it is not.
    t.hostile[slotAt(t, 5, 10)] = 1;
    t.hostile[slotAt(t, 6, 10)] = 1;
    const auto kinds = [&](int i, int j, bool east) {
        return st::edgeKinds(t, slotAt(t, i, j), slotAt(t, east ? i + 1 : i, east ? j : j + 1),
                             east, rule);
    };
    TEST_CHECK(kinds(4, 10, true) == st::kAlongHostile);
    TEST_CHECK(kinds(5, 10, true) == 0);
    TEST_CHECK(kinds(5, 9, false) == st::kAlongHostile);
    std::vector<std::uint16_t> owner(t.mask.size(), 1); // one state up to the shore
    const st::BorderStats b = st::borderStats(t, owner, {}, rule);
    TEST_CHECK(b.edges == 0 && b.landEdges > 0);
}

// Alone on open land, a state stops at overflow times its target; in a ring of rivers enclosing
// a little more than its target, it fills up to the ring and keeps the rivers, nothing beyond.
void testTargetAndBarrier() {
    st::ExpansionTerrain open = grid(60, 60, [](int, int) { return true; });
    const st::SuitabilityMaps s = liked(open);
    const st::ExpansionResult a = alone(open, s, 100, 30, 30);
    TEST_CHECK(a.cells[0] == 130);
    st::ExpansionTerrain ring = grid(60, 60, [](int, int) { return true; });
    for (int j = 24; j <= 36; ++j) {
        for (int i = 24; i <= 36; ++i) {
            if (i == 24 || i == 36 || j == 24 || j == 36) {
                ring.mask[slotAt(ring, i, j)] = 1; // a river around 11 x 11 cells
            }
        }
    }
    const st::ExpansionResult b = alone(ring, s, 100, 30, 30);
    std::printf("ring: %zu cells\n", b.cells[0]);
    TEST_CHECK(b.cells[0] >= 121 && b.cells[0] <= 130);
    TEST_CHECK(ownerAt(ring, b, 30, 37) == 0 && ownerAt(ring, b, 30, 36) == 1);
    // The landCap holds a state below its target.
    const st::ExpansionResult c = alone(open, s, 100, 30, 30, 40.0);
    TEST_CHECK(c.cells[0] == 40);
}

// From 0.7 times its target a state crosses no barrier, and keeps the rivers along its land but
// does not follow them: in a ring of rivers around 7 x 7 cells (49, and the 28 ring cells along
// them, not the 4 corners: 77, at least 70 of a target of 100) it stops on the ring, below its
// target; around 5 x 5 cells (25 + 20) it must cross to reach 70.
st::ExpansionResult inRing(int inner) {
    st::ExpansionTerrain t = grid(60, 60, [](int, int) { return true; });
    const int low = 30 - inner / 2 - 1;
    const int high = 30 + inner / 2 + 1;
    for (int j = low; j <= high; ++j) {
        for (int i = low; i <= high; ++i) {
            if (i == low || i == high || j == low || j == high) {
                t.mask[slotAt(t, i, j)] = 1;
            }
        }
    }
    return alone(t, liked(t), 100, 30, 30);
}

void testBarrierStop() {
    const st::ExpansionTerrain open = grid(60, 60, [](int, int) { return true; });
    const st::ExpansionResult seven = inRing(7);
    std::printf("barrier stop: %zu cells in a ring around 7 x 7\n", seven.cells[0]);
    TEST_CHECK(seven.cells[0] == 77);
    TEST_CHECK(ownerAt(open, seven, 30, 35) == 0 && ownerAt(open, seven, 30, 34) == 1);
    const st::ExpansionResult five = inRing(5);
    TEST_CHECK(five.cells[0] >= 70 && ownerAt(open, five, 30, 34) == 1);
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
    const st::ExpansionResult r = alone(t, s, 600, 13, 10);
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
        grid(60, 20, [&](int i, int) {
            return (i >= 5 && i < 20) || (i >= 20 + width && i < 40 + width);
        });
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

// The compartments: the dry land that is not hostile, cut by the rivers, the ridges, the hostile
// land and the sea.
void testCompartments() {
    st::ExpansionTerrain t = grid(60, 20, strip);
    for (int j = 0; j < 20; ++j) {
        t.mask[slotAt(t, 25, j)] = 1;                  // a river
        t.mask[slotAt(t, 35, j)] |= st::kRidgeEastBit; // a ridge between 35 and 36
        t.hostile[slotAt(t, 10, j)] = 1;               // a strip of desert
    }
    const st::Compartments c = st::computeCompartments(t);
    TEST_CHECK(c.cells.size() == 4);
    const auto size = [&](int i) { return c.cells[c.of(slotAt(t, i, 3))]; };
    TEST_CHECK(size(7) == 5 * 20 && size(15) == 14 * 20 && size(30) == 10 * 20 &&
               size(40) == 9 * 20);
    TEST_CHECK(c.of(slotAt(t, 25, 3)) == st::kNoCompartment &&
               c.of(slotAt(t, 10, 3)) == st::kNoCompartment);
    TEST_CHECK(c.of(slotAt(t, 30, 3)) != c.of(slotAt(t, 40, 3)));
}

// A minor state takes the best seed in a free compartment of its size, not the best seed of all:
// rivers at the columns 25 and 30 cut the strip into compartments of 400, 80 and 280 cells; the
// best seed is in the first, the state aims at 280.
void testCompartmentCapital() {
    st::ExpansionTerrain t = grid(60, 20, strip);
    for (int j = 0; j < 20; ++j) {
        t.mask[slotAt(t, 25, j)] = 1;
        t.mask[slotAt(t, 30, j)] = 1;
    }
    const st::Compartments c = st::computeCompartments(t);
    st::SettlementMaps m;
    m.grid = t.grid;
    m.land = t.land;
    m.habitability = terrain::planet::LandValues<std::uint8_t>(t.land, 100, 0);
    m.waterKm = terrain::planet::LandValues<std::uint8_t>(t.land, 0, 255);
    m.water = terrain::planet::LandValues<std::uint8_t>(t.land, 0, 0);
    st::SuitabilityMaps s = liked(t);
    for (int j = 0; j < 20; ++j) {
        for (int i = 5; i < 45; ++i) {
            m.habitability.at(t.grid.index(i, j)) = j == 9 ? 255 : 100; // the candidates
            const int peak = i < 25 ? 250 - 5 * std::abs(i - 15) : 200 - 5 * std::abs(i - 38);
            s.byPeople[0].at(t.grid.index(i, j)) =
                static_cast<std::uint8_t>(i == 25 || i == 30 ? 10 : (i < 30 && i > 25 ? 50 : peak));
        }
    }
    const std::vector<st::StatePlan> minor = {{0, st::StateRank::Minor, 280.0}};
    const st::CapitalRule rule;
    const st::CapitalChoice plain = st::placeCapitals(m, s, rule, minor, 1);
    const st::CapitalChoice fitted = st::placeCapitals(m, s, rule, minor, 1, {&c, 0.7, 1.3});
    TEST_CHECK(plain.capitals[0].cell == t.grid.index(15, 9) && !plain.capitals[0].compartment);
    TEST_CHECK(fitted.capitals[0].cell == t.grid.index(38, 9) && fitted.capitals[0].compartment);
    // A second minor state of that size finds the compartment taken: the usual rule.
    const std::vector<st::StatePlan> two = {{0, st::StateRank::Minor, 280.0},
                                            {0, st::StateRank::Minor, 280.0}};
    const st::CapitalChoice both = st::placeCapitals(m, s, rule, two, 1, {&c, 0.7, 1.3});
    TEST_CHECK(both.capitals[0].compartment && !both.capitals[1].compartment);
}

// The massifs: a block of mountains of 10 x 10 cells held by two states meeting on a ridge in its
// middle, another held by no one with a state along its foot.
void testMassifs() {
    st::ExpansionTerrain t = grid(60, 20, strip);
    std::vector<std::uint16_t> owner(t.mask.size(), 0);
    for (int j = 5; j < 15; ++j) {
        for (int i = 10; i < 20; ++i) {
            t.mask[slotAt(t, i, j)] |= st::kMountainBit;
            owner[slotAt(t, i, j)] = i < 15 ? 1 : 2;
        }
        t.mask[slotAt(t, 14, j)] |= st::kRidgeEastBit;
        for (int i = 30; i < 40; ++i) {
            t.mask[slotAt(t, i, j)] |= st::kMountainBit;
        }
        owner[slotAt(t, 29, j)] = 3; // along the foot of the second massif
    }
    t.hostile[slotAt(t, 35, 10)] = 1; // a summit of the second massif, hostile
    const st::MassifStats m = st::massifStats(t, owner, st::BorderRule{});
    TEST_CHECK(m.massifs == 2 && m.cells == 199 && m.held == 100);
    TEST_CHECK(m.shared == 1 && m.sharedCells == 100 && m.ridgeMeet == 1);
    TEST_CHECK(m.footEdges == 10 && m.footNatural == 0);
    // Below 25 cells a block is no massif.
    TEST_CHECK(st::massifStats(t, owner, st::BorderRule{}, 101).massifs == 0);
}

// Filling, a state climbs the valleys of a massif up to its crest, never over it, and never onto
// hostile summits: plains of 300 cells (columns 5 to 19), then mountains (20 to 34, cost 10) with a
// crest between 27 and 28; a target of 400 starts the filling on the plains.
void testMountains() {
    st::ExpansionTerrain t = grid(60, 20, strip);
    for (int j = 0; j < 20; ++j) {
        for (int i = 20; i < 35; ++i) {
            t.mask[slotAt(t, i, j)] |= st::kMountainBit;
            t.cost[slotAt(t, i, j)] = 10.0f;
        }
        t.mask[slotAt(t, 27, j)] |= st::kRidgeEastBit;
    }
    const st::ExpansionResult up = alone(t, liked(t), 400, 10, 10);
    bool crest = true;
    for (int j = 0; j < 20; ++j) {
        crest = crest && ownerAt(t, up, 27, j) == 1 && ownerAt(t, up, 28, j) == 0;
    }
    std::printf("mountains: %zu cells up to the crest\n", up.cells[0]);
    TEST_CHECK(crest && up.cells[0] == 300 + 8 * 20);
    for (int j = 0; j < 20; ++j) {
        for (int i = 20; i < 35; ++i) {
            t.hostile[slotAt(t, i, j)] = 1; // the massif hostile: a summit of glaciers
        }
    }
    const st::ExpansionResult summit = alone(t, liked(t), 400, 10, 10);
    TEST_CHECK(summit.cells[0] == 300 && ownerAt(t, summit, 20, 10) == 0);
}

// The extent of a state along x and along z (in columns and rows of the grid).
std::pair<int, int> extent(const st::ExpansionTerrain& t, const st::ExpansionResult& r) {
    int i0 = 1000;
    int i1 = -1;
    int j0 = 1000;
    int j1 = -1;
    for (int j = 0; j < t.grid.nz; ++j) {
        for (int i = 0; i < t.grid.nx; ++i) {
            if (t.land->land(t.grid.index(i, j)) && ownerAt(t, r, i, j) != 0) {
                i0 = std::min(i0, i);
                i1 = std::max(i1, i);
                j0 = std::min(j0, j);
                j1 = std::max(j1, j);
            }
        }
    }
    return {i1 - i0 + 1, j1 - j0 + 1};
}

// A minor state grows along the floor of its valley; a major kingdom does not care: a valley along
// the row 20 of an open land, 16 m higher every row away from it, no river to cross.
void testValleys() {
    st::ExpansionTerrain t = grid(60, 40, strip);
    for (int j = 0; j < 40; ++j) {
        for (int i = 5; i < 45; ++i) {
            t.drainage[slotAt(t, i, j)] = static_cast<std::uint8_t>(2 * std::abs(j - 20));
        }
    }
    const st::SuitabilityMaps s = liked(t);
    const auto grow = [&](st::StateRank rank) {
        return st::expand(t, s, rule(), {{0, rank, 150.0}}, {t.grid.index(25, 20)}, 1e9);
    };
    const auto [majorX, majorZ] = extent(t, grow(st::StateRank::Major));
    const auto [minorX, minorZ] = extent(t, grow(st::StateRank::Minor));
    std::printf("valleys: a major kingdom %d x %d cells, a minor state %d x %d\n", majorX, majorZ,
                minorX, minorZ);
    TEST_CHECK(majorX <= majorZ + 2);
    TEST_CHECK(minorX >= 2 * minorZ);
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
    testBarrierStop();
    testUnlivable();
    testStraits();
    testCleanup();
    testCompartments();
    testCompartmentCapital();
    testMassifs();
    testMountains();
    testValleys();
    testDeterminism();
    return test::result();
}
