#include "terrain/map/map_layers.hpp"

#include <algorithm>
#include <stdexcept>

namespace terrain {

namespace {

auto byName(std::string_view name) {
    return [name](const std::pair<std::string, MapLayerFn>& layer) { return layer.first == name; };
}

} // namespace

void MapLayers::add(std::string name, MapLayerFn layer) {
    if (name.empty() || !layer) {
        throw std::invalid_argument("map layer: a name and a function expected");
    }
    const auto it = std::find_if(layers_.begin(), layers_.end(), byName(name));
    if (it != layers_.end()) {
        it->second = std::move(layer);
        return;
    }
    layers_.emplace_back(std::move(name), std::move(layer));
}

bool MapLayers::remove(std::string_view name) {
    const auto it = std::find_if(layers_.begin(), layers_.end(), byName(name));
    if (it == layers_.end()) {
        return false;
    }
    layers_.erase(it);
    return true;
}

const MapLayerFn* MapLayers::find(std::string_view name) const {
    const auto it = std::find_if(layers_.begin(), layers_.end(), byName(name));
    return it == layers_.end() ? nullptr : &it->second;
}

std::vector<std::string> MapLayers::names() const {
    std::vector<std::string> out;
    out.reserve(layers_.size());
    for (const auto& layer : layers_) {
        out.push_back(layer.first);
    }
    return out;
}

std::vector<const MapLayerFn*> MapLayers::enabled(const std::vector<std::string>& names) const {
    for (const std::string& name : names) {
        if (find(name) == nullptr) {
            throw std::invalid_argument("map layer \"" + name + "\" is not registered");
        }
    }
    std::vector<const MapLayerFn*> out;
    for (const auto& layer : layers_) {
        if (std::find(names.begin(), names.end(), layer.first) != names.end()) {
            out.push_back(&layer.second);
        }
    }
    return out;
}

// Straight alpha: C = (Ca A + Cb B (1 - A)) / (A + B (1 - A)), every term scaled by 255^2 so
// that integers hold it exactly, rounded to nearest.
MapColor over(MapColor below, MapColor above) {
    const std::uint32_t a = above.a;
    const std::uint32_t weightBelow = static_cast<std::uint32_t>(below.a) * (255u - a);
    const std::uint32_t den = 255u * a + weightBelow;
    if (den == 0u) {
        return {};
    }
    const auto mix = [&](std::uint8_t ca, std::uint8_t cb) {
        const std::uint32_t num = 255u * a * ca + weightBelow * cb;
        return static_cast<std::uint8_t>((num + den / 2u) / den);
    };
    return {mix(above.r, below.r), mix(above.g, below.g), mix(above.b, below.b),
            static_cast<std::uint8_t>((den + 127u) / 255u)};
}

} // namespace terrain
