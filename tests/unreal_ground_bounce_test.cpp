// The warm bounce of the ground in the shade (Bridge/ground_bounce.hpp, agent 40): on, the lower
// hemisphere of the SkyLight from its capture and the ground of the SkyAtmosphere warm; off, the
// values of the map exactly, whatever they were (Terrain.Bounce 0 and the end of the play set
// them back).
#include "ground_bounce.hpp"

#include "test_assert.hpp"

#include <array>

int main() {
    using terrain::unreal::BounceLook;
    const std::array<double, 3> warm{0.45, 0.38, 0.30};
    for (const BounceLook& map : {BounceLook{}, BounceLook{false, {0.1, 0.2, 0.3}},
                                  BounceLook{true, {0.9, 0.8, 0.7}}}) {
        TEST_CHECK(terrain::unreal::bounceLook(map, false, warm) == map);
        const BounceLook on = terrain::unreal::bounceLook(map, true, warm);
        TEST_CHECK(!on.lowerHemisphereBlack && on.groundAlbedo == warm);
        // On, then off again: the map's look, exactly.
        TEST_CHECK(terrain::unreal::bounceLook(map, false, on.groundAlbedo) == map);
    }
    return test::result();
}
