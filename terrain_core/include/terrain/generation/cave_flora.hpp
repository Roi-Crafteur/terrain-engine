#pragma once

#include "terrain/generation/cave_regions.hpp"
#include "terrain/generation/cave_sampler.hpp"

#include <cstdint>
#include <vector>

namespace terrain {

enum class CaveFloraKind : std::uint8_t { Fern, Mushroom, Vine };

// A plant of the lush cave regions, an object like the speleothems: a fern or a mushroom
// standing on a floor at (x, y, z), size meters high, or a vine hanging from a ceiling at y down
// to y - size. It never changes the density.
struct CaveFlora {
    CaveFloraKind kind = CaveFloraKind::Fern;
    double x = 0.0;
    double z = 0.0;
    float y = 0.0f;
    float size = 0.0f;

    friend bool operator==(const CaveFlora&, const CaveFlora&) = default;
};

// Deterministic cave flora (CaveFloraParams in cave_regions.hpp): the same seed, parameters,
// caves and cell always give the same plants, wherever the area being placed is.
class CaveFloraField {
public:
    explicit CaveFloraField(std::uint64_t seed, const CaveFloraParams& params = {});

    // Plants of the cells whose candidate lies in [minX, maxX) x [minZ, maxZ], in cell order, on
    // the caves read from the sampler, as SpeleothemField::place: adjacent rectangles give the
    // plants of their union; none without lush cave regions.
    [[nodiscard]] std::vector<CaveFlora> place(CaveSampler& sampler, double minX, double minZ,
                                               double maxX, double maxZ) const;

    [[nodiscard]] const CaveFloraParams& params() const { return params_; }

private:
    void placeCandidate(CaveSampler& sampler, int cellX, int cellZ,
                        std::vector<CaveFlora>& result) const;

    CaveFloraParams params_;
    std::uint64_t seed_;
};

} // namespace terrain
