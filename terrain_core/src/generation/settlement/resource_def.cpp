#include "terrain/generation/settlement/resource_def.hpp"

#include "data_fields.hpp"

#include <array>
#include <string>
#include <string_view>

namespace terrain::settlement {

namespace {

struct KindName {
    std::string_view kind;
    std::string_view type; // empty: the kind alone
    SourceKind value;
};

constexpr std::array<KindName, 18> kKinds = {{
    {"forestCover", "", SourceKind::ForestCover},
    {"roughness", "", SourceKind::Roughness},
    {"relief", "mountain", SourceKind::Mountain},
    {"relief", "oldMountain", SourceKind::OldMountain},
    {"relief", "hills", SourceKind::Hills},
    {"plateBoundary", "collision", SourceKind::Collision},
    {"plateBoundary", "divergence", SourceKind::Divergence},
    {"plateBoundary", "transform", SourceKind::Transform},
    {"fault", "", SourceKind::Fault},
    {"fault", "any", SourceKind::Fault},
    {"fault", "major", SourceKind::MajorFault},
    {"volcano", "", SourceKind::Volcano},
    {"volcano", "arc", SourceKind::VolcanicArc},
    {"aridCoast", "", SourceKind::AridCoast},
    {"saltLake", "", SourceKind::SaltLake},
    {"closedBasin", "", SourceKind::ClosedBasin},
    {"manaNode", "", SourceKind::ManaNode},
    {"crater", "", SourceKind::Crater},
}};

std::string catalog() {
    std::string list;
    for (const KindName& k : kKinds) {
        list += list.empty() ? "" : ", ";
        list += k.kind;
        if (!k.type.empty()) {
            list += "/";
            list += k.type;
        }
    }
    return list;
}

ResourceSource readSource(const Fields& f) {
    f.only({"kind", "type", "weight"});
    const std::string kind = f.text("kind");
    const std::string type = f.text("type", "");
    for (const KindName& k : kKinds) {
        if (k.kind == kind && k.type == type) {
            return ResourceSource{k.value, f.numberIn("weight", 0.0, 10.0)};
        }
    }
    f.fail(f.member("kind"), "kind",
           "unknown source " + kind + (type.empty() ? "" : "/" + type) + " (known: " +
               catalog() + ")");
}

} // namespace

std::string_view sourceKindName(SourceKind kind) {
    for (const KindName& k : kKinds) {
        if (k.value == kind) {
            return k.type.empty() ? k.kind : k.type;
        }
    }
    return "?";
}

ResourceDef readResourceDef(const json::Value& document, std::string_view source) {
    const Fields f(document, std::string(source), "");
    f.only({"id", "name", "hidden", "hiddenShare", "sources", "depositNoise"});
    ResourceDef r;
    r.id = f.text("id");
    if (r.id.empty()) {
        f.fail(f.member("id"), "id", "empty");
    }
    r.name = f.text("name", r.id);
    r.hidden = f.flag("hidden", false);
    r.hiddenShare = f.numberIn("hiddenShare", 0.0, 1.0, 0.0);
    const json::Value& sources = f.member("sources");
    if (!sources.isArray() || sources.size() == 0) {
        f.fail(sources, "sources", "expected a non-empty array");
    }
    for (std::size_t k = 0; k < sources.size(); ++k) {
        r.sources.push_back(readSource(Fields(sources.items()[k], f.source(),
                                              "sources[" + std::to_string(k) + "]")));
    }
    const Fields noise = f.object("depositNoise");
    noise.only({"scaleKm", "threshold", "softness"});
    r.depositScaleKm = noise.numberIn("scaleKm", 8.0, 1.0e5);
    r.depositThreshold = noise.numberIn("threshold", 0.0, 1.0);
    r.depositSoftness = noise.numberIn("softness", 0.01, 1.0, 0.15);
    r.document = document;
    return r;
}

} // namespace terrain::settlement
