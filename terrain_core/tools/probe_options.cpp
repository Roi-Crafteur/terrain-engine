// The command line of terrain_probe (probe_options.hpp).
#include "probe_options.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace probe {

namespace {

template <class T> bool parse(const char* text, T& value) {
    const char* end = text + std::strlen(text);
    const auto [last, error] = std::from_chars(text, end, value);
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return error == std::errc{} && last == end;
}

bool fail(const char* what, const char* text) {
    std::fprintf(stderr, "Invalid %s '%s'\n", what, text);
    return false;
}

// --sections a,b,c: each one known.
bool parseSections(const char* text, const std::vector<std::string>& known, Options& o) {
    std::string list = text;
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t comma = std::min(list.find(',', start), list.size());
        const std::string name = list.substr(start, comma - start);
        if (std::find(known.begin(), known.end(), name) == known.end()) {
            return fail("section", name.c_str());
        }
        if (std::find(o.sections.begin(), o.sections.end(), name) == o.sections.end()) {
            o.sections.push_back(name);
        }
        start = comma + 1;
    }
    return true;
}

bool parseLine(int argc, char** argv, int& i, Options& o) {
    if (i + 5 >= argc) {
        return fail("line", "(x1 z1 x2 z2 step expected)");
    }
    Line l;
    double* values[5] = {&l.x1, &l.z1, &l.x2, &l.z2, &l.step};
    for (double* v : values) {
        if (!parse(argv[++i], *v)) {
            return fail("line", argv[i]);
        }
    }
    const double length = terrain::dmath::hypot(l.x2 - l.x1, l.z2 - l.z1);
    if (!(l.step > 0.0) || length / l.step >= kMaxLinePoints) {
        return fail("line step", argv[i]);
    }
    o.line = l;
    return true;
}

// The options with a value; false when flag is none of them (handled set to false).
bool parseValue(int argc, char** argv, int& i, const std::vector<std::string>& known,
                Options& o, bool& handled) {
    const std::string flag = argv[i];
    handled = true;
    const bool has = i + 1 < argc;
    if (flag == "--planet" && has) {
        o.planet = argv[++i];
        return true;
    }
    if (flag == "--planet-check" && has) {
        o.planetCheck = argv[++i];
        return true;
    }
    if (flag == "--seed" && has) {
        return parse(argv[++i], o.seed) || fail("seed", argv[i]);
    }
    if (flag == "--world" && i + 2 < argc) {
        i += 2;
        return (parse(argv[i - 1], o.world.x) && parse(argv[i], o.world.z)) ||
               fail("world", argv[i - 1]);
    }
    if (flag == "--threads" && has) {
        return (parse(argv[++i], o.threads) && o.threads >= 0) || fail("threads", argv[i]);
    }
    if (flag == "--radius" && has) {
        return (parse(argv[++i], o.radius) && o.radius > 0.0) || fail("radius", argv[i]);
    }
    if (flag == "--falls" && has) {
        return (parse(argv[++i], o.falls) && o.falls >= 0.0) || fail("falls", argv[i]);
    }
    if (flag == "--sections" && has) {
        return parseSections(argv[++i], known, o);
    }
    if (flag == "--line") {
        return parseLine(argc, argv, i, o);
    }
    handled = false;
    return false;
}

bool parseOption(int argc, char** argv, int& i, const std::vector<std::string>& known,
                 Options& o) {
    const std::string flag = argv[i];
    if (flag == "--json" || flag == "--caves" || flag == "--list-sections") {
        (flag == "--json" ? o.json : flag == "--caves" ? o.caves : o.listSections) = true;
        return true;
    }
    bool handled = false;
    const bool ok = parseValue(argc, argv, i, known, o, handled);
    if (handled) {
        return ok;
    }
    if (flag.size() > 1 && flag[0] == '-' && (flag[1] < '0' || flag[1] > '9')) {
        return fail("option", argv[i]);
    }
    std::pair<double, double> p;
    if (i + 1 >= argc || !parse(argv[i], p.first) || !parse(argv[i + 1], p.second)) {
        return fail("point", argv[i]);
    }
    o.points.push_back(p);
    ++i;
    return true;
}

// What the options ask must be one thing: a planet check, the list of the sections, points or
// a line.
bool checkModes(const Options& o) {
    if (!o.planetCheck.empty()) {
        if (!o.points.empty() || o.line || !o.planet.empty()) {
            return fail("--planet-check", "(takes no point, line nor --planet)");
        }
        return true;
    }
    if (o.listSections) {
        return true;
    }
    if (o.points.empty() && !o.line) {
        return fail("point", "none");
    }
    if (!o.points.empty() && o.line) {
        return fail("line", "(points and a line together)");
    }
    return true;
}

} // namespace

bool parseOptions(int argc, char** argv, const std::vector<std::string>& known, Options& o) {
    for (int i = 1; i < argc; ++i) {
        if (!parseOption(argc, argv, i, known, o)) {
            return false;
        }
    }
    if (!checkModes(o)) {
        return false;
    }
    try {
        terrain::validate(o.world);
    } catch (const std::invalid_argument&) {
        return fail("world size", "(multiples of 65,536 m expected)");
    }
    return true;
}

std::string usage(const char* tool) {
    return std::string("Usage: ") + tool +
           " [--planet <file>] [--seed <seed>] [--world <x> <z>] [--threads <n>] [--json] "
           "[--sections <a,b,...>] [--radius <meters>] [--falls <meters>] [--caves] "
           "(<x> <z> [<x> <z>]... | --line <x1> <z1> <x2> <z2> <step>)\n"
           "       " +
           tool + " --planet-check <file> [--seed <seed>] [--world <x> <z>] [--json]\n" +
           "       " + tool + " --list-sections\n";
}

} // namespace probe
