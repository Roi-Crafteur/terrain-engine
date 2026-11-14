#include "dev_ab_cost.hpp"

#include "dev_commands.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

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
    if (costSteps(out.a).empty() || costSteps(out.b).empty()) {
        error = kCostUsage;
        return std::nullopt;
    }
    return out;
}

std::vector<std::string> costSteps(const std::string& side) {
    std::vector<std::string> steps;
    std::size_t from = 0;
    while (from <= side.size()) {
        const std::size_t end = std::min(side.find(';', from), side.size());
        if (std::string step = trimmed(side.substr(from, end - from)); !step.empty()) {
            steps.push_back(std::move(step));
        }
        from = end + 1;
    }
    return steps;
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
    lastFrame_ = now;
    sums_ = {};
    phases_.clear();
    return sideOf(0);
}

AbLimit limitOf(const AbPhase& phase) {
    const double busiest = std::max({phase.meanGpuMs, phase.meanGameMs, phase.meanRenderMs});
    if (phase.meanFrameMs > kAbWaitingRatio * busiest + kAbWaitingMs) {
        return AbLimit::Waiting;
    }
    if (busiest == phase.meanGpuMs) {
        return AbLimit::Gpu;
    }
    return busiest == phase.meanGameMs ? AbLimit::Game : AbLimit::Render;
}

const char* limitName(AbLimit limit) {
    switch (limit) {
    case AbLimit::Gpu:
        return "the GPU";
    case AbLimit::Game:
        return "the game thread";
    case AbLimit::Render:
        return "the render thread";
    case AbLimit::Waiting:
        break;
    }
    return "neither thread nor the GPU (waiting)";
}

std::optional<int> AbCost::addFrame(double now, const AbFrame& frame) {
    if (done()) {
        return std::nullopt;
    }
    const double intervalMs = (now - lastFrame_) * 1000.0;
    lastFrame_ = now;
    const double elapsed = now - phaseStart_;
    if (elapsed < params_.warmupSeconds) {
        return std::nullopt;
    }
    if (elapsed < params_.warmupSeconds + params_.measureSeconds) {
        sums_.gpu += frame.gpuMs;
        sums_.frame += intervalMs;
        sums_.game += frame.gameMs;
        sums_.render += frame.renderMs;
        sums_.foreground += frame.foreground ? 1.0 : 0.0;
        ++sums_.frames;
        return std::nullopt;
    }
    const double n = std::max(sums_.frames, 1);
    phases_.push_back({sideOf(phase_), sums_.gpu / n, sums_.frames, sums_.frame / n,
                       sums_.game / n, sums_.render / n,
                       sums_.frames > 0 ? sums_.foreground / n : 1.0});
    ++phase_;
    sums_ = {};
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
