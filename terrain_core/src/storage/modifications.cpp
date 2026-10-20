#include "terrain/storage/modifications.hpp"

#include "file_commit.hpp"
#include "storage_codec.hpp"
#include "terrain/meshing/density_grid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace terrain {

namespace {

// Blocks are centered on the LOD 0 lattice, one point per meter: the chunk of a lattice
// coordinate is worldToChunk of it.
static_assert(kCellSizeMeters == 1.0);

constexpr storage::Magic kMagic = {'T', 'M', 'O', 'D'};
constexpr const char* kInvalid = "invalid modification file: ";

ChunkCoord plantChunk(double x, double z) {
    return worldToChunk(x, z);
}

int chebyshev(BlockCoord block, int x, int y, int z) {
    return std::max({std::abs(x - block.x), std::abs(y - block.y), std::abs(z - block.z)});
}

[[noreturn]] void invalid(StorageErrorKind kind, const char* what) {
    throw StorageError(kind, std::string(kInvalid) + what);
}

} // namespace

ChunkCoord blockChunk(BlockCoord block) {
    return worldToChunk(block.x, block.z);
}

std::vector<ChunkCoord> blockChunks(BlockCoord block) {
    // Chunk n holds the lattice points [16 n, 16 n + 16]: it meets [low, high] for
    // ceil(low / 16) - 1 = floor((low - 1) / 16) <= n <= floor(high / 16).
    const int low = -kBlockReach - 1;
    const int high = kBlockReach;
    const ChunkCoord first = worldToChunk(block.x + low, block.z + low);
    const ChunkCoord last = worldToChunk(block.x + high, block.z + high);
    std::vector<ChunkCoord> chunks;
    for (int z = first.z; z <= last.z; ++z) {
        for (int x = first.x; x <= last.x; ++x) {
            chunks.push_back({x, z});
        }
    }
    return chunks;
}

float blockDistance(BlockCoord block, int x, int y, int z) {
    constexpr double kHalf = 0.5;
    const std::array<double, 3> q = {std::abs(x - block.x) - kHalf, std::abs(y - block.y) - kHalf,
                                     std::abs(z - block.z) - kHalf};
    double outside = 0.0;
    for (const double v : q) {
        outside += std::max(v, 0.0) * std::max(v, 0.0);
    }
    const double inside = std::min(std::max({q[0], q[1], q[2]}), 0.0);
    return static_cast<float>(std::sqrt(outside) + inside);
}

float editDensity(float density, BlockCoord block, BlockEdit edit, int x, int y, int z) {
    if (chebyshev(block, x, y, z) > kBlockReach) {
        return density;
    }
    const float cube = blockDistance(block, x, y, z);
    return edit == BlockEdit::Added ? std::min(density, cube) : std::max(density, -cube);
}

bool ChunkModifications::empty() const {
    return blocks.empty() && removedPlants.empty() && addedPlants.empty();
}

bool ChunkOrder::operator()(ChunkCoord a, ChunkCoord b) const {
    return std::tie(a.z, a.x) < std::tie(b.z, b.x);
}

ChunkModifications& ModificationStore::edit(ChunkCoord coord) {
    return chunks_[coord];
}

void ModificationStore::pruneIfEmpty(ChunkCoord coord) {
    const auto it = chunks_.find(coord);
    if (it != chunks_.end() && it->second.empty()) {
        chunks_.erase(it);
    }
}

void ModificationStore::setBlock(BlockCoord block, BlockEdit edit) {
    this->edit(blockChunk(block)).blocks[block] = edit;
}

bool ModificationStore::clearBlock(BlockCoord block) {
    const ChunkCoord coord = blockChunk(block);
    const auto it = chunks_.find(coord);
    if (it == chunks_.end() || it->second.blocks.erase(block) == 0) {
        return false;
    }
    pruneIfEmpty(coord);
    return true;
}

std::optional<BlockEdit> ModificationStore::blockAt(BlockCoord block) const {
    const ChunkModifications* modifications = chunk(blockChunk(block));
    if (modifications == nullptr) {
        return std::nullopt;
    }
    const auto it = modifications->blocks.find(block);
    return it == modifications->blocks.end() ? std::nullopt : std::optional(it->second);
}

BlockEdits ModificationStore::blocksIn(int minX, int maxX, int minZ, int maxZ) const {
    BlockEdits edits;
    const BlockCoord low{minX, 0, minZ};
    const BlockCoord high{maxX, 0, maxZ};
    for (int cz = blockChunk(low).z; cz <= blockChunk(high).z; ++cz) {
        for (int cx = blockChunk(low).x; cx <= blockChunk(high).x; ++cx) {
            const ChunkModifications* modifications = chunk({cx, cz});
            if (modifications == nullptr) {
                continue;
            }
            for (const auto& [block, edit] : modifications->blocks) {
                if (block.x >= minX && block.x <= maxX && block.z >= minZ && block.z <= maxZ) {
                    (edit == BlockEdit::Added ? edits.added : edits.removed).push_back(block);
                }
            }
        }
    }
    std::sort(edits.added.begin(), edits.added.end());
    std::sort(edits.removed.begin(), edits.removed.end());
    return edits;
}

float ModificationStore::editedDensity(float density, int x, int y, int z) const {
    const BlockEdits edits =
        blocksIn(x - kBlockReach, x + kBlockReach, z - kBlockReach, z + kBlockReach);
    for (const BlockCoord& block : edits.added) {
        density = editDensity(density, block, BlockEdit::Added, x, y, z);
    }
    for (const BlockCoord& block : edits.removed) {
        density = editDensity(density, block, BlockEdit::Removed, x, y, z);
    }
    return density;
}

void ModificationStore::addPlant(const Plant& plant) {
    edit(plantChunk(plant.x, plant.z)).addedPlants.push_back(plant);
}

void ModificationStore::removePlant(const Plant& plant) {
    const ChunkCoord coord = plantChunk(plant.x, plant.z);
    const auto it = chunks_.find(coord);
    if (it != chunks_.end()) {
        std::vector<Plant>& added = it->second.addedPlants;
        const auto match = std::find(added.begin(), added.end(), plant);
        if (match != added.end()) {
            added.erase(match);
            pruneIfEmpty(coord);
            return;
        }
    }
    removeGeneratedPlant({plant.x, plant.z});
}

void ModificationStore::removeGeneratedPlant(PlantSpot spot) {
    edit(plantChunk(spot.x, spot.z)).removedPlants.insert(spot);
}

bool ModificationStore::isRemoved(const Plant& generated) const {
    const ChunkModifications* modifications = chunk(plantChunk(generated.x, generated.z));
    return modifications != nullptr &&
           modifications->removedPlants.contains({generated.x, generated.z});
}

void ModificationStore::applyPlantEdits(ChunkCoord coord, std::vector<Plant>& plants) const {
    std::erase_if(plants, [this](const Plant& plant) { return isRemoved(plant); });
    if (const ChunkModifications* modifications = chunk(coord)) {
        plants.insert(plants.end(), modifications->addedPlants.begin(),
                      modifications->addedPlants.end());
    }
}

void ModificationStore::merge(ModificationStore&& other) {
    for (const auto& [coord, modifications] : other.chunks_) {
        if (chunks_.contains(coord)) {
            throw std::invalid_argument("merged stores modify the same chunk");
        }
    }
    chunks_.merge(other.chunks_);
}

const ChunkModifications* ModificationStore::chunk(ChunkCoord coord) const {
    const auto it = chunks_.find(coord);
    return it == chunks_.end() ? nullptr : &it->second;
}

std::size_t ModificationStore::blockCount() const {
    std::size_t count = 0;
    for (const auto& [coord, chunk] : chunks_) {
        count += chunk.blocks.size();
    }
    return count;
}

std::size_t ModificationStore::removedPlantCount() const {
    std::size_t count = 0;
    for (const auto& [coord, chunk] : chunks_) {
        count += chunk.removedPlants.size();
    }
    return count;
}

std::size_t ModificationStore::addedPlantCount() const {
    std::size_t count = 0;
    for (const auto& [coord, chunk] : chunks_) {
        count += chunk.addedPlants.size();
    }
    return count;
}

std::uint32_t fnv1a(std::span<const std::uint8_t> bytes) {
    std::uint32_t hash = 2166136261U;
    for (const std::uint8_t byte : bytes) {
        hash = (hash ^ byte) * 16777619U;
    }
    return hash;
}

std::vector<std::uint8_t> serializeModifications(const ModificationStore& store,
                                                 std::uint64_t seed) {
    constexpr std::size_t kHeaderAndChecksum = 24;
    std::size_t size = kHeaderAndChecksum;
    for (const auto& [coord, chunk] : store.byChunk()) {
        size += storage::chunkRecordSize(chunk);
    }
    storage::ByteWriter out(size);
    out.bytes(kMagic);
    out.u32(kModificationFileVersion);
    out.u64(seed);
    out.count(store.byChunk().size());
    for (const auto& [coord, chunk] : store.byChunk()) {
        storage::writeChunk(out, coord, chunk);
    }
    out.u32(fnv1a(out.written()));
    return out.take();
}

SavedModifications deserializeModifications(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kChecksumSize = 4;
    if (bytes.size() < kMagic.size() + kChecksumSize ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        invalid(StorageErrorKind::Invalid, "not a modification file");
    }
    const auto body = bytes.first(bytes.size() - kChecksumSize);
    storage::ByteReader checksum(bytes.last(kChecksumSize), kInvalid);
    if (checksum.u32() != fnv1a(body)) {
        invalid(StorageErrorKind::Corrupt, "checksum mismatch");
    }
    storage::ByteReader in(body.subspan(kMagic.size()), kInvalid);
    const std::uint32_t version = in.u32();
    if (version != kModificationFileVersion) {
        invalid(version > kModificationFileVersion ? StorageErrorKind::NewerVersion
                                                   : StorageErrorKind::Invalid,
                "unsupported version");
    }
    SavedModifications saved;
    saved.seed = in.u64();
    const std::size_t chunks = in.count(storage::kMinChunkRecord);
    std::optional<ChunkCoord> previous;
    for (std::size_t i = 0; i < chunks; ++i) {
        const ChunkCoord coord = storage::readChunkCoord(in);
        if (previous && !ChunkOrder{}(*previous, coord)) {
            in.fail("chunks out of order");
        }
        previous = coord;
        storage::readChunkBody(in, coord, saved.store);
    }
    in.expectEnd("trailing bytes");
    return saved;
}

void saveModifications(const std::filesystem::path& path, const ModificationStore& store,
                       std::uint64_t seed) {
    const storage::FileWrite write{path, serializeModifications(store, seed)};
    storage::commitFiles({&write, 1}, {}, {});
}

SavedModifications loadModifications(const std::filesystem::path& path) {
    return deserializeModifications(storage::readFileBytes(path));
}

} // namespace terrain
