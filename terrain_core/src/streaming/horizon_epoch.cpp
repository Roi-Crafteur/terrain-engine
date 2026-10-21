#include "terrain/streaming/horizon_epoch.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terrain {

namespace {

// Pieces of whole rows of a rectangle, about pieceSamples samples each.
void addPieces(int level, const HorizonRect& rect, std::size_t pieceSamples,
               std::vector<HorizonPiece>& pieces) {
    if (rect.empty()) {
        return;
    }
    const auto width = static_cast<std::size_t>(rect.width());
    const auto rows = static_cast<std::int64_t>(std::max<std::size_t>(1, pieceSamples / width));
    for (std::int64_t z = rect.z0; z < rect.z1; z += rows) {
        pieces.push_back({level, {rect.x0, z, rect.x1, std::min(z + rows, rect.z1)}, {}});
    }
}

// Samples of a level, around the sample center, whose footprint (half a pitch around them) or
// whose neighbor's meets [low, high] along an axis.
std::pair<std::int64_t, std::int64_t> axisSamples(double low, double high, std::int64_t center,
                                                  int pitch, double period) {
    const double middle = 0.5 * (low + high);
    const double shift = nearestImage(middle, samplePosition(center, pitch), period) - middle;
    const double half = 0.5 * pitch;
    const auto first = static_cast<std::int64_t>(std::ceil((low + shift - half) / pitch)) - 1;
    const auto last = static_cast<std::int64_t>(std::floor((high + shift + half) / pitch)) + 1;
    return {first, last + 1};
}

} // namespace

std::int64_t HorizonEpoch::samples() const {
    std::int64_t total = 0;
    for (const HorizonPiece& piece : pieces) {
        total += piece.rect.count();
    }
    return total;
}

void computePiece(const HorizonSampler& sampler, HorizonPiece& piece) {
    piece.cells.resize(static_cast<std::size_t>(piece.rect.count()));
    sampler.fill(piece.level, piece.rect, piece.cells);
}

HorizonClipmap::HorizonClipmap(const HorizonParams& params) : params_(params) {
    validate(params_);
    levels_.reserve(static_cast<std::size_t>(params_.levels));
    for (int level = 0; level < params_.levels; ++level) {
        levels_.emplace_back(params_, level);
    }
}

std::optional<HorizonEpoch> HorizonClipmap::plan(double x, double z,
                                                 std::size_t pieceSamples) const {
    if (!std::isfinite(x) || !std::isfinite(z) || pieceSamples == 0) {
        throw std::invalid_argument("HorizonClipmap: invalid observer or piece size");
    }
    HorizonEpoch epoch;
    epoch.base = commits_;
    epoch.invalidations = dirty_.empty() ? 0 : dirty_.back().id;
    bool moves = false;
    for (int level = 0; level < levelCount(); ++level) {
        planLevel(level, x, z, pieceSamples, epoch);
        const HorizonLevel& current = levels_[static_cast<std::size_t>(level)];
        moves = moves || !current.placed() || !(current.center() == epoch.centers.back());
    }
    if (!moves && epoch.pieces.empty() && dirty_.empty()) {
        return std::nullopt;
    }
    return epoch;
}

void HorizonClipmap::planLevel(int level, double x, double z, std::size_t pieceSamples,
                               HorizonEpoch& epoch) const {
    const HorizonLevel& current = levels_[static_cast<std::size_t>(level)];
    const HorizonIndex target = canonicalCenter(params_, level, x, z);
    const int cells = params_.cellsPerSide;
    if (!current.placed()) {
        epoch.centers.push_back(target);
        addPieces(level, windowAround(target, cells), pieceSamples, epoch.pieces);
        return;
    }
    const HorizonIndex center = moveCenter(current.center(), target, current.counts());
    epoch.centers.push_back(center);
    const HorizonRect from = current.window();
    const HorizonRect to = windowAround(center, cells);
    for (const HorizonRect& rect : newSamples(from, to)) {
        addPieces(level, rect, pieceSamples, epoch.pieces);
    }
    const HorizonRect kept = intersection(from, to);
    for (const Area& area : dirty_) {
        addPieces(level, intersection(areaSamples(area, level, center), kept), pieceSamples,
                  epoch.pieces);
    }
}

HorizonRect HorizonClipmap::areaSamples(const Area& area, int level, HorizonIndex center) const {
    const int pitch = levelPitch(params_, level);
    const auto [x0, x1] = axisSamples(area.minX, area.maxX, center.x, pitch, params_.world.x);
    const auto [z0, z1] = axisSamples(area.minZ, area.maxZ, center.z, pitch, params_.world.z);
    return {x0, z0, x1, z1};
}

HorizonChanges HorizonClipmap::commit(const HorizonEpoch& epoch) {
    if (epoch.base != commits_ || epoch.centers.size() != levels_.size()) {
        throw std::invalid_argument("HorizonClipmap: the epoch was not planned after the last "
                                    "commit");
    }
    for (const HorizonPiece& piece : epoch.pieces) {
        if (piece.level < 0 || piece.level >= levelCount() ||
            static_cast<std::int64_t>(piece.cells.size()) != piece.rect.count()) {
            throw std::invalid_argument("HorizonClipmap: a piece of the epoch is not computed");
        }
    }
    HorizonChanges changes;
    std::vector<HorizonLevelChange> perLevel(levels_.size());
    for (std::size_t n = 0; n < levels_.size(); ++n) {
        HorizonLevel& level = levels_[n];
        perLevel[n].level = static_cast<int>(n);
        perLevel[n].moved = !level.placed() || !(level.center() == epoch.centers[n]);
        level.moveTo(epoch.centers[n]);
    }
    for (const HorizonPiece& piece : epoch.pieces) {
        const auto n = static_cast<std::size_t>(piece.level);
        levels_[n].write(piece.rect, piece.cells);
        perLevel[n].written.push_back(piece.rect);
    }
    for (HorizonLevelChange& change : perLevel) {
        if (change.moved || !change.written.empty()) {
            changes.levels.push_back(std::move(change));
        }
    }
    std::erase_if(dirty_, [&epoch](const Area& area) { return area.id <= epoch.invalidations; });
    ++commits_;
    return changes;
}

void HorizonClipmap::invalidate(double minX, double minZ, double maxX, double maxZ) {
    const bool finite =
        std::isfinite(minX) && std::isfinite(minZ) && std::isfinite(maxX) && std::isfinite(maxZ);
    if (!finite || maxX < minX || maxZ < minZ || maxX - minX >= params_.world.x ||
        maxZ - minZ >= params_.world.z) {
        throw std::invalid_argument("HorizonClipmap: invalid area");
    }
    dirty_.push_back({nextArea_++, minX, minZ, maxX, maxZ});
}

const HorizonLevel& HorizonClipmap::level(int level) const {
    if (level < 0 || level >= levelCount()) {
        throw std::out_of_range("HorizonClipmap: no such level");
    }
    return levels_[static_cast<std::size_t>(level)];
}

std::size_t HorizonClipmap::memoryBytes() const {
    std::size_t bytes = 0;
    for (const HorizonLevel& level : levels_) {
        bytes += level.memoryBytes();
    }
    return bytes;
}

} // namespace terrain
