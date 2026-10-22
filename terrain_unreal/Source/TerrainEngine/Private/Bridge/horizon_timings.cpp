#include "horizon_timings.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace terrain::unreal {

namespace {

void add(StageStats& stats, double milliseconds) {
    ++stats.tiles;
    stats.totalMilliseconds += milliseconds;
    stats.maxMilliseconds = std::max(stats.maxMilliseconds, milliseconds);
}

std::string describeStats(const char* name, const StageStats& stats, const char* unit) {
    char text[160];
    std::snprintf(text, sizeof(text), "%s %zu %s %.2f ms (max %.2f)", name, stats.tiles, unit,
                  stats.meanMilliseconds(), stats.maxMilliseconds);
    return text;
}

std::string ticksLine(const HorizonTimingsReport& report) {
    std::string line;
    const auto append = [&line](const std::string& part) {
        line += (line.empty() ? "horizon ticks: " : ", ") + part;
    };
    if (report.firstShow.tiles > 0) {
        append(describeStats("first show", report.firstShow, "ticks"));
    }
    for (std::size_t level = 0; level < report.moves.size(); ++level) {
        if (report.moves[level].tiles > 0) {
            const std::string name = "level " + std::to_string(level) + " moves";
            append(describeStats(name.c_str(), report.moves[level], "ticks"));
        }
    }
    if (report.idle.tiles > 0) {
        append(describeStats("without epoch", report.idle, "ticks"));
    }
    return line;
}

std::string sectionLine(const HorizonTimingsReport& report, std::size_t section) {
    const StageStats& upload = report.upload[section];
    const std::size_t each = upload.tiles == 0 ? 0 : report.vertices[section] / upload.tiles;
    if (section == static_cast<std::size_t>(HorizonSection::Texels) ||
        section == static_cast<std::size_t>(HorizonSection::Normals)) {
        // Prepared on the jobs: nothing converted on the game thread.
        const char* const name = section == static_cast<std::size_t>(HorizonSection::Texels)
                                     ? "horizon texels: "
                                     : "horizon normals: ";
        return name + describeStats("upload", upload, "ticks") + ", " + std::to_string(each) +
               " texels per tick";
    }
    const char* const name = section == 0 ? "horizon body: " : "horizon trim: ";
    return name + describeStats("conversion", report.conversion[section], "meshes") + ", " +
           describeStats("upload", upload, "meshes") + ", " + std::to_string(each) +
           " vertices per mesh";
}

} // namespace

void HorizonTimings::recordTick(int coarsestMoved, bool first, double milliseconds) {
    if (coarsestMoved < -1 || coarsestMoved >= kHorizonMaxLevels) {
        throw std::invalid_argument("HorizonTimings: level out of range");
    }
    if (first) {
        add(report_.firstShow, milliseconds);
    } else if (coarsestMoved >= 0) {
        add(report_.moves[static_cast<std::size_t>(coarsestMoved)], milliseconds);
    } else {
        add(report_.idle, milliseconds);
    }
}

void HorizonTimings::recordUpload(HorizonSection section, double conversionMilliseconds,
                                  double uploadMilliseconds, std::size_t vertices) {
    const auto index = static_cast<std::size_t>(section);
    add(report_.conversion[index], conversionMilliseconds);
    add(report_.upload[index], uploadMilliseconds);
    report_.vertices[index] += vertices;
}

void HorizonTimings::recordEpoch(double milliseconds, std::int64_t samples) {
    add(report_.epochs, milliseconds);
    report_.samples += samples;
}

HorizonTimingsReport HorizonTimings::take() {
    return std::exchange(report_, HorizonTimingsReport{});
}

std::vector<std::string> describe(const HorizonTimingsReport& report) {
    std::vector<std::string> lines;
    const std::string ticks = ticksLine(report);
    if (!ticks.empty()) {
        lines.push_back(ticks);
    }
    for (std::size_t section = 0; section < kHorizonSectionCount; ++section) {
        if (report.upload[section].tiles > 0) {
            lines.push_back(sectionLine(report, section));
        }
    }
    if (report.epochs.tiles > 0) {
        lines.push_back(describeStats("horizon epochs:", report.epochs, "epochs") +
                        " from plan to commit, " + std::to_string(report.samples) + " samples");
    }
    return lines;
}

} // namespace terrain::unreal
