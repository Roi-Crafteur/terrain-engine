#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace terrain::unreal {

// The measure of Terrain.Cost (agent 40, TerrainDevCost.cpp): the cost on the GPU of a switch of
// the look, two console commands A and B run in turn in the same window, so that its size and the
// view are the same for both. Each round is a pair of phases, its order flipped every other round
// (A B, B A, A B...): a slow drift of the GPU weighs on both alike. The first frames of a phase are
// thrown away (the caches of the shadows and the render states are rebuilt after a switch), then
// the GPU time of the frames is averaged over the rest of the phase.

struct AbCostParams {
    int rounds = 3;               // pairs of phases
    double warmupSeconds = 1.5;   // thrown away after each switch
    double measureSeconds = 4.0;  // averaged
};

// Terrain.Cost [<seconds> [rounds]] <command A> | <command B>: the seconds averaged per phase, the
// rounds, and the two commands (the arguments after the numbers joined by spaces, cut at "|").
struct CostCommand {
    AbCostParams params;
    std::string a;
    std::string b;
};
[[nodiscard]] std::optional<CostCommand> parseCost(std::span<const std::string> args,
                                                   std::string& error);

// The console commands of one side, its text cut at each ";" and run in order: one side can switch
// several things at once (the total of the photo gains). Empty pieces are dropped.
[[nodiscard]] std::vector<std::string> costSteps(const std::string& side);

// One frame as stat unit reads it, ms: the GPU, the game thread and the render thread; and whether
// the window of the play was in the foreground.
struct AbFrame {
    double gpuMs = 0.0;
    double gameMs = 0.0;
    double renderMs = 0.0;
    bool foreground = true;
};

struct AbPhase {
    int side = 0; // 0: A, 1: B
    double meanGpuMs = 0.0;
    int frames = 0;
    double meanFrameMs = 0.0; // from one frame to the next
    double meanGameMs = 0.0;
    double meanRenderMs = 0.0;
    double foreground = 1.0; // the share of its frames drawn with the window in the foreground
};

// What held the frames of a phase back: the busiest of the GPU, the game thread and the render
// thread, or none of them (Waiting) when a frame lasts much longer than all three (more than
// kAbWaitingRatio times the busiest plus kAbWaitingMs): the engine sleeps or waits. An editor in
// the background draws 3 frames a second (Editor Preferences, Performance, Use Less CPU when in
// Background); t.MaxFPS, the vertical sync or a stall also make it wait.
enum class AbLimit { Gpu, Game, Render, Waiting };
inline constexpr double kAbWaitingRatio = 1.5;
inline constexpr double kAbWaitingMs = 4.0;
[[nodiscard]] AbLimit limitOf(const AbPhase& phase);
[[nodiscard]] const char* limitName(AbLimit limit);

struct AbSummary {
    std::array<double, 2> mean{};   // the mean of the phases of A and of B, ms
    std::array<double, 2> spread{}; // their largest minus their smallest, ms
    double difference = 0.0;        // B - A, ms
    std::vector<AbPhase> phases;    // in their order
};

class AbCost {
public:
    explicit AbCost(const AbCostParams& params);

    // The side of a phase: A B B A A B...
    [[nodiscard]] static int sideOf(int phase);

    // Starts the first phase at this time: its side, to run now.
    int start(double now);

    // A frame drawn at this time with these times: the side to run now when a new phase starts
    // with it, nullopt otherwise (and once done).
    std::optional<int> addFrame(double now, const AbFrame& frame);

    [[nodiscard]] bool done() const { return phase_ >= 2 * params_.rounds; }
    [[nodiscard]] int phase() const { return phase_; }
    [[nodiscard]] const std::vector<AbPhase>& phases() const { return phases_; }
    [[nodiscard]] AbSummary summary() const;

private:
    AbCostParams params_;
    int phase_ = 0;
    double phaseStart_ = 0.0;
    double lastFrame_ = 0.0;
    struct Sums {
        double gpu = 0.0, frame = 0.0, game = 0.0, render = 0.0, foreground = 0.0;
        int frames = 0;
    } sums_;
    std::vector<AbPhase> phases_;
};

} // namespace terrain::unreal
