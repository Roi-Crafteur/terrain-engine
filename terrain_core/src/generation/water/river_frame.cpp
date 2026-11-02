// The frame of the slots of the corridors of falls and what bounds it (river_winding.hpp).
#include "river_winding.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace terrain::water {

namespace {

// The bounds of the winding: measured every kBoundStep meters, with a margin of half the step at
// the fastest it comes aside plus kBoundMargin; its rate, with a step at the fastest it changes
// plus kRateMargin.
constexpr double kBoundStep = 1.0;
constexpr double kBoundMargin = 0.5;
constexpr double kRateMargin = 0.05;

double smoothOf(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The least and the most the slot of c lies left of its axis from from to to meters along it, and
// the most its frame stretches there (slotFrame).
struct Aside {
    double lo = 0.0;
    double hi = 0.0;
    double stretch = 1.0;
};
Aside asideOf(const Course& c, double from, double to) {
    double lo = 0.0;
    double hi = 0.0;
    double rate = 0.0;
    double bend = 0.0;
    double last = shiftAt(c, from).rate;
    for (double s = from; s <= to + kBoundStep; s += kBoundStep) {
        const SlotShift shift = shiftAt(c, std::min(s, to));
        lo = std::min(lo, shift.offset);
        hi = std::max(hi, shift.offset);
        rate = std::max(rate, std::abs(shift.rate));
        bend = std::max(bend, std::abs(shift.rate - last));
        last = shift.rate;
    }
    const double margin = 0.5 * kBoundStep * rate + kBoundMargin;
    const double fastest = std::min(rate + bend + kRateMargin, kRateCap);
    return {lo - margin, hi + margin, std::sqrt(1.0 + fastest * fastest)};
}

// The bounds of the windings of c (Course::windTable), from the start of the first to the end of
// the last.
void tableBounds(Course& c) {
    WindTable& t = c.windTable;
    t.levels.clear();
    if (c.windings.empty()) {
        return;
    }
    t.from = c.windings.front().from;
    const double to = c.windings.back().to;
    std::vector<float> bins;
    for (double u = t.from; u < to; u += kBoundBin) {
        const double end = std::min(u + kBoundBin, to);
        const Aside bin = asideOf(c, u, end);
        double inner = 0.0;
        for (const SlotWinding& w : c.windings) {
            inner = w.from <= end && w.to >= u ? std::max(inner, w.inner) : inner;
        }
        const double stretch = (1.0 + 1e-6) * bin.stretch;
        const double outer = frameOuter(std::max(-bin.lo, bin.hi), stretch, inner);
        bins.push_back(static_cast<float>(bin.lo));
        bins.push_back(static_cast<float>(bin.hi));
        bins.push_back(static_cast<float>(stretch));
        bins.push_back(static_cast<float>((1.0 + 1e-6) * outer));
    }
    const std::size_t n = bins.size() / 4;
    t.levels.push_back(std::move(bins));
    for (std::size_t span = 1; 2 * span <= n; span *= 2) {
        const std::vector<float>& below = t.levels.back();
        std::vector<float> level(below.size() - 4 * span);
        for (std::size_t k = 0; k < level.size(); k += 4) {
            level[k] = std::min(below[k], below[k + 4 * span]);
            for (std::size_t e = 1; e < 4; ++e) {
                level[k + e] = std::max(below[k + e], below[k + e + 4 * span]);
            }
        }
        t.levels.push_back(std::move(level));
    }
}

} // namespace

void boundWindings(Course& c) {
    for (SlotWinding& w : c.windings) {
        const Aside aside = asideOf(c, w.from, w.to);
        w.most = std::max(-aside.lo, aside.hi);
    }
    tableBounds(c);
}

WindBounds windBounds(const Course& c, double from, double to) {
    const WindTable& t = c.windTable;
    if (t.levels.empty() || !(to >= t.from)) {
        return {}; // off the corridors
    }
    const auto bins = static_cast<double>(t.levels[0].size() / 4);
    const double first = (std::max(from, t.from) - t.from) / kBoundBin;
    if (!(first < bins)) {
        return {};
    }
    // The bins from the first to the last (non-negative: truncated down).
    const auto i = static_cast<std::size_t>(first);
    const auto j = static_cast<std::size_t>(std::min((to - t.from) / kBoundBin, bins - 1.0));
    // Two spans of 2^level bins cover them.
    const auto level = static_cast<std::size_t>(std::bit_width(j - i + 1) - 1);
    const std::vector<float>& v = t.levels[level];
    const std::size_t k = 4 * (j + 1 - (std::size_t{1} << level));
    return {std::min({0.0, static_cast<double>(v[4 * i]), static_cast<double>(v[k])}),
            std::max({0.0, static_cast<double>(v[4 * i + 1]), static_cast<double>(v[k + 1])}),
            std::max(static_cast<double>(v[4 * i + 2]), static_cast<double>(v[k + 2])),
            std::max(static_cast<double>(v[4 * i + 3]), static_cast<double>(v[k + 3]))};
}

SlotFrame slotFrame(const Course& c, double s, double side) {
    if (!(std::abs(side) < windAt(c, s).outer)) {
        return {side, 1.0}; // the walls of its valley on the straight axis
    }
    const SlotShift shift = shiftAt(c, s);
    const double rate = std::min(std::abs(shift.rate), kRateCap);
    const double stretch = std::sqrt(1.0 + rate * rate);
    return {frameSide(shift.offset, stretch, shift.inner, side), stretch};
}

double frameOuter(double offset, double stretch, double inner) {
    const double w = std::abs(offset);
    return w + inner * stretch + std::max(w + kFadeBand, kFadeShare * w);
}

double frameSide(double offset, double stretch, double inner, double side) {
    const double w = std::abs(offset);
    const double from = w + inner * stretch;
    const double away = std::abs(side);
    if (!(away > from)) {
        return (side - offset) / stretch;
    }
    const double band = std::max(w + kFadeBand, kFadeShare * w);
    if (!(away < from + band)) {
        return side;
    }
    // Both the offset and the stretch fade out (a smooth step, its slope 1.5 / band at most): its
    // derivative never below (1 - 1.5 w / band) / stretch > 0, it grows all the way (no fold).
    const double keep = 1.0 - smoothOf((away - from) / band);
    return (side - offset * keep) / (1.0 + (stretch - 1.0) * keep);
}

} // namespace terrain::water
