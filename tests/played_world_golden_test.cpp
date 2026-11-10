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
using PlaceSums = std::array<std::uint64_t, terrain::kLodCount + 1>;
constexpr std::array<PlaceSums, kPlaces.size()> kExpected = {{
    {0x4274B04EE562A220ULL, 0x02B5619E5AEBBB03ULL, 0xFAFA06FEB4719A1DULL,
     0x0D3E6FD5A64AA1CAULL, 0x6F40877B597CB6C2ULL}, // k2_paroi
    {0x978E7F1CB32B9B7DULL, 0x9901D8B0EB2377D0ULL, 0xC06893241A1BF661ULL,
     0x18180B56E93260AEULL, 0x713303E2794DF134ULL}, // fleuve_plaine
    {0xEC39F7FD844B34E7ULL, 0x25C7085375E57FE8ULL, 0x5C625420911FD66DULL,
     0xF1D85D4B7D8F324AULL, 0x586E92880DABBFD9ULL}, // cote_falaise_encoche
    {0x4F79C94F623EC04AULL, 0xBF67E9EEEAF95F96ULL, 0xF15E5E8BF9AB4522ULL,
     0xF986C88CE56B7E25ULL, 0xB3D825734CBE5B21ULL}, // erg_face_33
    {0xA76576BFA41388A5ULL, 0x67F2748153EB1CE9ULL, 0xCE30AF1BD3E9FAC5ULL,
     0xA98F4C8152F5F357ULL, 0xF255DF5139BD9DF2ULL}, // lisiere_tropicale_savane
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
