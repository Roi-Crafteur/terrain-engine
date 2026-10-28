#include "terrain/streaming/horizon_sampler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace terrain {

namespace {

// Samples of one level: their pitch and their canonical world positions.
struct LevelFrame {
    int pitch = 1;
    HorizonIndex counts;

    [[nodiscard]] double x(std::int64_t i) const {
        return samplePosition(wrapIndex(i, counts.x), pitch);
    }
    [[nodiscard]] double z(std::int64_t k) const {
        return samplePosition(wrapIndex(k, counts.z), pitch);
    }
};

// Natural ground (no edit) of the samples of a rectangle, row by row.
struct GroundGrid {
    HorizonRect rect;
    std::vector<float> heights;
    std::vector<float> continentalness;
    std::vector<WaterKind> water;
    std::vector<float> waterLevel;

    [[nodiscard]] std::size_t index(std::int64_t i, std::int64_t k) const {
        return static_cast<std::size_t>((k - rect.z0) * rect.width() + (i - rect.x0));
    }
};

GroundGrid sampleGround(const Heightfield& field, const LevelFrame& frame,
                        const HorizonRect& rect) {
    GroundGrid grid{rect, {}, {}, {}, {}};
    const auto count = static_cast<std::size_t>(rect.count());
    grid.heights.reserve(count);
    grid.continentalness.reserve(count);
    grid.water.reserve(count);
    grid.waterLevel.reserve(count);
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        const double z = frame.z(k);
        for (std::int64_t i = rect.x0; i < rect.x1; ++i) {
            const HeightSample sample = field.sampleAt(frame.x(i), z);
            grid.heights.push_back(sample.height);
            grid.continentalness.push_back(sample.continentalness);
            grid.water.push_back(sample.water);
            grid.waterLevel.push_back(sample.waterLevel);
        }
    }
    return grid;
}

// Site of the ground at a sample from the heights of its 4 neighbors step meters away (east,
// west, south, north), with the water of the field there.
//
// A copy of the site of surfaceFromHeightfield (setSlopes and setWater in
// generation/terrain_surface.cpp): the same expressions, so that the same inputs give the same
// bits. horizon_sampler_test (testMaterialOracle) compares the materials and biomes of the
// horizon with Ecology::map(surfaceFromHeightfield(...)) bit for bit in several regions: it
// fails if the two ever diverge.
SurfaceSite groundSite(const GroundGrid& grid, std::size_t at, const std::array<float, 4>& h,
                       double step) {
    SurfaceSite site;
    site.ground = grid.heights[at];
    site.continentalness = grid.continentalness[at];
    const double run = 2.0 * step;
    site.gradX = static_cast<float>((static_cast<double>(h[0]) - h[1]) / run);
    site.gradZ = static_cast<float>((static_cast<double>(h[2]) - h[3]) / run);
    float difference = 0.0f;
    for (const float height : h) {
        difference = std::max(difference, std::abs(height - site.ground));
    }
    site.steepness = static_cast<float>(difference / step);
    site.water = grid.water[at];
    site.waterLevel = grid.waterLevel[at];
    return site;
}

// Material, biome and water of the sample (i, k) of the grid, from its natural ground.
void classify(const Heightfield& field, const Ecology& ecology, const LevelFrame& frame,
              const GroundGrid& grid, int slopeStep, std::int64_t i, std::int64_t k,
              HorizonCell& cell) {
    const double x = frame.x(i);
    const double z = frame.z(k);
    const std::size_t at = grid.index(i, k);
    std::array<float, 4> neighbors{};
    if (slopeStep == frame.pitch) {
        neighbors = {grid.heights[grid.index(i + 1, k)], grid.heights[grid.index(i - 1, k)],
                     grid.heights[grid.index(i, k + 1)], grid.heights[grid.index(i, k - 1)]};
    } else {
        neighbors = {field.heightAt(x + slopeStep, z), field.heightAt(x - slopeStep, z),
                     field.heightAt(x, z + slopeStep), field.heightAt(x, z - slopeStep)};
    }
    const SurfaceSite site = groundSite(grid, at, neighbors, slopeStep);
    const EcologyParams& params = ecology.params();
    const ClimateSample climate = ecology.climate().sample(x, z, site);
    cell.biome = classifyBiome(climate, site, params.biomes);
    cell.material = surfaceMaterial(cell.biome, climate, site, params.materials);
    cell.flags = static_cast<std::uint8_t>((site.dry() ? 0U : kHorizonWater) |
                                           (site.water == WaterKind::Ocean ? kHorizonSea : 0U));
    cell.waterLevel = site.dry() ? kNoWaterLevel : site.waterLevel;
}

// Raises the samples of the rectangle to the crests of their cells (their natural ground).
void raiseToCrests(const HorizonCrestSampler& crests, int level, const HorizonRect& rect,
                   const GroundGrid& grid, std::span<HorizonCell> cells) {
    if (!crests.raises(level)) {
        return;
    }
    std::vector<float> lifts(static_cast<std::size_t>(rect.count()));
    crests.fill(level, rect, grid.heights, lifts);
    std::size_t next = 0;
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i, ++next) {
            if (lifts[next] > 0.0f) {
                cells[grid.index(i, k)].height += lifts[next];
            }
        }
    }
}

} // namespace

void applyEditSummary(const EditSummary& summary, HorizonCell& cell) {
    cell.height += summary.heightChange;
    if (summary.material) {
        cell.material = *summary.material;
    }
    cell.flags = static_cast<std::uint8_t>(cell.flags | kHorizonEdited);
}

HorizonSampler::HorizonSampler(const Heightfield& field, const Ecology& ecology,
                               const HorizonParams& params, EditSummarySource edits)
    : field_(&field), ecology_(&ecology), params_(params), crests_(field, params),
      edits_(std::move(edits)) {
    validate(params_);
    if (!(field.world() == params_.world) || !(ecology.world() == params_.world)) {
        throw std::invalid_argument("HorizonSampler: the relief, the ecology and the horizon "
                                    "must share one world");
    }
}

void HorizonSampler::fill(int level, const HorizonRect& rect, std::span<HorizonCell> out) const {
    if (level < 0 || level >= params_.levels) {
        throw std::invalid_argument("HorizonSampler: no such level");
    }
    if (static_cast<std::int64_t>(out.size()) != rect.count()) {
        throw std::invalid_argument("HorizonSampler: the output does not fit the rectangle");
    }
    if (rect.empty()) {
        return;
    }
    const LevelFrame frame{levelPitch(params_, level), levelCounts(params_, level)};
    const int slopeStep = params_.materialSlopeStep == 0 ? frame.pitch : params_.materialSlopeStep;
    const HorizonRect margin{rect.x0 - 1, rect.z0 - 1, rect.x1 + 1, rect.z1 + 1};
    const GroundGrid grid = sampleGround(*field_, frame, margin);

    // The samples of the margin only need a height, for the gradients.
    std::vector<HorizonCell> cells(grid.heights.size());
    for (std::size_t n = 0; n < cells.size(); ++n) {
        cells[n].height = grid.heights[n];
    }
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i) {
            classify(*field_, *ecology_, frame, grid, slopeStep, i, k, cells[grid.index(i, k)]);
        }
    }
    raiseToCrests(crests_, level, rect, grid, cells);
    // The point heights and their edits, for the gradients: the lifts leave them out.
    std::vector<float> ground = grid.heights;
    if (edits_) {
        const double half = frame.pitch / 2.0;
        for (std::int64_t k = margin.z0; k < margin.z1; ++k) {
            for (std::int64_t i = margin.x0; i < margin.x1; ++i) {
                const std::optional<EditSummary> summary =
                    edits_(CellFootprint{level, frame.x(i), frame.z(k), half});
                if (summary) {
                    applyEditSummary(*summary, cells[grid.index(i, k)]);
                    ground[grid.index(i, k)] += summary->heightChange;
                }
            }
        }
    }
    const double run = 2.0 * frame.pitch;
    std::size_t next = 0;
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i) {
            HorizonCell cell = cells[grid.index(i, k)];
            const double east = ground[grid.index(i + 1, k)];
            const double south = ground[grid.index(i, k + 1)];
            cell.gradX = static_cast<float>((east - ground[grid.index(i - 1, k)]) / run);
            cell.gradZ = static_cast<float>((south - ground[grid.index(i, k - 1)]) / run);
            out[next++] = cell;
        }
    }
}

std::vector<HorizonCell> HorizonSampler::sample(int level, const HorizonRect& rect) const {
    std::vector<HorizonCell> cells(static_cast<std::size_t>(rect.count()));
    fill(level, rect, cells);
    return cells;
}

} // namespace terrain
