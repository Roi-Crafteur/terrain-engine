// Golden checksums of the caves, asserted on every platform (see cave_golden_support.hpp).
#include "cave_golden_support.hpp"

#include "test_assert.hpp"

int main() {
    golden::report("caves", golden::caveParts());
    return test::result();
}
