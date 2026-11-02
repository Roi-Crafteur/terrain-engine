#include "terrain/streaming/horizon_sampler.hpp"

#include "horizon_blend.hpp"

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
    std::vector<float> rock; // HeightSample::rock

    [[nodiscard]] std::size_t index(std::int64_t i, std::int64_t k) const {
        return static_cast<std::size_t>((k - rect.z0) * rect.width() + (i - rect.x0));
    }
};

GroundGrid sampleGround(const Heightfield& field, const LevelFrame& frame,
                        const HorizonRect& rect) {
    GroundGrid grid{rect, {}, {}, {}, {}, {}};
    const auto count = static_cast<std::size_t>(rect.count());
    grid.heights.reserve(count);
    grid.continentalness.reserve(count);
    grid.water.reserve(count);
    grid.waterLevel.reserve(count);
    grid.rock.reserve(count);
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        const double z = frame.z(k);
        for (std::int64_t i = rect.x0; i < rect.x1; ++i) {
            const HeightSample sample = field.seenAt(frame.x(i), z, frame.pitch);
            grid.heights.push_back(sample.height);
            grid.continentalness.push_back(sample.continentalness);
            grid.water.push_back(sample.water);
            grid.waterLevel.push_back(sample.waterLevel);
            grid.rock.push_back(sample.rock);
        }
    }
    return grid;
}

// Site of the ground at a sample from the heights of its 4 neighbors step meters away (east,
// west, south, north), with the water of the field there (horizon::pointSite).
SurfaceSite groundSite(const GroundGrid& grid, std::size_t at, const std::array<float, 4>& h,
                       double step) {
    HeightSample sample;
    sample.height = grid.heights[at];
    sample.continentalness = grid.continentalness[at];
    sample.water = grid.water[at];
    sample.waterLevel = grid.waterLevel[at];
    return horizon::pointSite(sample, h, step);
}

// The height of the sample (i, k): the grid where it holds it, beyond it the field at its
// canonical position (the same bits).
float gridHeight(const Heightfield& field, const LevelFrame& frame, const GroundGrid& grid,
                 std::int64_t i, std::int64_t k) {
    return grid.rect.contains({i, k}) ? grid.heights[grid.index(i, k)]
                                      : field.seenAt(frame.x(i), frame.z(k), frame.pitch).height;
}

// The steepest rise, per meter, from the sample (i, k) to the samples one and two pitches away
// (the material is drawn between the samples of the level: a wall rising there takes the colour
// of this sample at its foot).
float riseAround(const Heightfield& field, const LevelFrame& frame, const GroundGrid& grid,
                 std::int64_t i, std::int64_t k) {
    const float ground = grid.heights[grid.index(i, k)];
    const auto pitch = static_cast<double>(frame.pitch);
    float rise = 0.0f;
    for (const auto& [di, dk] : {std::array<int, 2>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
        const float near = gridHeight(field, frame, grid, i + di, k + dk);
        const float far = gridHeight(field, frame, grid, i + 2 * di, k + 2 * dk);
        rise = std::max({rise, static_cast<float>(static_cast<double>(near - ground) / pitch),
                         static_cast<float>(static_cast<double>(far - ground) / (2.0 * pitch))});
    }
    return rise;
}

// Site and climate of the ground of the sample (i, k), from its natural ground: its neighbors
// slopeStep meters away, the samples of the grid where slopeStep is the pitch (gridHeight).
// Bare rock (SurfaceSite::bareRock): the reefs of the coasts, and the foot of the walls under the
// sea, the rise read over the pitches of the level; it enters the blend (the GPU horizon draws
// its colours from it, agent 29) and the footprints of the levels 4 and up.
struct PointClass {
    double x = 0.0;
    double z = 0.0;
    SurfaceSite site;
    ClimateSample climate;
};

PointClass pointClass(const Heightfield& field, const Ecology& ecology, const LevelFrame& frame,
                      const GroundGrid& grid, int slopeStep, std::int64_t i, std::int64_t k) {
    const double x = frame.x(i);
    const double z = frame.z(k);
    std::array<float, 4> neighbors{};
    if (slopeStep == frame.pitch) {
        neighbors = {gridHeight(field, frame, grid, i + 1, k),
                     gridHeight(field, frame, grid, i - 1, k),
                     gridHeight(field, frame, grid, i, k + 1),
                     gridHeight(field, frame, grid, i, k - 1)};
    } else {
        const double p = frame.pitch;
        neighbors = {field.seenAt(x + slopeStep, z, p).height,
                     field.seenAt(x - slopeStep, z, p).height,
                     field.seenAt(x, z + slopeStep, p).height,
                     field.seenAt(x, z - slopeStep, p).height};
    }
    const std::size_t at = grid.index(i, k);
    SurfaceSite site = groundSite(grid, at, neighbors, slopeStep);
    const ClimateSample climate = ecology.climate().sample(x, z, site);
    site.bareRock = grid.rock[at] > 0.5f ||
                    (mayPlunge(site) && barePlunge(site, riseAround(field, frame, grid, i, k),
                                                   ecology.params().materials.rockSlope));
    return {x, z, site, climate};
}

// Material, biome, point blend and water of a sample; returns its point blend. The biome and
// the material are slot 0 of the blend: classifyBiome and surfaceMaterial, the same bits
// (biome_blend.hpp), computed once.
SurfaceBlend classify(const Ecology& ecology, const PointClass& point, HorizonCell& cell) {
    const SurfaceSite& site = point.site;
    const SurfaceBlend blend = ecology.blendAt(point.x, point.z, site, point.climate);
    cell.biome = blend.biomes[0];
    cell.material = blend.materials[0];
    cell.blend = packBlend(blend);
    cell.flags = static_cast<std::uint8_t>((site.dry() ? 0U : kHorizonWater) |
                                           (site.water == WaterKind::Ocean ? kHorizonSea : 0U));
    cell.waterLevel = site.dry() ? kNoWaterLevel : site.waterLevel;
    return blend;
}

// Classifies the samples of the rectangle (the grid holds it and a margin of one sample). With
// footprint, their blends become the means over their footprints (horizon::footprintBlend) of
// the point blends of the 3 x 3 samples around them, the margin included.
void classifyRect(const Heightfield& field, const Ecology& ecology, const LevelFrame& frame,
                  const GroundGrid& grid, int slopeStep, const HorizonRect& rect,
                  bool footprint, std::span<HorizonCell> cells) {
    std::vector<SurfaceBlend> blends(footprint ? cells.size() : 0);
    const HorizonRect& classified = footprint ? grid.rect : rect;
    for (std::int64_t k = classified.z0; k < classified.z1; ++k) {
        for (std::int64_t i = classified.x0; i < classified.x1; ++i) {
            const PointClass point = pointClass(field, ecology, frame, grid, slopeStep, i, k);
            const std::size_t at = grid.index(i, k);
            if (rect.contains({i, k})) {
                const SurfaceBlend blend = classify(ecology, point, cells[at]);
                if (footprint) {
                    blends[at] = blend;
                }
            } else {
                blends[at] = ecology.blendAt(point.x, point.z, point.site, point.climate);
            }
        }
    }
    if (!footprint) {
        return;
    }
    for (std::int64_t k = rect.z0; k < rect.z1; ++k) {
        for (std::int64_t i = rect.x0; i < rect.x1; ++i) {
            std::array<const SurfaceBlend*, 9> around{};
            for (std::size_t n = 0; n < around.size(); ++n) {
                const auto di = static_cast<std::int64_t>(n % 3) - 1;
                const auto dk = static_cast<std::int64_t>(n / 3) - 1;
                around[n] = &blends[grid.index(i + di, k + dk)];
            }
            cells[grid.index(i, k)].blend = packBlend(horizon::footprintBlend(around));
        }
    }
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

float editedHeight(const EditSummary& summary, float height) {
    const float edited = height + summary.heightChange;
    return summary.range ? std::clamp(edited, summary.range->low, summary.range->high) : edited;
}

void applyEditSummary(const EditSummary& summary, HorizonCell& cell) {
    cell.height = editedHeight(summary, cell.height);
    if (summary.material) {
        cell.material = *summary.material;
        cell.blend = horizon::withMaterial(cell.blend, *summary.material);
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
    classifyRect(*field_, *ecology_, frame, grid, slopeStep, rect, level >= params_.blendFromLevel,
                 cells);
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
                    ground[grid.index(i, k)] = editedHeight(*summary, ground[grid.index(i, k)]);
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
