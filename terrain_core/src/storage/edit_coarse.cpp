#include "terrain/storage/edit_coarse.hpp"

#include "edit_coarse_cells.hpp"
#include "terrain/det_math.hpp"

#include <algorithm>
#include <cassert>
#include <limits>

namespace terrain {

using coarse_cells::coverOf;
using coarse_cells::coverTerm;
using coarse_cells::edited;
using coarse_cells::kAdded;
using coarse_cells::kRemoved;
using coarse_cells::maskRow;
using coarse_cells::masksOf;
using coarse_cells::measureCell;

// The sections of the blocks around the samples, read once from the window, with the masks of
// those not filled with one edit as asked.
class CoarseEdits::Sections {
public:
    Sections(const EditWindow& window, std::array<int, 3> low, std::array<int, 3> high) {
        for (std::size_t a = 0; a < 3; ++a) {
            first_[a] = low[a] >> kSectionShift;
            count_[a] = (high[a] >> kSectionShift) - first_[a] + 1;
        }
        sections_.resize(static_cast<std::size_t>(count_[0] * count_[1] * count_[2]));
        masks_.resize(sections_.size());
        for (int k = 0; k < count_[2]; ++k) {
            for (int j = 0; j < count_[1]; ++j) {
                for (int i = 0; i < count_[0]; ++i) {
                    const BlockSection* section =
                        window.section(first_[0] + i, first_[1] + j, first_[2] + k);
                    sections_[slot(i, j, k)] = section;
                    if (section == nullptr) {
                        continue;
                    }
                    const std::array<int, 3> at{first_[0] + i, first_[1] + j, first_[2] + k};
                    for (std::size_t a = 0; a < 3; ++a) {
                        lowest_[a] = any_ ? std::min(lowest_[a], at[a]) : at[a];
                        highest_[a] = any_ ? std::max(highest_[a], at[a]) : at[a];
                    }
                    any_ = true;
                }
            }
        }
    }
    [[nodiscard]] bool any() const { return any_; }
    // The lowest and highest blocks of the sections holding edits, along an axis.
    [[nodiscard]] std::pair<int, int> blocks(std::size_t axis) const {
        return {lowest_[axis] * kSectionSide, highest_[axis] * kSectionSide + kSectionSide - 1};
    }
    [[nodiscard]] const BlockSection* at(int sx, int sy, int sz) const {
        return sections_[slot(sx - first_[0], sy - first_[1], sz - first_[2])];
    }
    std::uint32_t row(int sx, int sy, int sz, std::size_t edit, int ly, int lz) {
        const std::size_t s = slot(sx - first_[0], sy - first_[1], sz - first_[2]);
        if (!masks_[s]) {
            masks_[s] = masksOf(*sections_[s]);
        }
        return maskRow(*masks_[s], edit, ly, lz);
    }

private:
    [[nodiscard]] std::size_t slot(int i, int j, int k) const {
        return static_cast<std::size_t>((k * count_[1] + j) * count_[0] + i);
    }

    std::array<int, 3> first_{};
    std::array<int, 3> count_{};
    std::vector<const BlockSection*> sections_;
    std::vector<std::unique_ptr<std::array<SectionMask, 2>>> masks_;
    std::array<int, 3> lowest_{};
    std::array<int, 3> highest_{};
    bool any_ = false;
};

namespace {

int floorDiv(int a, int b) {
    return a >= 0 ? a / b : -((-a - 1) / b) - 1;
}

} // namespace

CoarseEdits::CoarseEdits(const EditWindow& window, int x0, int y0, int z0, int spacing,
                         std::array<int, 3> counts, float relay)
    : x0_(x0), y0_(y0), z0_(z0), spacing_(spacing), counts_(counts), relay_(relay),
      beyond_(relay > 0.0f ? 2 : 1) {
    assert(spacing >= 2 && spacing % 2 == 0 && spacing <= 16);
    const int s = spacing;
    const std::array<int, 3> origin{x0, y0, z0};
    std::array<int, 3> low{};
    std::array<int, 3> high{};
    for (std::size_t a = 0; a < 3; ++a) {
        low[a] = origin[a] - beyond_ * s - s / 2;
        high[a] = origin[a] + (counts[a] - 1 + beyond_) * s + s / 2;
    }
    if (window.empty() || !window.meets({low[0], low[1], low[2]}, {high[0], high[1], high[2]})) {
        return;
    }
    Sections sections(window, low, high);
    if (!sections.any()) {
        return;
    }
    // The cells whose taps meet the sections holding edits, and the cells read around them (the
    // 27 cells of a point near them, and their neighbors for the relay): the others hold nothing.
    std::array<int, 3> content{};
    for (std::size_t a = 0; a < 3; ++a) {
        const auto [first, last] = sections.blocks(a);
        const int lo = std::max(-beyond_, floorDiv(first - s / 2 - origin[a] + s - 1, s));
        const int hi = std::min(counts[a] + beyond_ - 1, floorDiv(last + s / 2 - origin[a], s));
        regionLow_[a] = std::max(-beyond_, lo - 2);
        regionCount_[a] = std::min(counts[a] + beyond_ - 1, hi + 2) - regionLow_[a] + 1;
        content[a] = lo;
        contentHigh_[a] = hi;
    }
    contentLow_ = content;
    cells_.resize(static_cast<std::size_t>(regionCount_[0] * regionCount_[1] * regionCount_[2]));
    near_.assign(cells_.size(), 0);
    for (int k = content[2]; k <= contentHigh_[2]; ++k) {
        for (int j = content[1]; j <= contentHigh_[1]; ++j) {
            for (int i = content[0]; i <= contentHigh_[0]; ++i) {
                const CoarseCell cell =
                    measureCell(sections, {x0 + i * s, y0 + j * s, z0 + k * s}, s);
                cells_[index(i, j, k)] = cell;
                if (cell[kAdded].weight + cell[kRemoved].weight != 0) {
                    markNear(i, j, k);
                }
            }
        }
    }
    snapBoxes();
}

// The points around a cell holding an edited block read it (their 27 cells).
void CoarseEdits::markNear(int i, int j, int k) {
    for (int dk = k - 1; dk <= k + 1; ++dk) {
        for (int dj = j - 1; dj <= j + 1; ++dj) {
            for (int di = i - 1; di <= i + 1; ++di) {
                if (inRegion(di, dj, dk)) {
                    near_[index(di, dj, dk)] = 1;
                }
            }
        }
    }
}

bool CoarseEdits::inRegion(int i, int j, int k) const {
    return i >= regionLow_[0] && i < regionLow_[0] + regionCount_[0] && j >= regionLow_[1] &&
           j < regionLow_[1] + regionCount_[1] && k >= regionLow_[2] &&
           k < regionLow_[2] + regionCount_[2];
}

std::size_t CoarseEdits::index(int i, int j, int k) const {
    return static_cast<std::size_t>(((k - regionLow_[2]) * regionCount_[1] + j - regionLow_[1]) *
                                        regionCount_[0] +
                                    i - regionLow_[0]);
}

// A box thinner than its cell along x or z, with no block of its edit in the cells beside it
// along that axis, moves onto the column of its cell (its center on the center of the cell).
void CoarseEdits::snapBoxes() {
    if (relay_ <= 0.0f) {
        return; // no box is read
    }
    const int s = spacing_;
    const std::array<int, 3> origin{x0_, y0_, z0_};
    // The cells holding blocks whose neighbors are known (as far as the samples need).
    for (int k = std::max(contentLow_[2], 1 - beyond_);
         k <= std::min(contentHigh_[2], counts_[2] + beyond_ - 2); ++k) {
        for (int j = std::max(contentLow_[1], 1 - beyond_);
             j <= std::min(contentHigh_[1], counts_[1] + beyond_ - 2); ++j) {
            for (int i = std::max(contentLow_[0], 1 - beyond_);
                 i <= std::min(contentHigh_[0], counts_[0] + beyond_ - 2); ++i) {
                const std::array<int, 3> at{i, j, k};
                for (std::size_t e = 0; e < 2; ++e) {
                    CoarsePart& part = cells_[index(i, j, k)][e];
                    if (part.count == 0) {
                        continue;
                    }
                    for (const std::size_t a : {std::size_t{0}, std::size_t{2}}) {
                        std::array<int, 3> before = at;
                        std::array<int, 3> after = at;
                        --before[a];
                        ++after[a];
                        const bool alone = cell(before[0], before[1], before[2])[e].count == 0 &&
                                           cell(after[0], after[1], after[2])[e].count == 0;
                        if (part.high[a] - part.low[a] < static_cast<float>(s) && alone) {
                            const auto center = static_cast<float>(origin[a] + at[a] * s);
                            const float shift = center - 0.5f * (part.low[a] + part.high[a]);
                            part.low[a] += shift;
                            part.high[a] += shift;
                        }
                    }
                }
            }
        }
    }
}

namespace {

// The box of a relay: the box of the blocks at its height times the relay, kept on the side of
// the ground (an added box keeps its foot, a removed one its top).
struct RelayBox {
    std::array<double, 3> low{};
    std::array<double, 3> high{};
};

RelayBox relayBox(const CoarsePart& part, bool removing, float relay) {
    RelayBox box;
    for (std::size_t a = 0; a < 3; ++a) {
        box.low[a] = static_cast<double>(part.low[a]);
        box.high[a] = static_cast<double>(part.high[a]);
    }
    const double height = (box.high[1] - box.low[1]) * static_cast<double>(relay);
    if (removing) {
        box.low[1] = box.high[1] - height;
    } else {
        box.high[1] = box.low[1] + height;
    }
    return box;
}

// Per axis, how far p lies beyond the faces of the box (> 0 outside, the larger side).
std::array<double, 3> beyondFaces(const RelayBox& box, const std::array<double, 3>& p) {
    std::array<double, 3> q{};
    for (std::size_t a = 0; a < 3; ++a) {
        q[a] = std::max(box.low[a] - p[a], p[a] - box.high[a]);
    }
    return q;
}

double boxInside(const std::array<double, 3>& q) {
    const double outside =
        dmath::hypot(std::max(q[0], 0.0), std::max(q[1], 0.0), std::max(q[2], 0.0));
    return -(outside + std::min(std::max({q[0], q[1], q[2]}), 0.0));
}

// How far a point lies into the box of a relay (meters, < 0 outside), and its column. A point on
// the column of the box, the last sample before the face away from the ground (the top of an
// added box, the bottom of a removed one), over ground within half a block of the other face:
// the column between the ground and that face is the edit's (air over a hole open to the sky,
// rock under a block standing on the ground), and the point lies into it by its distance to that
// face along y. The face then falls where it is, whichever row of the lattice the box lies on.
double relayInside(const RelayBox& box, bool removing, double density,
                   const std::array<double, 3>& p, double s) {
    const std::array<double, 3> q = beyondFaces(box, p);
    const double inside = boxInside(q);
    if (q[0] > 0.0 || q[2] > 0.0) {
        return inside; // off the column
    }
    const double low = box.low[1];
    const double high = box.high[1];
    if (removing && p[1] > low && p[1] <= low + s && density >= p[1] - high - 0.5) {
        return std::max(inside, p[1] - low);
    }
    if (!removing && p[1] < high && p[1] >= high - s && density <= p[1] - low + 0.5) {
        return std::max(inside, high - p[1]);
    }
    return inside;
}

// The outward normal of the box at p: away from its nearest point outside, through its nearest
// face inside.
std::array<double, 3> boxNormal(const RelayBox& box, const std::array<double, 3>& p) {
    const std::array<double, 3> q = beyondFaces(box, p);
    std::array<double, 3> n{};
    if (q[0] > 0.0 || q[1] > 0.0 || q[2] > 0.0) {
        for (std::size_t a = 0; a < 3; ++a) {
            n[a] = q[a] <= 0.0 ? 0.0 : (p[a] < box.low[a] ? -q[a] : q[a]);
        }
        return n;
    }
    std::size_t nearest = 0;
    for (std::size_t a = 1; a < 3; ++a) {
        nearest = q[a] > q[nearest] ? a : nearest;
    }
    n[nearest] = p[nearest] - box.low[nearest] < box.high[nearest] - p[nearest] ? -1.0 : 1.0;
    return n;
}

} // namespace

bool CoarseEdits::relays(const CoarsePart& part) const {
    return relay_ > 0.0f && part.count != 0 && coverOf(part, spacing_) < 0.5f;
}

float CoarseEdits::applyEdit(float density, std::size_t edit, int i, int j, int k,
                             bool& relayed) const {
    const bool removing = edit == kRemoved;
    const std::array<double, 3> p{static_cast<double>(x0_ + i * spacing_),
                                  static_cast<double>(y0_ + j * spacing_),
                                  static_cast<double>(z0_ + k * spacing_)};
    float largest = 0.0f;
    bool boxed = false;
    double inside = -std::numeric_limits<double>::max(); // into the nearest box, meters
    for (int dk = -1; dk <= 1; ++dk) {
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                const CoarsePart& part = cell(i + di, j + dj, k + dk)[edit];
                largest = std::max(largest, coverOf(part, spacing_));
                if (!relays(part)) {
                    continue;
                }
                inside = std::max(inside, relayInside(relayBox(part, removing, relay_), removing,
                                                      density, p, spacing_));
                boxed = true;
            }
        }
    }
    float result =
        coverTerm(density, removing, coverOf(cell(i, j, k)[edit], spacing_), largest, spacing_);
    relayed = false;
    if (boxed) {
        const float r = edited(density, removing, static_cast<float>(inside));
        relayed = removing ? r > result : r < result;
        result = removing ? std::max(result, r) : std::min(result, r);
    }
    return result;
}

float CoarseEdits::density(float generated, int i, int j, int k) const {
    bool relayed = false;
    return density(generated, i, j, k, relayed);
}

float CoarseEdits::density(float generated, int i, int j, int k, bool& relayed) const {
    relayed = false;
    if (cells_.empty() || !inRegion(i, j, k) || near_[index(i, j, k)] == 0) {
        return generated; // no edited block in the cells around the point
    }
    bool byAdded = false;
    bool byRemoved = false;
    const float added = applyEdit(generated, kAdded, i, j, k, byAdded);
    const float result = applyEdit(added, kRemoved, i, j, k, byRemoved);
    relayed = result != added ? byRemoved : byAdded;
    return result;
}

std::array<float, 3> CoarseEdits::relayDirection(bool removing, int i, int j, int k,
                                                 const std::array<double, 3>& at) const {
    std::array<float, 3> direction{};
    if (cells_.empty() || !inRegion(i, j, k)) {
        return direction;
    }
    const std::size_t edit = removing ? kRemoved : kAdded;
    const std::array<double, 3> p{x0_ + at[0] * spacing_, y0_ + at[1] * spacing_,
                                  z0_ + at[2] * spacing_};
    double nearest = -std::numeric_limits<double>::max();
    std::array<double, 3> normal{};
    for (int dk = -1; dk <= 1; ++dk) {
        for (int dj = -1; dj <= 1; ++dj) {
            for (int di = -1; di <= 1; ++di) {
                const CoarsePart& part = cell(i + di, j + dj, k + dk)[edit];
                if (!relays(part)) {
                    continue;
                }
                const RelayBox box = relayBox(part, removing, relay_);
                const double inside = boxInside(beyondFaces(box, p));
                if (inside > nearest) {
                    nearest = inside;
                    normal = boxNormal(box, p);
                }
            }
        }
    }
    // Toward the air: into a removed box, out of an added one.
    const double sign = removing ? -1.0 : 1.0;
    const double length = dmath::hypot(normal[0], normal[1], normal[2]);
    if (length > 0.0) {
        for (std::size_t a = 0; a < 3; ++a) {
            direction[a] = static_cast<float>(sign * normal[a] / length);
        }
    }
    return direction;
}

} // namespace terrain
