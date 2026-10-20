// Volume: 100,000 modified chunks in a dense area (about 120 region files), then chunks spread
// over 300 region groups (one file and one directory each), saved and loaded again whole; the
// times are printed. Each save flushes every file to the disk.
#include "terrain/storage/world_save.hpp"

#include "storage_test_support.hpp"
#include "test_assert.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>

namespace {

namespace fs = std::filesystem;

using terrain::BlockEdit;
using terrain::ModificationStore;

class Stopwatch {
public:
    double seconds() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

// 400 x 250 chunks around the origin: a block in each, an added plant in one chunk of 7 and a
// removed plant in one of 11.
ModificationStore denseStore() {
    ModificationStore store;
    for (int cz = -125; cz < 125; ++cz) {
        for (int cx = -200; cx < 200; ++cx) {
            const int x = cx * 16 + 8;
            const int z = cz * 16 + 8;
            const int index = (cz + 125) * 400 + cx + 200;
            store.setBlock({x, 10 + index % 7, z}, index % 2 == 0 ? BlockEdit::Added
                                                                   : BlockEdit::Removed);
            if (index % 7 == 0) {
                store.addPlant({terrain::PlantType::Bush, x + 0.5, z - 3.25, 11.0f, 1.0f, 0.5f});
            }
            if (index % 11 == 0) {
                store.removeGeneratedPlant({x - 2.75, z + 1.5});
            }
        }
    }
    return store;
}

// One chunk in each of 300 region groups (1024 chunks apart).
ModificationStore scatteredStore() {
    ModificationStore store;
    for (int i = 0; i < 300; ++i) {
        const int x = ((i % 20) - 10) * 1024 * 16 + 5;
        const int z = ((i / 20) - 7) * 1024 * 16 - 3;
        store.setBlock({x, 64, z}, BlockEdit::Removed);
    }
    return store;
}

void saveAndLoad(const fs::path& world, const ModificationStore& store, const char* what) {
    const terrain::WorldMeta meta = storage_test::sampleMeta();
    const Stopwatch save;
    terrain::saveWorld(world, meta, store);
    const double saveSeconds = save.seconds();
    const Stopwatch load;
    const terrain::LoadedWorld loaded = terrain::loadWorld(world);
    const double loadSeconds = load.seconds();
    TEST_CHECK(loaded.meta == meta && loaded.modifications == store);
    const std::size_t regions = terrain::savedRegions(world).size();
    TEST_CHECK(regions == terrain::regionsOf(store).size());
    std::printf("%s: %zu chunks in %zu region files, saved in %.2f s, loaded in %.2f s\n", what,
                store.byChunk().size(), regions, saveSeconds, loadSeconds);
}

} // namespace

int main() {
    const fs::path dir = storage_test::freshDirectory("terrain_engine_world_volume_test");
    const Stopwatch build;
    const ModificationStore dense = denseStore();
    std::printf("100,000 chunks built in %.2f s\n", build.seconds());
    TEST_CHECK(dense.byChunk().size() == 100'000);
    saveAndLoad(dir / "dense", dense, "dense");
    const ModificationStore scattered = scatteredStore();
    TEST_CHECK(scattered.byChunk().size() == 300);
    saveAndLoad(dir / "scattered", scattered, "scattered");
    fs::remove_all(dir);
    return test::result();
}
