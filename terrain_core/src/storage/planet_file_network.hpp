#pragma once

// The parts of the planet file of task 11C (agent 31, planet_file.cpp), each written only when
// its experimental flag is on (experimental_flags.hpp): off, the bytes of the file are those of
// main.
// - The parameters of the flags smooth_bands, pass_routing and planet_network, after the others
//   (PlanetParams): each behind its tag, only when on.
// - The sections after the level, each behind its tag, in a file of version 2 (its flags first):
//   planet_network, the discharge of every land cell (PlanetCells::landDischarge); sphere_latitude,
//   the rise of the climate of every land cell (PlanetCells::climateRise).

#include "frame_reader.hpp"
#include "terrain/generation/planet/planet_level.hpp"
#include "terrain/generation/planet/planet_params.hpp"

#include <cstdint>

namespace terrain::planet_file_network {

inline constexpr std::uint32_t kNetworkSection = 0x5754454EU; // "NETW"
inline constexpr std::uint32_t kClimateSection = 0x4D494C43U; // "CLIM"

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

template <class Out> void writeNetwork(Out& out, const PlanetCells& c) {
    out.u32(kNetworkSection);
    out.count(c.landDischarge.size());
    for (const std::uint16_t q : c.landDischarge) {
        out.u16(q);
    }
}

inline void readNetwork(storage::FrameReader& in, PlanetCells& c) {
    if (in.u32() != kNetworkSection) {
        in.fail("the section of the network (planet_network) missing");
    }
    c.landDischarge.resize(in.count(2));
    for (std::uint16_t& q : c.landDischarge) {
        q = in.u16();
    }
}

template <class Out> void writeClimate(Out& out, const PlanetCells& c) {
    out.u32(kClimateSection);
    out.count(c.climateRise.size());
    for (const std::uint8_t r : c.climateRise) {
        out.u8(r);
    }
}

inline void readClimate(storage::FrameReader& in, PlanetCells& c) {
    if (in.u32() != kClimateSection) {
        in.fail("the section of the climate (sphere_latitude) missing");
    }
    c.climateRise.resize(in.count(1));
    for (std::uint8_t& r : c.climateRise) {
        r = in.u8();
    }
}

} // namespace terrain::planet_file_network
