#include "geography_grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace terrain::geography {

namespace {

constexpr double kFar = kNoSource;

// The squared distance transform along one line of n nodes spacing apart, periodic: values
// from in (stride apart) to out. The line is laid out over three periods so that the nodes of
// the middle one see their nearest sources across both ends.
void transformLine(const double* in, std::size_t stride, int n, double spacing, double* out,
                   std::vector<int>& v, std::vector<double>& z) {
    const int length = 3 * n;
    v.assign(static_cast<std::size_t>(length), 0);
    z.assign(static_cast<std::size_t>(length) + 1, 0.0);
    const auto value = [&](int q) { return in[static_cast<std::size_t>(q % n) * stride]; };
    int k = -1;
    for (int q = 0; q < length; ++q) {
        const double fq = value(q);
        if (!(fq < kFar)) {
            continue; // not a source
        }
        const double xq = q * spacing;
        double s = -kFar;
        while (k >= 0) {
            const double xv = v[static_cast<std::size_t>(k)] * spacing;
            s = ((fq + xq * xq) - (value(v[static_cast<std::size_t>(k)]) + xv * xv)) /
                (2.0 * (xq - xv));
            if (s > z[static_cast<std::size_t>(k)]) {
                break;
            }
            --k;
        }
        ++k;
        v[static_cast<std::size_t>(k)] = q;
        z[static_cast<std::size_t>(k)] = k == 0 ? -kFar : s;
        z[static_cast<std::size_t>(k) + 1] = kFar;
    }
    for (int p = n; p < 2 * n; ++p) {
        double& result = out[static_cast<std::size_t>(p - n) * stride];
        if (k < 0) {
            result = kFar;
            continue;
        }
        const double xp = p * spacing;
        int j = 0;
        while (j < k && z[static_cast<std::size_t>(j) + 1] < xp) {
            ++j;
        }
        const double dx = xp - v[static_cast<std::size_t>(j)] * spacing;
        result = dx * dx + value(v[static_cast<std::size_t>(j)]);
    }
}

} // namespace

Grid makeGrid(const WorldSize& world, double nominal) {
    const PeriodicCells x = periodicCells(world.x, nominal);
    const PeriodicCells z = periodicCells(world.z, nominal);
    Grid grid;
    grid.nx = static_cast<int>(x.count);
    grid.nz = static_cast<int>(z.count);
    grid.sizeX = x.size;
    grid.sizeZ = z.size;
    return grid;
}

Spline bspline(double t) {
    const double u = 1.0 - t;
    const double t2 = t * t;
    const double t3 = t2 * t;
    Spline s;
    s.w = {u * u * u / 6.0, (3.0 * t3 - 6.0 * t2 + 4.0) / 6.0,
           (-3.0 * t3 + 3.0 * t2 + 3.0 * t + 1.0) / 6.0, t3 / 6.0};
    s.dw = {-0.5 * u * u, 0.5 * (3.0 * t2 - 4.0 * t), 0.5 * (-3.0 * t2 + 2.0 * t + 1.0),
            0.5 * t2};
    return s;
}

Span spanOf(double v, double period, double size) {
    const double u = (v + 0.5 * period) / size;
    const double node = std::floor(u);
    return {static_cast<int>(node), u - node};
}

std::vector<double> distanceTransform(const Grid& grid, std::vector<double> seed) {
    std::vector<double> rows(seed.size());
    std::vector<int> v;
    std::vector<double> z;
    for (int j = 0; j < grid.nz; ++j) {
        const std::size_t first = grid.index(0, j);
        transformLine(seed.data() + first, 1, grid.nx, grid.sizeX, rows.data() + first, v, z);
    }
    for (int i = 0; i < grid.nx; ++i) {
        transformLine(rows.data() + i, static_cast<std::size_t>(grid.nx), grid.nz, grid.sizeZ,
                      seed.data() + i, v, z);
    }
    return seed;
}

std::vector<std::int32_t> components(const Grid& grid, const std::vector<std::uint8_t>& member,
                                     bool eightConnected, int& count) {
    static constexpr std::array<int, 8> kDi = {1, -1, 0, 0, 1, -1, 1, -1};
    static constexpr std::array<int, 8> kDj = {0, 0, 1, -1, 1, 1, -1, -1};
    const int neighbors = eightConnected ? 8 : 4;
    std::vector<std::int32_t> label(grid.nodes(), -1);
    std::vector<std::size_t> queue;
    count = 0;
    for (std::size_t start = 0; start < grid.nodes(); ++start) {
        if (member[start] == 0 || label[start] >= 0) {
            continue;
        }
        queue.assign(1, start);
        label[start] = count;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const int i = grid.column(queue[head]);
            const int j = grid.row(queue[head]);
            for (int k = 0; k < neighbors; ++k) {
                const std::size_t n = grid.index(i + kDi[static_cast<std::size_t>(k)],
                                                 j + kDj[static_cast<std::size_t>(k)]);
                if (member[n] != 0 && label[n] < 0) {
                    label[n] = count;
                    queue.push_back(n);
                }
            }
        }
        ++count;
    }
    return label;
}

void boxBlur(const Grid& grid, std::vector<float>& field, int radius) {
    if (radius <= 0) {
        return;
    }
    std::vector<float> along(field.size());
    const double norm = 1.0 / (2 * radius + 1);
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<float>& in = pass == 0 ? field : along;
        std::vector<float> out(field.size());
        for (int j = 0; j < grid.nz; ++j) {
            for (int i = 0; i < grid.nx; ++i) {
                double sum = 0.0;
                for (int d = -radius; d <= radius; ++d) {
                    sum += pass == 0 ? in[grid.index(i + d, j)] : in[grid.index(i, j + d)];
                }
                out[grid.index(i, j)] = static_cast<float>(sum * norm);
            }
        }
        (pass == 0 ? along : field) = std::move(out);
    }
}

float quantile(std::vector<float> values, double fraction) {
    if (values.empty()) {
        return 0.0f;
    }
    const auto rank = static_cast<std::size_t>(
        std::floor(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(rank),
                     values.end());
    return values[rank];
}

} // namespace terrain::geography
