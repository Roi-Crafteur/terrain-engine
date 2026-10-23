#pragma once

// Compressed masks of the sections of a chunk, shared by the files of src/storage only.
//
// The blocks of the sections are coded one after the other by a binary range coder (the
// carry-less scheme of LZMA, integers only: every platform writes and reads the same bytes).
// Each block costs two decisions, edited or not, then removed or added, each with an adaptive
// probability chosen by the states of the 3 blocks before it in the section (x - 1, z - 1 and
// y - 1, the generated state outside the section). The probabilities start even, learn fast,
// then slowly; they carry over from one section of the chunk to the next, so that the sections
// of a column of a chunk share what they learned. The blocks of a section are read by index
// (x fastest, then z, then y).

#include "terrain/storage/block_section.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace terrain::storage {

// An adaptive probability of a 0, out of 65536.
struct BitModel {
    std::uint16_t zero = 1U << 15;
    std::uint8_t seen = 0;
};

// States of the 3 previous neighbors of a block: 27 contexts, for each decision.
struct SectionModel {
    static constexpr std::size_t kContexts = 27;
    std::array<BitModel, kContexts> edited{};
    std::array<BitModel, kContexts> removed{};
};

class SectionEncoder {
public:
    // Codes the blocks of the section after those of the previous ones.
    void add(const BlockSection& section);
    // The bytes of every section added.
    [[nodiscard]] std::vector<std::uint8_t> finish();

private:
    void encode(BitModel& model, bool bit);
    void shiftLow();

    SectionModel model_;
    std::vector<std::uint8_t> out_;
    std::uint64_t low_ = 0;
    std::uint32_t range_ = 0xFFFFFFFFU;
    std::uint8_t cache_ = 0;
    std::uint64_t pending_ = 1;
    bool first_ = true; // the first byte of the scheme is always 0: never written
};

// Reads the sections of a SectionEncoder, in order. A read past the bytes yields zeros and
// marks the decoder overrun: exact() tells whether the bytes were read exactly.
class SectionDecoder {
public:
    explicit SectionDecoder(std::span<const std::uint8_t> bytes);

    [[nodiscard]] BlockSection next();
    // Every byte read, none beyond: the bytes were those of the sections read.
    [[nodiscard]] bool exact() const { return !overrun_ && position_ == bytes_.size(); }

private:
    bool decode(BitModel& model);
    std::uint8_t nextByte();

    SectionModel model_;
    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
    std::uint32_t range_ = 0xFFFFFFFFU;
    std::uint32_t code_ = 0;
    bool overrun_ = false;
};

} // namespace terrain::storage
