// Golden checksums of the world as the game plays it (agent 42; see golden_support.hpp): the
// default world with its planet level read from the file of the fixture coast_planet
// (TERRAIN_COAST_PLANET), opened as an Unreal level opens it (TerrainSession of the Bridge): its
// rivers and lakes, its seas of dunes, its coasts, its rock in three dimensions. At five places of
// data/dev/places.json (a mountain, a river, a coast, an erg, a plain): every value the game reads
// of the tiles of LOD 0 to 3 holding the place (the mesh, the plants, the blends of the surface,
// the water and its falls, the rock and its bands), and every cell of the 10 levels of the far
// horizon around it (windows of 33 x 33 samples, the crests sought 4 times finer as in Unreal);
// and the checksum of the planet level itself. world_golden_test keeps the relief without water.
//
// Pinned by « Guards: the golden of the played world » (agent 42): every part, created.
// Re-pasted by « Guards: the golden of the played world re-pasted for the texels of #76 »
// (agent 42): the 25 parts of tiles and horizon, not the planet level (see kExpected).
// Re-pasted by « Tests: the golden of the played world re-pasted for the biomes of the land »
// (agent 49): 23 of the 25 parts of tiles and horizon (see kExpected).
#include "terrain_session.hpp"

#include "coast_planet_support.hpp"
#include "golden_support.hpp"
#include "horizon_golden_support.hpp"
#include "horizon_test_support.hpp"

#include "terrain/chunk_coord.hpp"
#include "terrain/streaming/horizon_epoch.hpp"
#include "terrain/streaming/lod.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

using golden::Checksum;

namespace {

struct Place {
    const char* name;
    double x;
    double z;
};

// From data/dev/places.json, copied here: the goldens do not move when a view is moved.
constexpr std::array<Place, 5> kPlaces = {{
    {"k2_paroi", 416102.0, -11414891.0},                   // a mountain: the wall by the K2
    {"fleuve_plaine", -241631.0, -86277.0},                // a river of 304 m in a plain
    {"cote_falaise_encoche", 429778.8, -855913.3},         // a coast: the notched sea cliff
    {"erg_face_33", -997282.0, -3555842.0},                // an erg: a transverse dune
    {"lisiere_tropicale_savane", -26927500.0, -270336.0},  // a plain: the edge of a savanna
}};

// A value of plain words (32 bits each, no padding), word by word: robust to the names of its
// fields.
template <class T>
void addWords(Checksum& sum, const T& value) {
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) % 4 == 0);
    std::array<std::uint32_t, sizeof(T) / 4> words{};
    std::memcpy(words.data(), &value, sizeof(T));
    for (const std::uint32_t word : words) {
        sum.add(static_cast<std::uint64_t>(word), 4);
    }
}

void addFall(Checksum& sum, const terrain::TileFall& f) {
    for (const double v : {f.x, f.z, f.footX, f.footZ}) {
        sum.add(v);
    }
    for (const float v : {f.top, f.bottom, f.width, f.wetFrom, f.wetTo, f.pool}) {
        sum.add(v);
    }
    for (const auto* edge : {&f.edgeX, &f.edgeZ, &f.footEdgeX, &f.footEdgeZ}) {
        for (const double v : *edge) {
            sum.add(v);
        }
    }
    sum.add(static_cast<std::uint64_t>(f.edge), 1);
    sum.add(f.here);
}

// Everything a tile gives the game: what golden::addTile sums, then what Unreal reads besides.
void addPlayedTile(Checksum& sum, const terrain::TileData& data) {
    golden::addTile(sum, data);
    sum.add(static_cast<std::uint64_t>(data.surfaceBlend.size()));
    for (const terrain::BlendTexel& texel : data.surfaceBlend) {
        addWords(sum, texel);
    }
    sum.add(static_cast<std::uint64_t>(data.waterLevels.size()));
    for (const float level : data.waterLevels) {
        sum.add(level);
    }
    sum.add(static_cast<std::uint64_t>(data.waterColumns.size()));
    for (const terrain::WaterColumn& c : data.waterColumns) {
        sum.add(c.level);
        sum.add(static_cast<std::uint64_t>(c.kind), 1);
        sum.add(c.flowX);
        sum.add(c.flowZ);
    }
    sum.add(static_cast<std::uint64_t>(data.falls.size()));
    for (const terrain::TileFall& f : data.falls) {
        addFall(sum, f);
    }
    sum.add(static_cast<std::uint64_t>(data.rock.size()));
    for (const float share : data.rock) {
        sum.add(share);
    }
    sum.add(static_cast<std::uint64_t>(data.rockBands.size()));
    for (const terrain::RockBands& bands : data.rockBands) {
        addWords(sum, bands);
    }
}

std::uint64_t tileChecksum(const terrain::TerrainTiles& tiles, const Place& place, int lod) {
    const terrain::ChunkCoord chunk = terrain::worldToChunk(place.x, place.z);
    const terrain::TileKey key = terrain::tileContaining(chunk, lod);
    const terrain::TileData data = tiles.generate({key, 0});
    if (lod == 0) { // what the tile holds, to read a failure
        std::size_t wet = 0;
        for (const terrain::WaterColumn& c : data.waterColumns) {
            wet += c.kind != terrain::WaterKind::None ? 1U : 0U;
        }
        std::printf("%s, LOD 0: %zu vertices, %zu plants, %zu wet columns, %zu falls, "
                    "ground %.1f to %.1f m\n",
                    place.name, data.mesh.vertices.size(), data.plants.size(), wet,
                    data.falls.size(), static_cast<double>(data.minHeight),
                    static_cast<double>(data.maxHeight));
    }
    Checksum sum;
    addPlayedTile(sum, data);
    return sum.value();
}

// Every cell of every level around the place, from the canonical low corner of each window.
std::uint64_t horizonChecksum(const terrain::TerrainTiles& tiles, const Place& place) {
    terrain::HorizonParams params;
    params.cellsPerSide = 32; // small windows, for a fast test
    params.crestRatio = 4;    // ATerrainWorld::HorizonCrestRatio
    const terrain::HorizonSampler sampler(tiles.heightfield(), tiles.ecology(), params);
    terrain::HorizonClipmap clipmap(params);
    horizon_test::settle(clipmap, sampler, place.x, place.z);
    Checksum sum;
    for (int n = 0; n < clipmap.levelCount(); ++n) {
        const terrain::HorizonLevel& level = clipmap.level(n);
        const terrain::HorizonRect window = level.window();
        const terrain::HorizonIndex corner =
            terrain::canonicalSample({window.x0, window.z0}, level.counts());
        sum.add(static_cast<std::uint64_t>(corner.x));
        sum.add(static_cast<std::uint64_t>(corner.z));
        for (int k = 0; k < level.side(); ++k) {
            for (int i = 0; i < level.side(); ++i) {
                const terrain::HorizonCell& cell = level.atWindow(i, k);
                golden::horizon_detail::addCell(sum, cell);
                addWords(sum, cell.blend);
            }
        }
    }
    return sum.value();
}

// The expected values, place by place: the tiles of LOD 0 to 3, then the horizon.
// Re-pasted by « Guards: the golden of the played world re-pasted for the texels of #76 »
// (agent 42), after « Biome format: 16-byte blend texels (64 biomes, 32 materials), tiles read
// the water of the core (agent 49, PR 1 of 2) (#76) » (844d9e9). Parts changed: the 25 of tiles
// and horizon, at the 5 places; "planet level of the default world" unchanged. Cause: the blend
// texels hashed here (TileData::surfaceBlend, HorizonCell::blend) hold 16 bytes instead of 8, and
// near water the tiles read the water of the core (#76). Hashed without those blends, the 25
// parts and the planet level keep their bits: the relief, plants, water, falls and rock do not
// move. No double hash: golden::addTile and addCell hash no blend.
// Re-pasted by « Tests: the golden of the played world re-pasted for the biomes of the land »
// (agent 49), after « Blend texels: the water of the column, a cover kept for agent 44 »
// (5b22f44), « Biome humidity: read the planet level at a warped point » (a6cf93a) and « Biomes
// of the land: 21 biomes, desert soils, beaches and marshes near the sea » (b1da378). Parts
// changed: 23 of the 25 of tiles and horizon, all but "cote_falaise_encoche tiles LOD 0" and
// "LOD 1"; "planet level of the default world" unchanged. Cause: the surface the game reads
// changes with the biomes (21 biomes, the humidity read at a warped point, the soils of the
// deserts and their grass near the rivers, the water kind in the blend texels): the plants, the
// materials and the blend texels of the tiles, the biome, material and blend of the horizon
// cells. Their geometry keeps its bits: a one-off probe hashing apart the meshes of the ground,
// the water and the lava, the water levels and columns, the falls, the rock and its bands, and
// the heights, gradients and flags of the horizon cells gives the same 25 sums as on main
// (38b5a82). Before (main):
//   k2_paroi                 0x2DB95E2DEDCAE65D 0x0F4816CF5197CDA2 0x6ACDA1A4A5034A6C
//                            0x79500B46720FC2CB 0x36A547250DA2D1D8
//   fleuve_plaine            0xF80E51F3F34E433F 0x0A0B36A86130F65E 0x5D75DF6C06DFBAB9
//                            0xDDBD746207611A38 0xAE19942AECE535BF
//   cote_falaise_encoche     (LOD 0 and 1 unchanged) 0xB6FDE4A9BE07A49E 0xD361F9F9C996CFEA
//                            0x3B26264897B43019
//   erg_face_33              0x7438F8B8F93CF313 0x1F258EC35674853F 0x3CFAB32E13A5110B
//                            0x8FC716A896B5B544 0xFC0E29F1009BE681
//   lisiere_tropicale_savane 0xC4E9DF7393A8673C 0xEFFCE5C97AD8C098 0xFFCB979E99585B14
//                            0xC24A186A819D8D8E 0x9701DB828062AD3C
using PlaceSums = std::array<std::uint64_t, terrain::kLodCount + 1>;
constexpr std::array<PlaceSums, kPlaces.size()> kExpected = {{
    {0x6225081DBEF25EB3ULL, 0xA61A05E3D168EF7CULL, 0x882EAA49BEB557A6ULL,
     0x43CA7EB3E4D4E7C9ULL, 0xF4A12F1814949302ULL}, // k2_paroi
    {0x7D11E6D65ECCDFA2ULL, 0xDC52776574BF1917ULL, 0x521B0764F1AE48BAULL,
     0x5A334D00BBE8F6E1ULL, 0x6B64747479D46DFDULL}, // fleuve_plaine
    {0xCEE2647425926AF4ULL, 0xF2CA8FE728FF95DFULL, 0xDB8EC4F6DE7E7D9EULL,
     0x1645C747032572DAULL, 0xDB9789A6D62C7B7BULL}, // cote_falaise_encoche
    {0xD8C8B7A665EC2C17ULL, 0xCB2ABFA81E27CF9FULL, 0x44B3B6A76FE79965ULL,
     0xF395CF34C9EF9984ULL, 0x3627B898016FB789ULL}, // erg_face_33
    {0x329AE0B7C313A189ULL, 0x8C0227004C8193B1ULL, 0xE35F93D24A374A99ULL,
     0x0599E2771EDAF727ULL, 0xD41C143792161C0BULL}, // lisiere_tropicale_savane
}};
constexpr std::uint64_t kPlanetLevel = 0x26626564570B0D9EULL;

} // namespace

int main() {
    terrain::unreal::SessionParams params;
    params.planetFile = coast_planet::filePath();
    const terrain::unreal::TerrainSession session(params);
    const terrain::unreal::PlanetSource& planet = session.planet();
    // Read, never built here: the fixture wrote the planet of this generator, which other tests
    // read at the same time.
    std::printf("planet level: %s\n", planet.built ? planet.reason.c_str() : "read from the file");
    TEST_CHECK(planet.level != nullptr && !planet.built);
    std::vector<golden::Part> parts;
    parts.push_back({"planet level of the default world", planet.level->checksum(), kPlanetLevel});
    std::vector<std::string> names; // kept alive for the parts
    names.reserve(kPlaces.size() * (terrain::kLodCount + 1));
    for (std::size_t p = 0; p < kPlaces.size(); ++p) {
        const Place& place = kPlaces[p];
        for (int lod = 0; lod < terrain::kLodCount; ++lod) {
            names.push_back(std::string(place.name) + " tiles LOD " + std::to_string(lod));
            parts.push_back({names.back().c_str(), tileChecksum(session.tiles(), place, lod),
                             kExpected[p][static_cast<std::size_t>(lod)]});
        }
        names.push_back(std::string(place.name) + " horizon");
        parts.push_back({names.back().c_str(), horizonChecksum(session.tiles(), place),
                         kExpected[p][terrain::kLodCount]});
    }
    golden::report("played world", parts);
    return test::result();
}
