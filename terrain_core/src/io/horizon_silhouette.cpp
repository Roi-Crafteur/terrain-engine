#include "terrain/io/horizon_silhouette.hpp"

#include "terrain/det_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace terrain {

namespace {

constexpr double kDegreesPerRadian = 57.295779513082320876798154814105;

using Rgb = std::array<std::uint8_t, 3>;

constexpr Rgb kSkyTop = {70, 120, 200};
constexpr Rgb kSkyLow = {200, 215, 235};
constexpr Rgb kNear = {125, 85, 50};
constexpr Rgb kFar = {150, 172, 205};
constexpr Rgb kTruthLine = {225, 25, 25};
constexpr Rgb kSeparatorRgb = {0, 0, 0};
constexpr Rgb kPaper = {250, 250, 250};
constexpr Rgb kGrid = {200, 200, 200};
constexpr Rgb kZero = {110, 110, 110};
constexpr std::array<Rgb, 3> kCurves = {{{235, 125, 25}, {35, 105, 220}, {40, 160, 70}}};

Rgb mix(const Rgb& a, const Rgb& b, double t) {
    Rgb color{};
    for (std::size_t c = 0; c < 3; ++c) {
        color[c] =
            static_cast<std::uint8_t>(std::clamp(a[c] + (b[c] - a[c]) * t + 0.5, 0.0, 255.0));
    }
    return color;
}

class Canvas {
public:
    Canvas(int width, int height)
        : image_{width, height, 3,
                 std::vector<std::uint8_t>(static_cast<std::size_t>(width) *
                                               static_cast<std::size_t>(height) * 3,
                                           0)} {}

    void set(int column, int row, const Rgb& color) {
        if (column < 0 || row < 0 || column >= image_.width || row >= image_.height) {
            return;
        }
        const auto at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(image_.width) +
                         static_cast<std::size_t>(column)) *
                        3;
        std::copy(color.begin(), color.end(), image_.pixels.begin() + static_cast<long>(at));
    }
    // A vertical run from row a to row b, both included, so that a line stays connected.
    void run(int column, int a, int b, const Rgb& color) {
        for (int row = std::min(a, b); row <= std::max(a, b); ++row) {
            set(column, row, color);
        }
    }
    void fillRows(int first, int count, const Rgb& color) {
        for (int row = first; row < first + count; ++row) {
            for (int column = 0; column < image_.width; ++column) {
                set(column, row, color);
            }
        }
    }
    [[nodiscard]] Image take() { return std::move(image_); }

private:
    Image image_;
};

// Rows of a band linear in the elevation angle: hi degrees on its first row, lo on its last.
struct AngleRows {
    int top;
    int height;
    double lo;
    double hi;

    [[nodiscard]] int rowOf(double degrees) const {
        const double t = std::clamp((hi - degrees) / (hi - lo), 0.0, 1.0);
        return top + static_cast<int>(std::lround(t * (height - 1)));
    }
};

Rgb groundColor(double distance) {
    const double t = std::clamp(
        dmath::log(std::max(distance, 1000.0) / 1000.0) / dmath::log(500.0), 0.0, 1.0);
    return mix(kNear, kFar, t);
}

void drawBand(Canvas& canvas, const Silhouette& silhouette, const Silhouette* truth,
              const AngleRows& rows) {
    int previous = -1;
    for (std::size_t c = 0; c < silhouette.tangent.size(); ++c) {
        const int column = static_cast<int>(c);
        const int surface = rows.rowOf(silhouetteDegrees(silhouette.tangent[c]));
        for (int row = rows.top; row < rows.top + rows.height; ++row) {
            const double sky = static_cast<double>(row - rows.top) / rows.height;
            canvas.set(column, row,
                       row < surface ? mix(kSkyTop, kSkyLow, sky)
                                     : groundColor(silhouette.distance[c]));
        }
        if (truth != nullptr) {
            const int line = rows.rowOf(silhouetteDegrees(truth->tangent[c]));
            canvas.run(column, previous < 0 ? line : previous, line, kTruthLine);
            previous = line;
        }
    }
}

void drawErrors(Canvas& canvas, const std::vector<Silhouette>& silhouettes, int top, int height,
                double range) {
    canvas.fillRows(top, height, kPaper);
    const AngleRows rows{top, height, -range, range};
    const int columns = static_cast<int>(silhouettes.front().tangent.size());
    for (int quarter = -4; quarter <= 4; ++quarter) {
        const int row = rows.rowOf(range * quarter / 4.0);
        for (int column = 0; column < columns; ++column) {
            canvas.set(column, row, quarter == 0 ? kZero : kGrid);
        }
    }
    for (std::size_t s = 1; s < silhouettes.size(); ++s) {
        const Rgb& color = kCurves[(s - 1) % kCurves.size()];
        int previous = -1;
        for (int column = 0; column < columns; ++column) {
            const auto c = static_cast<std::size_t>(column);
            const double difference = silhouetteDegrees(silhouettes[s].tangent[c]) -
                                      silhouetteDegrees(silhouettes.front().tangent[c]);
            const int row = rows.rowOf(difference);
            canvas.run(column, previous < 0 ? row : previous, row, color);
            previous = row;
        }
    }
}

} // namespace

double silhouetteDegrees(double tangent) {
    return std::isfinite(tangent) ? dmath::atan(tangent) * kDegreesPerRadian : -90.0;
}

SilhouetteError silhouetteError(const Silhouette& silhouette, const Silhouette& truth,
                                double minDistance, double maxDistance) {
    if (silhouette.tangent.size() != truth.tangent.size() ||
        truth.distance.size() != truth.tangent.size()) {
        throw std::invalid_argument("silhouetteError: silhouettes of different sizes");
    }
    std::vector<double> absolute;
    SilhouetteError error;
    for (std::size_t c = 0; c < truth.tangent.size(); ++c) {
        if (!(truth.distance[c] >= minDistance && truth.distance[c] < maxDistance) ||
            !std::isfinite(truth.tangent[c]) || !std::isfinite(silhouette.tangent[c])) {
            continue;
        }
        const double difference =
            silhouetteDegrees(silhouette.tangent[c]) - silhouetteDegrees(truth.tangent[c]);
        error.meanDegrees += difference;
        absolute.push_back(std::abs(difference));
    }
    error.columns = static_cast<int>(absolute.size());
    if (absolute.empty()) {
        return error;
    }
    error.meanDegrees /= static_cast<double>(absolute.size());
    std::sort(absolute.begin(), absolute.end());
    for (const double value : absolute) {
        error.meanAbsDegrees += value / static_cast<double>(absolute.size());
    }
    error.p95AbsDegrees = absolute[std::min(absolute.size() * 95 / 100, absolute.size() - 1)];
    error.maxAbsDegrees = absolute.back();
    return error;
}

Image renderSilhouettes(const std::vector<Silhouette>& silhouettes, int bandHeight,
                        double errorRangeDegrees) {
    if (silhouettes.empty() || silhouettes.front().tangent.empty() || bandHeight < 8 ||
        !(errorRangeDegrees > 0.0)) {
        throw std::invalid_argument("renderSilhouettes: no silhouette, column or band");
    }
    const Silhouette& truth = silhouettes.front();
    double lo = 90.0;
    double hi = -90.0;
    for (const Silhouette& silhouette : silhouettes) {
        if (silhouette.tangent.size() != truth.tangent.size() ||
            silhouette.distance.size() != truth.tangent.size()) {
            throw std::invalid_argument("renderSilhouettes: silhouettes of different sizes");
        }
    }
    for (const double tangent : truth.tangent) {
        if (std::isfinite(tangent)) {
            lo = std::min(lo, silhouetteDegrees(tangent));
            hi = std::max(hi, silhouetteDegrees(tangent));
        }
    }
    lo = std::min(lo, hi) - 1.0;
    hi += 1.0;
    constexpr int kGap = 2; // rows of the separators
    const int errorHeight = bandHeight * 2 / 3;
    const auto bands = static_cast<int>(silhouettes.size());
    Canvas canvas(static_cast<int>(truth.tangent.size()),
                  bands * (bandHeight + kGap) + errorHeight);
    for (int b = 0; b < bands; ++b) {
        const int top = b * (bandHeight + kGap);
        drawBand(canvas, silhouettes[static_cast<std::size_t>(b)], b == 0 ? nullptr : &truth,
                 {top, bandHeight, lo, hi});
        canvas.fillRows(top + bandHeight, kGap, kSeparatorRgb);
    }
    drawErrors(canvas, silhouettes, bands * (bandHeight + kGap), errorHeight, errorRangeDegrees);
    return canvas.take();
}

} // namespace terrain
