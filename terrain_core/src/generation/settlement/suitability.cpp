#include "terrain/generation/settlement/suitability.hpp"

#include "../planet/planet_parallel.hpp"
#include "settlement_hash.hpp"
#include "settlement_random.hpp"
#include "terrain/generation/noise_octaves.hpp"
#include "terrain/generation/planet/planet_level.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace terrain::settlement {

namespace {

// The noise (2 octaves, standard deviation about 0.23) spread over [-1, 1], as the deposits.
constexpr double kNoiseSpread = 2.0;

noise::FbmOctaves noiseOf(const People& p, const SuitabilityRule& rule, std::uint64_t worldSeed,
                          const WorldSize& world) {
    return noise::FbmOctaves(streamOf(worldSeed, "suitability:" + p.id),
                             noise::FbmParams{.octaves = 2,
                                              .frequency = 1.0 / (rule.noiseScaleKm * 1000.0),
                                              .gain = 0.5,
                                              .lattices = noise::OctaveLattices::Unrotated},
                             world);
}

// The scores of the land cells of row j for every people.
void scoreRow(const PlanetLevel& level, const SettlementMaps& m, const SettlementData& data,
              const std::vector<noise::FbmOctaves>& noises, SuitabilityMaps& out, int j) {
    const SuitabilityRule& rule = data.config.suitability;
    for (int i = 0; i < m.grid.nx; ++i) {
        const std::size_t cell = m.grid.index(i, j);
        if (!m.land->land(cell)) {
            continue;
        }
        const std::size_t slot = m.land->slot(cell);
        const CriterionValues c = m.criteria(level, cell);
        SuitabilityTerms t;
        t.habitability = unquantize(m.habitability.values()[slot]);
        t.prana = unquantize(m.prana.values()[slot]);
        t.mana = unquantize(m.mana.values()[slot]);
        t.danger = unquantize(m.danger.values()[slot]);
        const double x = m.grid.centerX(i);
        const double z = m.grid.centerZ(j);
        for (std::size_t k = 0; k < data.peoples.size(); ++k) {
            const People& p = data.peoples[k];
            t.physical = physicalScore(p.preferences, c, data.config.physicalScore);
            t.noise = std::clamp(kNoiseSpread * noises[k].fbm2D(x, z), -1.0, 1.0);
            out.byPeople[k].values()[slot] = quantize(suitabilityOf(p, rule, t));
        }
    }
}

} // namespace

double suitabilityMax(const People& p, const SuitabilityRule& rule) {
    return p.habitabilityWeight + p.physicalWeight + std::max(p.prana, 0.0) +
           std::max(p.mana, 0.0) + rule.noiseAmplitude;
}

double suitabilityOf(const People& p, const SuitabilityRule& rule, const SuitabilityTerms& t) {
    const double max = suitabilityMax(p, rule);
    if (t.physical <= 0.0 || max <= 0.0) {
        return 0.0;
    }
    const double sum = t.habitability * p.habitabilityWeight + t.physical * p.physicalWeight +
                       t.prana * p.prana + t.mana * p.mana - t.danger * p.dangerSensitivity +
                       t.noise * rule.noiseAmplitude;
    return std::clamp(sum / max, 0.0, 1.0);
}

std::size_t SuitabilityMaps::memoryBytes() const {
    std::size_t bytes = 0;
    for (const auto& map : byPeople) {
        bytes += map.values().capacity();
    }
    return bytes;
}

std::uint64_t SuitabilityMaps::checksum() const {
    Fnv64 f;
    for (const auto& map : byPeople) {
        f.addBytes(map.values());
    }
    return f.value();
}

SuitabilityMaps computeSuitability(const PlanetLevel& level, const SettlementMaps& maps,
                                   const SettlementData& data, std::uint64_t worldSeed,
                                   int threads) {
    const auto start = std::chrono::steady_clock::now();
    const WorldSize world{maps.grid.periodX(), maps.grid.periodZ()};
    std::vector<noise::FbmOctaves> noises;
    SuitabilityMaps out;
    for (const People& p : data.peoples) {
        noises.push_back(noiseOf(p, data.config.suitability, worldSeed, world));
        out.byPeople.emplace_back(maps.land, std::uint8_t{0}, std::uint8_t{0});
    }
    planet::parallelRows(maps.grid.nz, planet::resolveThreads(threads), [&](int begin, int end) {
        for (int j = begin; j < end; ++j) {
            scoreRow(level, maps, data, noises, out, j);
        }
    });
    out.seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}

} // namespace terrain::settlement
