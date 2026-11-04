// The volcanoes of a world (volcanoes.hpp): the positions of the geology of the settlement
// (settlement/geology_points.cpp, placeVolcanoes, on the plates made again from the geography),
// their forms and sizes drawn, and the grid of buckets that finds them.
#include "terrain/generation/volcanoes.hpp"

#include "coast_shape.hpp"
#include "settlement/geology_plates.hpp"
#include "settlement/geology_points.hpp"
#include "terrain/generation/geography.hpp"
#include "terrain/generation/settlement/settlement_config.hpp"
#include "volcano_sites.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <tuple>

namespace terrain {

namespace {

static_assert(static_cast<int>(settlement::VolcanoKind::Arc) ==
                  static_cast<int>(VolcanoOrigin::Arc) &&
              static_cast<int>(settlement::VolcanoKind::Rift) ==
                  static_cast<int>(VolcanoOrigin::Rift) &&
              static_cast<int>(settlement::VolcanoKind::Ridge) ==
                  static_cast<int>(VolcanoOrigin::Ridge) &&
              static_cast<int>(settlement::VolcanoKind::Hotspot) ==
                  static_cast<int>(VolcanoOrigin::Hotspot),
              "the origins of the volcanoes follow the kinds of the geology");

// The rule of the geology of the settlement that these parameters stand for.
settlement::GeologyRule ruleOf(const VolcanoParams& p) {
    settlement::GeologyRule rule;
    rule.volcanoes = {p.volcanoes.min, p.volcanoes.max};
    rule.activeVolcanoes = {p.active.min, p.active.max};
    rule.largeVolcanoes = {p.large.min, p.large.max};
    rule.arcDistanceKm = {p.arcMinKm, p.arcMaxKm};
    rule.riftShare = p.riftShare;
    rule.hotspotShare = p.hotspotShare;
    rule.volcanoSpacingKm = p.spacingKm;
    return rule;
}

std::vector<VolcanoSite> placeSites(std::uint64_t worldSeed, const Geography& geo,
                                    const VolcanoParams& params, int threads) {
    const WorldSize& world = geo.world();
    std::vector<settlement::Volcano> placed;
    {
        // The plates hold references: made, read and dropped here.
        const settlement::GeographyPlates plates(worldSeed, geo, threads);
        const double scale = world.x * world.z / 1.0e6 / params.referenceAreaKm2;
        placed = settlement::placeVolcanoes(plates, worldSeed, ruleOf(params), scale, threads);
    }
    const std::uint64_t stream = noise::deriveSeed(worldSeed, volcano_sites::kShapeStream);
    std::vector<VolcanoSite> sites;
    sites.reserve(placed.size());
    for (const settlement::Volcano& v : placed) {
        VolcanoSite s;
        s.x = v.x;
        s.z = v.z;
        s.origin = static_cast<VolcanoOrigin>(v.kind);
        s.active = v.active;
        s.large = v.large;
        const GeographySample g = geo.sampleAt(v.x, v.z);
        s.atSea = g.coast < 0.0;
        s.floor = coast_shape::groundBase(geo.params(), g, g.coast);
        s.seed = volcano_sites::siteSeed(stream, v.x, v.z);
        s.form = volcano_sites::drawForm(s.origin, s.atSea, s.large, s.seed);
        volcano_sites::drawSizes(s);
        sites.push_back(s);
    }
    return sites;
}

// The buckets of [center - reach, center + reach] along an axis of count buckets: their first
// unwrapped index and how many (never more than count).
struct Span {
    std::int64_t first = 0;
    std::int64_t count = 0;
};

Span spanOf(double center, double reach, double period, std::int64_t count) {
    const double from = (center - reach + 0.5 * period) / Volcanoes::kBucket;
    const double to = (center + reach + 0.5 * period) / Volcanoes::kBucket;
    const auto first = static_cast<std::int64_t>(std::floor(from));
    const auto last = static_cast<std::int64_t>(std::floor(to));
    return {first, std::min(count, last - first + 1)};
}

std::int64_t wrapBucket(std::int64_t b, std::int64_t count) {
    return ((b % count) + count) % count;
}

} // namespace

const char* volcanoFormName(VolcanoForm form) {
    static constexpr const char* kNames[kVolcanoFormCount] = {
        "strato",     "twin strato", "collapsed strato", "shield",         "cinder field",
        "lava field", "fissure row", "maar field",       "caldera",        "plateau caldera",
        "island strato", "island shield", "island caldera", "seamount"};
    const auto i = static_cast<std::size_t>(form);
    return i < static_cast<std::size_t>(kVolcanoFormCount) ? kNames[i] : "unknown";
}

const char* volcanoOriginName(VolcanoOrigin origin) {
    switch (origin) {
    case VolcanoOrigin::Arc:
        return "arc";
    case VolcanoOrigin::Rift:
        return "rift";
    case VolcanoOrigin::Ridge:
        return "ridge";
    case VolcanoOrigin::Hotspot:
        return "hotspot";
    }
    return "unknown";
}

VolcanoOffset volcanoOffset(const WorldSize& world, double fromX, double fromZ, double toX,
                            double toZ) {
    return {wrapCoordinate(toX - fromX, world.x), wrapCoordinate(toZ - fromZ, world.z)};
}

Volcanoes::Volcanoes(std::uint64_t worldSeed, const Geography& geo, const VolcanoParams& params,
                     int threads)
    : params_(params), world_(geo.world()) {
    validate(world_);
    if (params_.enabled) {
        sites_ = placeSites(worldSeed, geo, params_, threads);
    }
    index();
}

void Volcanoes::index() {
    bucketsX_ = std::max<std::int64_t>(1, std::llround(world_.x / kBucket));
    bucketsZ_ = std::max<std::int64_t>(1, std::llround(world_.z / kBucket));
    const auto buckets = static_cast<std::size_t>(bucketsX_ * bucketsZ_);
    std::vector<std::uint32_t> counts(buckets, 0);
    // Twice over the sites: the counts, then the entries, in the order of the sites.
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = 0; i < sites_.size(); ++i) {
            const VolcanoSite& s = sites_[i];
            const auto reach = static_cast<double>(s.reach);
            const Span sx = spanOf(s.x, reach, world_.x, bucketsX_);
            const Span sz = spanOf(s.z, reach, world_.z, bucketsZ_);
            for (std::int64_t j = 0; j < sz.count; ++j) {
                for (std::int64_t k = 0; k < sx.count; ++k) {
                    const auto b = static_cast<std::size_t>(
                        wrapBucket(sz.first + j, bucketsZ_) * bucketsX_ +
                        wrapBucket(sx.first + k, bucketsX_));
                    if (pass == 0) {
                        ++counts[b];
                    } else {
                        entries_[counts[b]++] = static_cast<std::uint32_t>(i);
                    }
                }
            }
        }
        if (pass == 0) {
            starts_.assign(buckets + 1, 0);
            for (std::size_t b = 0; b < buckets; ++b) {
                starts_[b + 1] = starts_[b] + counts[b];
                counts[b] = starts_[b];
            }
            entries_.assign(starts_[buckets], 0);
        }
    }
}

std::size_t Volcanoes::bucketOf(double x, double z) const {
    const auto bx = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::floor((x + 0.5 * world_.x) / kBucket)), 0, bucketsX_ - 1);
    const auto bz = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::floor((z + 0.5 * world_.z) / kBucket)), 0, bucketsZ_ - 1);
    return static_cast<std::size_t>(bz * bucketsX_ + bx);
}

bool Volcanoes::mayHold(double x, double z) const {
    const std::size_t b = bucketOf(x, z);
    return starts_[b] != starts_[b + 1];
}

std::span<const std::uint32_t> Volcanoes::near(double x, double z) const {
    const std::size_t b = bucketOf(x, z);
    return std::span<const std::uint32_t>(entries_).subspan(starts_[b],
                                                            starts_[b + 1] - starts_[b]);
}

std::vector<VolcanoSite> Volcanoes::in(double minX, double minZ, double maxX, double maxZ) const {
    std::vector<VolcanoSite> out;
    for (const VolcanoSite& s : sites_) {
        if (s.x >= minX && s.x <= maxX && s.z >= minZ && s.z <= maxZ) {
            out.push_back(s);
        }
    }
    std::sort(out.begin(), out.end(), [](const VolcanoSite& a, const VolcanoSite& b) {
        return std::tie(a.x, a.z) < std::tie(b.x, b.z);
    });
    return out;
}

std::size_t Volcanoes::memoryBytes() const {
    return sizeof(*this) + sites_.capacity() * sizeof(VolcanoSite) +
           (starts_.capacity() + entries_.capacity()) * sizeof(std::uint32_t);
}

std::shared_ptr<const Volcanoes> Volcanoes::shared(std::uint64_t worldSeed,
                                                   const std::shared_ptr<const Geography>& geo,
                                                   const VolcanoParams& params) {
    if (!geo) {
        throw std::invalid_argument("Volcanoes: a geography is needed");
    }
    struct Entry {
        std::uint64_t seed;
        GeographyParams geography;
        WorldSize world;
        VolcanoParams params;
        std::weak_ptr<const Volcanoes> volcanoes;
    };
    static std::mutex mutex;
    static std::vector<Entry> entries;
    static std::deque<std::shared_ptr<const Volcanoes>> recent;
    const auto find = [&]() -> std::shared_ptr<const Volcanoes> {
        for (const Entry& e : entries) {
            if (e.seed == worldSeed && e.geography == geo->params() && e.world == geo->world() &&
                e.params == params) {
                return e.volcanoes.lock();
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
    auto built = std::make_shared<const Volcanoes>(worldSeed, *geo, params);
    const std::lock_guard<std::mutex> lock(mutex);
    if (auto found = find()) {
        return found; // built meanwhile by another thread: the same bits
    }
    std::erase_if(entries, [](const Entry& e) { return e.volcanoes.expired(); });
    entries.push_back({worldSeed, geo->params(), geo->world(), params, built});
    recent.push_back(built);
    if (recent.size() > 2) {
        recent.pop_front();
    }
    return built;
}

} // namespace terrain
