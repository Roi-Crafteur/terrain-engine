// The frame of a volcano (volcano_relief.hpp): around its summit, its long axis shrunk to its
// short one, the summit shifted off its center (fading out to its foot); from an offset to its
// frame and back.
#include "volcano_relief.hpp"

#include "terrain/det_math.hpp"
#include "volcano_forms.hpp"

namespace terrain::volcano_relief {

namespace vf = volcano_forms;

FramePoint frameOfOffset(const VolcanoSite& s, const Shape& sh, double dx, double dz) {
    FramePoint p;
    p.u = (dx * sh.cosAxis + dz * sh.sinAxis) / static_cast<double>(s.elongation);
    p.v = -dx * sh.sinAxis + dz * sh.cosAxis;
    p.rho0 = dmath::hypot(p.u, p.v) / static_cast<double>(s.radius);
    const double shift = 1.0 - vf::smoothstep(0.0, 1.0, p.rho0);
    p.u -= sh.offsetU * shift;
    p.v -= sh.offsetV * shift;
    return p;
}

FrameOffset offsetOfFrame(const VolcanoSite& s, const Shape& sh, double u, double v) {
    // frameOf shifts a point by the offset of the summit, fading with its distance before the
    // shift: a few steps of a fixed point find it.
    const auto radius = static_cast<double>(s.radius);
    double u0 = u + sh.offsetU;
    double v0 = v + sh.offsetV;
    for (int k = 0; k < 4; ++k) {
        const double shift = 1.0 - vf::smoothstep(0.0, 1.0, dmath::hypot(u0, v0) / radius);
        u0 = u + sh.offsetU * shift;
        v0 = v + sh.offsetV * shift;
    }
    const double stretched = u0 * static_cast<double>(s.elongation);
    return {stretched * sh.cosAxis - v0 * sh.sinAxis, stretched * sh.sinAxis + v0 * sh.cosAxis};
}

} // namespace terrain::volcano_relief
