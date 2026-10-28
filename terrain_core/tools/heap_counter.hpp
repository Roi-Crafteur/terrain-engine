#pragma once

// The bytes of the heap of a process, counted by the global operator new and delete of
// heap_counter.cpp. Linked only into the executables that measure their memory (the tools and
// the tests that link heap_counter.cpp): they then count every block they hand out.

#include <cstddef>

namespace heap_counter {

// Bytes asked of operator new and not yet given back.
[[nodiscard]] std::size_t live();
// The most live bytes since the process started or since the last resetPeak.
[[nodiscard]] std::size_t peak();
// Starts a new peak from the live bytes.
void resetPeak();

} // namespace heap_counter
