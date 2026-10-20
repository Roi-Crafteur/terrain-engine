#pragma once

// Entrances of the streamed world with cave districts, by the district of their opening: shared
// by the tile tests of the cave districts.

#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/cave_regions.hpp"
#include "terrain/generation/caves.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"

#include <algorithm>
#include <iterator>
#include <vector>

namespace districts_test {

// An entrance of the world with the cave districts, the type of the district that weighs most at
// its opening, and whether another weighs there too (the opening lies in a border zone).
struct DistrictEntrance {
    terrain::CaveEntrance entrance;
    terrain::CaveDistrictType type;
    bool blended;
};

// The entrances of the list, each with the district of its opening.
inline std::vector<DistrictEntrance> withDistricts(const terrain::TerrainTiles& tiles,
                                                   const std::vector<terrain::CaveEntrance>& list) {
    terrain::AquiferCache cache(*tiles.caves(), tiles.caveGround());
    std::vector<DistrictEntrance> found;
    for (const terrain::CaveEntrance& e : list) {
        const terrain::DistrictWeights weights = cache.districtWeights(e.topX, e.topZ);
        found.push_back({e, weights.districts[0].type, weights.count > 1});
    }
    return found;
}

// The entrances around the center of the streaming tests (entrancesNearOrigin).
inline std::vector<DistrictEntrance> districtEntrances(const terrain::TerrainTiles& tiles) {
    return withDistricts(tiles, streaming_test::entrancesNearOrigin(tiles));
}

inline std::vector<DistrictEntrance> select(const std::vector<DistrictEntrance>& list,
                                            bool blended) {
    std::vector<DistrictEntrance> kept;
    std::copy_if(list.begin(), list.end(), std::back_inserter(kept),
                 [blended](const DistrictEntrance& d) { return d.blended == blended; });
    return kept;
}

} // namespace districts_test
