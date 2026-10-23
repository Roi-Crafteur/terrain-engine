#include "section_codec.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace terrain::storage {

namespace {

constexpr int kProbabilityBits = 16;
constexpr std::uint32_t kOne = 1U << kProbabilityBits;
constexpr std::uint32_t kTop = 1U << 24;
// The adaptation: halfway at the first decision, then a step of 1/32 from the fifth on.
constexpr int kSlowestShift = 5;
// The probabilities stay in [1/2048, 1 - 1/2048]: an unexpected bit costs at most 11 bits.
constexpr std::uint32_t kMinZero = 32;
constexpr std::uint32_t kMaxZero = kOne - kMinZero;

// States of the blocks of a section by index: 0 generated, 1 removed, 2 added.
using States = std::array<std::uint8_t, kSectionBlocks>;
constexpr std::uint8_t kRemoved = 1;
constexpr std::uint8_t kAdded = 2;

void update(BitModel& model, bool bit) {
    const int shift = std::min(model.seen + 1, kSlowestShift);
    std::uint32_t zero = model.zero;
    if (bit) {
        zero -= zero >> shift;
    } else {
        zero += (kOne - zero) >> shift;
    }
    model.zero = static_cast<std::uint16_t>(std::clamp(zero, kMinZero, kMaxZero));
    if (model.seen < kSlowestShift) {
        ++model.seen;
    }
}

// Context of a block: the states of the blocks at x - 1, z - 1 and y - 1 in the section.
std::size_t contextOf(const States& states, int index) {
    const auto at = [&states](int i) {
        return static_cast<std::size_t>(states[static_cast<std::size_t>(i)]);
    };
    const std::size_t west = (index & 15) != 0 ? at(index - 1) : 0;
    const std::size_t south = ((index >> 4) & 15) != 0 ? at(index - kSectionSide) : 0;
    const std::size_t below = index >= kSectionSide * kSectionSide
                                  ? at(index - kSectionSide * kSectionSide)
                                  : 0;
    return (west * 3 + south) * 3 + below;
}

} // namespace

void SectionEncoder::add(const BlockSection& section) {
    States states{};
    section.forEach([&states](int index, BlockEdit edit) {
        states[static_cast<std::size_t>(index)] = edit == BlockEdit::Removed ? kRemoved : kAdded;
    });
    for (int index = 0; index < kSectionBlocks; ++index) {
        const std::size_t context = contextOf(states, index);
        const std::uint8_t state = states[static_cast<std::size_t>(index)];
        encode(model_.edited[context], state != 0);
        if (state != 0) {
            encode(model_.removed[context], state == kRemoved);
        }
    }
}

std::vector<std::uint8_t> SectionEncoder::finish() {
    for (int i = 0; i < 5; ++i) {
        shiftLow();
    }
    return std::move(out_);
}

void SectionEncoder::encode(BitModel& model, bool bit) {
    const std::uint32_t bound = (range_ >> kProbabilityBits) * model.zero;
    if (bit) {
        low_ += bound;
        range_ -= bound;
    } else {
        range_ = bound;
    }
    update(model, bit);
    while (range_ < kTop) {
        range_ <<= 8;
        shiftLow();
    }
}

void SectionEncoder::shiftLow() {
    if (static_cast<std::uint32_t>(low_) < 0xFF000000U || (low_ >> 32) != 0) {
        const auto carry = static_cast<std::uint8_t>(low_ >> 32);
        std::uint8_t byte = cache_;
        do {
            const auto value = static_cast<std::uint8_t>(byte + carry);
            if (first_) {
                assert(value == 0);
                first_ = false;
            } else {
                out_.push_back(value);
            }
            byte = 0xFF;
        } while (--pending_ != 0);
        cache_ = static_cast<std::uint8_t>((low_ >> 24) & 0xFFU);
    }
    ++pending_;
    low_ = (low_ & 0x00FFFFFFU) << 8;
}

SectionDecoder::SectionDecoder(std::span<const std::uint8_t> bytes) : bytes_(bytes) {
    for (int i = 0; i < 4; ++i) {
        code_ = (code_ << 8) | nextByte();
    }
}

BlockSection SectionDecoder::next() {
    States states{};
    SectionMask removed{};
    SectionMask added{};
    for (int index = 0; index < kSectionBlocks; ++index) {
        const std::size_t context = contextOf(states, index);
        if (!decode(model_.edited[context])) {
            continue;
        }
        const bool isRemoved = decode(model_.removed[context]);
        states[static_cast<std::size_t>(index)] = isRemoved ? kRemoved : kAdded;
        (isRemoved ? removed : added)[static_cast<std::size_t>(index / 64)] |=
            std::uint64_t{1} << (index % 64);
    }
    return BlockSection::fromMasks(removed, added);
}

bool SectionDecoder::decode(BitModel& model) {
    const std::uint32_t bound = (range_ >> kProbabilityBits) * model.zero;
    const bool bit = code_ >= bound;
    if (bit) {
        code_ -= bound;
        range_ -= bound;
    } else {
        range_ = bound;
    }
    update(model, bit);
    while (range_ < kTop) {
        range_ <<= 8;
        code_ = (code_ << 8) | nextByte();
    }
    return bit;
}

std::uint8_t SectionDecoder::nextByte() {
    if (position_ == bytes_.size()) {
        overrun_ = true;
        return 0;
    }
    return bytes_[position_++];
}

} // namespace terrain::storage
