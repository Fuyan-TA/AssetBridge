#pragma once

#include "assetbridge/product/format_capabilities.hpp"

#include <vector>

namespace assetbridge {

enum class RouteFeature {
    multiple_referenced_materials,
    base_color_texture,
    embedded_base_color_texture,
    shared_texture_deduplication
};

struct RouteFeatureCapability {
    RouteFeature feature;
    bool product_enabled;
    bool verified;
};

struct ConversionRoute {
    FormatId source;
    FormatId target;
    bool product_enabled;
    bool verified;
    std::vector<AssetFeature> verified_features;
    std::vector<RouteFeatureCapability> feature_capabilities;
};

[[nodiscard]] const std::vector<ConversionRoute>& conversion_route_registry();
[[nodiscard]] const ConversionRoute& conversion_route(
    FormatId source,
    FormatId target);
[[nodiscard]] bool route_feature_verified(
    const ConversionRoute& route,
    AssetFeature feature) noexcept;
[[nodiscard]] const RouteFeatureCapability& route_feature_capability(
    const ConversionRoute& route,
    RouteFeature feature);
[[nodiscard]] std::string_view to_string(RouteFeature feature) noexcept;

} // namespace assetbridge
