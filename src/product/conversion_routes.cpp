#include "assetbridge/product/conversion_routes.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace assetbridge {
namespace {

constexpr std::array all_formats {
    FormatId::obj,
    FormatId::gltf2,
    FormatId::glb2,
    FormatId::stl,
    FormatId::ply
};

std::vector<ConversionRoute> make_registry() {
    std::vector<ConversionRoute> routes;
    routes.reserve(all_formats.size() * all_formats.size());
    for (const auto source : all_formats) {
        for (const auto target : all_formats) {
            const bool verified_obj_to_glb =
                source == FormatId::obj && target == FormatId::glb2;
            routes.push_back({
                source,
                target,
                verified_obj_to_glb,
                verified_obj_to_glb,
                verified_obj_to_glb
                    ? std::vector<AssetFeature> {
                        AssetFeature::mesh,
                        AssetFeature::multiple_meshes,
                        AssetFeature::normals,
                        AssetFeature::uv0,
                        AssetFeature::material_slots
                    }
                    : std::vector<AssetFeature> {}
            });
        }
    }
    return routes;
}

} // namespace

const std::vector<ConversionRoute>& conversion_route_registry() {
    static const std::vector<ConversionRoute> registry = make_registry();
    return registry;
}

const ConversionRoute& conversion_route(FormatId source, FormatId target) {
    const auto& registry = conversion_route_registry();
    const auto iterator = std::find_if(
        registry.begin(),
        registry.end(),
        [source, target](const ConversionRoute& route) {
            return route.source == source && route.target == target;
        });
    if (iterator == registry.end()) {
        throw std::logic_error("Conversion route is missing from the registry.");
    }
    return *iterator;
}

bool route_feature_verified(
    const ConversionRoute& route,
    AssetFeature feature) noexcept {
    return std::find(
        route.verified_features.begin(),
        route.verified_features.end(),
        feature) != route.verified_features.end();
}

} // namespace assetbridge
