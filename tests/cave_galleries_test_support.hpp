#pragma once

// Helpers of the gallery tests: worlds where every cave region has the same profile, and the 2D
// graph of the network of galleries over a square, with its connected components.

#include "terrain/generation/aquifers.hpp"
#include "terrain/generation/caves.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <vector>

namespace galleries_test {

// Cave parameters whose ten cave regions all have the profile of the given type.
inline terrain::CaveParams uniformParams(terrain::CaveDistrictType type) {
    terrain::CaveDistrictParams districts;
    districts.profiles.fill(terrain::defaultDistrictProfiles()[static_cast<std::size_t>(type)]);
    terrain::CaveParams params;
    params.districts = districts;
    return params;
}

// Half width of the galleries of a column: GalleryParams::halfWidth scaled by its profile.
inline float halfWidthOf(const terrain::CaveField& field, const terrain::CaveColumn& column) {
    return field.params().galleries.halfWidth *
           column.profile[terrain::CaveProfile::kGalleries];
}

// The network over a square of size x size samples, step meters apart from (x0, z0): a sample
// is open where the axis of a gallery is within its half width and the limits allow caves at
// the center of the gallery, cut where only the limits close it, and a junction where both
// networks are within the half width.
struct Network {
    int size = 0;
    double x0 = 0.0;
    double z0 = 0.0;
    double step = 0.0;
    std::vector<unsigned char> open;
    std::vector<unsigned char> junction;
    std::size_t openCount = 0;
    std::size_t cutCount = 0;
};

inline Network scanNetwork(const terrain::CaveField& field, const terrain::CaveGround& ground,
                           double x0, double z0, double step, int size) {
    Network net{size, x0, z0, step, {}, {}, 0, 0};
    const auto cells = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    net.open.assign(cells, 0);
    net.junction.assign(cells, 0);
    terrain::AquiferCache cache(field, ground);
    for (int k = 0; k < size; ++k) {
        for (int i = 0; i < size; ++i) {
            const double x = x0 + i * step;
            const double z = z0 + k * step;
            const terrain::CaveColumn column =
                field.column(x, z, ground.height(x, z), ground, {}, &cache, false);
            const double halfWidth = static_cast<double>(halfWidthOf(field, column));
            if (static_cast<double>(column.galleryDistance) >= halfWidth) {
                continue;
            }
            const double center = static_cast<double>(column.galleryFloor) +
                                  field.params().galleries.halfHeight *
                                      column.profile[terrain::CaveProfile::kGalleries];
            const auto index = static_cast<std::size_t>(k * size + i);
            if (field.limit(column, center) >= 0.0f) {
                ++net.cutCount;
                continue;
            }
            net.open[index] = 1;
            ++net.openCount;
            const std::array<double, 2> d = field.galleryDistances(x, z);
            net.junction[index] = d[0] < halfWidth && d[1] < halfWidth ? 1 : 0;
        }
    }
    return net;
}

// A connected component of marked samples (8-connected): its size, the sides of the square it
// touches (bits 1, 2, 4, 8 for -x, +x, -z, +z), its extent in samples and a sample of it.
struct Component {
    std::size_t size = 0;
    unsigned sides = 0;
    int minI = 0;
    int maxI = 0;
    int minK = 0;
    int maxK = 0;
    std::size_t first = 0;
};

inline std::vector<Component> components(const std::vector<unsigned char>& marked, int size) {
    std::vector<int> label(marked.size(), -1);
    std::vector<Component> result;
    for (std::size_t start = 0; start < marked.size(); ++start) {
        if (marked[start] == 0 || label[start] >= 0) {
            continue;
        }
        Component c;
        c.first = start;
        c.minI = c.maxI = static_cast<int>(start) % size;
        c.minK = c.maxK = static_cast<int>(start) / size;
        std::queue<std::size_t> queue;
        queue.push(start);
        label[start] = static_cast<int>(result.size());
        while (!queue.empty()) {
            const std::size_t at = queue.front();
            queue.pop();
            ++c.size;
            const int i = static_cast<int>(at) % size;
            const int k = static_cast<int>(at) / size;
            c.sides |= (i == 0 ? 1U : 0U) | (i == size - 1 ? 2U : 0U) | (k == 0 ? 4U : 0U) |
                       (k == size - 1 ? 8U : 0U);
            c.minI = std::min(c.minI, i);
            c.maxI = std::max(c.maxI, i);
            c.minK = std::min(c.minK, k);
            c.maxK = std::max(c.maxK, k);
            for (int n = 0; n < 9; ++n) {
                const int a = i + n % 3 - 1;
                const int b = k + n / 3 - 1;
                if (a < 0 || b < 0 || a >= size || b >= size) {
                    continue;
                }
                const auto next = static_cast<std::size_t>(b * size + a);
                if (marked[next] != 0 && label[next] < 0) {
                    label[next] = static_cast<int>(result.size());
                    queue.push(next);
                }
            }
        }
        result.push_back(c);
    }
    return result;
}

} // namespace galleries_test
