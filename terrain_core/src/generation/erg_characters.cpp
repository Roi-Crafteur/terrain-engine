// The characters of the ergs (erg_characters.hpp).
#include "erg_characters.hpp"

#include "dune_forms.hpp"
#include "terrain/generation/noise.hpp"

#include <algorithm>
#include <cstddef>
#include <deque>

namespace terrain::erg_characters {

namespace {

constexpr int kNone = -1;

// The 4 neighbors of a block on the torus.
std::array<std::size_t, 4> sides(const erg_field::Field& f, std::size_t b) {
    const int bi = static_cast<int>(b % static_cast<std::size_t>(f.bx));
    const int bj = static_cast<int>(b / static_cast<std::size_t>(f.bx));
    return {f.block((bi + 1) % f.bx, bj), f.block((bi + f.bx - 1) % f.bx, bj),
            f.block(bi, (bj + 1) % f.bz), f.block(bi, (bj + f.bz - 1) % f.bz)};
}

// The label of the erg of every block (kNone outside), and the ergs, by label.
struct Labels {
    std::vector<int> of;
    std::vector<ErgSummary> ergs;
    std::vector<std::size_t> first; // the first block of each erg
};

Labels label(const erg_field::Field& f, const std::vector<float>& cover) {
    Labels out;
    out.of.assign(cover.size(), kNone);
    for (std::size_t b = 0; b < cover.size(); ++b) {
        if (!(cover[b] > 0.0f) || out.of[b] != kNone) {
            continue;
        }
        const int id = static_cast<int>(out.ergs.size());
        ErgSummary erg;
        std::deque<std::size_t> queue{b};
        out.of[b] = id;
        while (!queue.empty()) {
            const std::size_t c = queue.front();
            queue.pop_front();
            erg.area += static_cast<double>(cover[c]);
            for (const std::size_t n : sides(f, c)) {
                if (cover[n] > 0.0f && out.of[n] == kNone) {
                    out.of[n] = id;
                    queue.push_back(n);
                }
            }
        }
        const auto bi = static_cast<double>(b % static_cast<std::size_t>(f.bx));
        const auto bj = static_cast<double>(b / static_cast<std::size_t>(f.bx));
        erg.x = -0.5 * f.grid.periodX() + (bi + 0.5) * f.blockSize;
        erg.z = -0.5 * f.grid.periodZ() + (bj + 0.5) * f.blockSize;
        out.ergs.push_back(erg);
        out.first.push_back(b);
    }
    return out;
}

// The characters of the ergs (by label) from the rule.
std::vector<ErgCharacter> characters(const Labels& l, std::uint64_t seed,
                                     const std::array<double, 3>& targets) {
    std::vector<std::size_t> order(l.ergs.size());
    for (std::size_t k = 0; k < order.size(); ++k) {
        order[k] = k;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return l.ergs[a].area > l.ergs[b].area;
    });
    double total = 0.0;
    for (const ErgSummary& e : l.ergs) {
        total += e.area;
    }
    static constexpr std::array<std::array<int, 3>, 6> kPermutations = {
        {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}};
    const auto& first = kPermutations[noise::deriveSeed(seed, 0x43484152ULL) % 6];
    std::vector<ErgCharacter> out(l.ergs.size(), ErgCharacter::Realistic);
    std::array<double, 3> given{};
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        const std::size_t e = order[rank];
        int c = 0;
        if (rank < 3) {
            c = first[rank];
        } else {
            double lag = -1.0e300;
            const std::uint64_t h = noise::deriveSeed(seed, l.first[e]);
            for (int k = 0; k < 3; ++k) {
                const auto at = static_cast<std::size_t>((k + static_cast<int>(h % 3)) % 3);
                const double behind = targets[at] * total - given[at];
                if (behind > lag) {
                    lag = behind;
                    c = static_cast<int>(at);
                }
            }
        }
        given[static_cast<std::size_t>(c)] += l.ergs[e].area;
        out[e] = static_cast<ErgCharacter>(c);
    }
    return out;
}

} // namespace

Assignment assign(const erg_field::Field& f, const std::vector<float>& cover, std::uint64_t seed,
                  const std::array<double, 3>& targets) {
    const Labels l = label(f, cover);
    const std::vector<ErgCharacter> of = characters(l, seed, targets);
    // Every block: the character of the nearest erg (a search from all of them at once).
    std::vector<int> nearest = l.of;
    std::deque<std::size_t> queue;
    for (std::size_t b = 0; b < nearest.size(); ++b) {
        if (nearest[b] != kNone) {
            queue.push_back(b);
        }
    }
    while (!queue.empty()) {
        const std::size_t c = queue.front();
        queue.pop_front();
        for (const std::size_t n : sides(f, c)) {
            if (nearest[n] == kNone) {
                nearest[n] = nearest[c];
                queue.push_back(n);
            }
        }
    }
    Assignment out;
    out.waves.assign(cover.size(), 0.0f);
    out.giants.assign(cover.size(), 0.0f);
    for (std::size_t b = 0; b < nearest.size(); ++b) {
        if (nearest[b] == kNone) {
            continue;
        }
        const ErgCharacter c = of[static_cast<std::size_t>(nearest[b])];
        out.waves[b] = c == ErgCharacter::Waves ? 1.0f : 0.0f;
        out.giants[b] = c == ErgCharacter::Giants ? 1.0f : 0.0f;
    }
    out.ergs = l.ergs;
    for (std::size_t e = 0; e < out.ergs.size(); ++e) {
        out.ergs[e].character = of[e];
    }
    std::stable_sort(out.ergs.begin(), out.ergs.end(),
                     [](const ErgSummary& a, const ErgSummary& b) { return a.area > b.area; });
    return out;
}

} // namespace terrain::erg_characters
