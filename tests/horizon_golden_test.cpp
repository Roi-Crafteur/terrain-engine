// Golden checksums of the far horizon, asserted on every platform (see
// horizon_golden_support.hpp).
#include "horizon_golden_support.hpp"

#include "test_assert.hpp"

int main() {
    golden::report("far horizon", golden::horizonParts());
    return test::result();
}
