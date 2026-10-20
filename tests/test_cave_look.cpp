// How the caves look in the viewer, computed on the CPU: a distinct color per material, the
// altitude tint of the outer surface, the colors of the rock and lake vertices of a tile, the
// speleothem cones (counts, tips, winding toward the outside, colors), and the cave regions: a
// color per district type, the tint of the district mix on the rock, the cave flora meshes and
// the text naming the mix.
#include "altitude_tint.hpp"
#include "cave_look.hpp"
#include "district_look.hpp"
#include "flora_look.hpp"

#include "terrain/generation/cave_flora.hpp"
#include "terrain/generation/cave_materials.hpp"
#include "terrain/generation/cave_regions.hpp"
#include "terrain/generation/speleothems.hpp"
#include "terrain/io/cave_regions_png.hpp"
#include "terrain/streaming/stream_manager.hpp"

#include "test_assert.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace {

using terrain::CaveDistrictType;
using terrain::CaveFlora;
using terrain::CaveFloraKind;
using terrain::CaveMaterial;
using viewer::Vec3;

bool same(Vec3 a, Vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// Every material of a cave wall or a lake has its own color, within [0, 1].
void testPalette() {
    bool distinct = true;
    bool inRange = true;
    for (int a = 1; a < terrain::kCaveMaterialCount; ++a) {
        const Vec3 color = viewer::caveMaterialColor(static_cast<CaveMaterial>(a));
        inRange = inRange && color.x >= 0.0f && color.x <= 1.0f && color.y >= 0.0f &&
                  color.y <= 1.0f && color.z >= 0.0f && color.z <= 1.0f;
        for (int b = 1; b < a; ++b) {
            const Vec3 other = viewer::caveMaterialColor(static_cast<CaveMaterial>(b));
            distinct = distinct && !same(color, other);
        }
    }
    TEST_CHECK(distinct);
    TEST_CHECK(inRange);
}

// Basalt, the material of the volcanic districts, is near black and has its own color.
void testBasaltColor() {
    const Vec3 basalt = viewer::caveMaterialColor(CaveMaterial::Basalt);
    TEST_CHECK(std::max({basalt.x, basalt.y, basalt.z}) < 0.25f);
    bool unique = true;
    for (int m = 1; m < terrain::kCaveMaterialCount; ++m) {
        const auto material = static_cast<CaveMaterial>(m);
        const Vec3 other = viewer::caveMaterialColor(material);
        unique = unique && (material == CaveMaterial::Basalt || !same(basalt, other));
    }
    TEST_CHECK(unique);
}

// The outer surface is green at sea level and gray from the high altitude, as in the streamed
// world, and the tint is clamped beyond them.
void testAltitudeTint() {
    const Vec3 low = viewer::altitudeColor(viewer::kStreamedLowAltitude);
    const Vec3 high = viewer::altitudeColor(viewer::kStreamedHighAltitude);
    TEST_CHECK(same(low, viewer::altitudeColor(-500.0f)));
    TEST_CHECK(same(high, viewer::altitudeColor(9000.0f)));
    TEST_CHECK(low.y > low.x && high.x > low.x);
    const Vec3 middle = viewer::altitudeColor(viewer::kStreamedHighAltitude / 2.0f);
    TEST_CHECK(middle.x > low.x && middle.x < high.x);
}

terrain::TileData tileWithMaterials() {
    terrain::TileData data;
    data.originY = 100.0;
    data.mesh.vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, -50.0f, 0.0f}, {0.0f, -60.0f, 1.0f}};
    data.mesh.normals = {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
    data.mesh.indices = {0, 1, 2};
    data.materials = {CaveMaterial::Surface, CaveMaterial::Limestone, CaveMaterial::Ice};
    data.water = data.mesh;
    data.waterMaterials = {CaveMaterial::Water, CaveMaterial::Ice, CaveMaterial::Water};
    return data;
}

// Rock vertices take the color of their material, the outer surface its altitude tint (and so
// does a tile without materials); lake vertices are water or ice.
void testTileColors() {
    terrain::TileData data = tileWithMaterials();
    const std::vector<Vec3> rock = viewer::rockColors(data);
    TEST_CHECK(rock.size() == 3);
    TEST_CHECK(same(rock[0], viewer::altitudeColor(100.0f)));
    TEST_CHECK(same(rock[1], viewer::caveMaterialColor(CaveMaterial::Limestone)));
    TEST_CHECK(same(rock[2], viewer::caveMaterialColor(CaveMaterial::Ice)));
    const std::vector<Vec3> lakes = viewer::lakeColors(data);
    TEST_CHECK(lakes.size() == 3);
    TEST_CHECK(same(lakes[0], viewer::caveMaterialColor(CaveMaterial::Water)));
    TEST_CHECK(same(lakes[1], viewer::caveMaterialColor(CaveMaterial::Ice)));

    data.materials.clear();
    const std::vector<Vec3> plain = viewer::rockColors(data);
    TEST_CHECK(same(plain[1], viewer::altitudeColor(50.0f)));
}

// Cones: the same number of vertices per speleothem, tips below the base of a stalactite and
// above the base of a stalagmite, triangles turning counter-clockwise seen from outside (their
// geometric normal points away from the axis), ice in its color.
void testSpeleothemCones() {
    const std::vector<terrain::Speleothem> speleothems = {
        {terrain::SpeleothemKind::Stalactite, 10.0, 20.0, -30.0f, 2.0f, 0.4f, false},
        {terrain::SpeleothemKind::Stalagmite, 10.0, 20.0, -36.0f, 1.0f, 0.3f, true},
        {terrain::SpeleothemKind::Stalagmite, 0.0, 0.0, 0.0f, 0.0f, 0.3f, false}, // no length
    };
    const viewer::SpeleothemMesh cones = viewer::speleothemMesh(speleothems);
    const std::size_t perCone = cones.mesh.vertices.size() / 2;
    TEST_CHECK(cones.mesh.vertices.size() == 2 * perCone && perCone >= 6);
    TEST_CHECK(cones.colors.size() == cones.mesh.vertices.size());
    TEST_CHECK(cones.mesh.normals.size() == cones.mesh.vertices.size());
    TEST_CHECK(cones.mesh.indices.size() % 3 == 0 && !cones.mesh.indices.empty());

    float lowest = 0.0f;
    float highest = -100.0f;
    for (std::size_t i = 0; i < perCone; ++i) {
        lowest = std::min(lowest, cones.mesh.vertices[i].y);
        highest = std::max(highest, cones.mesh.vertices[perCone + i].y);
    }
    TEST_CHECK(std::abs(lowest - (-32.0f)) < 1e-4f);  // stalactite tip: base - length
    TEST_CHECK(std::abs(highest - (-35.0f)) < 1e-4f); // stalagmite tip: base + length
    TEST_CHECK(same(cones.colors[perCone], viewer::caveMaterialColor(CaveMaterial::Ice)) ||
               same(cones.colors.back(), viewer::caveMaterialColor(CaveMaterial::Ice)));

    bool outward = true;
    for (std::size_t t = 0; t + 2 < cones.mesh.indices.size(); t += 3) {
        const auto at = [&](std::size_t k) {
            const terrain::Vec3f v = cones.mesh.vertices[cones.mesh.indices[t + k]];
            return Vec3{v.x, v.y, v.z};
        };
        const Vec3 a = at(0);
        const Vec3 normal = viewer::cross(at(1) - a, at(2) - a);
        const Vec3 center = (a + at(1) + at(2)) * (1.0f / 3.0f);
        const Vec3 away{center.x - 10.0f, 0.0f, center.z - 20.0f};
        outward = outward && viewer::dot(normal, away) > 0.0f;
    }
    TEST_CHECK(outward);
}

bool near(Vec3 a, Vec3 b, float tolerance = 1e-5f) {
    return viewer::length(a - b) <= tolerance;
}

// Weights of a mix of districts, in the given order (the first being the nearest).
terrain::DistrictWeights mixOf(std::initializer_list<std::pair<CaveDistrictType, float>> shares) {
    terrain::DistrictWeights weights;
    for (const auto& [type, weight] : shares) {
        const auto slot = static_cast<std::size_t>(weights.count++);
        weights.districts[slot].type = type;
        weights.weights[slot] = weight;
    }
    return weights;
}

// Each of the ten district types has its own color within [0, 1], clearly apart from the others:
// the color of the cave regions PNGs.
void testDistrictColors() {
    bool inRange = true;
    bool asPng = true;
    float closest = 10.0f;
    for (int a = 0; a < terrain::kCaveDistrictTypeCount; ++a) {
        const Vec3 color = viewer::caveDistrictColor(static_cast<CaveDistrictType>(a));
        const auto rgb = terrain::caveDistrictRgb(static_cast<CaveDistrictType>(a));
        asPng = asPng && std::abs(color.x * 255.0f - rgb[0]) < 1e-3f &&
                std::abs(color.y * 255.0f - rgb[1]) < 1e-3f &&
                std::abs(color.z * 255.0f - rgb[2]) < 1e-3f;
        inRange = inRange && std::min({color.x, color.y, color.z}) >= 0.0f &&
                  std::max({color.x, color.y, color.z}) <= 1.0f;
        for (int b = 0; b < a; ++b) {
            const Vec3 other = viewer::caveDistrictColor(static_cast<CaveDistrictType>(b));
            closest = std::min(closest, viewer::length(color - other));
        }
    }
    TEST_CHECK(inRange);
    TEST_CHECK(asPng);
    TEST_CHECK(closest > 0.2f);
}

// The color of a mix is the weighted sum of the colors of its districts: exactly the color of the
// type where one district weighs, and the type's own color when two districts have the same type.
void testDistrictMixColor() {
    const Vec3 lush = viewer::caveDistrictColor(CaveDistrictType::Lush);
    const Vec3 glacier = viewer::caveDistrictColor(CaveDistrictType::Glacier);
    TEST_CHECK(same(viewer::districtMixColor(mixOf({{CaveDistrictType::Lush, 1.0f}})), lush));
    const Vec3 two = viewer::districtMixColor(
        mixOf({{CaveDistrictType::Lush, 0.25f}, {CaveDistrictType::Glacier, 0.75f}}));
    TEST_CHECK(near(two, lush * 0.25f + glacier * 0.75f));
    const Vec3 twins = viewer::districtMixColor(
        mixOf({{CaveDistrictType::Lush, 0.4f}, {CaveDistrictType::Lush, 0.6f}}));
    TEST_CHECK(near(twins, lush));
    const Vec3 none = viewer::districtMixColor(terrain::DistrictWeights{});
    TEST_CHECK(near(none, {0.5f, 0.5f, 0.5f}));
}

// The tint mixes the material color with the district mix: strength 0 keeps the material, 1 gives
// the mix, in between is linear.
void testTintBlend() {
    const Vec3 stone = viewer::caveMaterialColor(CaveMaterial::Stone);
    const Vec3 mix = viewer::caveDistrictColor(CaveDistrictType::Volcanic);
    TEST_CHECK(same(viewer::tintColor(stone, mix, 0.0f), stone));
    TEST_CHECK(near(viewer::tintColor(stone, mix, 1.0f), mix));
    TEST_CHECK(near(viewer::tintColor(stone, mix, 0.3f), stone * 0.7f + mix * 0.3f));
    TEST_CHECK(viewer::kDistrictTint > 0.0f && viewer::kDistrictTint <= 0.5f);
}

// Rock below the outer surface is tinted by the district mix, the outer surface keeps its
// altitude color; without a mix, without materials or without rock, nothing changes (and the
// mix is not even asked).
void testTintedTile() {
    terrain::TileData data = tileWithMaterials();
    const std::vector<Vec3> plain = viewer::rockColors(data);
    const Vec3 mix{0.2f, 0.4f, 0.8f};
    int asked = 0;
    const viewer::DistrictColorAt constant = [&](double, double) {
        ++asked;
        return mix;
    };
    const std::vector<Vec3> tinted = viewer::districtRockColors(data, constant);
    TEST_CHECK(tinted.size() == 3 && asked > 0);
    TEST_CHECK(same(tinted[0], plain[0]));
    TEST_CHECK(same(tinted[1], viewer::tintColor(plain[1], mix, viewer::kDistrictTint)));
    TEST_CHECK(same(tinted[2], viewer::tintColor(plain[2], mix, viewer::kDistrictTint)));
    const std::vector<Vec3> whole = viewer::districtRockColors(data, constant, 1.0f);
    TEST_CHECK(near(whole[1], mix) && near(whole[2], mix) && same(whole[0], plain[0]));
    const std::vector<Vec3> untouched = viewer::districtRockColors(data, constant, 0.0f);
    TEST_CHECK(same(untouched[1], plain[1]) && same(untouched[2], plain[2]));

    const std::vector<Vec3> noMix = viewer::districtRockColors(data, viewer::DistrictColorAt{});
    TEST_CHECK(noMix.size() == 3 && same(noMix[1], plain[1]) && same(noMix[2], plain[2]));

    asked = 0;
    data.materials.assign(3, CaveMaterial::Surface);
    const std::vector<Vec3> surface = viewer::districtRockColors(data, constant);
    TEST_CHECK(asked == 0 && same(surface[1], viewer::rockColors(data)[1]));
    data.materials.clear();
    const std::vector<Vec3> bare = viewer::districtRockColors(data, constant);
    TEST_CHECK(asked == 0 && same(bare[2], viewer::rockColors(data)[2]));
}

// The mix is sampled on a coarse grid, then interpolated: a mix that is linear in x and z is
// followed exactly, with few samples, on a tile far from the origin; a lone vertex works too.
void testTintedTileGrid() {
    terrain::TileData data;
    data.originX = 1000.0;
    data.originY = 50.0;
    data.originZ = -300.0;
    for (int j = 0; j <= 13; j += 1) {
        for (int i = 0; i <= 20; i += 3) {
            data.mesh.vertices.push_back({static_cast<float>(i), -20.0f, static_cast<float>(j)});
            data.materials.push_back(CaveMaterial::Stone);
        }
    }
    int asked = 0;
    const auto linear = [](double x, double z) {
        return Vec3{static_cast<float>(x - 1000.0) * 0.05f, static_cast<float>(z + 300.0) * 0.05f,
                    0.25f};
    };
    const viewer::DistrictColorAt counted = [&](double x, double z) {
        ++asked;
        return linear(x, z);
    };
    const std::vector<Vec3> colors = viewer::districtRockColors(data, counted);
    const Vec3 stone = viewer::caveMaterialColor(CaveMaterial::Stone);
    bool exact = colors.size() == data.mesh.vertices.size();
    for (std::size_t i = 0; exact && i < colors.size(); ++i) {
        const terrain::Vec3f v = data.mesh.vertices[i];
        const Vec3 expected = viewer::tintColor(
            stone, linear(1000.0 + static_cast<double>(v.x), -300.0 + static_cast<double>(v.z)),
            viewer::kDistrictTint);
        exact = near(colors[i], expected, 1e-4f);
    }
    TEST_CHECK(exact);
    TEST_CHECK(asked > 0 && asked <= 81 && static_cast<std::size_t>(asked) < colors.size());

    terrain::TileData lone;
    lone.originX = 12.0;
    lone.originZ = 34.0;
    lone.mesh.vertices = {{1.0f, 2.0f, 3.0f}};
    lone.materials = {CaveMaterial::Moss};
    const std::vector<Vec3> single = viewer::districtRockColors(lone, counted);
    const Vec3 moss = viewer::caveMaterialColor(CaveMaterial::Moss);
    TEST_CHECK(single.size() == 1 &&
               near(single[0], viewer::tintColor(moss, linear(13.0, 37.0), viewer::kDistrictTint),
                    1e-4f));
}

CaveFlora fern(double x, double z, float y, float size) {
    return {CaveFloraKind::Fern, x, z, y, size};
}

CaveFlora mushroom(double x, double z, float y, float size) {
    return {CaveFloraKind::Mushroom, x, z, y, size};
}

CaveFlora vine(double x, double z, float y, float size) {
    return {CaveFloraKind::Vine, x, z, y, size};
}

// Checks one plant alone: consistent counts, indices in range, unit normals turned the same way
// as their triangles, colors in [0, 1], and every vertex inside the box of the plant (vertically
// between low and high, horizontally within reach of its axis), the box being filled to its top
// and bottom.
bool plantFitsBox(const CaveFlora& plant, float low, float high, float reach) {
    const viewer::PaintedMesh result = viewer::caveFloraMesh({plant});
    const terrain::Mesh& mesh = result.mesh;
    bool ok = !mesh.vertices.empty() && mesh.normals.size() == mesh.vertices.size() &&
              result.colors.size() == mesh.vertices.size() && mesh.indices.size() % 3 == 0 &&
              !mesh.indices.empty();
    float lowest = high;
    float highest = low;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const terrain::Vec3f v = mesh.vertices[i];
        const terrain::Vec3f n = mesh.normals[i];
        const Vec3 c = result.colors[i];
        ok = ok && v.y >= low - 1e-4f && v.y <= high + 1e-4f;
        ok = ok && std::hypot(static_cast<double>(v.x) - plant.x,
                              static_cast<double>(v.z) - plant.z) <= static_cast<double>(reach);
        ok = ok && std::abs(std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z) - 1.0f) < 1e-4f;
        ok = ok && std::min({c.x, c.y, c.z}) >= 0.0f && std::max({c.x, c.y, c.z}) <= 1.0f;
        lowest = std::min(lowest, v.y);
        highest = std::max(highest, v.y);
    }
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const auto at = [&](std::size_t k) {
            const terrain::Vec3f v = mesh.vertices[mesh.indices[t + k]];
            return Vec3{v.x, v.y, v.z};
        };
        const Vec3 geometric = viewer::cross(at(1) - at(0), at(2) - at(0));
        for (std::size_t k = 0; k < 3; ++k) {
            const terrain::Vec3f n = mesh.normals[mesh.indices[t + k]];
            ok = ok && mesh.indices[t + k] < mesh.vertices.size() &&
                 viewer::dot(geometric, Vec3{n.x, n.y, n.z}) > 0.0f;
        }
    }
    return ok && lowest <= low + 1e-4f && highest >= high - 1e-4f;
}

// Cave flora: ferns and mushrooms stay between y and y + size, vines between y - size and y, all
// close to their axis, as a valid mesh (counts, normals, winding, colors); sizeless plants and an
// empty list give nothing, and the meshes of several plants are the sum of theirs.
void testFloraMesh() {
    TEST_CHECK(plantFitsBox(fern(10.0, 20.0, -30.0f, 1.2f), -30.0f, -28.8f, 1.2f));
    TEST_CHECK(plantFitsBox(fern(-5.5, 7.25, 12.0f, 0.4f), 12.0f, 12.4f, 0.4f));
    TEST_CHECK(plantFitsBox(mushroom(12.5, 21.0, -30.0f, 0.8f), -30.0f, -29.2f, 0.8f));
    TEST_CHECK(plantFitsBox(mushroom(300.0, -400.0, 5.0f, 1.6f), 5.0f, 6.6f, 1.6f));
    TEST_CHECK(plantFitsBox(vine(14.0, 22.0, -25.0f, 1.5f), -26.5f, -25.0f, 0.2f));
    TEST_CHECK(plantFitsBox(vine(0.0, 0.0, 3.0f, 0.5f), 2.5f, 3.0f, 0.2f));

    TEST_CHECK(viewer::caveFloraMesh({}).mesh.vertices.empty());
    TEST_CHECK(viewer::caveFloraMesh({}).mesh.indices.empty());
    TEST_CHECK(viewer::caveFloraMesh({}).colors.empty());
    TEST_CHECK(viewer::caveFloraMesh({fern(0, 0, 0.0f, 0.0f), vine(1, 1, 2.0f, -1.0f)})
                   .mesh.vertices.empty());

    const std::vector<CaveFlora> all = {fern(10.0, 20.0, -30.0f, 1.2f),
                                        mushroom(12.5, 21.0, -30.0f, 0.8f),
                                        vine(14.0, 22.0, -25.0f, 1.5f)};
    const viewer::PaintedMesh together = viewer::caveFloraMesh(all);
    std::size_t vertices = 0;
    std::size_t indices = 0;
    for (const CaveFlora& plant : all) {
        const viewer::PaintedMesh one = viewer::caveFloraMesh({plant});
        vertices += one.mesh.vertices.size();
        indices += one.mesh.indices.size();
    }
    TEST_CHECK(together.mesh.vertices.size() == vertices);
    TEST_CHECK(together.mesh.indices.size() == indices);
    TEST_CHECK(together.colors.size() == vertices && together.mesh.normals.size() == vertices);
    const viewer::PaintedMesh again = viewer::caveFloraMesh(all);
    TEST_CHECK(again.mesh == together.mesh && again.colors.size() == together.colors.size());
}

// Ferns and vines are green, a mushroom is not: the cap is a glowing teal over a pale stem.
void testFloraColors() {
    bool green = true;
    for (const CaveFlora& plant : {fern(1, 1, 0.0f, 1.0f), vine(2, 2, 3.0f, 1.0f)}) {
        for (const Vec3& c : viewer::caveFloraMesh({plant}).colors) {
            green = green && c.y > c.x && c.y > c.z;
        }
    }
    TEST_CHECK(green);
    bool teal = false;
    bool pale = false;
    for (const Vec3& c : viewer::caveFloraMesh({mushroom(3, 3, 0.0f, 1.0f)}).colors) {
        teal = teal || (c.y >= 0.9f && c.x < c.y - 0.3f && c.z > c.x + 0.2f);
        pale = pale || std::min({c.x, c.y, c.z}) >= 0.7f;
    }
    TEST_CHECK(teal && pale);
}

// The label of a mix: names capitalized, whole percents, largest first, at most three, types
// added, shares rounding to 0% left out.
void testMixLabel() {
    using T = CaveDistrictType;
    const auto label = [](std::initializer_list<std::pair<T, float>> shares) {
        return viewer::districtMixLabel(mixOf(shares));
    };
    TEST_CHECK(label({{T::Lush, 0.7f}, {T::Dripstone, 0.3f}}) == "Lush 70% / Dripstone 30%");
    TEST_CHECK(label({{T::Tunnels, 1.0f}}) == "Tunnels 100%");
    TEST_CHECK(label({{T::Lush, 0.3f}, {T::Dripstone, 0.7f}}) == "Dripstone 70% / Lush 30%");
    TEST_CHECK(label({{T::Halls, 0.654f}, {T::Lakes, 0.346f}}) == "Halls 65% / Lakes 35%");
    TEST_CHECK(label({{T::Halls, 0.125f}, {T::Lakes, 0.875f}}) == "Lakes 88% / Halls 13%");
    TEST_CHECK(label({{T::Lakes, 0.5f}, {T::Halls, 0.5f}}) == "Lakes 50% / Halls 50%");
    TEST_CHECK(label({{T::Halls, 0.4f}, {T::Lakes, 0.3f}, {T::Rivers, 0.2f}, {T::Flooded, 0.1f}}) ==
               "Halls 40% / Lakes 30% / Rivers 20%");
    TEST_CHECK(label({{T::Lush, 0.3f}, {T::Dripstone, 0.4f}, {T::Lush, 0.3f}}) ==
               "Lush 60% / Dripstone 40%");
    TEST_CHECK(label({{T::Lush, 0.997f}, {T::Halls, 0.003f}}) == "Lush 100%");
    TEST_CHECK(viewer::districtMixLabel(terrain::DistrictWeights{}).empty());

    bool named = true;
    for (int a = 0; a < terrain::kCaveDistrictTypeCount; ++a) {
        const auto type = static_cast<CaveDistrictType>(a);
        std::string expected = terrain::caveDistrictName(type);
        expected[0] = static_cast<char>(expected[0] - 'a' + 'A');
        named = named && label({{type, 1.0f}}) == expected + " 100%";
    }
    TEST_CHECK(named);
}

} // namespace

int main() {
    testPalette();
    testBasaltColor();
    testAltitudeTint();
    testTileColors();
    testSpeleothemCones();
    testDistrictColors();
    testDistrictMixColor();
    testTintBlend();
    testTintedTile();
    testTintedTileGrid();
    testFloraMesh();
    testFloraColors();
    testMixLabel();
    return test::result();
}
