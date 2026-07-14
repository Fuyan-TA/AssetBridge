#pragma once

#include "assetbridge/product/format_capabilities.hpp"
#include "assetbridge/product/conversion_routes.hpp"

#include <optional>
#include <string>
#include <vector>

namespace assetbridge {

enum class CompatibilityResult {
    safe,
    lossy,
    blocked
};

enum class OverallResult {
    safe,
    lossy,
    blocked,
    unverified
};

struct FeatureAssessment {
    AssetFeature feature;
    SupportLevel support;
    std::string reason;
};

struct LossItem {
    std::string code;
    std::optional<AssetFeature> feature;
    LossSeverity severity;
    bool overrideable;
    std::string reason;
};

struct PreflightDecision {
    FormatId target;
    bool runtime_exporter_available;
    bool product_enabled;
    bool verified;
    CompatibilityResult compatibility_result;
    OverallResult overall_result;
    std::vector<FeatureAssessment> assessments;
    std::vector<LossItem> losses;
};

[[nodiscard]] std::string_view to_string(CompatibilityResult result) noexcept;
[[nodiscard]] std::string_view to_string(OverallResult result) noexcept;

[[nodiscard]] PreflightDecision evaluate_preflight(
    FormatId target,
    const AssetFeatures& source_features,
    bool runtime_exporter_available,
    bool asset_valid = true);

[[nodiscard]] PreflightDecision evaluate_route_preflight(
    FormatId source,
    FormatId target,
    const AssetFeatures& source_features,
    bool runtime_exporter_available,
    bool asset_valid = true);

} // namespace assetbridge
