#pragma once

// What terrain_export_rock prints: its lines, written to rock.txt too.

#include <cstdio>
#include <filesystem>
#include <string>

namespace rock_tool {

// The lines of a run, printed as they come and written at its end.
class Report {
public:
    void add(const std::string& line) {
        std::fputs(line.c_str(), stdout);
        text_ += line;
    }
    [[nodiscard]] bool write(const std::filesystem::path& path) const {
        std::FILE* file = std::fopen(path.string().c_str(), "w");
        if (file == nullptr) {
            return false;
        }
        const bool ok = std::fputs(text_.c_str(), file) >= 0;
        return std::fclose(file) == 0 && ok;
    }

private:
    std::string text_;
};

// A line from a printf pattern, 511 characters at most.
template <typename... Args>
std::string format(const char* pattern, Args... args) {
    char line[512];
    std::snprintf(line, sizeof line, pattern, args...);
    return line;
}

} // namespace rock_tool
