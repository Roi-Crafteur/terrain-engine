// The lips of a course: where along its edge the water above each lip falls (FallLip::wetFrom,
// wetTo), once per course (Hydrology::Impl::makeCourse).
#include "hydrology_impl.hpp"
#include "river_lips.hpp"
#include "river_profile.hpp"
#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace terrain {

namespace {

// The water above a lip falls from a place of its edge where, kWetBack meters upstream of it,
// the channel of the course holds water (Valley::wet, as the water of the ground takes it) nearer
// the level above the lip than the level below it
// (it may rise upstream of a receded edge), kWetDepth meters deep at least over the ground; and
// where the ground under the water falling from it (kWetAhead meters in front of the edge, of the
// foot of its water and halfway) stands kWetSlack meters above the water above at most: never
// over the walls of its slot. Its margin: the least of how far each holds (meters, above 0 there).
constexpr double kWetBack = 1.0;
constexpr double kWetDepth = 0.02;
constexpr double kWetAhead = 0.5;
constexpr double kWetSlack = 0.05;
// An end of the span between two points of the edge, where the margin falls to 0: found by
// kWetHalvings halvings of the way between them (to an eighth of it), always at a place the water
// falls from.
constexpr int kWetHalvings = 4;
// Where a place is tested: kWetBack upstream of it, then three places along the way its water
// falls.
constexpr std::size_t kAt = 4;

using TestPoints = std::array<std::array<double, 2>, kAt>;

// A place of the edge of a lip (x, z) and where its water meets the pool (footX, footZ), at a
// position along it in points of the edge.
struct EdgePlace {
    double x = 0.0;
    double z = 0.0;
    double footX = 0.0;
    double footZ = 0.0;
};

EdgePlace placeOf(const water::LipEdge& e, double at) {
    const double first = std::clamp(std::floor(at), 0.0, static_cast<double>(kFallEdge) - 2.0);
    const auto p = static_cast<std::size_t>(first);
    const double t = at - first;
    return {e.x[p] + t * (e.x[p + 1] - e.x[p]), e.z[p] + t * (e.z[p + 1] - e.z[p]),
            e.footX[p] + t * (e.footX[p + 1] - e.footX[p]),
            e.footZ[p] + t * (e.footZ[p + 1] - e.footZ[p])};
}

TestPoints testPoints(const water::FallLip& l, const EdgePlace& q) {
    const double dx = q.footX - q.x;
    const double dz = q.footZ - q.z;
    const double run = dmath::hypot(dx, dz);
    const double aheadX = run > 1e-6 ? dx / run : l.dirX;
    const double aheadZ = run > 1e-6 ? dz / run : l.dirZ;
    TestPoints out{};
    out[0] = {q.x - kWetBack * l.dirX, q.z - kWetBack * l.dirZ};
    for (std::size_t t = 1; t < kAt; ++t) {
        const double share = 0.5 * static_cast<double>(t - 1);
        out[t] = {q.x + share * dx + kWetAhead * aheadX, q.z + share * dz + kWetAhead * aheadZ};
    }
    return out;
}

// The test of the places of the edge of one lip, the valley of its course read through a box
// over the points of its edge (the same bits as without it); M: Hydrology::Impl.
template <typename M>
class LipTest {
public:
    LipTest(const M& m, const water::Course& c, const water::FallLip& l, const water::LipEdge& e)
        : m_(m), c_(c), l_(l), e_(e) {
        std::array<TestPoints, kFallEdge> points{};
        std::array<std::array<double, kAt>, kFallEdge> grounds{};
        std::array<double, 6> box{1e300, 1e300, -1e300, -1e300, 1e300, -1e300};
        for (std::size_t p = 0; p < kFallEdge; ++p) {
            points[p] = testPoints(l, placeOf(e, static_cast<double>(p)));
            for (std::size_t t = 0; t < kAt; ++t) {
                const std::array<double, 2>& a = points[p][t];
                grounds[p][t] = groundAt(a[0], a[1]);
                box = {std::min(box[0], a[0]), std::min(box[1], a[1]), std::max(box[2], a[0]),
                       std::max(box[3], a[1]), std::min(box[4], grounds[p][t]),
                       std::max(box[5], grounds[p][t])};
            }
        }
        bounds_ = box;
        near_ = water::boxCourse(c, box[0], box[1], box[2], box[3], box[4], box[5]);
        for (std::size_t p = 0; p < kFallEdge; ++p) {
            margins_[p] = marginOf(points[p], grounds[p]);
        }
    }

    [[nodiscard]] double margin(std::size_t p) const { return margins_[p]; }

    // The margin of the place at between two points of the edge (its sign: past the first test
    // it fails), its valley read through the box where it holds the point and its ground, by
    // itself elsewhere.
    [[nodiscard]] double marginAt(double at) const {
        const TestPoints points = testPoints(l_, placeOf(e_, at));
        std::array<double, kAt> grounds{};
        for (std::size_t t = 0; t < kAt; ++t) {
            grounds[t] = std::nan("");
        }
        return marginOf(points, grounds);
    }

private:
    [[nodiscard]] double groundAt(double x, double z) const {
        return static_cast<double>(m_.relief.heightAt(wrapCoordinate(x, m_.grid.periodX()),
                                                      wrapCoordinate(z, m_.grid.periodZ())));
    }

    // The margin of a place from its test points and the ground at them (NaN: not known yet),
    // its sign only past the first test that fails (the others are not run).
    [[nodiscard]] double marginOf(const TestPoints& points,
                                  const std::array<double, kAt>& grounds) const {
        double margin = 1e300;
        for (std::size_t t = 0; t < kAt && margin > 0.0; ++t) {
            const double x = points[t][0];
            const double z = points[t][1];
            const double ground = std::isnan(grounds[t]) ? groundAt(x, z) : grounds[t];
            const bool in = x >= bounds_[0] && x <= bounds_[2] && z >= bounds_[1] &&
                            z <= bounds_[3] && ground >= bounds_[4] && ground <= bounds_[5];
            const std::array<double, 3> s = m_.surfaceOf(c_, in ? &near_ : nullptr, x, z, ground);
            const double top = static_cast<double>(l_.top);
            const double middle = 0.5 * static_cast<double>(l_.top + l_.bottom);
            const double upstream = std::min(s[1] - kWetDepth - s[0], s[1] - middle);
            margin = t == 0 ? (s[2] > 0.0 ? upstream : -1.0)
                            : std::min(margin, top + kWetSlack - s[0]);
        }
        return margin;
    }

    const M& m_;
    const water::Course& c_;
    const water::FallLip& l_;
    const water::LipEdge& e_;
    std::array<double, 6> bounds_{};
    water::BoxCourse near_;
    std::array<double, kFallEdge> margins_{};
};

// The end of the span on the side of step (-1 or +1) of point p the water falls from: toward the
// next point (it does not), the farthest place found where it still does.
template <typename M>
double spanEnd(const LipTest<M>& test, std::size_t p, int step) {
    const long q = static_cast<long>(p) + step;
    const double at = static_cast<double>(p);
    if (q < 0 || q >= static_cast<long>(kFallEdge)) {
        return at;
    }
    double in = 0.0;
    double out = 1.0;
    for (int h = 0; h < kWetHalvings; ++h) {
        const double t = 0.5 * (in + out);
        (test.marginAt(at + step * t) > 0.0 ? in : out) = t;
    }
    return at + step * in;
}

} // namespace

// The surface of the valley of c at (x, z) (carved into the relief, or the relief), its water,
// and 1 in its channel (0 elsewhere), where the relief is ground meters high: through box (what of
// c may change the valley there, boxCourse: the same bits as without, faster) when given.
std::array<double, 3> Hydrology::Impl::surfaceOf(const water::Course& c,
                                                 const water::BoxCourse* box, double x, double z,
                                                 double ground) const {
    water::WallWarp warp(wallWarp, wrapCoordinate(x, grid.periodX()),
                         wrapCoordinate(z, grid.periodZ()), nullptr);
    const water::Valley v = water::valleyAt(c, x, z, ground, meanderSeed, warp, box);
    return {std::min(ground, v.height), static_cast<double>(v.level), v.wet ? 1.0 : 0.0};
}

void Hydrology::Impl::wetLips(water::Course& c) const {
    for (water::FallLip& l : c.lips) {
        // The foot of its water: the first point of the course down at the water below it.
        std::size_t j = l.point;
        while (j + 1 < c.points.size() && c.points[j].level > l.bottom + 0.05f) {
            ++j;
        }
        const water::CoursePoint& foot = c.points[j];
        const water::LipEdge e = water::lipEdge(c, l, foot.x, foot.z, foot.s - l.s);
        const LipTest<Impl> test(*this, c, l, e);
        std::size_t first = kFallEdge;
        std::size_t last = 0;
        for (std::size_t p = 0; p < kFallEdge; ++p) {
            if (test.margin(p) > 0.0) {
                first = std::min(first, p);
                last = p;
            }
        }
        if (first == kFallEdge) {
            l.wetFrom = 0.0f;
            l.wetTo = 0.0f;
            continue;
        }
        l.wetFrom = static_cast<float>(spanEnd(test, first, -1));
        l.wetTo = static_cast<float>(spanEnd(test, last, 1));
    }
}

} // namespace terrain
