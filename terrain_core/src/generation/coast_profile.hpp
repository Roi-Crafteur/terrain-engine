#pragma once

// The profiles of the shores of the coasts (agent 33), internal to terrain_core: by kind of coast,
// the heights of their faces, their rock platforms and the widths of their faces; the noises of a
// point of the shore and the face of a profile there. Shared by the shapes of the coasts in two
// dimensions (coast_shape.cpp) and their faces in three dimensions (coast_faces.cpp).

#include "coast_shape.hpp"

#include "terrain/generation/coast.hpp"
#include "terrain/generation/noise_octaves.hpp"

namespace terrain::coast_profile {

// A face of width w rising h is at most kSmoothSlope h / w steep (a smoothstep), and the distance
// to the shore is that of the coast signal over its gradient, steeper by up to kDistanceSlack.
inline constexpr double kSmoothSlope = 1.5;
inline constexpr double kDistanceSlack = 1.25;

// The rock platform rises from kBenchLow to kBenchLow + kBenchRise meters across its width (less
// on a platform narrower than kBenchRun: none where the face rises from the water), from the
// water over its first kBenchFoot meters; the foot of the face moves kEdgeShift meters to and fro
// along the coast (capes and hollows of the top).
inline constexpr double kBenchLow = 0.4;
inline constexpr double kBenchRise = 1.1;
inline constexpr double kBenchRun = 20.0;
inline constexpr double kBenchFoot = 6.0;
inline constexpr double kEdgeShift = 30.0;

// The shape of the shore of a kind: the top of its face heightLow to heightHigh meters above its
// platform by the height noises, at most landShare of the regional elevation plus landTop (no
// plateau above its hinterland); a rock platform platformLow to platformHigh meters wide before
// a face as wide as the slope law wants plus faceLow to faceHigh meters; screes at the foot of
// its face along up to the share screes of its length; its mountains faded in over hills meters
// of signal. Out at sea its floor goes down, the share deepen of the way to the depth of the
// outer edge of the band within foot meters of the shore.
struct Profile {
    float heightLow;
    float heightHigh;
    float landShare;
    float landTop;
    float platformLow;
    float platformHigh;
    float faceLow;
    float faceHigh;
    float screes;
    double hills;
    float deepen;
    double foot;
};

// The profile of a kind (the low coasts keep the coastal plain and the shelf; the mountains and
// the fjords have no shore of their own: they plunge).
[[nodiscard]] const Profile& profileOf(CoastKind kind);

[[nodiscard]] double smooth(double edge0, double edge1, double v);
[[nodiscard]] double lerp(float low, float high, double t);
// A noise in [0, 1].
[[nodiscard]] double unit(const noise::FbmOctaves& n, double x, double z);

// The noises at a point of the shore, in [0, 1] (the edge in [-1, 1]).
struct Shore {
    double heightAt = 0.0;
    double platformAt = 0.0;
    double faceAt = 0.0;
    double edgeAt = 0.0;
    double screeAt = 0.0;
};

[[nodiscard]] Shore shoreAt(const coast_shape::Noises& n, double x, double z);

// The face of a shore: its foot, its width, the ground before it.
struct Face {
    double foot = 0.0;
    double width = 0.0;
    double bench = 0.0;
    double height = 0.0; // from the bench to the top
};

// The face of a shore of profile k rising to top at distance d > 0 from the water, as wide as the
// slope law wants (faceSlopeLimit), its foot moved by the platform and the edge. Its foot and its
// width are the same at every distance.
[[nodiscard]] Face faceAt(const Profile& k, double top, double d, const Shore& s);

// The top of the face of profile k at a point of shore noises s over a geography whose land
// stands land meters high: as high as the height noises want, at most landShare of the land plus
// landTop.
[[nodiscard]] double faceTop(const Profile& k, const Shore& s, double land);

} // namespace terrain::coast_profile
