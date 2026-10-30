#include "coast_shape.hpp"

#include "terrain/generation/coast.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>

namespace terrain::coast_shape {

namespace {

// The coastal plain rises this many meters per meter from the shore, up to kCoastPlainTop,
// before the regional elevation takes over over GeographyParams::coastRamp.
constexpr double kCoastPlainRise = 0.002;
constexpr double kCoastPlainTop = 20.0;

} // namespace

bool inBand(const GeographyParams& p, double coast) {
    return coast > -kCoastSeaBand && coast < p.coastRamp;
}

float landFactor(const GeographyParams& p, double coast) {
    return noise::smoothstep(0.0f, static_cast<float>(p.coastRamp), static_cast<float>(coast));
}

double shelf(const GeographyParams& p, double coast) {
    const double d = -coast;
    const double depth = static_cast<double>(p.shelfDepth);
    return d <= p.shelfWidth ? depth * d / p.shelfWidth
                             : depth - (d - p.shelfWidth) * p.shelfSlope;
}

float groundBase(const GeographyParams& p, const GeographySample& g, double coast) {
    if (coast < 0.0) {
        return std::max(g.sea, static_cast<float>(shelf(p, coast)));
    }
    const double plain = std::min(kCoastPlainRise * coast, kCoastPlainTop);
    const double t = landFactor(p, coast);
    return static_cast<float>(plain + (static_cast<double>(g.land) - plain) * t);
}

} // namespace terrain::coast_shape
