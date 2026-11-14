#pragma once

// The parts of the planet file of task 11C (agent 31), each written only when its experimental
// flag is on (experimental_flags.hpp): off, the bytes of the file are those of main.
// - The parameters of the flags smooth_bands, pass_routing, planet_network and sphere_latitude,
//   after the others (writeParams of planet_file.cpp): each behind its tag, only when on.
// - Two sections of the file of version 2 (planet_sections.hpp, their rows in planetSections()):
//   "NETW", the discharge of every land cell (PlanetCells::landDischarge, flag planet_network);
//   "CLIM", the rise of the climate of every land cell (PlanetCells::climateRise, flag
//   sphere_latitude).

#include "byte_stream.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/planet/planet_params.hpp"

#include <cstdint>

namespace terrain::planet_file_network {

template <class Out> void writeFlagParams(Out& out, const PlanetParams& p) {
    const PlanetClimateParams& c = p.climate;
    if (c.smoothBands) {
        out.u8(1);
        for (const double v : {c.bandJoin, c.bandShift, c.bandShiftWavelength}) {
            out.f64(v);
        }
    }
    if (p.links.enabled) {
        out.u8(2);
        out.f64(p.links.tolerance);
    }
    if (p.landDischarge) {
        out.u8(3);
    }
    if (c.sphereLatitude) {
        out.u8(4);
    }
    if (c.meanAltitude) {
        out.u8(5);
    }
    if (p.rivers.underIce) {
        out.u8(6);
    }
}

// The section "NETW": present with the discharge of the land (planet_network).
inline bool networkPresent(const PlanetLevel& level) {
    return !level.cells().landDischarge.empty();
}

inline void writeNetwork(storage::ByteWriter& out, const PlanetLevel& level) {
    const std::vector<std::uint16_t>& q = level.cells().landDischarge;
    out.count(q.size());
    for (const std::uint16_t v : q) {
        out.u16(v);
    }
}

inline void readNetwork(storage::ByteReader& in, PlanetLevel::Parts& parts) {
    std::vector<std::uint16_t>& q = parts.cells.landDischarge;
    q.resize(in.count(2));
    for (std::uint16_t& v : q) {
        v = in.u16();
    }
}

// The section "CLIM": present with the rise of the climate (sphere_latitude).
inline bool climatePresent(const PlanetLevel& level) {
    return !level.cells().climateRise.empty();
}

inline void writeClimate(storage::ByteWriter& out, const PlanetLevel& level) {
    const std::vector<std::uint8_t>& r = level.cells().climateRise;
    out.count(r.size());
    for (const std::uint8_t v : r) {
        out.u8(v);
    }
}

inline void readClimate(storage::ByteReader& in, PlanetLevel::Parts& parts) {
    std::vector<std::uint8_t>& r = parts.cells.climateRise;
    r.resize(in.count(1));
    for (std::uint8_t& v : r) {
        v = in.u8();
    }
}

} // namespace terrain::planet_file_network
