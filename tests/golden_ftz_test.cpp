// The golden checksums again, with denormals flushed to zero (FTZ) and read as zero (DAZ), as
// some engines and drivers set them on x86: the world must not change. x86-64 only; elsewhere
// the test says it is skipped.
#include "cave_golden_support.hpp"
#include "golden_support.hpp"
#include "planet_golden_support.hpp"
#include "surface_golden_support.hpp"

#include "test_assert.hpp"

#include <cstdio>
#include <limits>

#if defined(__x86_64__) || defined(_M_X64)
#include <pmmintrin.h>
#include <xmmintrin.h>
#define TERRAIN_GOLDEN_FTZ 1
#else
#define TERRAIN_GOLDEN_FTZ 0
#endif

int main() {
#if TERRAIN_GOLDEN_FTZ
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    // The modes are on: the smallest normal double divided by 4 flushes to zero.
    volatile double smallest = std::numeric_limits<double>::min();
    TEST_CHECK(smallest / 4.0 == 0.0);
    golden::report("heightfield and ecology, FTZ and DAZ", golden::heightfieldParts());
    golden::report("streamed world, FTZ and DAZ", golden::worldParts());
    golden::report("caves, FTZ and DAZ", golden::caveParts());
    golden::report("planet level, FTZ and DAZ", golden::planetGoldenParts());
#else
    std::printf("FTZ and DAZ are only tested on x86-64: skipped\n");
#endif
    return test::result();
}
