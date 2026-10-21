// Golden checksums of the Heightfield and the Ecology, asserted on every platform (see
// surface_golden_support.hpp).
#include "surface_golden_support.hpp"

#include "test_assert.hpp"

int main() {
    golden::report("heightfield and ecology", golden::heightfieldParts());
    return test::result();
}
