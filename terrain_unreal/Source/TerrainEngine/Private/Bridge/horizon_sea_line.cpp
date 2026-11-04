#include "horizon_sea_line.hpp"

#include <algorithm>

namespace terrain::unreal {

float shownHeight(const HorizonTexel& texel, SeaDrawing drawing) {
    return drawing == SeaDrawing::Layer && isSeaTexel(texel) ? texelGround(texel) : texel.height;
}

float paintedGround(const HorizonTexel& texel) {
    return isSeaTexel(texel) ? texelGround(texel) : texelGround(texel) + 0.01f;
}

EdgeCoast edgeCoast(const HorizonTexel& a, const HorizonTexel& b, SeaDrawing drawing) {
    const double za = shownHeight(a, drawing);
    const double zb = shownHeight(b, drawing);
    if (drawing == SeaDrawing::Layer) {
        // The layer shows on the ocean only; the coast is where the ground shown crosses it.
        if ((!isSeaTexel(a) && !isSeaTexel(b)) || (za < 0.0) == (zb < 0.0)) {
            return {};
        }
        return {true, za / (za - zb), 0.0};
    }
    const double ga = paintedGround(a);
    const double gb = paintedGround(b);
    if ((ga < 0.0) == (gb < 0.0)) {
        return {};
    }
    const double t = ga / (ga - gb);
    return {true, t, std::max(0.0, za + t * (zb - za))};
}

} // namespace terrain::unreal
