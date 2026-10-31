#pragma once

#include <string>

// Internal to terrain_core dev/: what the reader and the writer of the JSON share.
namespace terrain::dev {

// strtod of a JSON number (its syntax already checked), whatever the decimal point of the locale
// of the C library: the same double on every platform for a correctly rounding strtod.
[[nodiscard]] double readJsonNumber(std::string token);

} // namespace terrain::dev
