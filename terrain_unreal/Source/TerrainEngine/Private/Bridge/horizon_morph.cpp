#include "horizon_morph.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::unreal {

double morphFactor(double i, double k, double u, double v, int cells, double width) {
    const double distance = std::max(std::abs(i - u), std::abs(k - v));
    const double start = cells / 2.0 - 2.0 - width;
    return std::clamp((distance - start) / width, 0.0, 1.0);
}

MorphSources morphSources(int i, int k) {
    const bool oddI = i % 2 != 0;
    const bool oddK = k % 2 != 0;
    if (oddI && oddK) {
        return {i + 1, k - 1, i - 1, k + 1};
    }
    if (oddI) {
        return {i - 1, k, i + 1, k};
    }
    if (oddK) {
        return {i, k - 1, i, k + 1};
    }
    return {i, k, i, k};
}

} // namespace terrain::unreal
