// Golden checksums of the planet level: the same bits on every platform (Windows, Linux, macOS).
#include "planet_golden_support.hpp"

#include "test_assert.hpp"

int main() {
    golden::report("planet level", golden::planetGoldenParts());
    return test::result();
}
