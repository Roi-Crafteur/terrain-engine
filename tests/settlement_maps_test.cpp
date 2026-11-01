// The pieces of the analysis maps of the settlement on hand-made inputs: the distances exact on
// the torus, the scores of a cell (arable with its floodplains, food, danger, Prana, Mana,
// habitability normalized by its theoretical maximum), the resources in deposits.

#include "analysis_cell.hpp"
#include "analysis_distance.hpp"
#include "map_influences.hpp"
#include "terrain/generation/planet/planet_grid.hpp"
#include "terrain/generation/settlement/settlement_data.hpp"
#include "terrain/generation/settlement/settlement_maps.hpp"

#include "test_assert.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

namespace st = terrain::settlement;
using st::Criterion;

namespace {

const st::SettlementConfig kConfig = st::loadSettlementData(TERRAIN_SETTLEMENT_DATA).config;

void set(st::CriterionValues& c, Criterion k, double v) {
    c[static_cast<std::size_t>(k)] = v;
}

// Distances in km from one source cell, on a grid of 1 km cells: exact along the axes, the
// diagonals and across the seams (the polar line included), 255 beyond 32 cells.
void testDistances() {
    const terrain::PlanetGrid grid{100, 80, 1000.0};
    std::vector<std::uint32_t> all(grid.cells());
    for (std::size_t c = 0; c < all.size(); ++c) {
        all[c] = static_cast<std::uint32_t>(c);
    }
    const auto land = std::make_shared<const terrain::planet::LandIndex>(
        terrain::planet::LandIndex::ofCells(grid.cells(), all));
    std::vector<std::uint8_t> columns(grid.cells(), 1);
    columns[grid.index(2, 1)] = 0;
    st::columnDistances(grid, columns, 3);
    const auto km = st::landDistances(grid, land, columns, 2);
    TEST_CHECK(km[grid.index(2, 1)] == 0);
    TEST_CHECK(km[grid.index(12, 1)] == 10);
    TEST_CHECK(km[grid.index(5, 5)] == 5);                     // 3-4-5
    TEST_CHECK(km[grid.index(97, 1)] == 5);                    // across the east-west seam
    TEST_CHECK(km[grid.index(2, 77)] == 4);                    // across the polar line
    TEST_CHECK(km[grid.index(12, 11)] == 14);                  // 10 sqrt 2 = 14.1
    TEST_CHECK(km[grid.index(40, 1)] == st::kFarDistance);     // 38 cells away
    TEST_CHECK(km[grid.index(2, 33)] == 32);
    std::vector<std::uint8_t> again(grid.cells(), 1);
    again[grid.index(2, 1)] = 0;
    st::columnDistances(grid, again, 1);
    TEST_CHECK(st::landDistances(grid, land, again, 1).values() == km.values());
}

st::CellInputs temperatePlain() {
    st::CellInputs in;
    set(in.criteria, Criterion::Altitude, 200.0);
    set(in.criteria, Criterion::Temperature, 14.0);
    set(in.criteria, Criterion::Rainfall, 900.0);
    set(in.criteria, Criterion::Humidity, 0.62);
    set(in.criteria, Criterion::WaterDistance, 0.0);
    set(in.criteria, Criterion::CoastDistance, 10.0);
    set(in.criteria, Criterion::Latitude, 30.0);
    in.relief = 0.03;
    in.floorRange = 20.0;
    return in;
}

void testCellScores() {
    st::CellInputs plain = temperatePlain();
    const st::CellScores s = st::scoreCell(kConfig, plain);
    std::printf("temperate plain: roughness %.3f arable %.3f food %.3f danger %.3f prana %.3f "
                "habitability %.3f model %s\n",
                s.roughness, s.arable, s.food, s.danger, s.prana, s.habitability,
                kConfig.terrainModels[static_cast<std::size_t>(s.model)].id.c_str());
    TEST_CHECK(s.roughness < 0.05 && s.arable > 0.95 && s.danger == 0.0);
    TEST_CHECK(s.habitability > 0.6 && s.prana > 0.6);
    // A large river: the floodplain bonus, even in a desert (the Nile), and the river term.
    st::CellInputs desert = temperatePlain();
    set(desert.criteria, Criterion::Rainfall, 120.0);
    set(desert.criteria, Criterion::Humidity, 0.1);
    set(desert.criteria, Criterion::Temperature, 24.0);
    const st::CellScores dry = st::scoreCell(kConfig, desert);
    desert.riverNear = 2;
    const st::CellScores nile = st::scoreCell(kConfig, desert);
    TEST_CHECK(dry.arable == 0.0 && std::abs(nile.arable - 0.2) < 1e-9);
    TEST_CHECK(nile.habitability > dry.habitability);
    TEST_CHECK(kConfig.terrainModels[static_cast<std::size_t>(dry.model)].id == "desert");
    TEST_CHECK(dry.prana < s.prana && dry.danger > 0.2);
    // Frozen high mountains: no field, danger, little Prana.
    st::CellInputs ice = temperatePlain();
    set(ice.criteria, Criterion::Temperature, -25.0);
    set(ice.criteria, Criterion::Altitude, 5500.0);
    ice.relief = 0.9;
    ice.sampleRange = 1500.0;
    ice.floorRange = 2500.0;
    ice.water = st::kFrozenBit;
    const st::CellScores frozen = st::scoreCell(kConfig, ice);
    TEST_CHECK(frozen.arable == 0.0 && frozen.danger > 0.8 && frozen.prana < 0.2);
    TEST_CHECK(frozen.roughness == 1.0);
    TEST_CHECK(frozen.habitability < 0.25);
    // Mana: a node, a line, a dead zone.
    st::CellInputs magic = temperatePlain();
    magic.nodes = 0.9;
    TEST_CHECK(std::abs(st::manaOf(kConfig.mana, magic) - 0.9) < 1e-12);
    magic.lines = 0.5;
    TEST_CHECK(st::manaOf(kConfig.mana, magic) == 1.0);
    magic.deadZones = 1.0;
    TEST_CHECK(std::abs(st::manaOf(kConfig.mana, magic) - 0.4) < 1e-12);
}

// A perfect cell reaches 1: the habitability is divided by its theoretical maximum.
void testHabitabilityMaximum() {
    st::CellInputs in = temperatePlain();
    in.riverNear = 2;
    st::CellScores s;
    s.food = 1.0;
    s.prana = 1.0;
    s.mana = 1.0;
    s.danger = 0.0;
    in.criteria[static_cast<std::size_t>(Criterion::Roughness)] = 0.0;
    TEST_CHECK(std::abs(st::habitabilityOf(kConfig, in, s) - 1.0) < 1e-12);
    TEST_CHECK(std::abs(st::habitabilityMax(kConfig.habitability) - 4.8) < 1e-12);
}

void testRivers() {
    const st::RiverClasses r;
    TEST_CHECK(st::riverClass(5.0, r) == 0 && st::riverClass(10.0, r) == 1);
    TEST_CHECK(st::riverClass(150.0, r) == 2 && st::riverClass(4000.0, r) == 3);
    TEST_CHECK(st::riverClass(20000.0, r) == 4);
}

void testResources() {
    st::CellInputs in = temperatePlain();
    const st::CellScores s = st::scoreCell(kConfig, in);
    st::ResourceDef wood;
    wood.sources = {{st::SourceKind::ForestCover, 1.0}};
    wood.depositThreshold = 0.25;
    wood.depositSoftness = 0.3;
    TEST_CHECK(st::resourceValue(wood, in, s, 0.1) == 0.0);
    TEST_CHECK(std::abs(st::resourceValue(wood, in, s, 0.9) - s.vegetation) < 1e-12);
    st::ResourceDef salt;
    salt.sources = {{st::SourceKind::SaltLake, 1.0}};
    salt.depositThreshold = 0.0;
    TEST_CHECK(st::resourceValue(salt, in, s, 0.5) == 0.0);
    in.saltNear = true;
    TEST_CHECK(st::resourceValue(salt, in, s, 0.5) == 1.0);
}

void testPointField() {
    st::PointField f(1.0e6, 5.0e5, 1.0e4);
    TEST_CHECK(f.at(0.0, 0.0) == 0.0);
    f.add(499'000.0, 0.0, 10'000.0, 0.8); // near the east seam
    TEST_CHECK(std::abs(f.at(499'000.0, 0.0) - 0.8) < 1e-12);
    TEST_CHECK(f.at(-499'000.0, 0.0) > 0.7); // 2 km away across the seam
    TEST_CHECK(f.at(-480'000.0, 0.0) == 0.0);
    TEST_CHECK(st::bump(0.5) == 0.5625 && st::bump(1.0) == 0.0);
}

} // namespace

int main() {
    testDistances();
    testCellScores();
    testHabitabilityMaximum();
    testRivers();
    testResources();
    testPointField();
    return test::result();
}
