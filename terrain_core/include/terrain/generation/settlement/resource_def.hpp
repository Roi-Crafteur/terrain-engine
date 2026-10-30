#pragma once

#include "terrain/json/json.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A resource of the settlement (docs/PEUPLEMENT.md, section 4 ter): a data file, one per
// resource, with its rule of placement. Adding a resource is adding a file: its rule combines the
// sources of a fixed catalog, which the code knows; the list of the resources is the data's.
namespace terrain::settlement {

// What a resource can be drawn from, each a value in [0, 1] per land cell of the planet level.
// Its JSON name is "kind" (and "type" when it has several).
enum class SourceKind : std::uint8_t {
    ForestCover,   // "forestCover": the cover of the vegetation
    Roughness,     // "roughness": the roughness of the cell
    Mountain,      // "relief" "mountain": a high intensity of the relief
    OldMountain,   // "relief" "oldMountain": high relief of worn character (old massifs)
    Hills,         // "relief" "hills": a middle intensity of the relief
    Collision,     // "plateBoundary" "collision": near a colliding boundary of the plates
    Divergence,    // "plateBoundary" "divergence": near a diverging boundary
    Transform,     // "plateBoundary" "transform": near a sliding boundary
    Fault,         // "fault": the faults, every boundary of the plates and the rift edges
    MajorFault,    // "fault" "major": the major faults (transform boundaries, wide)
    Volcano,       // "volcano": near any volcano
    VolcanicArc,   // "volcano" "arc": near the volcanoes of the arcs of the subductions
    AridCoast,     // "aridCoast": a dry coast
    SaltLake,      // "saltLake": near a lake without outlet or a playa
    ClosedBasin,   // "closedBasin": a dry land draining to no sea
    ManaNode,      // "manaNode": near a node of the telluric lines
    Crater,        // "crater": near an arcane crater
};
inline constexpr std::size_t kSourceKinds = 17;

struct ResourceSource {
    SourceKind kind = SourceKind::ForestCover;
    double weight = 1.0;
};

// The value of a resource on a cell: the weighted sum of its sources, at most 1, times its
// deposits: a periodic noise of wavelength depositScaleKm spread over [0, 1] (0.5 + 2 n, clamped:
// a threshold of 0.5 keeps about half of the land, 0.7 about a fifth), through a smoothstep from
// depositThreshold to depositThreshold + depositSoftness (patches, never a uniform layer).
struct ResourceDef {
    std::string id;
    std::string name;
    // A resource partly hidden: its hidden part comes later from the secret seed (never derived
    // from the seed of the world). V1 computes its public part only; the share is kept.
    bool hidden = false;
    double hiddenShare = 0.0;
    std::vector<ResourceSource> sources;
    double depositScaleKm = 100.0;
    double depositThreshold = 0.5;
    double depositSoftness = 0.15;
    json::Value document; // kept whole for the copy of the world

    friend bool operator==(const ResourceDef& a, const ResourceDef& b) {
        return a.document == b.document;
    }
};

// The JSON names of a kind.
[[nodiscard]] std::string_view sourceKindName(SourceKind kind);

// A resource of a document. Throws json::Error with the source, the path and the reason.
[[nodiscard]] ResourceDef readResourceDef(const json::Value& document, std::string_view source);

} // namespace terrain::settlement
