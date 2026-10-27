// A lake at the fine scale (fine_lake.hpp).
#include "fine_lake.hpp"

#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>

namespace terrain::water {

namespace {

// The nodes lie kMinStep meters apart, farther apart on a window wider than kMaxNodes of them.
constexpr double kMinStep = 64.0;
constexpr double kMaxNodes = 1024.0;
// The border of the squares of the lake is sampled kBorderSamples times along each side.
constexpr int kBorderSamples = 4;

constexpr float kNever = std::numeric_limits<float>::infinity();

// The nodes of a raster: node (i, j) at (x0 + i step, z0 + j step), in the images of the seeds.
struct Frame {
    double x0 = 0.0;
    double z0 = 0.0;
    double step = 1.0;
    int nx = 0;
    int nz = 0;
    double periodX = 0.0;
    double periodZ = 0.0;

    [[nodiscard]] double x(double i) const { return noise::wrapCanonical(x0 + step * i, periodX); }
    [[nodiscard]] double z(double j) const { return noise::wrapCanonical(z0 + step * j, periodZ); }
    [[nodiscard]] bool on(int i, int j) const { return i >= 0 && j >= 0 && i < nx && j < nz; }
    [[nodiscard]] std::size_t index(int i, int j) const {
        return static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) +
               static_cast<std::size_t>(i);
    }
};

// The relief at the nodes of a raster, where inside holds: sampled when the water first comes
// near a node, never over the rest of the window.
class Raster {
public:
    Raster(const Heightfield& relief, const Frame& f,
           const std::function<bool(double, double)>& inside)
        : frame(f), relief_(relief), inside_(inside),
          ground_(static_cast<std::size_t>(f.nx) * static_cast<std::size_t>(f.nz), kUnsampled) {}

    // Whether node (i, j) lies on the raster and inside.
    [[nodiscard]] bool has(int i, int j) {
        return frame.on(i, j) && ground(frame.index(i, j)) < kOutside;
    }
    // The relief at node k, kOutside when not inside.
    [[nodiscard]] float ground(std::size_t k) {
        if (!(ground_[k] < kUnsampled)) {
            const auto i = static_cast<int>(k % static_cast<std::size_t>(frame.nx));
            const auto j = static_cast<int>(k / static_cast<std::size_t>(frame.nx));
            const double x = frame.x(i);
            const double z = frame.z(j);
            ground_[k] = inside_(x, z) ? relief_.heightAt(x, z) : kOutside;
        }
        return ground_[k];
    }
    [[nodiscard]] std::size_t size() const { return ground_.size(); }

    Frame frame;

private:
    static constexpr float kOutside = std::numeric_limits<float>::max();
    static constexpr float kUnsampled = std::numeric_limits<float>::infinity();
    const Heightfield& relief_;
    const std::function<bool(double, double)>& inside_;
    std::vector<float> ground_;
};

// For every node, the lowest height the water of the seeds rises to on its way there (only below
// out, which falls to the lowest height at which the water would leave the raster).
std::vector<float> flood(Raster& r, const std::vector<std::size_t>& starts, float& out) {
    std::vector<float> spill(r.size(), kNever);
    using Item = std::pair<float, std::size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
    for (const std::size_t k : starts) {
        const float g = r.ground(k);
        if (g < out && g < spill[k]) {
            spill[k] = g;
            queue.push({g, k});
        }
    }
    const auto nx = static_cast<std::size_t>(r.frame.nx);
    while (!queue.empty()) {
        const auto [h, k] = queue.top();
        queue.pop();
        if (h > spill[k] || !(h < out)) {
            continue;
        }
        const auto i = static_cast<int>(k % nx);
        const auto j = static_cast<int>(k / nx);
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                if (!r.has(i + di, j + dj)) {
                    out = std::min(out, h); // the water leaves the raster here
                    continue;
                }
                const std::size_t m = r.frame.index(i + di, j + dj);
                const float rise = std::max(h, r.ground(m));
                if (rise < spill[m] && rise < out) {
                    spill[m] = rise;
                    queue.push({rise, m});
                }
            }
        }
    }
    return spill;
}

// The lowest relief along the border of the squares with a node under the lake (spill below
// lake), at most lake.
float borderLowest(const Heightfield& relief, const Frame& f, const std::vector<float>& spill,
                   float lake) {
    const auto wetNode = [&](int i, int j) { return f.on(i, j) && spill[f.index(i, j)] < lake; };
    const auto wetSquare = [&](int i, int j) {
        return wetNode(i, j) || wetNode(i + 1, j) || wetNode(i, j + 1) || wetNode(i + 1, j + 1);
    };
    // The square across each side, and the corner the side starts from.
    constexpr std::array<std::array<int, 4>, 4> kSides = {
        {{1, 0, 1, 0}, {-1, 0, 0, 0}, {0, 1, 0, 1}, {0, -1, 0, 0}}};
    float lowest = lake;
    for (int j = -1; j < f.nz; ++j) {
        for (int i = -1; i < f.nx; ++i) {
            if (!wetSquare(i, j)) {
                continue;
            }
            for (const auto& side : kSides) {
                if (wetSquare(i + side[0], j + side[1])) {
                    continue;
                }
                for (int s = 0; s <= kBorderSamples; ++s) {
                    const double t = static_cast<double>(s) / kBorderSamples;
                    const double a = i + side[2] + (side[0] != 0 ? 0.0 : t);
                    const double b = j + side[3] + (side[0] != 0 ? t : 0.0);
                    lowest = std::min(lowest, relief.heightAt(f.x(a), f.z(b)));
                }
            }
        }
    }
    return lowest;
}

} // namespace

FineLake::FineLake(const Heightfield& relief, const std::vector<Seed>& seeds, float level,
                   const LakeWindow& window, const std::function<bool(double, double)>& inside,
                   double periodX, double periodZ)
    : x0_(window.x0), z0_(window.z0), periodX_(periodX), periodZ_(periodZ) {
    step_ = std::max(kMinStep, std::max(window.x1 - window.x0, window.z1 - window.z0) / kMaxNodes);
    nx_ = static_cast<int>(std::ceil((window.x1 - window.x0) / step_)) + 1;
    nz_ = static_cast<int>(std::ceil((window.z1 - window.z0) / step_)) + 1;
    const Frame f{x0_, z0_, step_, nx_, nz_, periodX_, periodZ_};
    Raster r(relief, f, inside);
    std::vector<std::size_t> starts;
    for (const Seed& s : seeds) {
        const auto i = static_cast<int>(std::lround((s.x - x0_) / step_));
        const auto j = static_cast<int>(std::lround((s.z - z0_) / step_));
        if (r.has(i, j)) {
            starts.push_back(f.index(i, j));
        }
    }
    float lake = level;
    const std::vector<float> spill = flood(r, starts, lake);
    lake = borderLowest(relief, f, spill, lake);
    wet_.assign((spill.size() + 63) / 64, 0);
    bool any = false;
    for (std::size_t k = 0; k < spill.size(); ++k) {
        if (spill[k] < lake) {
            wet_[k >> 6] |= std::uint64_t{1} << (k & 63);
            any = true;
        }
    }
    level_ = any ? lake : -std::numeric_limits<float>::infinity();
}

bool FineLake::wet(int i, int j) const {
    if (i < 0 || j < 0 || i >= nx_ || j >= nz_) {
        return false;
    }
    const std::size_t k =
        static_cast<std::size_t>(j) * static_cast<std::size_t>(nx_) + static_cast<std::size_t>(i);
    return ((wet_[k >> 6] >> (k & 63)) & 1U) != 0;
}

bool FineLake::covers(double x, double z) const {
    if (wet_.empty()) {
        return false;
    }
    const double px = x0_ + noise::wrapCanonical(x - x0_, periodX_);
    const double pz = z0_ + noise::wrapCanonical(z - z0_, periodZ_);
    const auto i = static_cast<int>(std::floor((px - x0_) / step_));
    const auto j = static_cast<int>(std::floor((pz - z0_) / step_));
    return wet(i, j) || wet(i + 1, j) || wet(i, j + 1) || wet(i + 1, j + 1);
}

} // namespace terrain::water
