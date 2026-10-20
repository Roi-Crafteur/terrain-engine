// Golden checksums of the streamed world without caves, tiles of LOD 0 to 3, asserted on every
// platform (see surface_golden_support.hpp).
#include "surface_golden_support.hpp"

#include "test_assert.hpp"

int main() {
    golden::report("streamed world", golden::worldParts());
    return test::result();
}
