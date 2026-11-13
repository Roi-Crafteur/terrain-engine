#pragma once

// The sections of a planet file of version 2 (planet_file.hpp), shared by the files of
// src/storage only: what an agent adds to the planet level, after the level of version 1. On disk:
// a count, then each section: a tag of 4 letters, the size of its bytes (u64), its bytes.
//
// A level with no section and no experimental flag is written in version 1, the bytes of main.
// A file with a section this reader does not know, or the same tag twice, is stale: built again,
// never read wrong.
//
// To add a section (agent 31: the discharge of the land cells, say):
// - its data in PlanetLevel::Parts (planet_level.hpp), in the checksum of the level;
// - one row in the table of planetSections() (planet_sections.cpp): a tag of 4 letters not yet
//   taken, then three functions:
//   - present: whether the level holds the section (written only then, so the files of the
//     levels without it keep their bytes: hold it only when the flag that makes it is on);
//   - write: its bytes, held in memory once while the file is written (keep them to tens of MB);
//   - read: from its bytes into the parts, every byte read (any other count fails);
// - a case in a test: the section written and read back, the level to the bit.
//
// Tags taken: "PROV", reserved for the provenance of the relief (agent 55, every file once it
// comes: every file is then of version 2, its flags text empty without flags, and "the bytes of
// main" become "the level of main": the same checksum, the same bytes between the flags and the
// sections); "TEST", for the tests.

#include "byte_stream.hpp"
#include "frame_reader.hpp"
#include "storage_codec.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/world_size.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace terrain::storage {

struct PlanetSection {
    Magic tag;
    bool (*present)(const PlanetLevel& level);
    void (*write)(ByteWriter& out, const PlanetLevel& level);
    void (*read)(ByteReader& in, PlanetLevel::Parts& parts);
};

// The sections of the planet files (empty until an agent adds its row).
[[nodiscard]] std::span<const PlanetSection> planetSections();

// The bytes of a section of a level.
struct SectionBytes {
    Magic tag;
    std::vector<std::uint8_t> bytes;
};

// The version of the file of a level with flags and sections: 1 with neither (the bytes of main),
// kPlanetFileVersion (2) otherwise.
[[nodiscard]] std::uint32_t planetFileVersion(const std::string& flags,
                                              const std::vector<SectionBytes>& sections);

// The experimental flags of a file of version 2, first in its body (their canonical text).
template <class Out> void writeFlags(Out& out, const std::string& flags) {
    out.count(flags.size());
    out.bytes({reinterpret_cast<const std::uint8_t*>(flags.data()), flags.size()});
}

// The flags of the file of in ("" before version 2), which must be wanted: otherwise
// PlanetFlagsError once its checksum holds (a damaged file is Corrupt). file names it.
void checkFlags(FrameReader& in, const std::string& wanted, const std::string& file);

// The sections of table level holds, in the order of table.
[[nodiscard]] std::vector<SectionBytes> encodeSections(const PlanetLevel& level,
                                                       std::span<const PlanetSection> table);

template <class Out> void writeSections(Out& out, const std::vector<SectionBytes>& sections) {
    out.count(sections.size());
    for (const SectionBytes& s : sections) {
        out.bytes(s.tag);
        out.u64(s.bytes.size());
        out.bytes(s.bytes);
    }
}

// Reads the sections of in into parts by the rows of table: an unknown or repeated tag, a section
// read wrong (too few or too many of its bytes), fail as FrameReader::fail (Corrupt first when the
// file is damaged).
void readSections(FrameReader& in, PlanetLevel::Parts& parts,
                  std::span<const PlanetSection> table);

// The planet file with the sections of table (planet_file.hpp, whose functions use
// planetSections()): for the tests.
void savePlanetFile(const std::filesystem::path& file, const PlanetLevel& level,
                    std::uint64_t seed, const WorldSize& world, const std::string& flags,
                    std::span<const PlanetSection> table);
[[nodiscard]] PlanetLevel loadPlanetFile(const std::filesystem::path& file, std::uint64_t seed,
                                         const PlanetParams& params, const WorldSize& world,
                                         const std::string& flags,
                                         std::span<const PlanetSection> table);

} // namespace terrain::storage
