#include "terrain/generation/settlement/world_preset.hpp"

#include "settlement_random.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace terrain::settlement {

namespace {

int drawPresetCount(std::uint64_t seed, const char* stream, const PresetValue& v, double scale) {
    if (v.exact) {
        return static_cast<int>(v.min);
    }
    return drawCount(seed, stream, CountRange{static_cast<int>(v.min), static_cast<int>(v.max)},
                     scale);
}

std::string presetList(const SettlementConfig& config) {
    std::string list;
    for (const WorldPreset& p : config.presets) {
        list += (list.empty() ? "" : ", ") + p.id;
    }
    return list;
}

// One category of states: the exact counts, their rounding, and the share of each people.
struct Category {
    std::vector<double> share;
    std::vector<double> exact;
    std::vector<int> count;
    int total = 0;
};

Category roundCategory(const std::vector<double>& share, int total) {
    Category c;
    c.share = share;
    c.total = total;
    const double sum = std::accumulate(share.begin(), share.end(), 0.0);
    for (const double s : share) {
        c.exact.push_back(sum > 0.0 ? total * s / sum : 0.0);
        c.count.push_back(static_cast<int>(std::floor(c.exact.back() + 0.5)));
    }
    return c;
}

// Brings the sum of a category to its total: one more state to the people the farthest below its
// exact count, one fewer to the one the farthest above it that keeps a state in all; on a tie the
// smaller id keeps the priority (it gains first and loses last).
void correct(Category& c, const std::vector<int>& other, const std::vector<People>& peoples) {
    const auto better = [&](std::size_t a, std::size_t b, bool more) {
        const double ra = c.exact[a] - c.count[a];
        const double rb = c.exact[b] - c.count[b];
        if (ra != rb) {
            return more ? ra > rb : ra < rb;
        }
        return more ? peoples[a].id < peoples[b].id : peoples[a].id > peoples[b].id;
    };
    for (int sum = std::accumulate(c.count.begin(), c.count.end(), 0); sum != c.total;) {
        const bool more = sum < c.total;
        std::size_t best = peoples.size();
        for (std::size_t p = 0; p < peoples.size(); ++p) {
            const bool can = more ? c.share[p] > 0.0 : c.count[p] >= 1 && c.count[p] + other[p] >= 2;
            if (can && (best == peoples.size() || better(p, best, more))) {
                best = p;
            }
        }
        if (best == peoples.size()) {
            return;
        }
        c.count[best] += more ? 1 : -1;
        sum += more ? 1 : -1;
    }
}

} // namespace

ResolvedPreset resolvePreset(const SettlementConfig& config, const SettlementRequest& request,
                             std::uint64_t worldSeed, double scale) {
    const std::string id = request.preset.empty() ? config.defaultPreset : request.preset;
    const WorldPreset* preset = config.preset(id);
    if (preset == nullptr) {
        throw std::invalid_argument("Invalid preset '" + id + "' (expected one of: " +
                                    presetList(config) + ")");
    }
    ResolvedPreset r;
    r.preset = id;
    const PresetValue& share = preset->civilizedShare;
    r.civilizedShare = share.exact ? share.min
                                   : drawIn(streamOf(worldSeed, "civilizedShare"), 0,
                                            RealRange{share.min, share.max});
    r.majorKingdoms = drawPresetCount(worldSeed, "kingdoms", preset->majorKingdoms, scale);
    r.minorStates = drawPresetCount(worldSeed, "minorStates", preset->minorStates, scale);
    if (request.civilizedShare) {
        if (!(*request.civilizedShare >= 0.0 && *request.civilizedShare <= 1.0)) {
            throw std::invalid_argument("Invalid preset value: civilizedShare out of [0, 1]");
        }
        r.civilizedShare = *request.civilizedShare;
    }
    const auto force = [](const std::optional<int>& forced, int& value) {
        if (forced) {
            if (*forced < 0) {
                throw std::invalid_argument("Invalid preset value: a negative count of states");
            }
            value = *forced;
        }
    };
    force(request.majorKingdoms, r.majorKingdoms);
    force(request.minorStates, r.minorStates);
    return r;
}

std::vector<StateCount> allocateStates(const std::vector<People>& peoples, int majorKingdoms,
                                       int minorStates) {
    std::vector<double> majorShare;
    std::vector<double> minorShare;
    for (const People& p : peoples) {
        majorShare.push_back(p.majorKingdomShare);
        minorShare.push_back(p.minorStateShare);
    }
    Category major = roundCategory(majorShare, majorKingdoms);
    Category minor = roundCategory(minorShare, minorStates);
    for (std::size_t p = 0; p < peoples.size(); ++p) {
        if ((majorShare[p] > 0.0 || minorShare[p] > 0.0) && major.count[p] + minor.count[p] == 0) {
            (minorShare[p] > 0.0 ? minor : major).count[p] = 1;
        }
    }
    correct(major, minor.count, peoples);
    correct(minor, major.count, peoples);
    std::vector<StateCount> counts;
    for (std::size_t p = 0; p < peoples.size(); ++p) {
        counts.push_back({major.count[p], minor.count[p]});
    }
    return counts;
}

std::vector<StatePlan> planStates(const SettlementConfig& config, const ResolvedPreset& preset,
                                  const std::vector<People>& peoples,
                                  const std::vector<StateCount>& counts, std::size_t landCells,
                                  std::uint64_t worldSeed) {
    std::vector<std::size_t> order(peoples.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return peoples[a].id < peoples[b].id; });
    int majors = 0;
    int minors = 0;
    for (const StateCount& c : counts) {
        majors += c.major;
        minors += c.minor;
    }
    const StateRule& rule = config.states;
    const double land = static_cast<double>(landCells);
    const double civilized = preset.civilizedShare * land;
    const double majorArea =
        minors == 0 ? civilized : (majors == 0 ? 0.0 : rule.majorAreaShare * civilized);
    const double cap = std::max(1.0, rule.maxLandShare * land);
    const std::uint64_t stream = streamOf(worldSeed, "stateArea");
    std::vector<StatePlan> states;
    for (const StateRank rank : {StateRank::Major, StateRank::Minor}) {
        const bool major = rank == StateRank::Major;
        const int n = major ? majors : minors;
        const double each = n > 0 ? (major ? majorArea : civilized - majorArea) / n : 0.0;
        for (const std::size_t p : order) {
            for (int k = 0; k < (major ? counts[p].major : counts[p].minor); ++k) {
                const double u = unitDraw(stream, states.size());
                const double target = each * (1.0 + rule.areaSpread * (2.0 * u - 1.0));
                states.push_back({static_cast<int>(p), rank, std::clamp(target, 1.0, cap)});
            }
        }
    }
    return states;
}

} // namespace terrain::settlement
