#pragma once

#include "assetbridge/product/format_capabilities.hpp"

#include <vector>

namespace assetbridge {

struct ConversionRoute {
    FormatId source;
    FormatId target;
    bool product_enabled;
    bool verified;
    std::vector<AssetFeature> verified_features;
};

[[nodiscard]] const std::vector<ConversionRoute>& conversion_route_registry();
[[nodiscard]] const ConversionRoute& conversion_route(
    FormatId source,
    FormatId target);
[[nodiscard]] bool route_feature_verified(
    const ConversionRoute& route,
    AssetFeature feature) noexcept;

} // namespace assetbridge
