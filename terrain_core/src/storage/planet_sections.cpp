// The sections of the planet files (planet_sections.hpp).
#include "planet_sections.hpp"

#include "terrain/storage/planet_file.hpp"
#include "terrain/storage/storage_error.hpp"

#include <algorithm>
#include <string>

namespace terrain::storage {

namespace {

std::string tagText(const Magic& tag) {
    return {tag.begin(), tag.end()};
}

} // namespace

std::span<const PlanetSection> planetSections() {
    // One row per section, its tag unique: {{'T','A','G','S'}, present, write, read}.
    static const std::vector<PlanetSection> sections = {};
    return sections;
}

std::uint32_t planetFileVersion(const std::string& flags,
                                const std::vector<SectionBytes>& sections) {
    return flags.empty() && sections.empty() ? 1U : kPlanetFileVersion;
}

void checkFlags(FrameReader& in, const std::string& wanted, const std::string& file) {
    std::string stored;
    if (in.version() >= 2) {
        const std::span<const std::uint8_t> text = in.take(in.count(1));
        stored.assign(text.begin(), text.end());
    }
    if (stored != wanted) {
        in.finish();
        throw PlanetFlagsError(file + ": the planet level of the experimental flags \"" + stored +
                                   "\", not \"" + wanted +
                                   "\" as asked: never read nor replaced (build it with these "
                                   "flags in another file, or ask for its flags)",
                               stored, wanted);
    }
}

std::vector<SectionBytes> encodeSections(const PlanetLevel& level,
                                         std::span<const PlanetSection> table) {
    std::vector<SectionBytes> out;
    for (const PlanetSection& section : table) {
        if (section.present(level)) {
            ByteWriter bytes;
            section.write(bytes, level);
            out.push_back({section.tag, bytes.take()});
        }
    }
    return out;
}

void readSections(FrameReader& in, PlanetLevel::Parts& parts,
                  std::span<const PlanetSection> table) {
    const std::size_t count = in.count(12); // a tag and a size at least
    std::vector<Magic> seen;
    for (std::size_t n = 0; n < count; ++n) {
        Magic tag{};
        const std::span<const std::uint8_t> letters = in.take(tag.size());
        std::copy(letters.begin(), letters.end(), tag.begin());
        const std::uint64_t size = in.u64();
        const auto row = std::find_if(table.begin(), table.end(),
                                      [&](const PlanetSection& s) { return s.tag == tag; });
        if (row == table.end() || std::find(seen.begin(), seen.end(), tag) != seen.end()) {
            in.fail("stale: a section \"" + tagText(tag) + "\" unknown here or repeated");
        }
        seen.push_back(tag);
        if (size > in.remaining()) {
            in.fail("truncated");
        }
        try {
            ByteReader bytes(in.take(static_cast<std::size_t>(size)),
                             "section \"" + tagText(tag) + "\": ");
            row->read(bytes, parts);
            bytes.expectEnd("bytes left unread");
        } catch (const StorageError& e) {
            in.fail(e.what()); // the damage of the file first, if any
        }
    }
}

} // namespace terrain::storage
