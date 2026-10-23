// Sizes, times and memory of the modifications by sections (phase 15), against the criteria: a
// hole of 10 m, and a mountain of 1 km x 1 km x 500 m razed by a box or by explosions. For each
// case: the region files of format 2 against format 1 (13 bytes per block) and against the full
// chunks of Minecraft (written to <output-dir>/<case>.chunks, compressed by
// scripts/minecraft_chunk_sizes.py), the encoding and decoding times, the memory of the store
// and of the windows of the tiles, and the generation of the tiles of LOD 0 to 3 over the edits
// against the same tiles unmodified (the reload of a modified tile). Not a test:
//   terrain_modification_sizes <output-dir> [runs]
#include "bench_support.hpp"

#include "terrain/storage/edit_window.hpp"
#include "terrain/storage/region_file.hpp"
#include "terrain/storage/volume_edit.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::ChunkCoord;
using terrain::EditMode;
using terrain::EditShape;
using terrain::ModificationStore;
using terrain::TerrainTiles;
using terrain::VolumeEdit;

const terrain::WorldSize kWorld{};

double elapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

int groundAt(const TerrainTiles& tiles, int x, int z) {
    return static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z)));
}

VolumeEdit roundEdit(EditShape shape, BlockCoord center, double radius) {
    return {shape, EditMode::Remove, center, {}, static_cast<std::uint16_t>(radius * 16.0)};
}

// The blocks of a section of the edited world: 1 for rock, 0 for air.
void writeSection(std::vector<std::uint8_t>& out, const terrain::BlockSection* edits, int sy,
                  const std::vector<int>& grounds) {
    std::vector<std::uint8_t> states(4096);
    for (int index = 0; index < 4096; ++index) {
        const std::size_t column = static_cast<std::size_t>(index % 256);
        const std::optional<terrain::BlockEdit> edit =
            edits == nullptr ? std::nullopt : edits->at(index);
        const bool rock = edit ? *edit == terrain::BlockEdit::Added
                               : sy * 16 + index / 256 < grounds[column];
        states[static_cast<std::size_t>(index)] = rock ? 1 : 0;
    }
    const bool mixed = std::any_of(states.begin(), states.end(),
                                   [&states](std::uint8_t s) { return s != states[0]; });
    out.push_back(static_cast<std::uint8_t>(sy & 0xFF));
    const std::string air = "minecraft:air";
    const std::string stone = "minecraft:stone";
    out.push_back(mixed ? 2 : 1);
    for (const std::string* name : {&air, &stone}) {
        if (mixed || (states[0] == 1) == (name == &stone)) {
            out.push_back(static_cast<std::uint8_t>(name->size()));
            out.insert(out.end(), name->begin(), name->end());
        }
    }
    if (mixed) { // 16 indices of 4 bits per long, the first in the low bits
        for (std::size_t i = 0; i < states.size(); i += 2) {
            out.push_back(static_cast<std::uint8_t>(states[i] | (states[i + 1] << 4)));
        }
    }
}

// Minecraft: each chunk of the store in full, solid or air block by block (the ground of the
// generated terrain, then the edits), from the section below its lowest ground or edit to the
// section of its highest one, as the block states of a chunk of Minecraft: per section its
// height, its palette and, with 2 states, 4 bits per block (2048 bytes). The light, biomes and
// heightmaps Minecraft also stores are left out. Each chunk is written as u32 size then bytes.
std::size_t writeMinecraft(const ModificationStore& store, const TerrainTiles& tiles,
                           const std::filesystem::path& file) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    std::size_t total = 0;
    for (const auto& [chunk, modifications] : store.byChunk()) {
        std::vector<int> grounds;
        int low = modifications.sections.begin()->first * 16;
        int high = modifications.sections.rbegin()->first * 16 + 15;
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                grounds.push_back(groundAt(tiles, chunk.x * 16 + x, chunk.z * 16 + z) + 1);
                low = std::min(low, grounds.back() - 1);
                high = std::max(high, grounds.back());
            }
        }
        std::vector<std::uint8_t> bytes;
        for (int sy = terrain::sectionOf(low) - 1; sy <= terrain::sectionOf(high); ++sy) {
            writeSection(bytes, store.section(chunk, sy), sy, grounds);
        }
        const auto size = static_cast<std::uint32_t>(bytes.size());
        const std::uint8_t header[4] = {static_cast<std::uint8_t>(size & 0xFF),
                                        static_cast<std::uint8_t>((size >> 8) & 0xFF),
                                        static_cast<std::uint8_t>((size >> 16) & 0xFF),
                                        static_cast<std::uint8_t>(size >> 24)};
        out.write(reinterpret_cast<const char*>(header), 4);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        total += bytes.size();
    }
    return total;
}

// The first chunk of a lattice of 1024 m around the center of the tests whose mountain layer
// rises above 1000 m (streaming_test_support.hpp: mountainChunk).
ChunkCoord mountainChunk(const terrain::Heightfield& field) {
    for (int j = -40; j <= 40; ++j) {
        for (int i = -40; i <= 40; ++i) {
            if (field.sampleAt(bench::kCenterX + i * 1024.0, bench::kCenterZ + j * 1024.0)
                    .mountain > 1000.0f) {
                return {bench::kCenterChunkX + i * 64, bench::kCenterChunkZ + j * 64};
            }
        }
    }
    return {bench::kCenterChunkX, bench::kCenterChunkZ};
}

struct Case {
    std::string name;
    ModificationStore store;
    std::size_t edits = 0;
    double applyMs = 0.0;
    ChunkCoord center;              // the tiles timed lie over this chunk...
    std::vector<ChunkCoord> others; // ...and these ones
};

Case applyCase(std::string name, const std::vector<VolumeEdit>& edits, ChunkCoord center,
               std::vector<ChunkCoord> others) {
    Case c{std::move(name), {}, edits.size(), 0.0, center, std::move(others)};
    const auto start = std::chrono::steady_clock::now();
    for (const VolumeEdit& edit : edits) {
        terrain::applyVolumeEdit(c.store, edit, kWorld);
    }
    c.applyMs = elapsedMs(start);
    return c;
}

// Holes of 10 m (a sphere of 5 m of radius) at the ground of a chunk: in the middle of one
// section, then on the corner of 8 sections, at the first corner east of the chunk whose
// ground lies within 2 m of a multiple of 16 m.
std::vector<Case> holes(const TerrainTiles& tiles, ChunkCoord chunk) {
    const int x = chunk.x * 16;
    const int z = chunk.z * 16;
    const int middle = (groundAt(tiles, x + 8, z + 8) & ~15) + 8;
    ChunkCoord corner = chunk;
    int height = 0;
    for (int i = 0; i < 1000; ++i) {
        corner = {chunk.x + i, chunk.z};
        const int ground = groundAt(tiles, corner.x * 16, corner.z * 16);
        height = (ground + 8) & ~15;
        if (std::abs(ground - height) <= 2) {
            break;
        }
    }
    return {applyCase("hole of 10 m in a section",
                      {roundEdit(EditShape::Sphere, {x + 8, middle, z + 8}, 5.0)}, chunk, {}),
            applyCase("hole of 10 m on 8 sections",
                      {roundEdit(EditShape::Sphere, {corner.x * 16, height, corner.z * 16}, 5.0)},
                      corner, {{corner.x - 1, corner.z - 1}})};
}

// The highest ground of the square of side meters from (x0, z0), every 25 m.
int highestGround(const TerrainTiles& tiles, int x0, int z0, int side) {
    int highest = groundAt(tiles, x0, z0);
    for (int dz = 0; dz <= side; dz += 25) {
        for (int dx = 0; dx <= side; dx += 25) {
            highest = std::max(highest, groundAt(tiles, x0 + dx, z0 + dz));
        }
    }
    return highest;
}

// The top 500 m of a mountain, 1 km x 1 km, removed by a box; then the same box with its floor
// and walls dug by explosions of 7 m every 8 m, as a player razing it with explosives.
std::vector<Case> razedMountain(const TerrainTiles& tiles, ChunkCoord mountain) {
    const int x0 = mountain.x * 16 + 5;
    const int z0 = mountain.z * 16 + 7;
    const int top = highestGround(tiles, x0, z0, 999);
    const int floor = top - 499;
    const VolumeEdit box{EditShape::Box, EditMode::Remove, {x0, floor, z0}, {1000, 500, 1000}, 0};
    std::vector<VolumeEdit> blasts = {box};
    for (int a = 0; a < 1000; a += 8) {
        for (int b = 0; b < 1000; b += 8) {
            blasts.push_back(roundEdit(EditShape::Explosion, {x0 + a, floor, z0 + b}, 7.0));
        }
        for (int y = floor; y < top; y += 8) {
            blasts.push_back(roundEdit(EditShape::Explosion, {x0, y, z0 + a}, 7.0));
            blasts.push_back(roundEdit(EditShape::Explosion, {x0 + 999, y, z0 + a}, 7.0));
            blasts.push_back(roundEdit(EditShape::Explosion, {x0 + a, y, z0}, 7.0));
            blasts.push_back(roundEdit(EditShape::Explosion, {x0 + a, y, z0 + 999}, 7.0));
        }
    }
    const ChunkCoord middle{(x0 + 500) >> 4, (z0 + 500) >> 4};
    const std::vector<ChunkCoord> edges = {{x0 >> 4, (z0 + 500) >> 4}};
    return {applyCase("mountain razed by a box", {box}, middle, edges),
            applyCase("mountain razed by explosions", blasts, middle, edges)};
}

// The region files of the store: their bytes, the time to write and read them all, the
// slowest region to read.
struct Files {
    std::size_t bytes = 0;
    std::size_t formatOne = 0; // the same blocks in format 1, one by one
    std::size_t regions = 0;
    std::size_t chunks = 0;
    double writeMs = 0.0;
    double readMs = 0.0;
    double slowestReadMs = 0.0;
};

Files measureFiles(const ModificationStore& store) {
    Files files;
    std::vector<std::vector<std::uint8_t>> written;
    const std::vector<terrain::RegionCoord> regions = terrain::regionsOf(store);
    auto start = std::chrono::steady_clock::now();
    for (const terrain::RegionCoord region : regions) {
        written.push_back(terrain::serializeRegion(store, region, bench::kSeed));
    }
    files.writeMs = elapsedMs(start);
    start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < regions.size(); ++i) {
        const auto one = std::chrono::steady_clock::now();
        const ModificationStore read =
            terrain::deserializeRegion(written[i], regions[i], bench::kSeed);
        files.slowestReadMs = std::max(files.slowestReadMs, elapsedMs(one));
        files.chunks += read.byChunk().size();
        files.bytes += written[i].size();
    }
    files.readMs = elapsedMs(start);
    files.regions = regions.size();
    // Format 1: the frame and body header of each region, then 20 bytes and 13 per block for
    // each chunk.
    files.formatOne = regions.size() * 40 + store.byChunk().size() * 20 + store.blockCount() * 13;
    return files;
}

// The memory of the windows of the tiles of each LOD over the chunk: the window of the tile and
// the largest lattice of its levels.
std::size_t windowBytes(const ModificationStore& store, ChunkCoord chunk) {
    std::size_t largest = 0;
    for (int lod = 0; lod < terrain::kLodCount; ++lod) {
        const terrain::TileKey key = terrain::tileContaining(chunk, lod);
        const int spacing = 1 << lod;
        const int x0 = key.origin.x * 16;
        const int z0 = key.origin.z * 16;
        const int side = 16 * spacing;
        const terrain::EditWindow window(store, x0 - 1, x0 + side + 1, z0 - 1, z0 + side + 1,
                                         kWorld);
        std::size_t lattice = 0;
        for (const auto& [low, high] : window.editedSpans()) {
            for (int y = (low / side - 1) * side; y <= high + side; y += side) {
                const terrain::EditLattice level(window, x0, y, z0, spacing, 17);
                lattice = std::max(lattice, level.heapBytes());
            }
        }
        largest = std::max(largest, window.heapBytes() + lattice);
    }
    return largest;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

// The median time to generate the tile of each LOD over a chunk, edited and not, in turn so
// that both see the same load of the machine; with the vertices of each.
void timeTiles(const Case& c, const TerrainTiles& plain, int runs) {
    TerrainTiles edited(bench::kSeed);
    edited.replaceModifications(c.store);
    std::vector<ChunkCoord> chunks = {c.center};
    chunks.insert(chunks.end(), c.others.begin(), c.others.end());
    for (const ChunkCoord chunk : chunks) {
        for (int lod = 0; lod < terrain::kLodCount; ++lod) {
            const terrain::TileRequest request{terrain::tileContaining(chunk, lod), 0};
            std::vector<double> before;
            std::vector<double> after;
            std::size_t plainVertices = 0;
            std::size_t editedVertices = 0;
            for (int run = 0; run < runs; ++run) {
                auto start = std::chrono::steady_clock::now();
                plainVertices = plain.generate(request).mesh.vertices.size();
                before.push_back(elapsedMs(start));
                start = std::chrono::steady_clock::now();
                editedVertices = edited.generate(request).mesh.vertices.size();
                after.push_back(elapsedMs(start));
            }
            std::printf("    tile LOD %d over chunk (%d, %d): %.3f ms unmodified (%zu vertices), "
                        "%.3f ms edited (%zu vertices), x%.2f\n",
                        lod, chunk.x, chunk.z, median(before), plainVertices, median(after),
                        editedVertices, median(after) / median(before));
        }
    }
}

void report(const Case& c, const TerrainTiles& plain, const std::filesystem::path& out,
            int runs) {
    const Files files = measureFiles(c.store);
    std::printf("%s: %zu edits applied in %.1f ms, %zu blocks\n", c.name.c_str(), c.edits,
                c.applyMs, c.store.blockCount());
    std::printf("    format 2: %zu bytes in %zu regions (%zu chunks); format 1: %zu bytes\n",
                files.bytes, files.regions, files.chunks, files.formatOne);
    std::printf("    written in %.1f ms, read in %.1f ms (slowest region %.2f ms, %.3f ms per "
                "chunk)\n",
                files.writeMs, files.readMs, files.slowestReadMs,
                files.readMs / static_cast<double>(std::max<std::size_t>(files.chunks, 1)));
    std::printf("    memory: store %zu bytes, largest tile window %zu bytes\n",
                c.store.heapBytes(), windowBytes(c.store, c.center));
    std::string name = c.name;
    std::replace(name.begin(), name.end(), ' ', '_');
    const std::size_t minecraft = writeMinecraft(c.store, plain, out / (name + ".chunks"));
    std::printf("    Minecraft chunks, uncompressed: %zu bytes (%s.chunks)\n", minecraft,
                name.c_str());
    timeTiles(c, plain, runs);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: terrain_modification_sizes <output-dir> [runs]\n");
        return EXIT_FAILURE;
    }
    const std::filesystem::path out = argv[1];
    const int runs = argc == 3 ? std::max(1, std::atoi(argv[2])) : 15;
    std::filesystem::create_directories(out);
    const TerrainTiles plain(bench::kSeed);
    std::vector<Case> cases = holes(plain, {bench::kCenterChunkX, bench::kCenterChunkZ});
    for (Case& c : razedMountain(plain, mountainChunk(plain.heightfield()))) {
        cases.push_back(std::move(c));
    }
    for (const Case& c : cases) {
        report(c, plain, out, runs);
    }
    return EXIT_SUCCESS;
}
