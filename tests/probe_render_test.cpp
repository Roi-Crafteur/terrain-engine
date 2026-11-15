// The render section of terrain_probe (agent 55): its horizon is the sample of level 0 of the far
// horizon as Unreal receives it, Heightfield::forFootprint(8).seenAt, not sampleAt. On the
// default world without its planet file, by a face of the coasts (the place cote_falaise_encoche)
// where the two differ, and at a point of the plains where they do not.
#include "probe_sections.hpp"
#include "probe_world.hpp"

#include "test_assert.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <variant>

namespace {

constexpr double kPitch = 8.0; // the pitch of level 0 of the far horizon

// The exact number at a path of the record ("render.horizon.ground"); NaN when absent.
double number(const probe::Record& r, const std::string& path) {
    const probe::Record* record = &r;
    std::size_t start = 0;
    while (record != nullptr) {
        const std::size_t dot = path.find('.', start);
        const probe::Field* f = record->find(path.substr(start, dot - start));
        if (f == nullptr) {
            return std::nan("");
        }
        if (dot == std::string::npos) {
            const auto* n = std::get_if<probe::Number>(&f->value);
            return n != nullptr ? n->value : std::nan("");
        }
        record = std::get_if<probe::Record>(&f->value);
        start = dot + 1;
    }
    return std::nan("");
}

const probe::Section* renderSection() {
    for (const probe::Section& s : probe::sections()) {
        if (std::string(s.name) == "render") {
            return &s;
        }
    }
    return nullptr;
}

bool same(double a, float b) {
    return std::bit_cast<std::uint64_t>(a) ==
           std::bit_cast<std::uint64_t>(static_cast<double>(b));
}

// The first point of the lattice of level 0 within reach of (x, z) where the seen ground differs
// from sampleAt (a face, a ramp or a spire of the coasts), or none.
bool facedPoint(const terrain::Heightfield& level, double& x, double& z, double reach) {
    const double x0 = kPitch * std::round(x / kPitch);
    const double z0 = kPitch * std::round(z / kPitch);
    for (double dz = -reach; dz <= reach; dz += kPitch) {
        for (double dx = -reach; dx <= reach; dx += kPitch) {
            const float sample = level.sampleAt(x0 + dx, z0 + dz).height;
            const float seen = level.seenAt(x0 + dx, z0 + dz, kPitch).height;
            if (std::bit_cast<std::uint32_t>(sample) != std::bit_cast<std::uint32_t>(seen)) {
                x = x0 + dx;
                z = z0 + dz;
                return true;
            }
        }
    }
    return false;
}

void checkPoint(const probe::ProbeWorld& world, double x, double z, bool faced) {
    const terrain::Heightfield level = world.field().forFootprint(kPitch);
    const probe::Record r = probe::probePoint(world, x, z, {renderSection()});
    const float seen = level.seenAt(x, z, kPitch).height;
    const float sample = world.field().sampleAt(x, z).height;
    const double ground = number(r, "points.render.horizon.ground");
    const double read = std::isnan(ground) ? number(r, "render.horizon.ground") : ground;
    std::printf("(%.0f, %.0f): render.horizon.ground %.3f, seenAt %.3f, sampleAt %.3f\n", x, z,
                read, static_cast<double>(seen), static_cast<double>(sample));
    TEST_CHECK(same(read, seen));
    TEST_CHECK(faced != same(read, sample));
}

} // namespace

int main() {
    TEST_CHECK(renderSection() != nullptr);
    const probe::ProbeWorld world(probe::Options{}, nullptr);
    double x = 429'778.8; // cote_falaise_encoche
    double z = -855'913.3;
    const bool found = facedPoint(world.field().forFootprint(kPitch), x, z, 400.0);
    TEST_CHECK(found);
    if (found) {
        checkPoint(world, x, z, true);
    }
    checkPoint(world, -827'000.0, -4'334'000.0, false); // the plains seen by the audit
    return test::result();
}
