#pragma once
// Pure C++ layer of the Unreal plugin (no Unreal header): the edits of the players kept between two
// plays of a level (agent 28, phase 15). A folder of the world format v2 per seed (world.meta and
// the region files of terrain_core storage/world_save.hpp), apart from the planet file:
//   <root>/seed_<seed>/
// Off by default in Unreal (bSaveEdits, Terrain.EditSave): a test or the launcher of the checks
// never loads a world edited by a player. The writes run on a thread of their own (EditSaver),
// each file written to <file>.tmp then renamed over the old one, kept as <file>.bak
// (saveWorldRegions): a save never damages the previous one and never holds a frame.

#include "terrain/chunk_coord.hpp"
#include "terrain/storage/modifications.hpp"
#include "terrain/storage/region_file.hpp"
#include "terrain/storage/world_meta.hpp"
#include "terrain/world_size.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace terrain::unreal {

// The folder of the edits of a seed under root ("seed_<seed>").
[[nodiscard]] std::filesystem::path editSaveFolder(const std::filesystem::path& root,
                                                   std::uint64_t seed);

// The metadata written with the edits of a seed in a world of that size.
[[nodiscard]] WorldMeta editWorldMeta(std::uint64_t seed, const WorldSize& world);

// What a load found.
struct EditLoad {
    bool found = false;                 // a saved world is there (world.meta or its .bak)
    ModificationStore modifications;    // its edits, empty when not found or refused
    std::string error;                  // why it was refused (unreadable, another seed...)
    std::vector<std::string> warnings;  // files read from their .bak copy
};

// Reads the edits of seed in folder. Never throws: a folder without a world is not found, a world
// that cannot be read (damaged without a sound .bak, newer, another seed or another size of world)
// gives its reason in error and no edits.
[[nodiscard]] EditLoad loadEdits(const std::filesystem::path& folder, std::uint64_t seed,
                                 const WorldSize& world);

// The thread writing the edits of a session. The thread owning it marks the chunks of the edits
// done (markEdited) and asks for a save (save); the saver reads a copy of the edits on its thread
// (snapshot, thread-safe: TerrainTiles::modifications) and writes the regions marked since the last
// save, with world.meta. Saves asked while one runs are merged into the next one.
class EditSaver {
public:
    using Snapshot = std::function<ModificationStore()>;

    EditSaver(std::filesystem::path folder, WorldMeta meta, Snapshot snapshot);
    // Ends the save running; a save asked and not started is written first (flush).
    ~EditSaver();

    EditSaver(const EditSaver&) = delete;
    EditSaver& operator=(const EditSaver&) = delete;

    void markEdited(std::span<const ChunkCoord> chunks);
    // Regions written again by the next save whatever they hold (a region without edits loses its
    // file): the regions of a folder whose save is replaced (savedRegions).
    void markRegions(std::span<const RegionCoord> regions);
    // Regions marked and not yet handed to a save.
    [[nodiscard]] std::size_t dirtyRegions() const;
    // Hands the regions marked to the thread; false (nothing done) when none is marked.
    bool save();
    // Until every save asked is written.
    void flush();

    // What the saves did, for the log.
    struct Report {
        std::size_t saves = 0;            // written
        std::size_t regions = 0;          // regions of the last save
        double lastMilliseconds = 0.0;    // the last save, its copy of the edits included
        std::string lastError;            // the last save refused (the files left as they were)
    };
    [[nodiscard]] Report report() const;

private:
    void loop();

    std::filesystem::path folder_;
    WorldMeta meta_;
    Snapshot snapshot_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::set<RegionCoord> marked_;  // owner side, not handed yet
    std::set<RegionCoord> pending_; // handed, not yet written
    bool running_ = false;
    bool stopping_ = false;
    Report report_;
    std::jthread thread_; // last member: joined before the others are destroyed
};

} // namespace terrain::unreal
