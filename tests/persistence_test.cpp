// Persistent modifications: a dug block, a built block, a removed plant and an added plant
// change the tiles they reach and only those; saved and loaded into a new world they give the
// same tiles; through the stream manager they persist after their tiles are unloaded and loaded
// again, and an edit regenerates the loaded tiles, which keep their old data until replaced.
#include "terrain/storage/modifications.hpp"
#include "terrain/streaming/stream_manager.hpp"
#include "terrain/streaming/terrain_tiles.hpp"

#include "streaming_test_support.hpp"
#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using terrain::BlockCoord;
using terrain::BlockEdit;
using terrain::ChunkCoord;
using terrain::Plant;
using terrain::StreamManager;
using terrain::StreamParams;
using terrain::TerrainTiles;
using terrain::TileData;
using terrain::TileKey;
using terrain::TileRequest;

struct Edits {
    ChunkCoord chunk;
    BlockCoord dug;   // rock below the ground, removed
    BlockCoord built; // air above the ground, added
    Plant removed;    // a generated plant
    Plant added;
};

int groundAt(const TerrainTiles& tiles, int x, int z) {
    return static_cast<int>(std::floor(tiles.heightfield().heightAt(x, z)));
}

// Edits in the middle of a chunk with plants, away from its borders.
Edits chooseEdits(const TerrainTiles& tiles) {
    Edits edits;
    edits.chunk = streaming_test::plantChunk(tiles, 2);
    const int x = edits.chunk.x * 16 + 8;
    const int z = edits.chunk.z * 16 + 8;
    edits.dug = {x, groundAt(tiles, x, z) - 2, z};
    edits.built = {x + 3, groundAt(tiles, x + 3, z) + 3, z};
    edits.removed = tiles.generate({{edits.chunk, 0}, 0}).plants.front();
    const double px = x - 3.5;
    const double pz = z + 2.25;
    edits.added = {terrain::PlantType::Tree, px, pz, tiles.heightfield().heightAt(px, pz), 1.2f,
                   0.5f};
    return edits;
}

std::vector<ChunkCoord> apply(TerrainTiles& tiles, const Edits& edits) {
    std::vector<ChunkCoord> chunks = tiles.setBlock(edits.dug, BlockEdit::Removed);
    const std::vector<ChunkCoord> built = tiles.setBlock(edits.built, BlockEdit::Added);
    chunks.insert(chunks.end(), built.begin(), built.end());
    chunks.push_back(tiles.removePlant(edits.removed).front());
    chunks.push_back(tiles.addPlant(edits.added).front());
    return chunks;
}

bool contains(const std::vector<Plant>& plants, const Plant& plant) {
    return std::find(plants.begin(), plants.end(), plant) != plants.end();
}

void testEditsChangeTiles() {
    TerrainTiles tiles(streaming_test::kSeed);
    const Edits edits = chooseEdits(tiles);
    const TileRequest request{{edits.chunk, 0}, 0};
    const TileRequest neighbor{{{edits.chunk.x + 1, edits.chunk.z}, 0}, 0};
    const TileData before = tiles.generate(request);
    const TileData neighborBefore = tiles.generate(neighbor);
    TEST_CHECK(tiles.densityAt(edits.dug.x, edits.dug.y, edits.dug.z) < 0.0f);
    TEST_CHECK(tiles.densityAt(edits.built.x, edits.built.y, edits.built.z) > 0.0f);

    const std::vector<ChunkCoord> chunks = apply(tiles, edits);
    TEST_CHECK(std::all_of(chunks.begin(), chunks.end(),
                           [&edits](ChunkCoord c) { return c == edits.chunk; }));
    TEST_CHECK(tiles.blockAt(edits.dug) == BlockEdit::Removed);
    TEST_CHECK(tiles.blockAt(edits.built) == BlockEdit::Added);
    TEST_CHECK(tiles.clearBlock({edits.dug.x, edits.dug.y + 5, edits.dug.z}).empty());
    TEST_CHECK(tiles.densityAt(edits.dug.x, edits.dug.y, edits.dug.z) > 0.0f);
    TEST_CHECK(tiles.densityAt(edits.built.x, edits.built.y, edits.built.z) < 0.0f);

    const TileData after = tiles.generate(request);
    TEST_CHECK(after.mesh != before.mesh);
    TEST_CHECK(after.mesh.vertices.size() > before.mesh.vertices.size()); // cavity and block
    TEST_CHECK(!contains(after.plants, edits.removed) && contains(before.plants, edits.removed));
    TEST_CHECK(contains(after.plants, edits.added));
    TEST_CHECK(after.plants.size() == before.plants.size());
    // A chunk the edits do not reach is unchanged.
    TEST_CHECK(tiles.generate(neighbor) == neighborBefore);
    // A coarser tile over the edits samples them on its own lattice: still deterministic.
    const TileRequest coarse{terrain::tileContaining(edits.chunk, 1), 0};
    TEST_CHECK(tiles.generate(coarse) == tiles.generate(coarse));
}

std::filesystem::path saveFile() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "terrain_engine_persistence_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir / "terrain_modifications.bin";
}

std::vector<std::uint8_t> fileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Saved, then loaded into a new world: the same tiles as the edited world.
void testSaveAndReload() {
    TerrainTiles edited(streaming_test::kSeed);
    const Edits edits = chooseEdits(edited);
    const TileRequest request{{edits.chunk, 0}, 0};
    const TileData original = edited.generate(request);
    apply(edited, edits);
    const TileData expected = edited.generate(request);
    const std::filesystem::path file = saveFile();
    terrain::saveModifications(file, edited.modifications(), edited.seed());

    TerrainTiles reloaded(streaming_test::kSeed);
    TEST_CHECK(reloaded.generate(request) == original);
    const terrain::SavedModifications saved = terrain::loadModifications(file);
    TEST_CHECK(saved.seed == streaming_test::kSeed);
    const std::vector<ChunkCoord> chunks = reloaded.replaceModifications(saved.store);
    TEST_CHECK(std::find(chunks.begin(), chunks.end(), edits.chunk) != chunks.end());
    TEST_CHECK(reloaded.modifications() == edited.modifications());
    TEST_CHECK(reloaded.generate(request) == expected);

    // Saving the reloaded world writes the same bytes.
    const std::vector<std::uint8_t> first = fileBytes(file);
    terrain::saveModifications(file, reloaded.modifications(), reloaded.seed());
    TEST_CHECK(fileBytes(file) == first);
    // Replacing by an empty store gives the generated terrain back.
    TEST_CHECK(!reloaded.replaceModifications({}).empty());
    TEST_CHECK(reloaded.generate(request) == original);
    std::filesystem::remove_all(file.parent_path());
}

StreamParams syncParams() {
    StreamParams params;
    params.lod = streaming_test::smallLod();
    params.workerThreads = 0;
    params.maxLoadsPerUpdate = 1000;
    return params;
}

void loadAll(StreamManager& manager, ChunkCoord player) {
    const auto update = [&] {
        manager.update((player.x + 0.5) * terrain::kChunkSizeMeters,
                       (player.z + 0.5) * terrain::kChunkSizeMeters);
    };
    update();
    for (int round = 0; round < 100 && manager.pendingCount() > 0; ++round) {
        manager.waitIdle();
        update();
    }
}

// The edits persist after their tile is unloaded and loaded again.
void testPersistAfterUnload() {
    TerrainTiles tiles(streaming_test::kSeed);
    const Edits edits = chooseEdits(tiles);
    apply(tiles, edits);
    StreamManager manager(terrain::tileGenerator(tiles), syncParams());
    const TileKey key{edits.chunk, 0};
    loadAll(manager, edits.chunk);
    TEST_CHECK(manager.find(key) != nullptr);
    const TileData loaded = *manager.find(key);
    TEST_CHECK(contains(loaded.plants, edits.added) && !contains(loaded.plants, edits.removed));
    TEST_CHECK(loaded == tiles.generate(loaded.request));

    const ChunkCoord far{edits.chunk.x + 300, edits.chunk.z};
    loadAll(manager, far);
    TEST_CHECK(manager.find(key) == nullptr);
    loadAll(manager, edits.chunk);
    TEST_CHECK(manager.find(key) != nullptr && *manager.find(key) == loaded);
}

// Editing a loaded world: the tiles over the edits are generated again and keep their old data
// meanwhile; loading a save does the same (the F9 key of the viewer).
void testEditsRegenerateLoadedTiles() {
    TerrainTiles tiles(streaming_test::kSeed);
    const Edits edits = chooseEdits(tiles);
    StreamManager manager(terrain::tileGenerator(tiles), syncParams());
    const TileKey key{edits.chunk, 0};
    loadAll(manager, edits.chunk);
    const TileData before = *manager.find(key);

    const std::vector<ChunkCoord> chunks = apply(tiles, edits);
    manager.invalidate(chunks);
    TEST_CHECK(manager.pendingCount() == 1);
    TEST_CHECK(*manager.find(key) == before); // old data until replaced
    loadAll(manager, edits.chunk);
    const TileData after = *manager.find(key);
    TEST_CHECK(after != before && after == tiles.generate(after.request));

    // Back to the saved state of a fresh world, then reloaded.
    const terrain::ModificationStore saved = tiles.modifications();
    manager.invalidate(tiles.replaceModifications({}));
    loadAll(manager, edits.chunk);
    TEST_CHECK(*manager.find(key) == before);
    manager.invalidate(tiles.replaceModifications(saved));
    loadAll(manager, edits.chunk);
    TEST_CHECK(*manager.find(key) == after);
}

} // namespace

int main() {
    testEditsChangeTiles();
    testSaveAndReload();
    testPersistAfterUnload();
    testEditsRegenerateLoadedTiles();
    return test::result();
}
