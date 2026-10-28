#pragma once

#include "terrain/generation/biome.hpp"
#include "terrain/generation/ecology.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/surface_material.hpp"
#include "terrain/streaming/horizon_crests.hpp"
#include "terrain/streaming/horizon_levels.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace terrain {

// Flags of a horizon sample.
inline constexpr std::uint8_t kHorizonWater = 1U << 0;  // under water: the sea, a lake, a river
inline constexpr std::uint8_t kHorizonEdited = 1U << 1; // changed by a summary of player edits
// Under the sea, where the geography puts it (HeightSample::water), whatever the height: a dry
// depression below the sea level is not the sea.
inline constexpr std::uint8_t kHorizonSea = 1U << 2;

// What the far horizon holds at a sample of a level: 20 bytes, for compact level buffers.
struct HorizonCell {
    // Top of the ground, meters: Heightfield::heightAt at the sample, bit for bit, plus its
    // lift to the crests of its cells from level 1 on (HorizonCrestSampler, when it rises), then
    // the height change of the player edits. Without lift, sample J of level n + 1 is sample 2J
    // of level n: the same bits on both levels (always so with HorizonParams::crestRatio 0).
    float height = 0.0f;
    // Gradient of the ground at the scale of the level (rise over run), by central differences
    // of the point heights (and edits) of its neighbor samples, without their lifts: the normal
    // is normalize(-gradX, 1, -gradZ).
    float gradX = 0.0f;
    float gradZ = 0.0f;
    // Material and biome of the ground, as the Ecology classifies a surface sampled every
    // HorizonParams::materialSlopeStep meters (the same on every level by default).
    SurfaceMaterial material = SurfaceMaterial::Water;
    Biome biome = Biome::Ocean;
    std::uint8_t flags = 0; // kHorizonWater, kHorizonEdited, kHorizonSea
    std::uint8_t reserved = 0;
    // The level of the water over the sample (HeightSample::waterLevel, bit for bit: the sea at
    // kSeaLevel, a lake at its level, a river at its surface), kNoWaterLevel where it is dry:
    // present exactly where kHorizonWater is. Neither the lifts of the crests nor the edits change
    // it, so a lifted ground may stand above its water.
    float waterLevel = kNoWaterLevel;

    friend bool operator==(const HorizonCell&, const HorizonCell&) = default;
};
static_assert(sizeof(HorizonCell) == 20, "a horizon cell holds 20 bytes");

// Square of the world a sample stands for: its canonical position (the center) and half the
// pitch of its level. A footprint on an edge of the world reaches across it.
struct CellFootprint {
    int level = 0;
    double centerX = 0.0;
    double centerZ = 0.0;
    double halfSide = 0.0;
};

// Summary of the player edits within a footprint, for the far terrain. Provisional: the
// summaries of the edit masks of the world format come later (phase 15).
struct EditSummary {
    float heightChange = 0.0f;               // mean change of the ground, meters
    std::optional<SurfaceMaterial> material; // what the edits leave on top, if they decide it
};

// Summary of the edits within a footprint, or nullopt where nothing was edited. Called from the
// threads computing the samples, concurrently: it must be thread-safe, and deterministic for the
// horizon to be. It handles the footprints across the edges of the world.
using EditSummarySource = std::function<std::optional<EditSummary>(const CellFootprint&)>;

// Applies a summary to a sample: its height changes, its material is replaced if the summary
// decides it, and it is flagged as edited.
void applyEditSummary(const EditSummary& summary, HorizonCell& cell);

// Samples of the horizon levels from the relief and the ecology of a world (TerrainTiles gives
// both). A sample is a pure function of the seed, the parameters, its level and its canonical
// index: neither the observer, nor the rectangle it is computed in, nor the thread change it.
//
// The ground of a sample is the Heightfield at its canonical position, raised to the crests of
// its cells (HorizonCrestSampler) from level 1 on unless crestRatio is 0. Its material and biome
// come from a SurfaceSite built as surfaceFromHeightfield builds it at a spacing of
// materialSlopeStep (the slopes of the 4 ground samples around it), then classified as
// Ecology::sampleAt does (climate, biome, material); the water is the ocean of that site. With
// edits, every sample of the rectangle and of its one-sample margin gets its summary before the
// gradients are taken: the edited heights shape the normals.
class HorizonSampler {
public:
    // field and ecology must outlive the sampler and share the world of params. Throws
    // std::invalid_argument for invalid parameters (validate) or another world.
    HorizonSampler(const Heightfield& field, const Ecology& ecology, const HorizonParams& params,
                   EditSummarySource edits = {});

    // Samples of the rectangle of a level, row by row (x varies fastest), into out, which holds
    // rect.count() cells. Thread-safe. Throws std::invalid_argument for a level out of range or
    // an output of another size.
    void fill(int level, const HorizonRect& rect, std::span<HorizonCell> out) const;
    [[nodiscard]] std::vector<HorizonCell> sample(int level, const HorizonRect& rect) const;

    [[nodiscard]] const HorizonParams& params() const { return params_; }
    [[nodiscard]] const Heightfield& heightfield() const { return *field_; }
    [[nodiscard]] const Ecology& ecology() const { return *ecology_; }
    [[nodiscard]] bool hasEdits() const { return static_cast<bool>(edits_); }

private:
    const Heightfield* field_;
    const Ecology* ecology_;
    HorizonParams params_;
    HorizonCrestSampler crests_;
    EditSummarySource edits_;
};

} // namespace terrain
