// The blend of the kinds of dunes at a point (dune_field.cpp).
#pragma once

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::dune_field {

// The kinds blended: the power mean of their heights above the floor (exponent 4, the crests stay
// sharp where two kinds meet) weighted by the fourth powers of their weights, times the sum of the
// weights; the troughs below it by their weights. A kind taking a share w of the weight rises by
// about w of its height: not w^(1/4), a step where it begins.
struct Blend {
    double above = 0.0;  // the sum of the weights^4 times the heights^4
    double weights = 0.0; // the sum of the weights^4
    double total = 0.0;  // the sum of the weights
    double below = 0.0;  // the sum of the weights times the depths (negative)
    void add(double h, float weight) {
        const auto w = static_cast<double>(weight);
        const double w4 = dmath::powInteger(w, 4);
        above += w4 * dmath::powInteger(std::max(h, 0.0), 4);
        weights += w4;
        total += w;
        below += w * std::min(h, 0.0);
    }
    void scale(double k) {
        const double k4 = dmath::powInteger(k, 4);
        above *= k4;
        weights *= k4;
        total *= k;
        below *= k;
    }
    [[nodiscard]] double value() const {
        return weights > 0.0 ? total * std::sqrt(std::sqrt(above / weights)) + below : below;
    }
};

} // namespace terrain::dune_field
