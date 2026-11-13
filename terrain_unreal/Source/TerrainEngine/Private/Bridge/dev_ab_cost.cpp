#include "dev_ab_cost.hpp"

#include "dev_commands.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace terrain::unreal {

namespace {

constexpr const char* kCostUsage =
    "expected Terrain.Cost [<seconds, 0.5 to 60> [rounds, 1 to 10]] <command A> | <command B>";

std::string trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

} // namespace

std::optional<CostCommand> parseCost(std::span<const std::string> args, std::string& error) {
    CostCommand out;
    std::size_t next = 0;
    if (next < args.size()) {
        if (const std::optional<double> seconds = parseNumber(args[next])) {
            if (*seconds < 0.5 || *seconds > 60.0) {
                error = kCostUsage;
                return std::nullopt;
            }
            out.params.measureSeconds = *seconds;
            ++next;
        }
    }
    if (next == 1 && next < args.size()) {
        if (const std::optional<double> rounds = parseNumber(args[next])) {
            if (*rounds < 1.0 || *rounds > 10.0 || std::floor(*rounds) != *rounds) {
                error = kCostUsage;
                return std::nullopt;
            }
            out.params.rounds = static_cast<int>(*rounds);
            ++next;
        }
    }
    std::string joined;
    for (std::size_t i = next; i < args.size(); ++i) {
        joined += (i == next ? "" : " ") + args[i];
    }
    const std::size_t bar = joined.find('|');
    if (bar == std::string::npos || joined.find('|', bar + 1) != std::string::npos) {
        error = kCostUsage;
        return std::nullopt;
    }
    out.a = trimmed(joined.substr(0, bar));
    out.b = trimmed(joined.substr(bar + 1));
    if (out.a.empty() || out.b.empty()) {
        error = kCostUsage;
        return std::nullopt;
    }
    return out;
}

AbCost::AbCost(const AbCostParams& params) : params_(params) {}

int AbCost::sideOf(int phase) {
    const int round = phase / 2;
    const int second = phase % 2;
    return round % 2 == 0 ? second : 1 - second;
}

int AbCost::start(double now) {
    phase_ = 0;
    phaseStart_ = now;
    sum_ = 0.0;
    frames_ = 0;
    phases_.clear();
    return sideOf(0);
}

std::optional<int> AbCost::addFrame(double now, double gpuMs) {
    if (done()) {
        return std::nullopt;
    }
    const double elapsed = now - phaseStart_;
    if (elapsed < params_.warmupSeconds) {
        return std::nullopt;
    }
    if (elapsed < params_.warmupSeconds + params_.measureSeconds) {
        sum_ += gpuMs;
        ++frames_;
        return std::nullopt;
    }
    phases_.push_back({sideOf(phase_), frames_ > 0 ? sum_ / frames_ : 0.0, frames_});
    ++phase_;
    sum_ = 0.0;
    frames_ = 0;
    phaseStart_ = now;
    if (done()) {
        return std::nullopt;
    }
    return sideOf(phase_);
}

AbSummary AbCost::summary() const {
    AbSummary out;
    out.phases = phases_;
    for (int side = 0; side < 2; ++side) {
        double sum = 0.0;
        double low = 0.0;
        double high = 0.0;
        int count = 0;
        for (const AbPhase& p : phases_) {
            if (p.side != side) {
                continue;
            }
            low = count == 0 ? p.meanGpuMs : std::min(low, p.meanGpuMs);
            high = count == 0 ? p.meanGpuMs : std::max(high, p.meanGpuMs);
            sum += p.meanGpuMs;
            ++count;
        }
        const auto s = static_cast<std::size_t>(side);
        out.mean[s] = count > 0 ? sum / count : 0.0;
        out.spread[s] = high - low;
    }
    out.difference = out.mean[1] - out.mean[0];
    return out;
}

} // namespace terrain::unreal
