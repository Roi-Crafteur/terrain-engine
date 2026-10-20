#pragma once

#include "terrain/generation/caves.hpp"

namespace terrain {

// Detail of the caves carved in the streamed tiles of a LOD. LOD 0 (1 m cells) carves every
// component; LOD 1 (2 m) drops the noodles, thinner than its cells; LOD 2 (4 m) keeps the
// caverns of the cheese caves and the entrances; LOD 3 (8 m), the far terrain, carves nothing
// and stays identical to the world without caves. Each LOD carves a subset of the components
// of the finer one, and its guard spans 3 of its cells plus 1 m.
[[nodiscard]] CaveDetail caveDetailForLod(int lod);

} // namespace terrain
