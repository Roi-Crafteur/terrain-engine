#pragma once

// Minimal checks for plain CTest executables: a test returns test::result() from main.
// Deliberately small so a test framework can replace it later.

#include <cstdio>
#include <cstdlib>
#include <source_location>

namespace test {

inline int failures = 0;

inline void check(bool passed, const char* expression,
                  std::source_location where = std::source_location::current()) {
    if (passed) {
        return;
    }
    ++failures;
    std::fprintf(stderr, "%s:%u: check failed: %s\n", where.file_name(),
                 static_cast<unsigned>(where.line()), expression);
}

inline int result() {
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace test

#define TEST_CHECK(...) ::test::check(static_cast<bool>(__VA_ARGS__), #__VA_ARGS__)
