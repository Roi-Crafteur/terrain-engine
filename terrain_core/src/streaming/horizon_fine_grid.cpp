#include "terrain/streaming/horizon_fine_grid.hpp"

#include <stdexcept>

namespace terrain {

HorizonFineGrid::HorizonFineGrid(const HorizonParams& params, int level, int ratio,
                                 const HorizonRect& box)
    : level_(level), ratio_(ratio), spacing_(0), box_(box) {
    validate(params);
    if (level < 0 || level >= params.levels) {
        throw std::invalid_argument("HorizonFineGrid: no such level");
    }
    const int pitch = levelPitch(params, level);
    if (ratio < 1 || pitch % ratio != 0) {
        throw std::invalid_argument("HorizonFineGrid: the ratio must divide the pitch of the "
                                    "level");
    }
    spacing_ = pitch / ratio;
    const HorizonIndex samples = levelCounts(params, level);
    counts_ = {samples.x * ratio, samples.z * ratio};
    heights_.assign(static_cast<std::size_t>(box_.count()), 0.0f);
}

void HorizonFineGrid::read(const Heightfield& field, const HorizonRect& nodes) {
    if (!covers(nodes)) {
        throw std::invalid_argument("HorizonFineGrid: the nodes are outside the box");
    }
    if (nodes.empty()) {
        return;
    }
    std::vector<std::int64_t> columns;
    for (std::int64_t u = nodes.x0; u < nodes.x1; ++u) {
        columns.push_back(u);
    }
    for (std::int64_t v = nodes.z0; v < nodes.z1; ++v) {
        readRow(field, v, columns);
    }
}

void HorizonFineGrid::readRow(const Heightfield& field, std::int64_t v,
                              std::span<const std::int64_t> columns) {
    if (columns.empty()) {
        return;
    }
    std::vector<double> xs(columns.size());
    for (std::size_t n = 0; n < columns.size(); ++n) {
        xs[n] = samplePosition(wrapIndex(columns[n], counts_.x), spacing_);
    }
    const std::vector<double> zs(columns.size(),
                                 samplePosition(wrapIndex(v, counts_.z), spacing_));
    std::vector<float> out(columns.size());
    field.heightsAt(xs, zs, out);
    for (std::size_t n = 0; n < columns.size(); ++n) {
        set(columns[n], v, out[n]);
    }
}

} // namespace terrain
