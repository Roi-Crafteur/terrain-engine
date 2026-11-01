#pragma once

#include "terrain/streaming/stream_manager.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace terrain::unreal {

// Toward the edge of the lip of a fall, the water of a tile fades out over kEdgeFade meters, to
// nothing kEdgeOver meters downstream of the edge, under the top of its curtain (which stands
// kCurtainAhead in front of the edge: fall_curtains.hpp); its fade goes on down to kEdgeBeyond
// beyond and up to kEdgeAbove before (the material clamps it to [0, 1]), linear in the distance,
// so that between two vertices on either side the water ends there, not at a side of a cell nor
// short of the curtain.
inline constexpr double kEdgeFade = 1.0;
inline constexpr double kEdgeOver = 0.75;
inline constexpr double kEdgeBeyond = -8.0;
inline constexpr double kEdgeAbove = 8.0;

// The edges of the lips of the falls near a tile (TileData::falls, here or not), in the frame of
// the tile (meters from its lowest corner): kFallEdge points across each, from one side of its
// channel to the other, and where the water falling from each meets the pool below (a straight
// lip across its channel at its lip and at its foot when the core gives no edge).
class FallEdges {
public:
    explicit FallEdges(const TileData& data);

    // Where the water stands at level at (x, z), within tolerance meters of the water above a
    // fall and within reach meters of the edge of its lip (across its channel, upstream or
    // downstream): its fade, kEdgeOver - kEdgeFade meters downstream of the edge 1 (above it
    // further upstream, kEdgeAbove at most), kEdgeOver meters downstream 0, below 0 further
    // (kEdgeBeyond at least), the fall the nearest; otherwise none.
    [[nodiscard]] std::optional<double> above(double x, double z, double level, double tolerance,
                                              double reach) const;
    // The same toward the edge of the lip of fall n alone, whatever the level (none beyond its
    // channel).
    [[nodiscard]] std::optional<double> toward(std::size_t n, double x, double z) const;
    // Whether the water at level at (x, z) is that of the pool below a fall, within tolerance
    // meters of its water and within reach meters of where its water meets the pool.
    [[nodiscard]] bool below(double x, double z, double level, double tolerance,
                             double reach) const;

    [[nodiscard]] std::size_t size() const { return edges_.size(); }
    [[nodiscard]] double top(std::size_t n) const { return edges_[n].top; }

private:
    struct Edge {
        std::array<double, kFallEdge> x{};
        std::array<double, kFallEdge> z{};
        std::array<double, kFallEdge> footX{};
        std::array<double, kFallEdge> footZ{};
        std::array<double, kFallEdge> across{};     // of each point of the lip
        std::array<double, kFallEdge> footAcross{}; // of each point of the foot
        double lipX = 0.0;
        double lipZ = 0.0;
        double dirX = 1.0; // downstream
        double dirZ = 0.0;
        double top = 0.0;
        double bottom = 0.0;
    };
    // The distance downstream of (x, z) from the line through points (px, pz) whose coordinates
    // across e are across, interpolated at that of (x, z); none beyond them by margin meters.
    [[nodiscard]] static std::optional<double>
    downstreamOf(const Edge& e, const std::array<double, kFallEdge>& px,
                 const std::array<double, kFallEdge>& pz,
                 const std::array<double, kFallEdge>& across, double x, double z, double margin);

    std::vector<Edge> edges_;
};

} // namespace terrain::unreal
