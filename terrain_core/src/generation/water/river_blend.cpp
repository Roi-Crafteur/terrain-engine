// The blend of the attributes of the valleys of a course, and the rises of its segments beyond
// their ends (river_profile.hpp).
#include "river_profile.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace terrain::water {

namespace {

// How fast a level comes down from a to b (meters per meter, 0 if it rises).
double secant(const CoursePoint& a, const CoursePoint& b, float CoursePoint::*level) {
    const double run = b.s - a.s;
    return run > 0.0 ? std::max(0.0, static_cast<double>(a.*level - b.*level)) / run : 0.0;
}

} // namespace

void ValleyBlend::add(const Valley& v) {
    if (!(v.height < lowest_ + kBlend)) {
        return;
    }
    double w = 1.0;
    if (v.height < lowest_) {
        // A new lowest: the weights of the others halve with their height above it.
        const double scale =
            weight_ > 0.0 ? dmath::exp2((v.height - lowest_) / kBlendHalving) : 0.0;
        weight_ *= scale;
        upX_ *= scale;
        upZ_ *= scale;
        slope_ *= scale;
        above_ *= scale;
        gorge_ *= scale;
        mountain_ *= scale;
        lowest_ = v.height;
    } else {
        w = dmath::exp2((lowest_ - v.height) / kBlendHalving);
    }
    weight_ += w;
    upX_ += w * v.upX;
    upZ_ += w * v.upZ;
    slope_ += w * v.slope;
    above_ += w * v.above;
    gorge_ += w * static_cast<double>(v.gorge);
    mountain_ += w * static_cast<double>(v.mountain);
}

void ValleyBlend::applyTo(Valley& v) const {
    if (!(weight_ > 0.0)) {
        return;
    }
    const double length = dmath::hypot(upX_, upZ_);
    if (length > 1e-9) {
        v.upX = upX_ / length;
        v.upZ = upZ_ / length;
    }
    v.slope = slope_ / weight_;
    v.above = above_ / weight_;
    v.gorge = static_cast<float>(gorge_ / weight_);
    v.mountain = static_cast<float>(mountain_ / weight_);
}

std::vector<float> risesOf(const std::vector<CoursePoint>& points, float CoursePoint::*level) {
    const std::size_t n = points.size();
    std::vector<float> rises(n > 0 ? n - 1 : 0);
    const double highest = n > 0 ? static_cast<double>(points.front().*level) : 0.0;
    const double lowest = n > 0 ? static_cast<double>(points.back().*level) : 0.0;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        double rise = std::max(kLeastRise, secant(points[i], points[i + 1], level));
        // Upstream of its start: no secant beyond the run where the highest water would rise
        // more slowly than kLeastRise.
        const double start = points[i].*level;
        for (std::size_t j = i; j-- > 0;) {
            const double run = points[i].s - points[j].s;
            if (kLeastRise * run >= highest - start) {
                break;
            }
            rise = std::max(rise, secant(points[j], points[i], level));
        }
        const double end = points[i + 1].*level;
        for (std::size_t j = i + 2; j < n; ++j) {
            const double run = points[j].s - points[i + 1].s;
            if (kLeastRise * run >= end - lowest) {
                break;
            }
            rise = std::max(rise, secant(points[i + 1], points[j], level));
        }
        rises[i] = static_cast<float>(rise);
    }
    return rises;
}

} // namespace terrain::water
