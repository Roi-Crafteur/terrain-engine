// The spots of terrain_export_rock to check the rock in a client (rock_spots.hpp).
#include "rock_spots.hpp"

#include "view_places.hpp"

#include "terrain/det_math.hpp"
#include "terrain/generation/rock_faces.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <utility>
#include <vector>

namespace rock_tool {

namespace {

constexpr double kFaceSearch = 40.0;
// The walks are searched within kWalkNear meters on a lattice of kLattice meters, then within
// kWalkFar meters on a lattice twice as wide.
constexpr double kWalkNear = 300.0;
constexpr double kWalkFar = 1'000.0;
constexpr double kLattice = 2.0; // meters between the columns searched
constexpr float kFull = 0.98f;         // the weight of a face at full weight
constexpr float kLeastHeight = 8.0f;   // of a face to walk to
constexpr float kLeastCap = 2.0f;      // of an overhang to walk under
constexpr double kLeastCeiling = 2.5;  // meters of air under its lip
constexpr double kNearest = 15.0;      // meters from the face of the start of the walk
constexpr double kFarthest = 40.0;
constexpr double kFlat = 0.176;        // tangent of 10 degrees: the ground of the start
constexpr double kFlatRadius = 3.0;
// Tangent of 35 degrees: the ground of the walk (a client walks up to about 45 degrees).
constexpr double kWalkable = 0.7;
constexpr double kFootGap = 3.0; // the ground before the face lies at its foot within it
constexpr double kRounded = 3.0; // meters the foot of a face is rounded over
constexpr std::size_t kTried = 400; // faces tried at most
constexpr double kApart = 6.0;      // meters between the columns of a face tried
constexpr double kAway = 1.6;          // face_side: the eye this many heights before the face
constexpr double kLeastAway = 40.0;
constexpr double kSideSun = 0.25;      // turns left of the axis: across the face
constexpr double kLowSun = 0.34;       // the sine of 20 degrees
constexpr double kSunBehind = -0.4;
constexpr double kWalkerEye = 1.7;

struct Candidate {
    double x = 0.0; // the middle of the face
    double z = 0.0;
    terrain::RockFace face;
    double rank = 0.0; // smaller first
};

// The faces at full weight, kLeastHeight meters high at least, within radius meters of (x, z),
// found at the columns of a lattice (lattice meters apart) within a cell of their middle: the
// tallest first, or the nearest.
std::vector<Candidate> facesNear(const terrain::Heightfield& field, double x, double z,
                                 double radius, bool tallest, double lattice = kLattice) {
    std::vector<double> xs;
    std::vector<double> zs;
    for (double dz = -radius; dz <= radius; dz += lattice) {
        for (double dx = -radius; dx <= radius; dx += lattice) {
            xs.push_back(std::round(x + dx));
            zs.push_back(std::round(z + dz));
        }
    }
    std::vector<terrain::HeightSample> samples(xs.size());
    std::vector<terrain::RockColumn> rock(xs.size());
    field.samplesAt(xs, zs, samples, rock);
    std::vector<Candidate> out;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        for (std::size_t k = 0; k < rock[i].faceCount; ++k) {
            const terrain::RockFace& f = rock[i].faces[k];
            const float height = f.top - f.foot;
            if (f.carved || f.weight < kFull || height < kLeastHeight ||
                std::abs(static_cast<double>(f.distance)) > lattice) {
                continue;
            }
            const double cx = xs[i] - static_cast<double>(f.distance) * f.airX;
            const double cz = zs[i] - static_cast<double>(f.distance) * f.airZ;
            out.push_back({cx, cz, f,
                           tallest ? -static_cast<double>(height)
                                   : terrain::dmath::hypot(cx - x, cz - z)});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const Candidate& a, const Candidate& b) { return a.rank < b.rank; });
    return out;
}

double degrees(double turns) {
    return std::fmod(360.0 * turns + 360.0, 360.0);
}

// The air under the lip of a face at (x, z): the height between the floor (the highest ground
// below its top, going up from its foot) and the ceiling over it; 0 without air.
double ceilingAt(const terrain::Heightfield& field, double x, double z, const terrain::RockFace& f) {
    constexpr double kStep = 0.05;
    const terrain::RockColumn column = field.rockAt(x, z);
    const terrain::RockDetail detail = terrain::rockDetailForLod(0);
    double floor = 0.0;
    bool air = false;
    for (double y = static_cast<double>(f.foot) - 3.0; y < static_cast<double>(f.top) + 2.0;
         y += kStep) {
        const bool rock = terrain::rockDensity(column, y, detail) < 0.0f;
        if (!rock && !air) {
            air = true;
            floor = y;
        } else if (rock && air) {
            return y - floor;
        }
    }
    return 0.0;
}

// The ground along the walk to a face: groundAt at meters before its middle.
float groundBefore(const terrain::TerrainTiles& tiles, const Candidate& c, double at) {
    return tiles.groundAt(c.x + at * c.face.airX, c.z + at * c.face.airZ);
}

bool flatAt(const terrain::TerrainTiles& tiles, double x, double z, float ground) {
    for (const auto& [dx, dz] : {std::pair{kFlatRadius, 0.0}, std::pair{-kFlatRadius, 0.0},
                                 std::pair{0.0, kFlatRadius}, std::pair{0.0, -kFlatRadius}}) {
        if (std::abs(static_cast<double>(tiles.groundAt(x + dx, z + dz) - ground)) >
            kFlat * kFlatRadius) {
            return false;
        }
    }
    return true;
}

// The walk to the face of a candidate: from the farthest flat spot kNearest to kFarthest meters
// before it, reached over walkable ground from its rounded foot (or from beyond its lip).
WalkSpot walkFrom(const terrain::TerrainTiles& tiles, const Candidate& c, bool overhang) {
    WalkSpot spot;
    const terrain::RockFace& f = c.face;
    // Beyond the foot of the step, rounded over kRounded meters (rock_faces.cpp), or its lip.
    const double first = (overhang ? static_cast<double>(f.cap) : 0.0) + kRounded;
    float previous = groundBefore(tiles, c, first);
    if (std::abs(static_cast<double>(previous - f.foot)) > kFootGap) {
        return spot;
    }
    for (double at = first + 1.0; at <= kFarthest; at += 1.0) {
        const float ground = groundBefore(tiles, c, at);
        if (std::abs(static_cast<double>(ground - previous)) > kWalkable) {
            break;
        }
        previous = ground;
        const double x = c.x + at * f.airX;
        const double z = c.z + at * f.airZ;
        if (at >= kNearest && flatAt(tiles, x, z, ground)) {
            spot = {true, x, z, ground, degrees(view_places::headingTo(x, z, c.x, c.z)), at,
                    c.x, c.z, f.foot, f.top, static_cast<double>(f.cap), 0.0};
        }
    }
    return spot;
}

// The first walk to a face (or under an overhang) within radius meters of (x, z), the nearest
// faces first.
WalkSpot walkWithin(const terrain::TerrainTiles& tiles, double x, double z, bool overhang,
                    double radius) {
    const std::vector<Candidate> faces = facesNear(tiles.heightfield(), x, z, radius, false,
                                                   radius > kWalkNear ? 2.0 * kLattice : kLattice);
    std::vector<const Candidate*> tried;
    for (const Candidate& c : faces) {
        // One column of a face of every kApart meters (its neighbors give the same walk).
        const bool near = std::any_of(tried.begin(), tried.end(), [&c](const Candidate* t) {
            return terrain::dmath::hypot(t->x - c.x, t->z - c.z) < kApart;
        });
        if ((overhang && c.face.cap < kLeastCap) || near || tried.size() >= kTried) {
            continue;
        }
        tried.push_back(&c);
        WalkSpot spot = walkFrom(tiles, c, overhang);
        if (!spot.found) {
            continue;
        }
        if (overhang) {
            const double under = 0.5 * c.face.cap;
            spot.ceiling = ceilingAt(tiles.heightfield(), c.x + under * c.face.airX,
                                     c.z + under * c.face.airZ, c.face);
            if (spot.ceiling < kLeastCeiling) {
                continue;
            }
        }
        return spot;
    }
    return {};
}

} // namespace

bool addFaceSide(const terrain::Heightfield& field, double x, double z, Places& out) {
    const std::vector<Candidate> faces = facesNear(field, x, z, kFaceSearch, true);
    if (faces.empty()) {
        return false;
    }
    const Candidate& c = faces.front();
    const double height = static_cast<double>(c.face.top - c.face.foot);
    const double away = std::max(kLeastAway, kAway * height);
    Shot s;
    s.name = "face_side";
    s.x = c.x + away * c.face.airX;
    s.z = c.z + away * c.face.airZ;
    s.heading = view_places::headingTo(s.x, s.z, c.x, c.z);
    const double middle = 0.5 * static_cast<double>(c.face.foot + c.face.top);
    s.above = std::max(s.above, middle - static_cast<double>(field.heightAt(s.x, s.z)));
    s.sunLeft = kSideSun;
    s.sunSine = kLowSun;
    char note[256];
    std::snprintf(note, sizeof note,
                  "the tallest face within %.0f m of (%.0f, %.0f): %.0f m high at (%.0f, %.0f), "
                  "seen from %.0f m before it, level with its middle, the sun across it",
                  kFaceSearch, x, z, height, c.x, c.z, away);
    s.note = note;
    out.shots.push_back(s);
    return true;
}

WalkSpot walkToFace(const terrain::TerrainTiles& tiles, double x, double z, bool overhang) {
    for (const double radius : {kWalkNear, kWalkFar}) {
        const WalkSpot spot = walkWithin(tiles, x, z, overhang, radius);
        if (spot.found) {
            return spot;
        }
    }
    return {};
}

Shot walkShot(const WalkSpot& spot, const std::string& name) {
    Shot s;
    s.name = name;
    s.x = spot.x;
    s.z = spot.z;
    s.above = kWalkerEye;
    s.heading = spot.heading / 360.0;
    const double middle = 0.5 * static_cast<double>(spot.foot + spot.top);
    s.tilt = (middle - static_cast<double>(spot.ground) - kWalkerEye) / spot.distance;
    s.sunLeft = kSunBehind;
    s.note = describe(spot, spot.cap >= kLeastCap && spot.ceiling > 0.0);
    return s;
}

std::string describe(const WalkSpot& spot, bool overhang) {
    if (!spot.found) {
        return overhang ? "no overhang to walk under" : "no face to walk to";
    }
    char line[400];
    std::snprintf(line, sizeof line,
                  "walk from (%.1f, %.1f), ground at %.2f m, heading %.1f degrees, %.0f m to the "
                  "face of (%.1f, %.1f): foot %.1f m, top %.1f m (%.0f m high)",
                  spot.x, spot.z, static_cast<double>(spot.ground), spot.heading, spot.distance,
                  spot.faceX, spot.faceZ, static_cast<double>(spot.foot),
                  static_cast<double>(spot.top), static_cast<double>(spot.top - spot.foot));
    std::string out = line;
    if (overhang) {
        std::snprintf(line, sizeof line, "; its lip juts %.1f m, %.1f m of air under it", spot.cap,
                      spot.ceiling);
        out += line;
    }
    return out;
}

} // namespace rock_tool
