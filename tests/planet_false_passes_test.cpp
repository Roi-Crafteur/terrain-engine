// False passes: how far the fine relief rises above the profile of the rivers.
#include "terrain/generation/planet/planet_false_passes.hpp"

#include "test_assert.hpp"

#include <cstddef>
#include <vector>

namespace {

using terrain::FalsePassStats;
using terrain::RiverGraph;
using terrain::RiverSegment;

// A river along z = 0 from x = 0 to 16 km, its points at 100, 120 (made 100: the profile never
// rises) and 0 m.
RiverSegment straightRiver(double z) {
    RiverSegment s;
    s.strahler = 2;
    s.vertices.resize(3);
    const float elevation[] = {100.0f, 120.0f, 0.0f};
    for (std::size_t v = 0; v < 3; ++v) {
        s.vertices[v].x = 8000.0 * static_cast<double>(v);
        s.vertices[v].z = z;
        s.vertices[v].elevation = elevation[v];
    }
    return s;
}

// The profile of straightRiver.
double profile(double x) {
    return x < 8000.0 ? 100.0 : 100.0 - 100.0 * (x - 8000.0) / 8000.0;
}

void testProfile() {
    RiverGraph graph;
    graph.segments.push_back(straightRiver(0.0));
    // Relief just under the profile: nothing to cut.
    const FalsePassStats flat = terrain::planet::measureFalsePasses(
        graph, [](double x, double) { return static_cast<float>(profile(x) - 10.0); }, 1);
    TEST_CHECK(flat.sampled == 16000.0 && flat.over50 == 0.0 && flat.maxExcess == 0.0f);
    // A ridge of 300 m from 4 to 6 km: 2 km above 50 and 200 m, none above 500 m.
    const auto ridge = [](double x, double) {
        return static_cast<float>(profile(x) + (x >= 4000.0 && x < 6000.0 ? 300.0 : 0.0));
    };
    const FalsePassStats crossed = terrain::planet::measureFalsePasses(graph, ridge, 1);
    TEST_CHECK(crossed.over50 == 2000.0 && crossed.over200 == 2000.0 && crossed.over500 == 0.0);
    TEST_CHECK(crossed.maxExcess == 300.0f && crossed.maxX == 4250.0);
    TEST_CHECK(crossed.sampledByOrder[2] == 16000.0 && crossed.over200ByOrder[2] == 2000.0);
}

void testThreads() {
    RiverGraph graph;
    for (int s = 0; s < 300; ++s) {
        graph.segments.push_back(straightRiver(1000.0 * s));
    }
    const auto relief = [](double x, double z) {
        return static_cast<float>(profile(x) + 0.001 * z + 0.01 * x);
    };
    const FalsePassStats one = terrain::planet::measureFalsePasses(graph, relief, 1);
    for (const int threads : {2, 3}) {
        const FalsePassStats other = terrain::planet::measureFalsePasses(graph, relief, threads);
        TEST_CHECK(other.sampled == one.sampled && other.over50 == one.over50 &&
                   other.over200 == one.over200 && other.over500 == one.over500 &&
                   other.maxExcess == one.maxExcess && other.maxX == one.maxX &&
                   other.maxZ == one.maxZ);
    }
    TEST_CHECK(one.over50 > 0.0 && one.over200 > 0.0);
}

} // namespace

int main() {
    testProfile();
    testThreads();
    return test::result();
}
