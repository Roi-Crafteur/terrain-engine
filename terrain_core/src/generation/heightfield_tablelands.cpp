// The plateaus and the karst in the Heightfield (agent 46, tablelands.hpp, tableland_forms.hpp):
// what their provinces make of the relief. Off (GeographyParams::tablelands and ::karst), the
// relief of before them, bit for bit.
#include "terrain/generation/heightfield.hpp"

#include "heightfield_layers.hpp"
#include "tableland_forms.hpp"
#include "terrain/generation/tablelands.hpp"

#include <algorithm>

namespace terrain {

float Heightfield::intensityAt(double x, double z, const GeographySample& g, Memos* memos) const {
    const float intensity = reliefAt(x, z, g.relief, memos);
    if (!tablelands_ || !params_.geography.tablelands) {
        return intensity;
    }
    const float surface = tableland_forms::surfaceShare(g);
    if (!(surface > 0.0f)) {
        return intensity;
    }
    return std::max(intensity,
                    tableland_forms::innerRanges(layers_->tablelandForms, x, z, surface));
}

void Heightfield::addSurfaceRelief(double x, double z, const GeographySample& g,
                                   float& local) const {
    if (!tablelands_ || !params_.geography.tablelands) {
        return;
    }
    const float surface = tableland_forms::surfaceShare(g);
    if (surface > 0.0f) {
        local += tableland_forms::surfaceRelief(layers_->tablelandForms, x, z, surface);
    }
}

} // namespace terrain
