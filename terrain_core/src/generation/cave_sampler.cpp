#include "terrain/generation/cave_sampler.hpp"

#include <algorithm>

namespace terrain {

namespace {

// Whole periods that bring the center of [low, high] into the canonical range.
double frameShift(double low, double high, double period) {
    const double center = 0.5 * (low + high);
    return wrapCoordinate(center, period) - center;
}

CaveBox shifted(const CaveBox& box, double shiftX, double shiftZ) {
    return {box.minX + shiftX, box.minY, box.minZ + shiftZ,
            box.maxX + shiftX, box.maxY, box.maxZ + shiftZ};
}

} // namespace

CaveSampler::CaveSampler(const CaveField& field, const CaveGround& ground, const CaveDetail& detail,
                         const CaveBox& box)
    : field_(&field), ground_(&ground), detail_(detail), noises_(field.noisesOf(detail)),
      shiftX_(frameShift(box.minX, box.maxX, field.world().x)),
      shiftZ_(frameShift(box.minZ, box.maxZ, field.world().z)), aquifers_(field, ground),
      cache_(field.lattice(), shifted(box, shiftX_, shiftZ_)), cellBounds_(cache_.cellCount()),
      boundKnown_(cache_.cellCount(), 0) {
    if (detail.has(kCaveEntrances)) {
        const CaveBox frame = shifted(box, shiftX_, shiftZ_);
        frameEntrances_ =
            field.entrancesNear(frame.minX, frame.minZ, frame.maxX, frame.maxZ, ground, aquifers_);
        entrances_ = frameEntrances_;
        for (CaveEntrance& entrance : entrances_) {
            entrance.topX -= shiftX_;
            entrance.bottomX -= shiftX_;
            entrance.topZ -= shiftZ_;
            entrance.bottomZ -= shiftZ_;
        }
    }
}

CaveColumn CaveSampler::column(double x, double z, float ground) const {
    return field_->column(x + shiftX_, z + shiftZ_, ground, *ground_, frameEntrances_, &aquifers_,
                          detail_.has(kCaveAquifers));
}

float CaveSampler::waterDensity(const CaveColumn& column, double y, float terrain,
                                float carved) const {
    if (!detail_.has(kCaveAquifers)) {
        return field_->params().voidClamp;
    }
    return field_->waterDensity(column, y, terrain, carved);
}

float CaveSampler::lavaDensity(const CaveColumn& column, double y, float terrain,
                               float carved) const {
    if (!detail_.has(kCaveAquifers)) {
        return field_->params().voidClamp;
    }
    return field_->lavaDensity(column, y, terrain, carved);
}

float CaveSampler::density(const CaveColumn& column, double y, float terrain) {
    const float threshold = detail_.guard + field_->params().fade;
    float entrance = field_->params().voidClamp;
    if (detail_.has(kCaveEntrances) && column.entranceCount > 0) {
        entrance = field_->limitedEntranceVoid(column, y, frameEntrances_);
    }
    bool inner = field_->limit(column, y) < threshold &&
                 (detail_.components & ~static_cast<unsigned>(kCaveEntrances)) != 0;
    if (inner) {
        float bound = field_->params().voidClamp;
        if (detail_.has(kCaveRavines)) {
            bound = std::min(bound, field_->ravineVoid(column, y));
        }
        if (detail_.has(kCaveGalleries)) {
            bound = std::min(bound, field_->galleryVoid(column, y));
        }
        if (noises_ != 0 && bound >= threshold) {
            const CaveCellBound& cell = cellBound(column.x, y, column.z);
            bound = std::min(bound, field_->noiseLowerBound(cell, column.profile, detail_));
        }
        inner = bound < threshold;
    }
    if (!inner && entrance >= threshold) {
        return terrain;
    }
    return field_->carve(terrain, voidAt(column, y), detail_);
}

const CaveCellBound& CaveSampler::cellBound(double x, double y, double z) {
    const std::size_t index = cache_.cellIndex(x, y, z);
    if (boundKnown_[index] == 0) {
        CaveNoiseValues low{};
        CaveNoiseValues high{};
        cache_.cellBounds(x, y, z, noises_, low, high);
        cellBounds_[index] = field_->cellBound(low, high, detail_);
        boundKnown_[index] = 1;
    }
    return cellBounds_[index];
}

CaveVoids CaveSampler::voids(const CaveColumn& column, double y) {
    CaveNoiseValues noise{};
    if (noises_ != 0) {
        cache_.sample(column.x, y, column.z, noises_, noise);
    }
    return field_->voids(column, y, noise, frameEntrances_, detail_);
}

float CaveSampler::voidAt(const CaveColumn& column, double y) {
    return field_->combine(voids(column, y));
}

float caveDensityAt(const CaveField& field, const CaveGround& ground, const CaveDetail& detail,
                    double x, double y, double z) {
    const float height = ground.height(x, z);
    CaveSampler sampler(field, ground, detail, {x, y, z, x, y, z});
    const CaveColumn column = sampler.column(x, z, height);
    return sampler.density(column, y, static_cast<float>(y - static_cast<double>(height)));
}

} // namespace terrain
