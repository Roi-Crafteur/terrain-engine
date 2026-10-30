#pragma once

#include "terrain/generation/settlement/settlement_config.hpp"
#include "terrain/world_size.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// The public magic network of a world (docs/PEUPLEMENT.md, section 4): telluric lines, long curves
// of the seed that cross the plains, the forests, the deserts, the mountains and the oceans alike
// (independent of the plates, of the faults and of the relief); their nodes, at their crossings
// first; the dead zones. The hidden Mana of the secret seed is not in V1: everything here comes
// from the seed of the world and is public.
namespace terrain::settlement {

// How well a world knows an element (section 4): the major lines and the great nodes by everyone,
// the minor lines and the secondary nodes partly. The hidden ones (3) come later from the
// secret seed, on the server.
enum class Known : std::uint8_t { Visible = 1, Partial = 2 };

// A line: its points every kLineStepKm, in meters, following each other across the seams of the
// world (never jumping by a period), the first in the canonical range; its intensity at each.
struct TelluricLine {
    std::vector<double> x;
    std::vector<double> z;
    std::vector<float> intensity;
    double widthKm = 0.0; // the reach of its Mana on each side
    bool major = false;
    Known known = Known::Visible;
};

struct ManaNode {
    double x = 0.0; // canonical range, meters
    double z = 0.0;
    double radiusKm = 0.0;
    double intensity = 0.0;
    bool crossing = false; // at a crossing of two lines (else along a line)
    Known known = Known::Visible;
};

struct DeadZone {
    double x = 0.0;
    double z = 0.0;
    double radiusKm = 0.0;
};

struct TelluricNetwork {
    std::vector<TelluricLine> lines; // the major ones first
    std::vector<ManaNode> nodes;
    std::vector<DeadZone> deadZones;

    [[nodiscard]] std::size_t memoryBytes() const;
};

inline constexpr double kLineStepKm = 8.0;

// The network of a world from its seed. The counts of rule are those of the reference planet,
// scale the share of its area the world covers (worldScale).
[[nodiscard]] TelluricNetwork buildTelluricNetwork(const WorldSize& world, std::uint64_t worldSeed,
                                                   const MagicRule& rule, double scale);

} // namespace terrain::settlement
