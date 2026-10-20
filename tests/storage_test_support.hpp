#pragma once

// Helpers of the storage tests: temporary directories, file bytes, frames sealed again after a
// change, a file held open by another reader, sample modifications and metadata, expected
// errors.

#include "terrain/storage/modifications.hpp"
#include "terrain/storage/storage_error.hpp"
#include "terrain/storage/world_meta.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace storage_test {

inline constexpr std::uint64_t kSeed = 20261019;

// An empty directory in the temporary directory of the system.
inline std::filesystem::path freshDirectory(const std::string& name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

inline std::vector<std::uint8_t> fileBytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline void writeBytes(const std::filesystem::path& file, std::span<const std::uint8_t> bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

// Every regular file under dir with its bytes, by path relative to dir.
inline std::map<std::string, std::vector<std::uint8_t>> filesUnder(
    const std::filesystem::path& dir) {
    std::map<std::string, std::vector<std::uint8_t>> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            files[entry.path().lexically_relative(dir).generic_string()] = fileBytes(entry.path());
        }
    }
    return files;
}

inline void putU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
    }
}

inline void putU64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU);
    }
}

inline std::uint64_t getU64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (8 * i);
    }
    return value;
}

// A world.meta or region file whose size and checksum are written again after a change of its
// bytes, so that only the change makes it invalid.
inline std::vector<std::uint8_t> resealed(std::vector<std::uint8_t> bytes) {
    putU64(bytes, 8, bytes.size());
    putU32(bytes, bytes.size() - 4,
           terrain::fnv1a(std::span<const std::uint8_t>(bytes).first(bytes.size() - 4)));
    return bytes;
}

// The kind of StorageError that action throws (and its message), nullopt if it throws none.
inline std::optional<terrain::StorageErrorKind> errorOf(const std::function<void()>& action,
                                                        std::string* message = nullptr) {
    try {
        action();
    } catch (const terrain::StorageError& error) {
        if (message != nullptr) {
            *message = error.what();
        }
        return error.kind();
    }
    return std::nullopt;
}

inline bool isDamage(std::optional<terrain::StorageErrorKind> kind) {
    return kind == terrain::StorageErrorKind::Truncated ||
           kind == terrain::StorageErrorKind::Corrupt;
}

// Holds a file open for reading in another thread for a while, as another program would (a
// viewer, an antivirus, a backup). Waits until the file is open; the destructor waits until it
// is closed again.
class BriefReader {
public:
    BriefReader(std::filesystem::path file, std::chrono::milliseconds duration)
        : thread_([this, file = std::move(file), duration] {
              std::ifstream in(file, std::ios::binary);
              opened_.set_value();
              std::this_thread::sleep_for(duration);
          }) {
        openedFuture_.wait();
    }
    BriefReader(const BriefReader&) = delete;
    BriefReader& operator=(const BriefReader&) = delete;
    ~BriefReader() { thread_.join(); }

private:
    std::promise<void> opened_;
    std::future<void> openedFuture_ = opened_.get_future();
    std::thread thread_;
};

// The rest of the bytes a reader opened before can read.
inline std::vector<std::uint8_t> readAll(std::ifstream& in) {
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline terrain::Plant tree(double x, double z) {
    return {terrain::PlantType::Tree, x, z, 12.5f, 1.1f, 0.3f};
}

// Modifications in five regions of four region groups, negative coordinates included.
inline terrain::ModificationStore sampleStore() {
    using terrain::BlockEdit;
    terrain::ModificationStore store;
    store.setBlock({3, 40, -2}, BlockEdit::Added);         // chunk (0, -1), region (0, -1)
    store.setBlock({-20, -7, 100}, BlockEdit::Removed);    // chunk (-2, 6), region (-1, 0)
    store.setBlock({-21, -7, 100}, BlockEdit::Removed);
    store.setBlock({600, 5, 600}, BlockEdit::Added);       // chunk (37, 37), region (1, 1)
    store.setBlock({20000, 0, -40000}, BlockEdit::Added);  // region (39, -79), group (1, -3)
    store.removePlant(tree(17.5, -0.125));                 // chunk (1, -1)
    store.removePlant(tree(-100.0625, 3.0));               // chunk (-7, 0)
    store.addPlant({terrain::PlantType::Cactus, 18.0, -1.0, 2.0f, 0.9f, 4.0f});
    store.addPlant({terrain::PlantType::Boulder, 18.0, -1.0, 2.0f, 0.9f, 4.0f});
    store.addPlant({terrain::PlantType::Flower, -9000.5, 7000.25, 31.0f, 0.5f, 1.5f});
    return store;
}

inline terrain::WorldMeta sampleMeta() {
    terrain::WorldMeta meta;
    meta.name = "Vall\xC3\xA9"
                "e des Brumes"; // "Vallée des Brumes", in UTF-8 on every compiler
    meta.seed = kSeed;
    meta.generation = {{"heightfield.macro_continent", 0.25}, {"plates.amplitude", 0.5}};
    meta.createdAt = 1'790'000'000;
    meta.savedAt = 1'790'003'600;
    meta.spawn = {1234.5, 87.25, -6789.75};
    meta.playTimeSeconds = 3600;
    return meta;
}

} // namespace storage_test
