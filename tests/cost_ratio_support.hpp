#pragma once

// The ratio of two costs timed on a machine that may be loaded (water_cost_test): the same items
// timed without a feature and with it, pair by pair. In each round every item runs on both sides
// back to back, the side first alternating with the item and the round, so that both sides of an
// item see the same machine a few milliseconds apart; the best time of each item on each side is
// kept over the rounds, so that a slowdown spoils one time of one item and not a whole pass. The
// ratio is the sum of the best times with the feature over the sum of those without it.
//
// ratioAtMost: a round not timed (what is built on first use is built), then kRounds rounds and
// a first ratio; only past the limit, measured again: kRounds more rounds, at most kRetries
// times, the best times kept. A best time never falls below the true cost of its item: a true
// regression keeps the times with the feature high and still fails.
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cost_test {

enum class Side { Without, With };

// Runs an item on a side; returns the seconds it took.
using TimedRun = std::function<double(Side side, std::size_t item)>;

// The seconds work() takes on the calling thread.
template <class Work>
double secondsOf(const Work& work) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    work();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// The best seconds of each item on each side, over the rounds timed.
class BestTimes {
public:
    BestTimes(std::size_t items, TimedRun run)
        : run_(std::move(run)), without_(items, kNever), with_(items, kNever) {}

    // A round whose times are not kept.
    void warmUp() { round(false); }

    void addRounds(int count) {
        for (int n = 0; n < count; ++n) {
            round(true);
            ++timed_;
        }
    }

    // The sum of the best times with the feature over the sum of those without it (NaN without
    // any item).
    [[nodiscard]] double ratio() const {
        double without = 0.0;
        double with = 0.0;
        for (std::size_t i = 0; i < without_.size(); ++i) {
            without += without_[i];
            with += with_[i];
        }
        return with / without;
    }

    [[nodiscard]] int rounds() const { return timed_; }

private:
    static constexpr double kNever = 1e30;

    // Each item on both sides, Without first when the round and the item have the same parity.
    void round(bool keep) {
        for (std::size_t i = 0; i < without_.size(); ++i) {
            const bool withoutFirst = (static_cast<std::size_t>(started_) + i) % 2 == 0;
            for (const Side side : {withoutFirst ? Side::Without : Side::With,
                                    withoutFirst ? Side::With : Side::Without}) {
                const double seconds = run_(side, i);
                double& best = side == Side::Without ? without_[i] : with_[i];
                if (keep && seconds < best) {
                    best = seconds;
                }
            }
        }
        ++started_;
    }

    TimedRun run_;
    std::vector<double> without_;
    std::vector<double> with_;
    int started_ = 0; // rounds run, the warm-up included
    int timed_ = 0;
};

struct RatioPlan {
    int rounds = 5;  // per measure
    int retries = 2; // measures again past the limit
};

struct Ratio {
    std::vector<double> measured; // the first ratio, then each one measured again
    double most = 0.0;            // the highest ratio allowed

    [[nodiscard]] bool passed() const { return !measured.empty() && measured.back() <= most; }
};

// The ratio of the items with the feature over without it, measured again only past most.
inline Ratio ratioAtMost(std::size_t items, const TimedRun& run, double most,
                         RatioPlan plan = {}) {
    BestTimes best(items, run);
    best.warmUp();
    best.addRounds(plan.rounds);
    Ratio ratio{{best.ratio()}, most};
    for (int retry = 0; retry < plan.retries && !ratio.passed(); ++retry) {
        best.addRounds(plan.rounds);
        ratio.measured.push_back(best.ratio());
    }
    return ratio;
}

// "x2.03, measured again x1.52 (at most x2.0)".
inline std::string ratioText(const Ratio& ratio) {
    std::string text;
    char part[64];
    for (std::size_t n = 0; n < ratio.measured.size(); ++n) {
        std::snprintf(part, sizeof part, "x%.2f", ratio.measured[n]);
        text += n == 0 ? "" : n == 1 ? ", measured again " : ", ";
        text += part;
    }
    std::snprintf(part, sizeof part, " (at most x%.1f)", ratio.most);
    return text + part;
}

} // namespace cost_test
