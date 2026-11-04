// The volcanoes of a world against real ones (agent 45). For a stratovolcano, a shield, a caldera
// and a cinder cone of the default world (the first of each on land whose sizes are those of the
// reference): cuts of their profile through their summit along 4 directions (the height over
// their foot along the distance to the summit), drawn over the profile of a real volcano
// (profile_<form>.png, profiles.csv); and views of them in perspective at the height of a walker
// (view_<name>.png, the relief alone: the sea at 0 m where the world puts it). The real profiles
// are approximate public figures: Fuji (3,776 m over a foot of 35 to 40 km), Mauna Loa above the
// sea (4,169 m, slopes of 3 to 8 degrees, a caldera of 180 m), Crater Lake (its rim 1.1 km over
// the land around, its floor at about the level of that land), Paricutin (a cone of about 300 m,
// a crater of 300 m wide).
// Usage: terrain_export_volcanoes <output-dir> [--views]

#include "terrain/det_math.hpp"
#include "terrain/generation/heightfield.hpp"
#include "terrain/generation/volcanoes.hpp"
#include "terrain/io/png.hpp"
#include "terrain/io/relief_view.hpp"
#include "volcano_relief.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using terrain::VolcanoForm;
using terrain::VolcanoSite;

constexpr double kPi = 3.14159265358979323846;

struct Reference {
    const char* name;
    std::vector<std::array<double, 2>> points; // km from the summit, meters over the foot
};

Reference referenceOf(VolcanoForm f) {
    switch (f) {
    case VolcanoForm::Strato:
        return {"Fuji",
                {{0, 3776}, {1, 3350}, {2, 2950}, {3, 2600}, {5, 2000}, {7.5, 1550}, {10, 1200},
                 {15, 700}, {20, 400}, {25, 200}, {30, 50}}};
    case VolcanoForm::Shield:
        return {"Mauna Loa above the sea",
                {{0, 3990}, {2.5, 4000}, {3.2, 4169}, {10, 3500}, {20, 2700}, {30, 1900},
                 {40, 1100}, {50, 400}, {60, 0}}};
    case VolcanoForm::Caldera:
        return {"Crater Lake",
                {{0, 0}, {3.5, 0}, {4.0, 600}, {4.6, 1100}, {6, 900}, {10, 500}, {15, 200},
                 {20, 0}}};
    default:
        return {"Paricutin",
                {{0, 180}, {0.1, 230}, {0.15, 300}, {0.3, 260}, {0.5, 150}, {0.7, 50},
                 {0.85, 0}}};
    }
}

// The first volcano on land of a form whose sizes are those of the reference.
const VolcanoSite* pick(const terrain::Volcanoes& v, VolcanoForm form) {
    for (const VolcanoSite& s : v.sites()) {
        if (s.form != form || s.atSea) {
            continue;
        }
        const auto h = static_cast<double>(s.height);
        const bool fits = (form == VolcanoForm::Strato && h > 2600.0 && h < 3600.0) ||
                          (form == VolcanoForm::Shield && h > 1200.0 && h < 2200.0) ||
                          (form == VolcanoForm::Caldera && s.crater > 3500.0f) ||
                          (form == VolcanoForm::CinderField && h > 200.0);
        if (fits) {
            return &s;
        }
    }
    return nullptr;
}

struct Cut {
    double angle = 0.0;
    std::vector<double> heights; // over the foot, from -half to +half
};

struct Profile {
    std::string name;
    double centerX = 0.0;
    double centerZ = 0.0;
    double base = 0.0;
    double half = 0.0; // meters on each side of the summit
    double step = 0.0;
    std::vector<Cut> cuts;
    Reference reference;
};

// The cuts of the relief through (cx, cz), base the ground it stands on (the lowest of the cuts
// at their ends when not given).
Profile cutsAt(const terrain::Heightfield& field, double cx, double cz, double half,
               Reference reference, std::string name) {
    Profile p{std::move(name), cx, cz, 0.0, half, half / 300.0, {}, std::move(reference)};
    for (int k = 0; k < 4; ++k) {
        Cut c;
        c.angle = kPi * k / 4.0;
        for (double d = -half; d <= half + 0.5 * p.step; d += p.step) {
            c.heights.push_back(static_cast<double>(
                field.heightAt(cx + d * terrain::dmath::cos(c.angle), cz + d * terrain::dmath::sin(c.angle))));
        }
        p.cuts.push_back(std::move(c));
    }
    double low = std::numeric_limits<double>::infinity();
    for (const Cut& c : p.cuts) {
        low = std::min({low, c.heights.front(), c.heights.back()});
    }
    p.base = low;
    for (Cut& c : p.cuts) {
        for (double& h : c.heights) {
            h -= low;
        }
    }
    return p;
}

void dot(terrain::Image& img, int x, int y, std::array<std::uint8_t, 3> rgb, int size) {
    for (int dy = -size; dy <= size; ++dy) {
        for (int dx = -size; dx <= size; ++dx) {
            const int px = x + dx;
            const int py = y + dy;
            if (px >= 0 && py >= 0 && px < img.width && py < img.height) {
                const auto at = static_cast<std::size_t>((py * img.width + px) * 3);
                img.pixels[at] = rgb[0];
                img.pixels[at + 1] = rgb[1];
                img.pixels[at + 2] = rgb[2];
            }
        }
    }
}

void line(terrain::Image& img, double x0, double y0, double x1, double y1,
          std::array<std::uint8_t, 3> rgb, int size) {
    const int steps = static_cast<int>(std::max(std::abs(x1 - x0), std::abs(y1 - y0))) + 1;
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        dot(img, static_cast<int>(std::lround(x0 + (x1 - x0) * t)),
            static_cast<int>(std::lround(y0 + (y1 - y0) * t)), rgb, size);
    }
}

// The cuts in colors, the reference in black on both sides of the summit, a gray grid every
// 500 m of height and every 5 km (1 km for a small form) of distance.
terrain::Image chart(const Profile& p) {
    terrain::Image img{1400, 560, 3, std::vector<std::uint8_t>(1400u * 560u * 3u, 255)};
    double top = 0.0;
    for (const Cut& c : p.cuts) {
        top = std::max(top, *std::max_element(c.heights.begin(), c.heights.end()));
    }
    for (const auto& r : p.reference.points) {
        top = std::max(top, r[1]);
    }
    top *= 1.1;
    const double bottom = -0.15 * top;
    const auto px = [&](double d) { return 40.0 + (d + p.half) / (2.0 * p.half) * 1320.0; };
    const auto py = [&](double h) { return 520.0 - (h - bottom) / (top - bottom) * 500.0; };
    const double grid = p.half > 4000.0 ? 5000.0 : 1000.0;
    for (double d = -std::floor(p.half / grid) * grid; d <= p.half; d += grid) {
        line(img, px(d), py(bottom), px(d), py(top), {220, 220, 220}, 0);
    }
    for (double h = 0.0; h <= top; h += top > 2000.0 ? 500.0 : 50.0) {
        line(img, px(-p.half), py(h), px(p.half), py(h), {220, 220, 220}, 0);
    }
    const std::array<std::array<std::uint8_t, 3>, 4> colors{
        {{200, 40, 40}, {40, 90, 200}, {30, 150, 60}, {230, 140, 20}}};
    for (std::size_t k = 0; k < p.cuts.size(); ++k) {
        const Cut& c = p.cuts[k];
        for (std::size_t i = 1; i < c.heights.size(); ++i) {
            const double d = -p.half + p.step * static_cast<double>(i);
            line(img, px(d - p.step), py(c.heights[i - 1]), px(d), py(c.heights[i]), colors[k], 1);
        }
    }
    const auto& ref = p.reference.points;
    for (const double side : {-1.0, 1.0}) {
        for (std::size_t i = 1; i < ref.size(); ++i) {
            line(img, px(side * ref[i - 1][0] * 1000.0), py(ref[i - 1][1]),
                 px(side * ref[i][0] * 1000.0), py(ref[i][1]), {0, 0, 0}, 2);
        }
    }
    return img;
}

void writeCsv(const fs::path& path, const std::vector<Profile>& profiles) {
    std::ofstream out(path);
    out << "form,center_x,center_z,base_m,distance_m,cut0,cut45,cut90,cut135\n";
    for (const Profile& p : profiles) {
        for (std::size_t i = 0; i < p.cuts.front().heights.size(); ++i) {
            out << p.name << ',' << p.centerX << ',' << p.centerZ << ',' << p.base << ','
                << -p.half + p.step * static_cast<double>(i);
            for (const Cut& c : p.cuts) {
                out << ',' << c.heights[i];
            }
            out << '\n';
        }
    }
}

// The tallest cone of a field (its details, drawn from its seed alone; their ground read from the
// relief without the volcanoes).
terrain::volcano_relief::Cone tallestCone(const terrain::Heightfield& bare, const VolcanoSite& s) {
    const terrain::volcano_relief::Detail d =
        terrain::volcano_relief::detailOf(s, [&bare](double x, double z) {
            const terrain::HeightSample h = bare.sampleAt(x, z);
            return terrain::volcano_relief::Ground{h.height, h.regional};
        });
    terrain::volcano_relief::Cone best;
    for (const auto& c : d.cones) {
        if (!c.maar && c.height > best.height) {
            best = c;
        }
    }
    return best;
}

std::vector<Profile> profiles(const terrain::Heightfield& field, const terrain::Heightfield& bare) {
    std::vector<Profile> out;
    for (const VolcanoForm f : {VolcanoForm::Strato, VolcanoForm::Shield, VolcanoForm::Caldera,
                                VolcanoForm::CinderField}) {
        const VolcanoSite* s = pick(*field.volcanoes(), f);
        if (s == nullptr) {
            continue;
        }
        const terrain::VolcanoOffset top = terrain::volcanoSummit(*s);
        double cx = s->x + top.dx;
        double cz = s->z + top.dz;
        double half = 1.3 * static_cast<double>(s->radius);
        if (f == VolcanoForm::CinderField) {
            const terrain::volcano_relief::Cone c = tallestCone(bare, *s);
            cx = s->x + c.x;
            cz = s->z + c.z;
            half = 1.5 * c.radius;
        }
        std::string name = terrain::volcanoFormName(f);
        std::replace(name.begin(), name.end(), ' ', '_');
        out.push_back(cutsAt(field, cx, cz, half, referenceOf(f), name));
        std::printf("%s at %.0f %.0f: cuts of %.1f km over a foot at %.0f m (%s)\n",
                    name.c_str(), cx, cz, 2.0 * half / 1000.0, out.back().base,
                    out.back().reference.name);
    }
    return out;
}

// The eye distance meters from (tx, tz) along heading turns, eye meters over the ground (or the
// sea), its axis toward that point.
terrain::ViewParams eyeAt(const terrain::Heightfield& field, double tx, double tz,
                          double distance, double heading, double eye) {
    terrain::ViewParams p;
    p.heading = heading;
    p.eyeX = tx - distance * terrain::dmath::sinTurns(heading);
    p.eyeZ = tz + distance * terrain::dmath::cosTurns(heading);
    const terrain::HeightSample s = field.sampleAt(p.eyeX, p.eyeZ);
    p.eyeHeight = static_cast<double>(std::max(s.height, s.waterLevel)) + eye;
    return p;
}

// A view toward the summit (tx, tz) from distance meters away, at the height of a walker: of 16
// headings, the one that sees the summit highest over the horizon (the ground before it hides
// nothing of it); from the rim of a caldera (rim > 0: its radius), toward its floor.
void view(const terrain::Heightfield& field, const fs::path& path, double tx, double tz,
          double distance, double rim) {
    const auto ground = [&field](double x, double z) { return field.heightAt(x, z); };
    const auto water = [&field](double x, double z) {
        const terrain::HeightSample h = field.sampleAt(x, z);
        return h.water == terrain::WaterKind::None ? -1e9f : h.waterLevel;
    };
    // The highest point of the rim of the summit: what the eye aims at.
    double summit = static_cast<double>(field.heightAt(tx, tz));
    for (int k = 0; k < 32; ++k) {
        for (const double r : {0.02 * distance, 0.04 * distance}) {
            summit = std::max(summit, static_cast<double>(field.heightAt(
                                          tx + r * terrain::dmath::cosTurns(k / 32.0),
                                          tz + r * terrain::dmath::sinTurns(k / 32.0))));
        }
    }
    terrain::ViewParams best = eyeAt(field, tx, tz, distance, 0.0, 1.8);
    double bestTangent = -1e9;
    for (int k = 0; k < 16; ++k) {
        const terrain::ViewParams p =
            eyeAt(field, tx, tz, rim > 0.0 ? 0.9 * rim : distance, k / 16.0, 1.8);
        const terrain::Sightline line =
            terrain::sightline(ground, water, p, tx, tz, summit, 0.05 * distance);
        const double tangent = rim > 0.0 ? p.eyeHeight : (line.visible ? line.tangent : -1e9);
        if (tangent > bestTangent) {
            bestTangent = tangent;
            best = p;
        }
    }
    best.tilt = rim > 0.0 ? 0.0 : 0.5 * (summit - best.eyeHeight) / std::max(distance, 1.0);
    (void)terrain::writePng(path, terrain::renderReliefView(ground, water, best, 0));
    std::printf("%s: eye at %.0f %.0f, %.1f m, heading %.1f degrees, %.1f km from %.0f %.0f\n",
                path.filename().string().c_str(), best.eyeX, best.eyeZ, best.eyeHeight,
                best.heading * 360.0,
                terrain::dmath::hypot(best.eyeX - tx, best.eyeZ - tz) / 1000.0, tx, tz);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: terrain_export_volcanoes <output-dir> [--views]\n");
        return 1;
    }
    const fs::path dir(argv[1]);
    const bool views = argc > 2 && std::string(argv[2]) == "--views";
    fs::create_directories(dir);
    const terrain::Heightfield field(terrain::kDefaultWorldSeed);
    terrain::HeightfieldParams off;
    off.volcanoes.enabled = false;
    const terrain::Heightfield bare(terrain::kDefaultWorldSeed, off);
    const std::vector<Profile> all = profiles(field, bare);
    for (const Profile& p : all) {
        (void)terrain::writePng(dir / ("profile_" + p.name + ".png"), chart(p));
    }
    writeCsv(dir / "profiles.csv", all);
    if (views) {
        for (const Profile& p : all) {
            const double far = p.name == "cinder_field" ? 1500.0 : 1.5 * p.half;
            view(field, dir / ("view_" + p.name + ".png"), p.centerX, p.centerZ, far, 0.0);
            if (p.name == "caldera") {
                const VolcanoSite* s = pick(*field.volcanoes(), VolcanoForm::Caldera);
                view(field, dir / "view_caldera_rim.png", p.centerX, p.centerZ, far,
                     static_cast<double>(s->crater));
            }
        }
    }
    return 0;
}
