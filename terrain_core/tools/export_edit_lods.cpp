// terrain_export_edit_lods <output-dir> [--width <pixels>]
//
// Views of the player edits from far away (agent 41, edit_views.hpp): a hole of one block and a
// wall one block thick near the origin, seen from the west 1.8 m over the ground at 30, 60, 64,
// 90, 128, 150, 200, 256 and 300 m, through the tiles a player there streams, under a sun at 30
// degrees (sun50) and at 6 degrees (sun10); each view with and without the edit
// (<edit>_<distance>m_<sun>.png and ..._without.png), and at 60 m the tiles of LOD 0 and of LOD 1
// over the edit from the same eye (<edit>_switch_lod0_<sun>.png, ..._lod1_..., with and
// without). edit_lods.txt gives for each view the LOD of the tile of the edit and the pixels where
// the edit shows, and how its look changes at the switch (edit_views::compareSwitch). A control
// of the coarse tiles: what Unreal shows is checked in Unreal.
#include "edit_views.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

struct Options {
    std::filesystem::path directory;
    int width = 640;
};

bool parse(int argc, char** argv, Options& o) {
    if (argc < 2) {
        return false;
    }
    o.directory = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--width" && i + 1 < argc) {
            o.width = std::atoi(argv[++i]);
        } else {
            return false;
        }
    }
    return o.width >= 64;
}

std::string sunName(double sine) {
    return sine > 0.3 ? "sun50" : "sun10";
}

bool write(const std::filesystem::path& file, const terrain::Image& image) {
    return terrain::writePng(file, image);
}

bool exportScene(edit_views::Scene& scene, const std::string& name, const Options& o,
                 std::ofstream& report) {
    bool ok = true;
    const int height = o.width * 9 / 16;
    for (const double sun : {0.5, 0.1}) {
        for (const double d : {30.0, 60.0, 64.0, 90.0, 128.0, 150.0, 200.0, 256.0, 300.0}) {
            const edit_views::View view{d, sun, o.width, height, std::nullopt};
            const terrain::Image with = edit_views::render(scene, view, true);
            const terrain::Image without = edit_views::render(scene, view, false);
            const std::string stem = name + "_" + std::to_string(static_cast<int>(d)) + "m_" +
                                     sunName(sun);
            ok = write(o.directory / (stem + ".png"), with) &&
                 write(o.directory / (stem + "_without.png"), without) && ok;
            report << stem << ": LOD " << edit_views::editLod(scene, view) << ", "
                   << edit_views::editPixels(with, without) << " pixels show the edit\n";
        }
        const terrain::ChunkCoord edit = terrain::worldToChunk(scene.x(), scene.z());
        std::array<terrain::Image, 4> views; // LOD 0 with and without, LOD 1 with and without
        for (const int lod : {0, 1}) {
            const edit_views::View view{60.0, sun, o.width, height,
                                        terrain::ChunkCoord{edit.x - 3 - lod, edit.z}};
            const std::string stem = name + "_switch_lod" + std::to_string(lod) + "_" +
                                     sunName(sun);
            const auto at = static_cast<std::size_t>(2 * lod);
            views[at] = edit_views::render(scene, view, true);
            views[at + 1] = edit_views::render(scene, view, false);
            ok = write(o.directory / (stem + ".png"), views[at]) &&
                 write(o.directory / (stem + "_without.png"), views[at + 1]) && ok;
        }
        const edit_views::Switch look =
            edit_views::compareSwitch(views[0], views[1], views[2], views[3], 2);
        report << name << "_switch_" << sunName(sun) << ": " << look.shownA << " pixels at LOD 0, "
               << look.shownB << " at LOD 1, " << look.nearA << " of LOD 0 within 2 pixels of LOD "
               << "1, luminance " << look.lumaA << " and " << look.lumaB << "\n";
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        std::fprintf(stderr, "usage: terrain_export_edit_lods <output-dir> [--width <pixels>]\n");
        return 1;
    }
    std::filesystem::create_directories(o.directory);
    std::ofstream report(o.directory / "edit_lods.txt");
    edit_views::Scene hole(edit_views::Edit::Hole, 44, 44);
    edit_views::Scene wall(edit_views::Edit::Wall, 45, 44);
    const bool ok = exportScene(hole, "hole", o, report) && exportScene(wall, "wall", o, report);
    std::printf("views written to %s\n", o.directory.string().c_str());
    return ok ? 0 : 1;
}
