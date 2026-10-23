// Golden checksums of the Heightfield and the Ecology, and of the planetary geography, asserted
// on every platform (see surface_golden_support.hpp and geography_golden_support.hpp).
#include "geography_golden_support.hpp"
#include "surface_golden_support.hpp"

#include "test_assert.hpp"

int main() {
    golden::report("heightfield and ecology", golden::heightfieldParts());
    golden::report("planetary geography", golden::geographyParts());
    return test::result();
}
