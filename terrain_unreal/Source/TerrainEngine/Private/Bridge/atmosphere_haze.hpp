#pragma once

#include <array>

namespace terrain {
class PlanetLevel;
}

namespace terrain::unreal {

// The haze of the dust over dry land (agent 40, TerrainAtmosphereHaze.cpp): seen from high over a
// desert, the aerial perspective of the SkyAtmosphere laid a lavender band over the far sand (its
// blue of Rayleigh over the orange of the sand); a real desert seen from a plane fades into a beige
// haze. The dryness d of the region around the camera (0: humid land, sea, cold; 1: a desert)
// turns the SkyAtmosphere of the map toward a dusty air: less depth of aerial perspective, a little
// more scattering of Mie, an absorption of Mie strongest in the blue. d = 0 gives back the values
// of the map exactly: the blue depth of the mountains of humid lands stays as it is.

// What the plugin sets on the SkyAtmosphere, as Unreal names them.
struct HazeLook {
    double viewDistanceScale = 1.0;       // AerialPespectiveViewDistanceScale
    double mieScatteringScale = 0.003996; // MieScatteringScale, 1/km (the default of Unreal)
    double mieAbsorptionScale = 0.000444; // MieAbsorptionScale, 1/km
    std::array<double, 3> mieAbsorption{1.0, 1.0, 1.0}; // MieAbsorption, linear RGB

    friend bool operator==(const HazeLook&, const HazeLook&) = default;
};

// The dusty air at d = 1 (FTerrainHorizonDrawing): its aerial perspective a scale of that of the
// map, its scattering of Mie a multiple of that of the map, its absorption of Mie (Saharan dust
// absorbs the blue most). Small enough that the sun through it barely changes (at most 2 % per
// channel and 1 % of tint down to 7 degrees: atmosphere_haze_test; Mie x 1.5 and an absorption of
// 0.001 dimmed the blue of a sun at 7 degrees by 2.5 %), so that the near sand keeps its color and
// only the far one turns beige.
struct DustHaze {
    double viewDistanceScale = 0.4;
    double mieScattering = 1.4;
    double mieAbsorptionScale = 0.0006;
    std::array<double, 3> mieAbsorption{0.25, 0.5, 1.0};
};

// The look at dryness d (clamped to [0, 1]): the map's at 0, the dusty air at 1, linear between.
[[nodiscard]] HazeLook hazeLook(const HazeLook& map, const DustHaze& dust, double dryness);

// The dryness of a climate, [0, 1]: 1 - smoothstep(0.15, 0.40, humidity) (0.15: the humidity
// where the core lays a full erg, ErgParams), times smoothstep(-6, 2, temperature, Celsius) (the
// polar deserts, under snow most of the year, have no dust; a cold desert of the Gobi kind keeps
// it), times smoothstep(-200, 0, elevation, m) (the open sea has none). Smooth everywhere: the
// planet level gives the three bilinear.
[[nodiscard]] double climateDryness(double humidity, double temperature, double elevation);

// The dryness at a point of the planet level, and that of the region around it: the mean of
// kHazePoints points, the point itself, 8 at 12 km and 16 at 30 km around it (the haze seen from
// high comes from tens of kilometers).
inline constexpr int kHazePoints = 25;
[[nodiscard]] double pointDryness(const PlanetLevel& planet, double x, double z);
[[nodiscard]] double regionDryness(const PlanetLevel& planet, double x, double z);

// The dryness shown follows that of the region with a time constant of kHazeSeconds, and takes it
// at once after a jump of the camera longer than kHazeJumpMeters (a teleport); the region is read
// again every kHazeRefreshSeconds, and the SkyAtmosphere set again only when the dryness shown
// moves by more than kHazeMinStep.
inline constexpr double kHazeSeconds = 3.0;
inline constexpr double kHazeJumpMeters = 10'000.0;
inline constexpr double kHazeRefreshSeconds = 0.5;
inline constexpr double kHazeMinStep = 0.002;

class HazeFollower {
public:
    // The dryness shown after dt seconds, the camera at (x, z), its region at target.
    double follow(double target, double dt, double x, double z);
    [[nodiscard]] double shown() const { return shown_; }
    void reset() { started_ = false; }

private:
    bool started_ = false;
    double shown_ = 0.0;
    double x_ = 0.0;
    double z_ = 0.0;
};

} // namespace terrain::unreal
