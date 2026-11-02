#pragma once

// A detector of regular patterns along a profile (phase 12A): the rows of grooves, bands or teeth
// a noise or a lattice repeats at one spacing, shared by the tests of the rock.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace pattern_test {

// The largest autocorrelation of a profile (samples one step apart, NaN where it has none) at
// the lags from minLag to maxLag samples, over the pairs of samples it holds: near 1 for a row
// repeating at one of those lags, about 0 for an irregular one.
inline double peakCorrelation(const std::vector<double>& profile, std::size_t minLag,
                              std::size_t maxLag) {
    double sum = 0.0;
    std::size_t count = 0;
    for (const double v : profile) {
        if (!std::isnan(v)) {
            sum += v;
            ++count;
        }
    }
    if (count < 2) {
        return 0.0;
    }
    const double mean = sum / static_cast<double>(count);
    double peak = -1.0;
    for (std::size_t lag = minLag; lag <= maxLag && lag < profile.size(); ++lag) {
        double cross = 0.0;
        double left = 0.0;
        double right = 0.0;
        for (std::size_t i = 0; i + lag < profile.size(); ++i) {
            const double a = profile[i] - mean;
            const double b = profile[i + lag] - mean;
            if (std::isnan(a) || std::isnan(b)) {
                continue;
            }
            cross += a * b;
            left += a * a;
            right += b * b;
        }
        if (left > 0.0 && right > 0.0) {
            peak = std::max(peak, cross / std::sqrt(left * right));
        }
    }
    return peak;
}

// A profile less its mean over the window samples centered on each (those it holds): what varies
// faster than the window, the slow swells of the ground left out.
inline std::vector<double> detrended(const std::vector<double>& profile, std::size_t window) {
    std::vector<double> out(profile.size(), std::nan(""));
    const std::size_t half = window / 2;
    for (std::size_t i = 0; i < profile.size(); ++i) {
        if (std::isnan(profile[i])) {
            continue;
        }
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t j = i > half ? i - half : 0; j <= i + half && j < profile.size(); ++j) {
            if (!std::isnan(profile[j])) {
                sum += profile[j];
                ++count;
            }
        }
        out[i] = profile[i] - sum / static_cast<double>(count);
    }
    return out;
}

// The grooves of a profile of depths (samples step meters apart): the runs of samples deeper
// than least, their widths, their deepest depths and the spacings between their middles.
struct Grooves {
    std::vector<double> widths;
    std::vector<double> depths;
    std::vector<double> spacings;
};

inline Grooves groovesOf(const std::vector<double>& depths, double step, double least) {
    Grooves out;
    double lastMiddle = -1.0;
    std::size_t i = 0;
    while (i < depths.size()) {
        if (!(depths[i] > least)) {
            ++i;
            continue;
        }
        const std::size_t first = i;
        double deepest = 0.0;
        while (i < depths.size() && depths[i] > least) {
            deepest = std::max(deepest, depths[i]);
            ++i;
        }
        if (first == 0 || i == depths.size()) {
            continue; // cut by the ends of the profile
        }
        const double middle = 0.5 * static_cast<double>(first + i - 1) * step;
        out.widths.push_back(static_cast<double>(i - first) * step);
        out.depths.push_back(deepest);
        if (lastMiddle >= 0.0) {
            out.spacings.push_back(middle - lastMiddle);
        }
        lastMiddle = middle;
    }
    return out;
}

// The coefficient of variation of values: their standard deviation over their mean (0 for a row
// of equal values).
inline double variation(const std::vector<double>& values) {
    if (values.size() < 2) {
        return 0.0;
    }
    double mean = 0.0;
    for (const double v : values) {
        mean += v;
    }
    mean /= static_cast<double>(values.size());
    double spread = 0.0;
    for (const double v : values) {
        spread += (v - mean) * (v - mean);
    }
    return mean > 0.0 ? std::sqrt(spread / static_cast<double>(values.size())) / mean : 0.0;
}

} // namespace pattern_test
