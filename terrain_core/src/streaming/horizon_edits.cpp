#include "terrain/streaming/horizon_edits.hpp"

#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terrain {

namespace {

constexpr int kSpacing = kEditColumnSpacing;
// Nearby columns share one area up to this extent; farther, one area per square of
// kAreaColumns columns.
constexpr double kOneAreaMeters = 4096.0;
constexpr std::int64_t kAreaColumns = 64;

std::uint64_t keyOf(std::int64_t x, std::int64_t z) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
           static_cast<std::uint32_t>(z);
}

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return q * b > a ? q - 1 : q;
}

// Sum of squares (or of columns) in a fixed order.
struct Sum {
    std::uint32_t edited = 0;
    double change = 0.0;
    float low = 0.0f;
    float high = 0.0f;

    void add(std::uint32_t count, double delta, float lowest, float highest) {
        if (count == 0) {
            return;
        }
        low = edited == 0 ? lowest : std::min(low, lowest);
        high = edited == 0 ? highest : std::max(high, highest);
        edited += count;
        change += delta;
    }
};

} // namespace

HorizonEditMap::HorizonEditMap(const Heightfield& field)
    : field_(&field), countX_(static_cast<std::int64_t>(field.world().x) / kSpacing),
      countZ_(static_cast<std::int64_t>(field.world().z) / kSpacing), squares_(kSquareLevels - 1) {}

void HorizonEditMap::stage(const TerrainTiles& tiles, std::span<const ChunkCoord> chunks) {
    stage(chunks, [&tiles](int minX, int maxX, int minZ, int maxZ) {
        return tiles.editWindow(minX, maxX, minZ, maxZ);
    });
}

void HorizonEditMap::stage(const ModificationStore& store, std::span<const ChunkCoord> chunks) {
    stage(chunks, [this, &store](int minX, int maxX, int minZ, int maxZ) {
        return EditWindow(store, minX, maxX, minZ, maxZ, world());
    });
}

void HorizonEditMap::stage(std::span<const ChunkCoord> chunks, const EditWindowSource& windows) {
    for (const EditedColumn& column : editedColumns(*field_, chunks, windows)) {
        staged_[column.column] = column.edited
                                     ? std::optional<Column>(Column{*column.edited, column.ground})
                                     : std::nullopt;
    }
}

std::vector<HorizonArea> HorizonEditMap::applyStaged() {
    std::vector<EditColumn> changed;
    for (const auto& [column, value] : staged_) {
        const std::uint64_t key = keyOf(column.x, column.z);
        const auto it = leaves_.find(key);
        if (value) {
            if (it != leaves_.end() && it->second == *value) {
                continue;
            }
            leaves_[key] = *value;
        } else {
            if (it == leaves_.end()) {
                continue;
            }
            leaves_.erase(it);
        }
        changed.push_back(column);
    }
    staged_.clear();
    std::vector<HorizonArea> areas = areasOf(changed);
    updateSquares(std::move(changed));
    return areas;
}

// The squares over the changed columns, level by level, each from its 4 children.
void HorizonEditMap::updateSquares(std::vector<EditColumn> changed) {
    for (int level = 1; level < kSquareLevels; ++level) {
        for (EditColumn& square : changed) {
            square = {floorDiv(square.x, 2), floorDiv(square.z, 2)};
        }
        std::sort(changed.begin(), changed.end());
        changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
        Squares& squares = squares_[static_cast<std::size_t>(level - 1)];
        for (const EditColumn& square : changed) {
            Sum sum;
            for (int dz = 0; dz < 2; ++dz) {
                for (int dx = 0; dx < 2; ++dx) {
                    const Square child = squareAt(level - 1, 2 * square.x + dx, 2 * square.z + dz);
                    sum.add(child.edited, child.change, child.low, child.high);
                }
            }
            const std::uint64_t key = keyOf(square.x, square.z);
            if (sum.edited == 0) {
                squares.erase(key);
            } else {
                squares[key] = {sum.edited, sum.change, sum.low, sum.high};
            }
        }
    }
}

// A square of a level by its canonical index (a column on level 0), empty where nothing is edited.
HorizonEditMap::Square HorizonEditMap::squareAt(int level, std::int64_t x, std::int64_t z) const {
    if (level == 0) {
        const auto it = leaves_.find(keyOf(x, z));
        if (it == leaves_.end()) {
            return {};
        }
        const Column& column = it->second;
        return {1, static_cast<double>(column.edited) - column.ground, column.edited,
                column.edited};
    }
    const Squares& squares = squares_[static_cast<std::size_t>(level - 1)];
    const auto it = squares.find(keyOf(x, z));
    return it == squares.end() ? Square{} : it->second;
}

FootprintEdits HorizonEditMap::footprint(const CellFootprint& footprint) const {
    const double side = 2.0 * footprint.halfSide / kSpacing; // columns per side
    const auto columns = static_cast<std::int64_t>(side);
    if (!(side >= 1.0) || static_cast<double>(columns) != side ||
        !std::has_single_bit(static_cast<std::uint64_t>(columns)) ||
        columns > (std::int64_t{1} << (kSquareLevels - 1)) || !std::isfinite(footprint.centerX) ||
        !std::isfinite(footprint.centerZ)) {
        throw std::invalid_argument("HorizonEditMap: the footprint is not a sample of a horizon "
                                    "of 8 m columns");
    }
    FootprintEdits result;
    result.columns = columns * columns;
    if (leaves_.empty()) {
        return result;
    }
    const auto cx = static_cast<std::int64_t>(std::floor(footprint.centerX / kSpacing));
    const auto cz = static_cast<std::int64_t>(std::floor(footprint.centerZ / kSpacing));
    Sum sum;
    if (columns == 1) {
        const Square square = squareAt(0, wrapIndex(cx, countX_), wrapIndex(cz, countZ_));
        sum.add(square.edited, square.change, square.low, square.high);
    } else {
        // 2 x 2 squares of half the side, the center on their common corner.
        const std::int64_t half = columns / 2;
        const int level = std::countr_zero(static_cast<std::uint64_t>(half));
        for (int dz = -1; dz <= 0; ++dz) {
            for (int dx = -1; dx <= 0; ++dx) {
                const Square square =
                    squareAt(level, wrapIndex(floorDiv(cx, half) + dx, countX_ / half),
                             wrapIndex(floorDiv(cz, half) + dz, countZ_ / half));
                sum.add(square.edited, square.change, square.low, square.high);
            }
        }
    }
    result.edited = sum.edited;
    result.change = sum.change;
    result.low = sum.low;
    result.high = sum.high;
    return result;
}

std::optional<EditSummary> HorizonEditMap::summary(const CellFootprint& footprint) const {
    const FootprintEdits edits = this->footprint(footprint);
    if (edits.edited == 0) {
        return std::nullopt;
    }
    EditSummary summary;
    summary.heightChange = static_cast<float>(edits.change / static_cast<double>(edits.columns));
    if (edits.edited == edits.columns) {
        summary.range = GroundRange{edits.low, edits.high};
    }
    return summary;
}

EditSummarySource HorizonEditMap::source() const {
    return [this](const CellFootprint& footprint) { return summary(footprint); };
}

std::optional<float> HorizonEditMap::columnGround(EditColumn column) const {
    const auto it = leaves_.find(keyOf(column.x, column.z));
    return it == leaves_.end() ? std::nullopt : std::optional<float>(it->second.edited);
}

std::size_t HorizonEditMap::heapBytes() const {
    constexpr std::size_t kWord = sizeof(void*);
    const auto table = [](const auto& map, std::size_t value) {
        return map.size() * (value + 2 * kWord) + map.bucket_count() * kWord;
    };
    std::size_t bytes = table(leaves_, sizeof(std::pair<const std::uint64_t, Column>));
    for (const Squares& squares : squares_) {
        bytes += table(squares, sizeof(std::pair<const std::uint64_t, Square>));
    }
    bytes +=
        staged_.size() * (sizeof(std::pair<const EditColumn, std::optional<Column>>) + 4 * kWord);
    return bytes;
}

void HorizonEditMap::validate(const HorizonParams& params) const {
    terrain::validate(params);
    if (!(params.world == world()) || params.basePitch < kSpacing ||
        params.basePitch % kSpacing != 0) {
        throw std::invalid_argument("HorizonEditMap: the horizon must share the world of the "
                                    "relief and sample it every 8 m or more");
    }
}

// The columns that changed, unwrapped around the first: one area if they lie close together,
// else one per square of kAreaColumns columns (aligned: never across the edge of the world).
std::vector<HorizonArea> HorizonEditMap::areasOf(const std::vector<EditColumn>& changed) const {
    if (changed.empty()) {
        return {};
    }
    const WorldSize& size = world();
    const double x0 = static_cast<double>(changed.front().x * kSpacing);
    const double z0 = static_cast<double>(changed.front().z * kSpacing);
    HorizonArea all{x0, z0, x0, z0};
    std::map<std::pair<std::int64_t, std::int64_t>, HorizonArea> squares;
    for (const EditColumn& column : changed) {
        const double x = static_cast<double>(column.x * kSpacing);
        const double z = static_cast<double>(column.z * kSpacing);
        const double ux = nearestImage(x, x0, size.x);
        const double uz = nearestImage(z, z0, size.z);
        all = {std::min(all.minX, ux), std::min(all.minZ, uz), std::max(all.maxX, ux),
               std::max(all.maxZ, uz)};
        const auto key =
            std::pair{floorDiv(column.x, kAreaColumns), floorDiv(column.z, kAreaColumns)};
        const auto it = squares.find(key);
        squares[key] =
            it == squares.end()
                ? HorizonArea{x, z, x, z}
                : HorizonArea{std::min(it->second.minX, x), std::min(it->second.minZ, z),
                              std::max(it->second.maxX, x), std::max(it->second.maxZ, z)};
    }
    if (all.maxX - all.minX <= kOneAreaMeters && all.maxZ - all.minZ <= kOneAreaMeters) {
        return {all};
    }
    std::vector<HorizonArea> areas;
    for (const auto& [key, area] : squares) {
        areas.push_back(area);
    }
    return areas;
}

} // namespace terrain
