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
                    : std::vector<AssetFeature> {},
                {
                    { RouteFeature::multiple_referenced_materials, false, false },
                    { RouteFeature::base_color_texture, false, false },
                    { RouteFeature::embedded_base_color_texture, false, false },
                    { RouteFeature::shared_texture_deduplication, false, false }
                }
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

const RouteFeatureCapability& route_feature_capability(
    const ConversionRoute& route,
    RouteFeature feature) {
    const auto iterator = std::find_if(
        route.feature_capabilities.begin(),
        route.feature_capabilities.end(),
        [feature](const RouteFeatureCapability& capability) {
            return capability.feature == feature;
        });
    if (iterator == route.feature_capabilities.end()) {
        throw std::logic_error("Route feature capability is missing from the registry.");
    }
    return *iterator;
}

std::string_view to_string(RouteFeature feature) noexcept {
    switch (feature) {
    case RouteFeature::multiple_referenced_materials:
        return "multiple_referenced_materials";
    case RouteFeature::base_color_texture:
        return "base_color_texture";
    case RouteFeature::embedded_base_color_texture:
        return "embedded_base_color_texture";
    case RouteFeature::shared_texture_deduplication:
        return "shared_texture_deduplication";
    }
    return "unknown";
}

} // namespace assetbridge
