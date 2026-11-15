// The provinces of the plateaus and the karst shared by seed, parameters and world (agent 46,
// tablelands.hpp): built once, kept while used, the two last ones kept besides.
#include "terrain/generation/tablelands.hpp"

#include "terrain/generation/pre_climate.hpp"

#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace terrain {

std::shared_ptr<const Tablelands> Tablelands::shared(const TablelandSeeds& seeds,
                                                     const GeographyParams& params,
                                                     const WorldSize& world) {
    struct Entry {
        TablelandSeeds seeds;
        GeographyParams params;
        WorldSize world;
        std::weak_ptr<const Tablelands> tablelands;
    };
    static std::mutex mutex;
    static std::vector<Entry> entries;
    static std::deque<std::shared_ptr<const Tablelands>> recent;
    const auto find = [&]() -> std::shared_ptr<const Tablelands> {
        for (const Entry& e : entries) {
            if (e.seeds == seeds && e.params == params && e.world == world) {
                return e.tablelands.lock();
            }
        }
        return nullptr;
    };
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (auto found = find()) {
            return found;
        }
    }
    auto built = std::make_shared<const Tablelands>(
        seeds, Geography::shared(seeds.geography, params, world),
        PreClimate::shared(seeds.geography, params, world));
    const std::lock_guard<std::mutex> lock(mutex);
    if (auto found = find()) {
        return found; // built meanwhile by another thread: the same bits
    }
    std::erase_if(entries, [](const Entry& e) { return e.tablelands.expired(); });
    entries.push_back({seeds, params, world, built});
    recent.push_back(built);
    if (recent.size() > 2) {
        recent.pop_front();
    }
    return built;
}

} // namespace terrain
