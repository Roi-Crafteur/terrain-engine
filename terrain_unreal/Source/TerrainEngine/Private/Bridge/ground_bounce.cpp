#include "ground_bounce.hpp"

namespace terrain::unreal {

BounceLook bounceLook(const BounceLook& map, bool on, const std::array<double, 3>& warmAlbedo) {
    if (!on) {
        return map;
    }
    return BounceLook{false, warmAlbedo};
}

} // namespace terrain::unreal
