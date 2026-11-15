#include "build_rules.hpp"

#include <algorithm>
#include <cmath>

namespace terrain::unreal {

namespace {

// Distance from v to the interval [low, high], 0 inside.
double outside(double v, double low, double high) {
    return std::max({low - v, v - high, 0.0});
}

} // namespace

const char* gateRefusal(const BuildGate& gate) {
    if (!gate.playing) {
        return "not playing";
    }
    if (gate.devMapOpen) {
        return "the dev map is open";
    }
    if (gate.cursorShown) {
        return "the mouse cursor is shown (a menu or the dev map)";
    }
    if (gate.loading) {
        return "the view is loading";
    }
    if (!gate.controlsOwner) {
        return "the player does not control this pawn";
    }
    if (!gate.focused) {
        return "the game has no focus";
    }
    return nullptr;
}

bool EditRate::allow(double nowSeconds) {
    if (any_ && nowSeconds - last_ < interval_) {
        return false;
    }
    any_ = true;
    last_ = nowSeconds;
    return true;
}

CoreCapsule capsuleFromUnreal(const UnrealFrame& frame, const UnrealPoint& center,
                              double halfHeightCentimeters, double radiusCentimeters) {
    const CorePoint core = toCore(frame, center);
    const double half = halfHeightCentimeters / kCentimetersPerMeter;
    CoreCapsule capsule;
    capsule.x = core.x;
    capsule.z = core.z;
    capsule.bottom = core.y - half;
    capsule.top = core.y + half;
    capsule.radius = radiusCentimeters / kCentimetersPerMeter;
    return capsule;
}

double capsuleDistance(const VolumeEdit& edit, const CoreCapsule& capsule) {
    const BlockBox box = editBounds(edit);
    const double minX = box.min.x - 0.5;
    const double maxX = box.max.x + 0.5;
    const double minY = box.min.y - 0.5;
    const double maxY = box.max.y + 0.5;
    const double minZ = box.min.z - 0.5;
    const double maxZ = box.max.z + 0.5;
    // The axis of the capsule is the vertical segment between the centers of its caps: the
    // distance from a vertical segment to a box splits into its horizontal and vertical parts.
    const double low = capsule.bottom + capsule.radius;
    const double high = std::max(low, capsule.top - capsule.radius);
    const double dx = outside(capsule.x, minX, maxX);
    const double dz = outside(capsule.z, minZ, maxZ);
    const double dy = std::max({minY - high, low - maxY, 0.0});
    return std::sqrt(dx * dx + dy * dy + dz * dz) - capsule.radius;
}

bool touchesCapsule(const VolumeEdit& edit, const CoreCapsule& capsule) {
    return capsuleDistance(edit, capsule) < kCapsuleMarginMeters;
}

bool blockFull(BlockCoord block, std::optional<BlockEdit> held,
               const std::function<float(int, int, int)>& density) {
    if (held) {
        return *held == BlockEdit::Added;
    }
    // The 27 lattice points around the center, then each corner: the mean of its cell.
    float samples[3][3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) {
                samples[i][j][k] = density(block.x + i - 1, block.y + j - 1, block.z + k - 1);
            }
        }
    }
    for (int cx = 0; cx < 2; ++cx) {
        for (int cy = 0; cy < 2; ++cy) {
            for (int cz = 0; cz < 2; ++cz) {
                double sum = 0.0;
                for (int n = 0; n < 8; ++n) {
                    sum += samples[cx + (n & 1)][cy + ((n >> 1) & 1)][cz + ((n >> 2) & 1)];
                }
                if (sum >= 0.0) {
                    return false; // this corner is in the air (or on the surface)
                }
            }
        }
    }
    return true;
}

} // namespace terrain::unreal
