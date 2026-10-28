#include "planet_blur.hpp"

#include "planet_parallel.hpp"
#include "terrain/generation/planet/planet_relief.hpp"

#include <algorithm>
#include <cstddef>

namespace terrain::planet {

namespace {

// Rows of a pass in place, a batch at a time: the along rows a batch needs, then its outputs.
constexpr int kBatch = 32;

// The along value of a pass for row k, from the values of row k (read before they change).
using AlongRow = std::function<void(int k, float* along)>;
// The output of a row from the along rows row - radius .. row + radius (wrapped), in that order.
using OutputRow = std::function<void(const std::vector<const float*>& rows, float* out)>;

// The z part of a separable pass, in place. The along rows of the radius first and last rows are
// computed first, from the values before any change: the last outputs wrap around to the first
// rows, rewritten by then. The other along rows pass through a ring of kBatch + 2 radius rows,
// each computed before its own row is rewritten. Needs 2 radius + 1 <= nz.
void zPassInPlace(const PlanetGrid& grid, std::vector<float>& values, int radius, int threads,
                  const AlongRow& along, const OutputRow& output) {
    const auto nx = static_cast<std::size_t>(grid.nx);
    const int nz = grid.nz;
    const int r = radius;
    std::vector<float> ends(static_cast<std::size_t>(2 * r) * nx);
    const auto endRow = [&](int k) {
        const int e = k < r ? k : r + k - (nz - r);
        return ends.data() + static_cast<std::size_t>(e) * nx;
    };
    parallelBlocks(2 * r, 1, threads, [&](int first, int last) {
        for (int q = first; q < last; ++q) {
            const int k = q < r ? q : nz - 2 * r + q;
            along(k, endRow(k));
        }
    });
    const int ringRows = kBatch + 2 * r;
    std::vector<float> ring(static_cast<std::size_t>(ringRows) * nx);
    const auto rowOf = [&](int k) -> float* {
        k = grid.wrapRow(k);
        if (k < r || k >= nz - r) {
            return endRow(k);
        }
        return ring.data() + static_cast<std::size_t>(k % ringRows) * nx;
    };
    int computed = r; // the middle rows [r, computed) have their along row
    for (int begin = 0; begin < nz; begin += kBatch) {
        const int end = std::min(nz, begin + kBatch);
        const int upTo = std::min(nz - r, end + r);
        if (upTo > computed) {
            const int from = computed;
            parallelBlocks(upTo - from, 1, threads, [&](int first, int last) {
                for (int q = first; q < last; ++q) {
                    along(from + q, rowOf(from + q));
                }
            });
            computed = upTo;
        }
        parallelBlocks(end - begin, 1, threads, [&](int first, int last) {
            std::vector<const float*> rows(static_cast<std::size_t>(2 * r + 1));
            for (int j = begin + first; j < begin + last; ++j) {
                for (int d = -r; d <= r; ++d) {
                    rows[static_cast<std::size_t>(d + r)] = rowOf(j + d);
                }
                output(rows, values.data() + static_cast<std::size_t>(j) * nx);
            }
        });
    }
}

// zPassInPlace on a grid too small for it: every along row first.
void zPassWhole(const PlanetGrid& grid, std::vector<float>& values, int radius, int threads,
                const AlongRow& along, const OutputRow& output) {
    const auto nx = static_cast<std::size_t>(grid.nx);
    std::vector<float> all(values.size());
    parallelRows(grid.nz, threads, [&](int begin, int end) {
        for (int k = begin; k < end; ++k) {
            along(k, all.data() + static_cast<std::size_t>(k) * nx);
        }
    });
    parallelRows(grid.nz, threads, [&](int begin, int end) {
        std::vector<const float*> rows(static_cast<std::size_t>(2 * radius + 1));
        for (int j = begin; j < end; ++j) {
            for (int d = -radius; d <= radius; ++d) {
                rows[static_cast<std::size_t>(d + radius)] =
                    all.data() + static_cast<std::size_t>(grid.wrapRow(j + d)) * nx;
            }
            output(rows, values.data() + static_cast<std::size_t>(j) * nx);
        }
    });
}

void zPass(const PlanetGrid& grid, std::vector<float>& values, int radius, int threads,
           const AlongRow& along, const OutputRow& output) {
    if (2 * radius + 1 <= grid.nz) {
        zPassInPlace(grid, values, radius, threads, along, output);
    } else {
        zPassWhole(grid, values, radius, threads, along, output);
    }
}

// Running box of 2 r + 1 cells along a periodic row of nx values value(i), into out: the sums of a
// running box pass, in its order.
template <class Value>
void runningRow(const PlanetGrid& grid, int r, const Value& value, double* out) {
    const double width = 2.0 * r + 1.0;
    double sum = 0.0;
    for (int d = -r; d <= r; ++d) {
        sum += value(grid.wrapColumn(d));
    }
    for (int i = 0; i < grid.nx; ++i) {
        out[i] = std::max(sum, 0.0) / width;
        sum += value(grid.wrapColumn(i + r + 1)) - value(grid.wrapColumn(i - r));
    }
}

// The running sums of a running box pass along z, one per column.
class ColumnSums {
public:
    ColumnSums(int nx, int r) : sum_(static_cast<std::size_t>(nx), 0.0), width_(2.0 * r + 1.0) {}

    void add(const double* row) {
        for (std::size_t i = 0; i < sum_.size(); ++i) {
            sum_[i] += row[i];
        }
    }
    void emit(double* out) const {
        for (std::size_t i = 0; i < sum_.size(); ++i) {
            out[i] = std::max(sum_[i], 0.0) / width_;
        }
    }
    void advance(const double* add, const double* sub) {
        for (std::size_t i = 0; i < sum_.size(); ++i) {
            sum_[i] += add[i] - sub[i];
        }
    }

private:
    std::vector<double> sum_;
    double width_;
};

// The two running box passes of the storm moisture, a row at a time (stormMoistureRows).
class StormSweep {
public:
    StormSweep(const PlanetGrid& grid, const std::vector<std::uint8_t>& ocean,
               const std::vector<double>& rowMoisture, int radius)
        : grid_(grid), ocean_(ocean), moisture_(rowMoisture), r_(radius),
          nx_(static_cast<std::size_t>(grid.nx)), add_(nx_), sub_(nx_), first_(nx_),
          saved_(static_cast<std::size_t>(2 * radius + 1) * nx_),
          ring_(static_cast<std::size_t>(2 * radius + 2) * nx_) {}

    void run(const StormRow& emit);

private:
    // The first pass along x of row k: the moisture of its sea cells.
    void seaRow(int k, double* out) const {
        const std::size_t start = grid_.index(0, k);
        const double sea = moisture_[static_cast<std::size_t>(k)];
        runningRow(
            grid_, r_,
            [&](int i) { return ocean_[start + static_cast<std::size_t>(i)] != kLand ? sea : 0.0; },
            out);
    }
    // The first pass along z: its column sums started, then the row k of its result (first_),
    // the sums advanced past it.
    ColumnSums startFirst();
    void firstRow(ColumnSums& sums, int k);
    // The second pass along x of the row k of the first pass, kept for the second along z: the
    // radius + 1 first rows and the radius last ones (the ends, its wrap), a ring for the others.
    [[nodiscard]] bool kept(int k) const { return k <= r_ || k >= grid_.nz - r_; }
    [[nodiscard]] double* keptRow(int k) {
        const int e = k <= r_ ? k : r_ + 1 + k - (grid_.nz - r_);
        return saved_.data() + static_cast<std::size_t>(e) * nx_;
    }
    [[nodiscard]] double* ringRow(int k) {
        return ring_.data() + static_cast<std::size_t>(k % (2 * r_ + 2)) * nx_;
    }

    const PlanetGrid& grid_;
    const std::vector<std::uint8_t>& ocean_;
    const std::vector<double>& moisture_;
    int r_;
    std::size_t nx_;
    std::vector<double> add_;
    std::vector<double> sub_;
    std::vector<double> first_;
    std::vector<double> saved_;
    std::vector<double> ring_;
};

ColumnSums StormSweep::startFirst() {
    ColumnSums sums(grid_.nx, r_);
    for (int d = -r_; d <= r_; ++d) {
        seaRow(grid_.wrapRow(d), add_.data());
        sums.add(add_.data());
    }
    return sums;
}

void StormSweep::firstRow(ColumnSums& sums, int k) {
    sums.emit(first_.data());
    if (k + 1 < grid_.nz) {
        seaRow(grid_.wrapRow(k + r_ + 1), add_.data());
        seaRow(grid_.wrapRow(k - r_), sub_.data());
        sums.advance(add_.data(), sub_.data());
    }
}

void StormSweep::run(const StormRow& emit) {
    const int nz = grid_.nz;
    const auto second = [&](double* out) {
        runningRow(grid_, r_, [&](int i) { return first_[static_cast<std::size_t>(i)]; }, out);
    };
    // First sweep: the ends of the second pass along x.
    ColumnSums sums = startFirst();
    for (int k = 0; k < nz; ++k) {
        firstRow(sums, k);
        if (kept(k)) {
            second(keptRow(k));
        }
    }
    // Second sweep: the first pass again, the second pass along z rows behind.
    sums = startFirst();
    ColumnSums seconds(grid_.nx, r_);
    for (int d = -r_; d <= r_; ++d) {
        seconds.add(keptRow(grid_.wrapRow(d)));
    }
    std::vector<double> out(nx_);
    const auto rowBefore = [&](int m) { return m >= 0 ? ringRow(m) : keptRow(m + nz); };
    for (int k = 0; k < nz; ++k) {
        firstRow(sums, k);
        second(ringRow(k));
        if (k >= r_ + 1) {
            const int t = k - r_ - 1;
            seconds.emit(out.data());
            emit(t, out);
            seconds.advance(ringRow(k), rowBefore(t - r_));
        }
    }
    for (int t = std::max(0, nz - r_ - 1); t < nz; ++t) {
        seconds.emit(out.data());
        emit(t, out);
        if (t + 1 < nz) {
            seconds.advance(keptRow(t + r_ + 1 - nz), rowBefore(t - r_));
        }
    }
}

} // namespace

void boxPassInPlace(const PlanetGrid& grid, std::vector<float>& values, int radius, int threads) {
    const auto nx = static_cast<std::size_t>(grid.nx);
    const double width = 2.0 * radius + 1.0;
    const AlongRow along = [&](int k, float* out) {
        const float* row = values.data() + static_cast<std::size_t>(k) * nx;
        for (int i = 0; i < grid.nx; ++i) {
            double sum = 0.0;
            for (int d = -radius; d <= radius; ++d) {
                sum += row[grid.wrapColumn(i + d)];
            }
            out[i] = static_cast<float>(sum / width);
        }
    };
    const OutputRow output = [&](const std::vector<const float*>& rows, float* out) {
        for (std::size_t i = 0; i < nx; ++i) {
            double sum = 0.0;
            for (const float* row : rows) {
                sum += row[i];
            }
            out[i] = static_cast<float>(sum / width);
        }
    };
    zPass(grid, values, radius, threads, along, output);
}

void blurPassInPlace(const PlanetGrid& grid, std::vector<float>& values, int threads) {
    const auto nx = static_cast<std::size_t>(grid.nx);
    const AlongRow along = [&](int k, float* out) {
        const float* row = values.data() + static_cast<std::size_t>(k) * nx;
        for (int i = 0; i < grid.nx; ++i) {
            const double sum = static_cast<double>(row[grid.wrapColumn(i - 1)]) + 2.0 * row[i] +
                               row[grid.wrapColumn(i + 1)];
            out[i] = static_cast<float>(0.25 * sum);
        }
    };
    const OutputRow output = [&](const std::vector<const float*>& rows, float* out) {
        for (std::size_t i = 0; i < nx; ++i) {
            const double sum = static_cast<double>(rows[0][i]) + 2.0 * rows[1][i] + rows[2][i];
            out[i] = static_cast<float>(0.25 * sum);
        }
    };
    zPass(grid, values, 1, threads, along, output);
}

void stormMoistureRows(const PlanetGrid& grid, const std::vector<std::uint8_t>& ocean,
                       const std::vector<double>& rowMoisture, int radius, const StormRow& row) {
    StormSweep(grid, ocean, rowMoisture, radius).run(row);
}

} // namespace terrain::planet
