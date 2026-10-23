#include "terrain/io/relief_rifts.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/geography.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace terrain {

namespace {

// The nodes of the map whose trough is at least kTroughCore, on land, 8-connected on the torus:
// the components, largest first (then by their first node).
constexpr float kTroughCore = 0.9f;

std::vector<std::vector<int>> troughs(const Geography& g) {
    const int nx = g.columns();
    const int nz = g.rows();
    const auto in = [&g](int i, int j) {
        return g.node(i, j, Geography::kRift) >= kTroughCore &&
               g.node(i, j, Geography::kCoast) > 0.0f;
    };
    std::vector<int> seen(static_cast<std::size_t>(nx) * static_cast<std::size_t>(nz), 0);
    std::vector<std::vector<int>> out;
    for (int j = 0; j < nz; ++j) {
        for (int i = 0; i < nx; ++i) {
            if (seen[static_cast<std::size_t>(j * nx + i)] != 0 || !in(i, j)) {
                continue;
            }
            std::vector<int> members;
            std::vector<int> stack{j * nx + i};
            seen[static_cast<std::size_t>(j * nx + i)] = 1;
            while (!stack.empty()) {
                const int k = stack.back();
                stack.pop_back();
                members.push_back(k);
                for (int dj = -1; dj <= 1; ++dj) {
                    for (int di = -1; di <= 1; ++di) {
                        const int a = ((k % nx + di) % nx + nx) % nx;
                        const int b = ((k / nx + dj) % nz + nz) % nz;
                        const auto next = static_cast<std::size_t>(b * nx + a);
                        if (seen[next] == 0 && in(a, b)) {
                            seen[next] = 1;
                            stack.push_back(b * nx + a);
                        }
                    }
                }
            }
            std::sort(members.begin(), members.end());
            out.push_back(std::move(members));
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const auto& a, const auto& b) { return a.size() > b.size(); });
    return out;
}

// The direction across the trough at (x, z): the one of 18 in which it falls fastest over 100 km.
double acrossAngle(const Geography& g, double x, double z) {
    double best = 0.0;
    double bestDrop = -1.0;
    for (int a = 0; a < 18; ++a) {
        const double angle = a / 36.0; // turns
        const double c = 100'000.0 * dmath::cosTurns(angle);
        const double s = 100'000.0 * dmath::sinTurns(angle);
        const double drop = 2.0 * static_cast<double>(g.sampleAt(x, z).rift) -
                            static_cast<double>(g.sampleAt(x + c, z + s).rift) -
                            static_cast<double>(g.sampleAt(x - c, z - s).rift);
        if (drop > bestDrop) {
            bestDrop = drop;
            best = angle;
        }
    }
    return best;
}

RiftProfile measure(const Heightfield& field, const Geography& g, int node, int nodes) {
    const double sx = g.world().x / g.columns();
    const double sz = g.world().z / g.rows();
    RiftProfile r;
    r.nodes = nodes;
    r.x = -0.5 * g.world().x + (node % g.columns()) * sx - g.anchorX();
    r.z = -0.5 * g.world().z + (node / g.columns()) * sz;
    const double angle = acrossAngle(g, r.x, r.z);
    r.acrossX = dmath::cosTurns(angle);
    r.acrossZ = dmath::sinTurns(angle);
    const int half = static_cast<int>(kRiftSpan / kRiftStep);
    std::vector<float> center;
    float low = -1.0e9f;
    float high = -1.0e9f;
    double atLow = 0.0;
    double atHigh = 0.0;
    for (int k = -half; k <= half; ++k) {
        const double d = k * kRiftStep;
        const float h = field.heightAt(r.x + d * r.acrossX, r.z + d * r.acrossZ);
        r.heights.push_back(h);
        if (std::abs(d) <= 10'000.0) {
            center.push_back(h);
        }
        float& rim = k < 0 ? low : high;
        double& at = k < 0 ? atLow : atHigh;
        if (k != 0 && h > rim) {
            rim = h;
            at = d;
        }
    }
    std::sort(center.begin(), center.end());
    r.floor = center[center.size() / 2];
    for (std::size_t k = r.heights.size() / 2; k < r.heights.size(); ++k) {
        if (r.heights[k] >= r.floor + kRiftRise) {
            r.wall = static_cast<double>(k - r.heights.size() / 2) * kRiftStep;
            break;
        }
    }
    r.rimLow = std::min(low, high);
    r.rimHigh = std::max(low, high);
    r.width = atHigh - atLow;
    return r;
}

} // namespace

std::vector<RiftProfile> measureRifts(const Heightfield& field, int count) {
    std::vector<RiftProfile> out;
    const Geography* g = field.geography();
    if (g == nullptr) {
        return out;
    }
    const std::vector<std::vector<int>> all = troughs(*g);
    for (std::size_t k = 0; k < all.size() && static_cast<int>(k) < count; ++k) {
        out.push_back(measure(field, *g, all[k][all[k].size() / 2],
                              static_cast<int>(all[k].size())));
    }
    return out;
}

} // namespace terrain
