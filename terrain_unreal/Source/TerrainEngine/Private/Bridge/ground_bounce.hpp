#pragma once

#include <array>

namespace terrain::unreal {

// The warm bounce of the ground in the shade (agent 40, TerrainAtmosphereBounce.h): the rock in
// the shade was lit by the blue sky alone (blue over red 1.67 in Unreal), as if the ground under it
// were black. The plugin lets the SkyLight take its lower hemisphere from its capture (the ground
// of the SkyAtmosphere, lit by the sun) and gives that ground a warm albedo: the faces in the
// shade get the warm light the sunlit ground sends back, at the strength of the sun and the sky.
// Off, the values of the map, exactly; the map is never saved.
struct BounceLook {
    bool lowerHemisphereBlack = true; // USkyLightComponent::bLowerHemisphereIsBlack
    std::array<double, 3> groundAlbedo{0.667, 0.667, 0.667}; // USkyAtmosphereComponent::GroundAlbedo

    friend bool operator==(const BounceLook&, const BounceLook&) = default;
};

// The look with the bounce on (the lower hemisphere from the capture, the ground warmAlbedo) or
// off (the map's look, as it was read).
[[nodiscard]] BounceLook bounceLook(const BounceLook& map, bool on,
                                    const std::array<double, 3>& warmAlbedo);

} // namespace terrain::unreal
