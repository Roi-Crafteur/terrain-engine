#pragma once

// The paths of the relief compared bit for bit (agent 55, relief_paths_test): the points of the
// default world with its planet file drawn in strata, the comparison of every field of a
// HeightSample, and the tally of the stages (which one differs first, at which point).

#include "coast_planet_support.hpp"
#include "test_assert.hpp"

#include "terrain/generation/coast.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/noise.hpp"
#include "terrain/generation/rock_faces.hpp"
#include "terrain/generation/water_kind.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

namespace relief_paths {

// The kinds of ground the paths go through. A point takes the first that holds (classify).
enum class Stratum : std::uint8_t { River, Erg, Depression, Coast, Wall, Sea, Land };
inline constexpr std::size_t kStrata = 7;
inline constexpr std::array<const char*, kStrata> kStratumNames = {
    "river corridor", "erg", "dry depression", "coast band", "wall", "sea", "land"};
inline constexpr std::size_t kQuota = 285; // points per stratum: about 2,000 in all

[[nodiscard]] inline std::size_t indexOf(Stratum s) {
    return static_cast<std::size_t>(s);
}

struct Point {
    double x = 0.0; // canonical
    double z = 0.0;
    Stratum stratum = Stratum::Land;
    bool inBand = false; // in the band of the coasts (CoastSample::inBand): seenAt may move it
};

// A window of the world where a stratum is drawn: a place of data/dev/places.json (copied here:
// a rebuilt planet moves them, the test says which stratum then lacks points), half its side.
struct Anchor {
    double x = 0.0;
    double z = 0.0;
    double half = 0.0;
    Stratum stratum = Stratum::Land;
};
inline constexpr std::array<Anchor, 10> kAnchors = {{
    {-323'111.0, 3'952'778.0, 20'000.0, Stratum::Erg},        // geantes_pied_mur
    {-1'197'516.0, -3'359'230.0, 20'000.0, Stratum::Erg},     // erg_barchanes_homme
    {-241'631.0, -86'277.0, 1'500.0, Stratum::River},         // fleuve_plaine
    {-6'185'104.0, 155'924.0, 1'500.0, Stratum::River},       // chute388_bassin
    {416'102.0, -11'414'891.0, 2'000.0, Stratum::Wall},       // k2_paroi
    {-410'000.0, 4'589'844.0, 15'000.0, Stratum::Depression}, // depression_seche_fond
    {429'778.8, -855'913.3, 4'000.0, Stratum::Coast},         // cote_falaise_encoche
    {160.0, 0.0, 4'000.0, Stratum::Coast},                    // plage_origine
    {1'819'677.0, 9'928'285.0, 4'000.0, Stratum::Coast},      // cote_fjord_axe
    {-26'488'769.4, 7'401'625.9, 2'000.0, Stratum::Coast},    // stack_arche
}};

// Deterministic draws in [-1, 1).
class Draws {
public:
    double next() {
        state_ = terrain::noise::deriveSeed(state_, 0);
        return 2.0 * (static_cast<double>(state_ >> 11) * 0x1p-53) - 1.0;
    }

private:
    std::uint64_t state_ = 55;
};

// The stratum of the point (x, z) of field (the world with its water, footprint 0); walls are
// sought only where asked (the rock of a column costs more).
[[nodiscard]] inline Stratum classify(const terrain::Heightfield& field, double x, double z,
                                      bool walls) {
    const terrain::HeightSample s = field.sampleAt(x, z);
    using terrain::WaterKind;
    if (s.water == WaterKind::River || s.water == WaterKind::Lake ||
        coast_planet::nearRiver(x, z)) {
        return Stratum::River;
    }
    if (s.erg > 0.5f) {
        return Stratum::Erg;
    }
    if (s.height < 0.0f && (s.water == WaterKind::None || s.water == WaterKind::SaltLake)) {
        return Stratum::Depression;
    }
    if (field.coastAt(x, z).inBand) {
        return Stratum::Coast;
    }
    if (walls && field.rockAt(x, z).hasRock()) {
        return Stratum::Wall;
    }
    return s.water == WaterKind::Ocean ? Stratum::Sea : Stratum::Land;
}

// About kQuota points of each stratum: round after round, a draw in the window of each anchor
// whose stratum still lacks points, and one over the whole world, each kept by the stratum it
// falls in while that one lacks points.
[[nodiscard]] inline std::vector<Point> drawPoints(const terrain::Heightfield& field) {
    std::array<std::size_t, kStrata> counts{};
    std::vector<Point> points;
    Draws draws;
    const terrain::WorldSize& world = field.world();
    const auto keep = [&](double x, double z, bool walls) {
        x = terrain::noise::wrapCanonical(x, world.x);
        z = terrain::noise::wrapCanonical(z, world.z);
        const Stratum s = classify(field, x, z, walls);
        if (counts[indexOf(s)] < kQuota) {
            ++counts[indexOf(s)];
            points.push_back({x, z, s, field.coastAt(x, z).inBand});
        }
    };
    for (int round = 0; round < 4'000 && points.size() < kQuota * kStrata; ++round) {
        for (const Anchor& a : kAnchors) {
            if (counts[indexOf(a.stratum)] < kQuota) {
                const double dx = a.half * draws.next();
                keep(a.x + dx, a.z + a.half * draws.next(), a.stratum == Stratum::Wall);
            }
        }
        const double gx = 0.5 * world.x * draws.next();
        keep(gx, 0.5 * world.z * draws.next(), false);
    }
    for (std::size_t s = 0; s < kStrata; ++s) {
        std::printf("stratum %-15s %4zu points\n", kStratumNames[s], counts[s]);
        if (counts[s] < kQuota / 2) {
            std::printf("  too few points: move the window of its anchor (a rebuilt planet moves "
                        "the places)\n");
        }
        TEST_CHECK(counts[s] >= kQuota / 2);
    }
    return points;
}

// Every field of a HeightSample, in the order of its declaration. A field added to it must be
// added here (the size below stops the build until then, unless it fits in the padding).
static_assert(sizeof(terrain::HeightSample) == 72,
              "relief_paths_support.hpp: a field was added to HeightSample: compare it");
struct FloatField {
    const char* name;
    float terrain::HeightSample::* member;
    bool seen; // seenAt may change it in the band of the coasts (faces, ramps, spires)
};
using HS = terrain::HeightSample;
inline const std::array<FloatField, 17> kFloatFields = {{
    {"continentalness", &HS::continentalness, false},
    {"macroContinentalness", &HS::macroContinentalness, false},
    {"plateContinentalness", &HS::plateContinentalness, false},
    {"plateCollision", &HS::plateCollision, false},
    {"mountainMask", &HS::mountainMask, false},
    {"relief", &HS::relief, false},
    {"rift", &HS::rift, false},
    {"base", &HS::base, false},
    {"regional", &HS::regional, false},
    {"mountain", &HS::mountain, false},
    {"detail", &HS::detail, false},
    {"height", &HS::height, true},
    {"waterLevel", &HS::waterLevel, true},
    {"dunes", &HS::dunes, false},
    {"erg", &HS::erg, false},
    {"ergFloor", &HS::ergFloor, false},
    {"rock", &HS::rock, true},
}};

[[nodiscard]] inline bool same(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// A field whose bits differ between two samples.
struct FieldDiff {
    const char* name = nullptr;
    double expected = 0.0;
    double read = 0.0;
};

// The first field of read whose bits differ from expected, in the order of the declaration
// (water before waterLevel); with seenBand, those seenAt may change in the band of the coasts
// are skipped.
[[nodiscard]] inline std::optional<FieldDiff> firstField(const HS& expected, const HS& read,
                                                         bool seenBand = false) {
    for (const FloatField& f : kFloatFields) {
        if (f.member == &HS::waterLevel && !seenBand && expected.water != read.water) {
            return FieldDiff{"water", static_cast<double>(expected.water),
                             static_cast<double>(read.water)};
        }
        if ((seenBand && f.seen) || same(expected.*f.member, read.*f.member)) {
            continue;
        }
        return FieldDiff{f.name, expected.*f.member, read.*f.member};
    }
    return std::nullopt;
}

// The stages compared with Heightfield::forFootprint(footprint).sampleAt, in the order of the
// chain: the first that differs names the copy of it that lost or added a step.
enum class Stage : std::uint8_t {
    SamplesAt,
    SamplesAtRock,
    HeightsAt,
    SeenAt,
    SeenHeightsAt,
    HorizonCell,
    Lod3Columns,
    Lod3Water,
    Lod3Texels,
};
inline constexpr std::size_t kStages = 9;
inline constexpr std::array<const char*, kStages> kStageNames = {
    "samplesAt",    "samplesAt with rock", "heightsAt",      "seenAt",       "seenHeightsAt",
    "horizon cell", "LOD 3 columns",       "LOD 3 water",    "LOD 3 texels"};

struct Difference {
    Stage stage = Stage::SamplesAt;
    FieldDiff field;
    Point point;
    double footprint = 0.0;
};

// The comparisons of each stage and stratum, and the earliest difference.
class Tally {
public:
    // A comparison of stage at point: diff, if set, says what differs.
    void add(Stage stage, const Point& p, double footprint, const std::optional<FieldDiff>& diff) {
        const auto st = static_cast<std::size_t>(stage);
        ++compared_[st][indexOf(p.stratum)];
        if (!diff) {
            return;
        }
        ++differ_[st][indexOf(p.stratum)];
        if (!first_ || stage < first_->stage) {
            first_ = Difference{stage, *diff, p, footprint};
        }
    }
    // A difference explained, counted apart, never failing: by the design of seenAt (the faces,
    // ramps and spires of the coasts), or known and left to another branch (Water76).
    enum class Known : std::uint8_t { CoastFaces, Water76 };
    void known(Known why, Stage stage, const Point& p) {
        ++known_[static_cast<std::size_t>(why)][static_cast<std::size_t>(stage)]
                [indexOf(p.stratum)];
    }

    // The table stage x stratum ("compared/differing", "+n known"), the first difference.
    void report() const;

    [[nodiscard]] std::size_t compared(Stage stage) const {
        std::size_t n = 0;
        for (const std::size_t c : compared_[static_cast<std::size_t>(stage)]) {
            n += c;
        }
        return n;
    }
    [[nodiscard]] const std::optional<Difference>& first() const { return first_; }

private:
    using Counts = std::array<std::array<std::size_t, kStrata>, kStages>;
    Counts compared_{};
    Counts differ_{};
    std::array<Counts, 2> known_{};
    std::optional<Difference> first_;
};

inline void Tally::report() const {
    std::printf("%-20s", "stage");
    for (const char* name : kStratumNames) {
        std::printf(" %15s", name);
    }
    std::printf("\n");
    for (std::size_t st = 0; st < kStages; ++st) {
        std::printf("%-20s", kStageNames[st]);
        for (std::size_t s = 0; s < kStrata; ++s) {
            char cell[64];
            int n = std::snprintf(cell, sizeof cell, "%zu/%zu", compared_[st][s], differ_[st][s]);
            for (std::size_t why = 0; why < known_.size() && n > 0; ++why) {
                if (known_[why][st][s] > 0 && static_cast<std::size_t>(n) < sizeof cell) {
                    n += std::snprintf(cell + n, sizeof cell - static_cast<std::size_t>(n),
                                       "+%zu%s", known_[why][st][s], why == 0 ? "c" : "w");
                }
            }
            std::printf(" %15s", cell);
        }
        std::printf("\n");
    }
    std::printf("(compared/differing; +nc: moved by the faces, ramps and spires of the coasts, by "
                "design; +nw: the water of the tile texels, known, fixed by #76)\n");
    if (first_) {
        const Difference& d = *first_;
        std::printf("first stage that differs: %s, field %s, %s, footprint %g m, at (%.3f, "
                    "%.3f): expected %.9g, read %.9g\n",
                    kStageNames[static_cast<std::size_t>(d.stage)], d.field.name,
                    kStratumNames[indexOf(d.point.stratum)], d.footprint, d.point.x, d.point.z,
                    d.field.expected, d.field.read);
    } else {
        std::printf("every path gives the bits of sampleAt\n");
    }
}

} // namespace relief_paths
