#include "terrain/storage/modifications.hpp"

#include "terrain/meshing/density_grid.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

namespace terrain {

namespace {

// Blocks are centered on the LOD 0 lattice, one point per meter: the chunk of a lattice
// coordinate is worldToChunk of it.
static_assert(kCellSizeMeters == 1.0);

constexpr std::array<std::uint8_t, 4> kMagic = {'T', 'M', 'O', 'D'};

ChunkCoord plantChunk(double x, double z) {
    return worldToChunk(x, z);
}

int chebyshev(BlockCoord block, int x, int y, int z) {
    return std::max({std::abs(x - block.x), std::abs(y - block.y), std::abs(z - block.z)});
}

// Bytes in little-endian order.
class Writer {
public:
    void bytes(std::span<const std::uint8_t> data) {
        out_.insert(out_.end(), data.begin(), data.end());
    }
    void u8(std::uint8_t value) { out_.push_back(value); }
    void u32(std::uint32_t value) { little(value, 4); }
    void u64(std::uint64_t value) { little(value, 8); }
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void f64(double value) { u64(std::bit_cast<std::uint64_t>(value)); }
    void count(std::size_t value) { u32(static_cast<std::uint32_t>(value)); }
    std::vector<std::uint8_t> take() { return std::move(out_); }
    [[nodiscard]] std::span<const std::uint8_t> written() const { return out_; }

private:
    void little(std::uint64_t value, int size) {
        for (int i = 0; i < size; ++i) {
            out_.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
        }
    }

    std::vector<std::uint8_t> out_;
};

[[noreturn]] void invalid(const char* what) {
    throw std::runtime_error(std::string("invalid modification file: ") + what);
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

    std::uint8_t u8() { return static_cast<std::uint8_t>(little(1)); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(little(4)); }
    std::uint64_t u64() { return little(8); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    float f32() { return finite(std::bit_cast<float>(u32())); }
    double f64() { return finite(std::bit_cast<double>(u64())); }
    // A count of records of recordSize bytes, which must fit in the bytes left.
    std::size_t count(std::size_t recordSize) {
        const std::size_t value = u32();
        if (value > remaining() / recordSize) {
            invalid("truncated");
        }
        return value;
    }
    [[nodiscard]] std::size_t remaining() const { return data_.size() - position_; }

private:
    std::uint64_t little(int size) {
        if (remaining() < static_cast<std::size_t>(size)) {
            invalid("truncated");
        }
        std::uint64_t value = 0;
        for (int i = 0; i < size; ++i) {
            value |= static_cast<std::uint64_t>(data_[position_++]) << (8 * i);
        }
        return value;
    }
    template <class T>
    static T finite(T value) {
        if (!std::isfinite(value)) {
            invalid("non-finite value");
        }
        return value;
    }

    std::span<const std::uint8_t> data_;
    std::size_t position_ = 0;
};

// Record sizes in the file.
constexpr std::size_t kBlockRecord = 13;
constexpr std::size_t kSpotRecord = 16;
constexpr std::size_t kPlantRecord = 29;

void writeChunk(Writer& out, ChunkCoord coord, const ChunkModifications& chunk) {
    out.i32(coord.x);
    out.i32(coord.z);
    out.count(chunk.blocks.size());
    for (const auto& [block, edit] : chunk.blocks) {
        out.i32(block.x);
        out.i32(block.y);
        out.i32(block.z);
        out.u8(static_cast<std::uint8_t>(edit));
    }
    out.count(chunk.removedPlants.size());
    for (const PlantSpot& spot : chunk.removedPlants) {
        out.f64(spot.x);
        out.f64(spot.z);
    }
    out.count(chunk.addedPlants.size());
    for (const Plant& plant : chunk.addedPlants) {
        out.u8(static_cast<std::uint8_t>(plant.type));
        out.f64(plant.x);
        out.f64(plant.z);
        out.f32(plant.y);
        out.f32(plant.scale);
        out.f32(plant.rotation);
    }
}

void readBlocks(Reader& in, ChunkCoord coord, ModificationStore& store) {
    const std::size_t count = in.count(kBlockRecord);
    for (std::size_t i = 0; i < count; ++i) {
        BlockCoord block;
        block.x = in.i32();
        block.y = in.i32();
        block.z = in.i32();
        const std::uint8_t edit = in.u8();
        if (edit > static_cast<std::uint8_t>(BlockEdit::Removed)) {
            invalid("unknown block edit");
        }
        if (blockChunk(block) != coord || store.blockAt(block)) {
            invalid("misplaced or repeated block");
        }
        store.setBlock(block, static_cast<BlockEdit>(edit));
    }
}

void readPlants(Reader& in, ChunkCoord coord, ModificationStore& store) {
    const std::size_t removed = in.count(kSpotRecord);
    for (std::size_t i = 0; i < removed; ++i) {
        const double x = in.f64();
        const double z = in.f64();
        const Plant probe{PlantType::Tree, x, z};
        if (plantChunk(x, z) != coord || store.isRemoved(probe)) {
            invalid("misplaced or repeated removed plant");
        }
        store.removeGeneratedPlant({x, z});
    }
    const std::size_t added = in.count(kPlantRecord);
    for (std::size_t i = 0; i < added; ++i) {
        Plant plant;
        const std::uint8_t type = in.u8();
        if (type >= kPlantTypeCount) {
            invalid("unknown plant type");
        }
        plant.type = static_cast<PlantType>(type);
        plant.x = in.f64();
        plant.z = in.f64();
        plant.y = in.f32();
        plant.scale = in.f32();
        plant.rotation = in.f32();
        if (plantChunk(plant.x, plant.z) != coord) {
            invalid("misplaced added plant");
        }
        store.addPlant(plant);
    }
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
    Writer out;
    out.bytes(kMagic);
    out.u32(kModificationFileVersion);
    out.u64(seed);
    out.count(store.byChunk().size());
    for (const auto& [coord, chunk] : store.byChunk()) {
        writeChunk(out, coord, chunk);
    }
    out.u32(fnv1a(out.written()));
    return out.take();
}

SavedModifications deserializeModifications(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kChecksumSize = 4;
    if (bytes.size() < kMagic.size() + kChecksumSize ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        invalid("not a modification file");
    }
    const auto body = bytes.first(bytes.size() - kChecksumSize);
    Reader checksum(bytes.last(kChecksumSize));
    if (checksum.u32() != fnv1a(body)) {
        invalid("checksum mismatch");
    }
    Reader in(body.subspan(kMagic.size()));
    if (in.u32() != kModificationFileVersion) {
        invalid("unsupported version");
    }
    SavedModifications saved;
    saved.seed = in.u64();
    constexpr std::size_t kMinChunkRecord = 20;
    const std::size_t chunks = in.count(kMinChunkRecord);
    std::optional<ChunkCoord> previous;
    for (std::size_t i = 0; i < chunks; ++i) {
        const ChunkCoord coord{in.i32(), in.i32()};
        if (previous && !ChunkOrder{}(*previous, coord)) {
            invalid("chunks out of order");
        }
        previous = coord;
        readBlocks(in, coord, saved.store);
        readPlants(in, coord, saved.store);
        if (saved.store.chunk(coord) == nullptr) {
            invalid("empty chunk");
        }
    }
    if (in.remaining() != 0) {
        invalid("trailing bytes");
    }
    return saved;
}

void saveModifications(const std::filesystem::path& path, const ModificationStore& store,
                       std::uint64_t seed) {
    const std::vector<std::uint8_t> bytes = serializeModifications(store, seed);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::error_code ignored;
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        file.close();
        if (!file) {
            std::filesystem::remove(temporary, ignored);
            throw std::runtime_error("cannot write " + temporary.string());
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error("cannot replace " + path.string() + ": " + error.message());
    }
}

SavedModifications loadModifications(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
    if (file.bad()) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return deserializeModifications(bytes);
}

} // namespace terrain
