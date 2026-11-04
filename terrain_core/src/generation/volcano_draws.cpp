// The draws of a volcano (volcano_sites.hpp): its form, then its sizes, taken from real
// volcanoes (meters):
//   - stratovolcanoes rise 1.5 to 3.5 km over their foot (large: 3 to 4.5 km) on a foot 5 to 6.5
//     times as wide as high in radius (Fuji: 3.5 km over a foot of 35 to 40 km), a crater of 150 to
//     450 m in radius, 100 to 300 m deep;
//   - shields rise 0.5 to 2 km on land (large: 2 to 3.5 km) at a mean slope of 4 to 7 degrees,
//     a caldera of 1 to 2.5 km in radius at their summit, 80 to 200 m deep (Mauna Loa, the
//     shields of Iceland); at sea they rise from the sea floor at 6 to 9 degrees to a summit of
//     0.5 to 3 km (large: 3 to 4.2 km), up to 90 km in radius;
//   - the fields of cinder cones, of lava, of maars and the rows of fissures cover 4 to 25 km in
//     radius (Chaine des Puys, Craters of the Moon, the Eifel, Laki);
//   - calderas of 2.5 to 8 km in radius on an edifice, 300 to 900 m under their rim (Crater Lake,
//     Ngorongoro); on a plateau of 200 to 600 m, 6 to 12 km in radius, their floor 100 to 300 m
//     under the plain around (Yellowstone);
//   - island stratovolcanoes rise from the sea floor to 0.3 to 2.5 km (Stromboli: 0.9 km over 3 km
//     of water, a foot of 8 to 10 km in radius); drowned calderas to 0.3 to 1.1 km, their floor 200
//     to 400 m under the sea (Santorini); seamounts stop 300 to 2,000 m under it.
#include "volcano_sites.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace terrain::volcano_sites {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegree = kPi / 180.0;

// The indices of the draws of a site.
enum Draw : std::uint64_t {
    kForm,
    kHeight,
    kRadius,
    kCrater,
    kCraterDepth,
    kElongation,
    kAxis,
    kSlope,
    kSharpness,
    kTopSlope,
};

struct Weighted {
    VolcanoForm form;
    double weight;
};

template <std::size_t N>
VolcanoForm pick(const std::array<Weighted, N>& table, double u) {
    double total = 0.0;
    for (const Weighted& w : table) {
        total += w.weight;
    }
    double at = u * total;
    for (const Weighted& w : table) {
        if (at < w.weight) {
            return w.form;
        }
        at -= w.weight;
    }
    return table.back().form;
}

VolcanoForm landForm(VolcanoOrigin origin, bool large, double u) {
    using F = VolcanoForm;
    switch (origin) {
    case VolcanoOrigin::Arc:
        if (large) {
            return pick(std::array<Weighted, 2>{{{F::Caldera, 0.6}, {F::Strato, 0.4}}}, u);
        }
        return pick(std::array<Weighted, 6>{{{F::Strato, 0.60},
                                             {F::TwinStrato, 0.10},
                                             {F::CollapsedStrato, 0.10},
                                             {F::CinderField, 0.10},
                                             {F::MaarField, 0.05},
                                             {F::Caldera, 0.05}}},
                    u);
    case VolcanoOrigin::Rift:
        if (large) {
            return pick(std::array<Weighted, 2>{{{F::Caldera, 0.5}, {F::Strato, 0.5}}}, u);
        }
        return pick(std::array<Weighted, 4>{{{F::LavaField, 0.45},
                                             {F::Shield, 0.20},
                                             {F::Strato, 0.15},
                                             {F::MaarField, 0.20}}},
                    u);
    case VolcanoOrigin::Ridge:
        if (large) {
            return F::Shield;
        }
        return pick(std::array<Weighted, 2>{{{F::Shield, 0.4}, {F::FissureRow, 0.6}}}, u);
    case VolcanoOrigin::Hotspot:
        if (large) {
            return F::PlateauCaldera;
        }
        return pick(std::array<Weighted, 3>{
                        {{F::Shield, 0.45}, {F::LavaField, 0.45}, {F::PlateauCaldera, 0.10}}},
                    u);
    }
    return F::Strato;
}

VolcanoForm seaForm(VolcanoOrigin origin, bool large, double u) {
    using F = VolcanoForm;
    if (origin == VolcanoOrigin::Arc) {
        if (large) {
            return F::IslandCaldera;
        }
        return pick(std::array<Weighted, 2>{{{F::IslandStrato, 0.75}, {F::Seamount, 0.25}}}, u);
    }
    if (large) {
        return F::IslandShield;
    }
    return pick(std::array<Weighted, 2>{{{F::IslandShield, 0.6}, {F::Seamount, 0.4}}}, u);
}

// The radius of a concave cone rising rise meters: its sharpness drawn in [kLow, kHigh], its slope
// at the summit in [topLow, topHigh] degrees (the radius follows: rise k / (top (1 - e^-k))).
double cone(VolcanoSite& s, double rise, double kLow, double kHigh, double topLow,
            double topHigh) {
    const double k = between(s.seed, kSharpness, kLow, kHigh);
    const double top = between(s.seed, kTopSlope, topLow, topHigh) * kDegree;
    s.sharpness = static_cast<float>(k);
    return rise * k * dmath::cos(top) / (dmath::sin(top) * (1.0 - dmath::exp(-k)));
}

// A stratovolcano: its height, its concave cone (35 to 39 degrees at its summit, 30 to 35 over the
// 500 m under its rim; 5 to 10 at its foot: k 1.6 to 2.1, as Mayon), its crater.
void strato(VolcanoSite& s, double low, double high) {
    s.height = static_cast<float>(between(s.seed, kHeight, low, high));
    s.radius = static_cast<float>(cone(s, s.height, 1.6, 2.1, 35.0, 39.0));
    s.crater = static_cast<float>(between(s.seed, kCrater, 150.0, s.large ? 600.0 : 450.0));
    s.craterDepth = static_cast<float>(between(s.seed, kCraterDepth, 100.0, 300.0));
}

// A shield of height meters at a mean slope drawn in [low, high] degrees, at most most meters in
// radius, its summit caldera.
void shield(VolcanoSite& s, double height, double low, double high, double most) {
    s.height = static_cast<float>(height);
    const double slope = between(s.seed, kSlope, low, high) * kDegree;
    s.radius = static_cast<float>(std::min(most, height * dmath::cos(slope) / dmath::sin(slope)));
    s.crater = static_cast<float>(between(s.seed, kCrater, 1000.0, s.large ? 3000.0 : 2500.0));
    s.craterDepth = static_cast<float>(between(s.seed, kCraterDepth, 80.0, 200.0));
}

// The rise of an edifice standing at sea, from its floor to its summit, at least least meters.
double rise(const VolcanoSite& s, double least) {
    return std::max(least, static_cast<double>(s.height) - static_cast<double>(s.floor));
}

void landSizes(VolcanoSite& s) {
    using F = VolcanoForm;
    const std::uint64_t seed = s.seed;
    switch (s.form) {
    case F::Strato:
    case F::CollapsedStrato:
        // 1.5 to 3 km; the large ones 3.5 to 4.8 km over the ground at their foot (Fuji 3.8,
        // Kilimanjaro about 4.9 over their plains): drawn a little higher, their crater and their
        // irregular foot taking some of it.
        strato(s, s.large ? 3700.0 : 1500.0, s.large ? 5000.0 : 3000.0);
        if (s.form == F::CollapsedStrato) {
            s.crater = static_cast<float>(between(seed, kCrater, 1000.0, 2000.0));
            s.craterDepth = static_cast<float>(between(seed, kCraterDepth, 300.0, 600.0));
        }
        break;
    case F::TwinStrato:
        strato(s, 1500.0, 3000.0);
        break;
    case F::Shield:
        shield(s, between(seed, kHeight, s.large ? 2000.0 : 500.0, s.large ? 3500.0 : 2000.0), 4.0,
               7.0, 55'000.0);
        break;
    case F::CinderField:
    case F::LavaField:
    case F::FissureRow:
    case F::MaarField: {
        const bool maars = s.form == F::MaarField;
        const bool cinders = s.form == F::CinderField;
        const double least = maars ? 4000.0 : (cinders ? 6000.0 : 8000.0);
        const double most = maars ? 10'000.0 : (cinders ? 15'000.0 : 25'000.0);
        s.radius = static_cast<float>(between(seed, kRadius, least, most));
        // The highest cone, the thickest sheet or the deepest maar.
        s.height = static_cast<float>(cinders ? between(seed, kHeight, 100.0, 300.0)
                                              : (maars ? between(seed, kHeight, 60.0, 200.0)
                                                       : between(seed, kHeight, 10.0, 30.0)));
        break;
    }
    case F::Caldera: {
        // Its height is that of its rim over its foot (Crater Lake, Aso, Ngorongoro: 0.5 to 1.8
        // km), the edifice it collapsed from higher.
        s.height = static_cast<float>(between(seed, kHeight, 500.0, 1800.0));
        s.crater = static_cast<float>(between(seed, kCrater, 2500.0, 8000.0));
        s.craterDepth = static_cast<float>(between(seed, kCraterDepth, 300.0, 900.0));
        s.radius = static_cast<float>(std::max(s.height * between(seed, kRadius, 9.0, 13.0),
                                               2.2 * static_cast<double>(s.crater)));
        s.sharpness = static_cast<float>(between(seed, kSharpness, 2.6, 3.1));
        break;
    }
    case F::PlateauCaldera:
        s.height = static_cast<float>(between(seed, kHeight, 200.0, 600.0));
        s.radius = static_cast<float>(between(seed, kRadius, 25'000.0, 40'000.0));
        s.crater = static_cast<float>(between(seed, kCrater, 6000.0, 12'000.0));
        s.craterDepth = s.height + static_cast<float>(between(seed, kCraterDepth, 100.0, 300.0));
        break;
    default:
        break;
    }
}

void seaSizes(VolcanoSite& s) {
    using F = VolcanoForm;
    const std::uint64_t seed = s.seed;
    switch (s.form) {
    case F::IslandStrato:
        strato(s, 300.0, 2500.0);
        // From the sea floor: steeper above the sea (30 to 36 degrees at the summit, Stromboli).
        s.radius = static_cast<float>(cone(s, rise(s, 500.0), 2.0, 2.6, 30.0, 36.0));
        break;
    case F::IslandShield: {
        const double summit =
            between(seed, kHeight, s.large ? 3000.0 : 500.0, s.large ? 4200.0 : 3000.0);
        s.height = static_cast<float>(summit);
        shield(s, rise(s, 800.0), 6.0, 9.0, 70'000.0);
        s.height = static_cast<float>(summit);
        break;
    }
    case F::IslandCaldera:
        s.height = static_cast<float>(between(seed, kHeight, 200.0, 1500.0));
        s.radius = static_cast<float>(rise(s, 600.0) * between(seed, kRadius, 2.8, 4.5));
        s.crater = static_cast<float>(
            std::min(0.6 * s.radius, between(seed, kCrater, 3000.0, 6000.0)));
        s.craterDepth = s.height + static_cast<float>(between(seed, kCraterDepth, 200.0, 400.0));
        s.sharpness = static_cast<float>(between(seed, kSharpness, 2.4, 3.0));
        break;
    case F::Seamount:
        s.height = static_cast<float>(between(seed, kHeight, -2000.0, -300.0));
        s.radius = static_cast<float>(rise(s, 300.0) * between(seed, kRadius, 3.0, 4.5));
        s.sharpness = 2.2f;
        break;
    default:
        break;
    }
}

// How far a form reaches from its center: its apron (its flows and satellite cones stay within it)
// or its field, stretched by its outline, and a margin.
double reachOf(const VolcanoSite& s) {
    const double r = static_cast<double>(s.radius) * static_cast<double>(s.elongation);
    const FormRule rule = formRule(s.form);
    if (rule.edifice) {
        return rule.apron * (1.0 + rule.outline) * r + 1500.0;
    }
    return (1.0 + kFieldLobes) * (1.0 + kFieldOutline) * r + 1500.0;
}

} // namespace

FormRule formRule(VolcanoForm form) {
    using F = VolcanoForm;
    switch (form) {
    case F::Strato:
    case F::TwinStrato:
    case F::CollapsedStrato:
    case F::Caldera:
    case F::IslandStrato:
    case F::IslandCaldera:
    case F::Seamount:
        return {true, false, 1.3, kOutline};
    case F::Shield:
    case F::IslandShield:
    case F::PlateauCaldera:
        return {true, true, 1.2, kOutline + kArms};
    case F::CinderField:
    case F::LavaField:
    case F::FissureRow:
    case F::MaarField:
        return {};
    }
    return {};
}

double unit(std::uint64_t seed, std::uint64_t index) {
    return static_cast<double>(noise::deriveSeed(seed, index) >> 11) * 0x1.0p-53;
}

double between(std::uint64_t seed, std::uint64_t index, double low, double high) {
    return low + (high - low) * unit(seed, index);
}

std::uint64_t siteSeed(std::uint64_t stream, double x, double z) {
    const std::uint64_t bx = std::bit_cast<std::uint64_t>(x);
    const std::uint64_t bz = std::bit_cast<std::uint64_t>(z);
    return noise::deriveSeed(noise::deriveSeed(stream, bx), bz);
}

VolcanoForm drawForm(VolcanoOrigin origin, bool atSea, bool large, bool rugged, float floor,
                     std::uint64_t seed) {
    using F = VolcanoForm;
    const double u = unit(seed, kForm);
    if (atSea) {
        const VolcanoForm form = seaForm(origin, large, u);
        if (form == F::Seamount && floor > kShelf) {
            return origin == VolcanoOrigin::Arc ? F::IslandStrato : F::IslandShield;
        }
        return form;
    }
    const VolcanoForm form = landForm(origin, large, u);
    if (!rugged) {
        return form;
    }
    switch (form) {
    case F::PlateauCaldera:
        return F::Caldera;
    case F::LavaField:
    case F::FissureRow:
        return F::Strato;
    case F::MaarField:
        return F::CinderField;
    default:
        return form;
    }
}

void drawSizes(VolcanoSite& s) {
    if (s.atSea) {
        seaSizes(s);
    } else {
        landSizes(s);
    }
    double elongation = between(s.seed, kElongation, 1.0, s.atSea ? 1.45 : 1.25);
    switch (s.form) {
    case VolcanoForm::TwinStrato:
        elongation = between(s.seed, kElongation, 1.3, 1.6);
        break;
    case VolcanoForm::Shield:
    case VolcanoForm::IslandShield:
        elongation = between(s.seed, kElongation, 1.1, 1.5);
        break;
    case VolcanoForm::CinderField:
        elongation = between(s.seed, kElongation, 1.3, 2.2);
        break;
    case VolcanoForm::LavaField:
        elongation = between(s.seed, kElongation, 1.1, 1.8);
        break;
    case VolcanoForm::FissureRow:
        elongation = between(s.seed, kElongation, 2.5, 4.0);
        break;
    default:
        break;
    }
    s.elongation = static_cast<float>(elongation);
    s.axis = static_cast<float>(between(s.seed, kAxis, 0.0, kPi));
    s.reach = static_cast<float>(reachOf(s));
}

} // namespace terrain::volcano_sites
