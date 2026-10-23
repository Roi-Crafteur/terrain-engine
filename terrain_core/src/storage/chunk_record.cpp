#include "chunk_record.hpp"

#include "section_codec.hpp"
#include "storage_codec.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace terrain::storage {

namespace {

enum class Kind : std::uint8_t { Removed = 1, Added = 2, List = 3, Coded = 4 };

// The sections of the 32-bit block heights.
constexpr std::int64_t kLowestSection = sectionOf(std::numeric_limits<std::int32_t>::min());
constexpr std::int64_t kHighestSection = sectionOf(std::numeric_limits<std::int32_t>::max());

struct Entry {
    std::int32_t y = 0;
    Kind kind = Kind::Coded;
    std::uint32_t count = 1; // sections of a run
    const BlockSection* section = nullptr;
};

std::uint32_t zigzag(std::int32_t value) {
    return (static_cast<std::uint32_t>(value) << 1) ^ static_cast<std::uint32_t>(value >> 31);
}

std::int32_t unzigzag(std::uint32_t value) {
    return static_cast<std::int32_t>((value >> 1) ^ (~(value & 1U) + 1U));
}

Kind kindOf(const BlockSection& section) {
    if (const std::optional<BlockEdit> edit = section.uniform()) {
        return *edit == BlockEdit::Removed ? Kind::Removed : Kind::Added;
    }
    return section.count() <= kListLimit ? Kind::List : Kind::Coded;
}

// The sections of the chunk as entries: consecutive filled sections of one edit merge.
std::vector<Entry> entriesOf(const ChunkModifications& chunk) {
    std::vector<Entry> entries;
    for (const auto& [y, section] : chunk.sections) {
        const Kind kind = kindOf(section);
        const bool run = kind == Kind::Removed || kind == Kind::Added;
        if (run && !entries.empty() && entries.back().kind == kind &&
            entries.back().y + static_cast<std::int64_t>(entries.back().count) == y) {
            ++entries.back().count;
        } else {
            entries.push_back({y, kind, 1, &section});
        }
    }
    return entries;
}

void writeList(ByteWriter& out, const BlockSection& section) {
    out.varint(static_cast<std::uint32_t>(section.count()));
    int previous = -1;
    section.forEach([&out, &previous](int index, BlockEdit edit) {
        const auto step = static_cast<std::uint32_t>(index - previous - 1);
        out.varint(step * 2 + (edit == BlockEdit::Removed ? 1U : 0U));
        previous = index;
    });
}

BlockSection readList(ByteReader& in) {
    const std::uint32_t count = in.varint();
    if (count == 0 || count > static_cast<std::uint32_t>(kListLimit)) {
        in.fail("invalid list of blocks");
    }
    BlockSection section;
    std::int64_t previous = -1;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t value = in.varint();
        const std::int64_t index = previous + 1 + (value >> 1);
        if (index >= kSectionBlocks) {
            in.fail("block outside its section");
        }
        section.set(static_cast<int>(index),
                    (value & 1U) != 0 ? BlockEdit::Removed : BlockEdit::Added);
        previous = index;
    }
    return section;
}

// Reads the entries, the coded sections left for later (in coded, by increasing y).
void readEntries(ByteReader& in, ChunkCoord coord, ModificationStore& store,
                 std::vector<std::int32_t>& coded) {
    const std::uint32_t entries = in.varint();
    if (entries > in.remaining() / 2) {
        in.fail("truncated");
    }
    std::int64_t next = 0; // the first section after the previous entry
    std::optional<Kind> previousRun;
    for (std::uint32_t i = 0; i < entries; ++i) {
        const std::uint32_t step = in.varint();
        const std::int64_t y = i == 0 ? unzigzag(step) : next + step;
        const auto kind = static_cast<Kind>(in.u8());
        if (kind < Kind::Removed || kind > Kind::Coded) {
            in.fail("unknown kind of section");
        }
        const bool run = kind == Kind::Removed || kind == Kind::Added;
        const std::int64_t count = run ? in.varint() : 1;
        if (y < kLowestSection || count == 0 || y + count - 1 > kHighestSection) {
            in.fail("section beyond the heights");
        }
        if (run && i > 0 && step == 0 && previousRun == kind) {
            in.fail("runs of sections that should be one");
        }
        const auto sectionY = static_cast<std::int32_t>(y);
        if (kind == Kind::List) {
            store.setSection(coord, sectionY, readList(in));
        } else if (kind == Kind::Coded) {
            coded.push_back(sectionY);
        } else {
            const BlockEdit edit = kind == Kind::Removed ? BlockEdit::Removed : BlockEdit::Added;
            for (std::int64_t s = 0; s < count; ++s) {
                store.setSection(coord, static_cast<std::int32_t>(y + s),
                                 BlockSection::filled(edit));
            }
        }
        previousRun = run ? std::optional(kind) : std::nullopt;
        next = y + count;
    }
}

void readCoded(ByteReader& in, ChunkCoord coord, ModificationStore& store,
               const std::vector<std::int32_t>& coded) {
    const std::uint32_t size = in.varint();
    const std::span<const std::uint8_t> masks = in.take(size);
    if (coded.empty()) {
        if (size != 0) {
            in.fail("masks without coded sections");
        }
        return;
    }
    SectionDecoder decoder(masks);
    for (const std::int32_t y : coded) {
        BlockSection section = decoder.next();
        if (section.count() <= kListLimit || section.uniform()) {
            in.fail("coded section that should be a list or a run");
        }
        store.setSection(coord, y, std::move(section));
    }
    if (!decoder.exact()) {
        in.fail("damaged masks");
    }
}

} // namespace

void writeChunkV2(ByteWriter& out, ChunkCoord coord, const ChunkModifications& chunk) {
    out.i32(coord.x);
    out.i32(coord.z);
    const std::size_t sizeAt = out.size();
    out.u32(0); // the size of the rest, written below
    const std::vector<Entry> entries = entriesOf(chunk);
    out.varint(static_cast<std::uint32_t>(entries.size()));
    SectionEncoder encoder;
    bool anyCoded = false;
    std::int64_t next = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        out.varint(i == 0 ? zigzag(entry.y) : static_cast<std::uint32_t>(entry.y - next));
        out.u8(static_cast<std::uint8_t>(entry.kind));
        if (entry.kind == Kind::Removed || entry.kind == Kind::Added) {
            out.varint(entry.count);
        } else if (entry.kind == Kind::List) {
            writeList(out, *entry.section);
        } else {
            encoder.add(*entry.section);
            anyCoded = true;
        }
        next = entry.y + static_cast<std::int64_t>(entry.count);
    }
    const std::vector<std::uint8_t> masks =
        anyCoded ? encoder.finish() : std::vector<std::uint8_t>{};
    out.varint(static_cast<std::uint32_t>(masks.size()));
    out.bytes(masks);
    writePlants(out, chunk);
    out.patchU32(sizeAt, static_cast<std::uint32_t>(out.size() - sizeAt - 4));
}

void readChunkBodyV2(ByteReader& in, ChunkCoord coord, ModificationStore& store) {
    const std::uint32_t size = in.u32();
    ByteReader record = in.sub(size, "");
    std::vector<std::int32_t> coded;
    readEntries(record, coord, store, coded);
    readCoded(record, coord, store, coded);
    readPlants(record, coord, store);
    record.expectEnd("bytes left in a chunk record");
    if (store.chunk(coord) == nullptr) {
        record.fail("empty chunk");
    }
}

} // namespace terrain::storage
