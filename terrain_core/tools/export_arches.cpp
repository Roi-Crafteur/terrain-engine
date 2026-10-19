// Writes overhangs_mask.png and arches_preview.png for visual checks of the overhangs and the
// arches carved into the cliffs (square region centered on the cliff site of the world, see
// findCliffRegion: the same region as terrain_export_cliffs with the same arguments).
// Usage: terrain_export_arches <output-dir> [seed] [meters-per-pixel] [size]

#include "terrain/generation/sculpted_region.hpp"
#include "terrain/io/arches_png.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {

template <class T>
bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    return error == std::errc{} && last == end;
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t seed = terrain::kDefaultWorldSeed;
    // 6.4 km around the cliff site, as in the viewer and the cliff export.
    double metersPerPixel = 12.5;
    int size = 512;
    const bool valid = (argc >= 2 && argc <= 5) && (argc < 3 || parse(argv[2], seed)) &&
                       (argc < 4 || (parse(argv[3], metersPerPixel) && metersPerPixel > 0.0)) &&
                       (argc < 5 || (parse(argv[4], size) && size >= 2));
    if (!valid) {
        std::fprintf(stderr, "Usage: %s <output-dir> [seed] [meters-per-pixel] [size]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }

    const terrain::HeightmapRegion region = terrain::findCliffRegion(seed, metersPerPixel, size);
    const terrain::SculptedRegion sculpted(seed, region);
    if (!terrain::exportArchPngs(sculpted, argv[1])) {
        std::fprintf(stderr, "Failed to write the arch images into %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    const auto& weights = sculpted.overhangWeight();
    const auto& air = sculpted.airBelowRock();
    const auto overhangSamples =
        std::count_if(weights.begin(), weights.end(), [](float w) { return w > 0.0f; });
    const auto airSamples = std::count(air.begin(), air.end(), std::uint8_t{1});
    std::printf("seed %llu, center (%.0f, %.0f) m, %d x %d px, %.1f km wide, %td samples with "
                "overhangs, %td with rock above air, %zu arches: %s/overhangs_mask.png, "
                "%s/arches_preview.png\n",
                static_cast<unsigned long long>(seed), region.centerX, region.centerZ,
                region.size, region.size, region.size * region.metersPerPixel / 1000.0,
                overhangSamples, airSamples, sculpted.arches().size(), argv[1], argv[1]);
    return EXIT_SUCCESS;
}
