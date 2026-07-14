#include "assetbridge/product/loss_preflight.hpp"

#include <string>

namespace assetbridge {
namespace {

CompatibilityResult compatibility_from(const std::vector<LossItem>& losses) {
    bool has_warning = false;
    for (const auto& loss : losses) {
        if (loss.severity == LossSeverity::blocking) {
            return CompatibilityResult::blocked;
        }
        has_warning = has_warning || loss.severity == LossSeverity::warning;
    }
    return has_warning ? CompatibilityResult::lossy : CompatibilityResult::safe;
}

OverallResult overall_from(
    CompatibilityResult compatibility,
    bool product_enabled,
    bool verified) {
    if (compatibility == CompatibilityResult::blocked) {
        return OverallResult::blocked;
    }
    if (compatibility == CompatibilityResult::lossy) {
        return OverallResult::lossy;
    }
    if (!product_enabled || !verified) {
        return OverallResult::unverified;
    }
    return OverallResult::safe;
}

} // namespace

std::string_view to_string(CompatibilityResult result) noexcept {
    switch (result) {
    case CompatibilityResult::safe: return "safe";
    case CompatibilityResult::lossy: return "lossy";
    case CompatibilityResult::blocked: return "blocked";
    }
    return "unknown";
}

std::string_view to_string(OverallResult result) noexcept {
    switch (result) {
    case OverallResult::safe: return "safe";
    case OverallResult::lossy: return "lossy";
    case OverallResult::blocked: return "blocked";
    case OverallResult::unverified: return "unverified";
    }
    return "unknown";
}

PreflightDecision evaluate_preflight(
    FormatId target,
    const AssetFeatures& source_features,
    bool runtime_exporter_available,
    bool asset_valid) {
    const auto& format = capability_for(target);
    PreflightDecision decision {
        target,
        runtime_exporter_available,
        format.product_enabled,
        format.verified,
        CompatibilityResult::safe,
        OverallResult::unverified,
        {},
        {}
    };

    if (!asset_valid) {
        decision.losses.push_back({
            "invalid_asset",
            std::nullopt,
            LossSeverity::blocking,
            false,
            "The source asset could not be inspected as a valid scene."
        });
    }

    if (!runtime_exporter_available) {
        decision.losses.push_back({
            "runtime_exporter_unavailable",
            std::nullopt,
            LossSeverity::blocking,
            false,
            "The current Assimp build does not provide the required target exporter."
        });
    }

    for (const auto feature : present_features(source_features)) {
        const auto& capability = capability_for(target, feature);
        decision.assessments.push_back({
            feature,
            capability.support,
            std::string(capability.reason)
        });

        if (capability.support == SupportLevel::partial
            || capability.support == SupportLevel::unsupported) {
            decision.losses.push_back({
                std::string(to_string(feature)) + "_" + std::string(to_string(capability.support)),
                feature,
                capability.loss_severity,
                capability.overrideable,
                std::string(capability.reason)
            });
        }
    }

    decision.compatibility_result = compatibility_from(decision.losses);
    decision.overall_result = overall_from(
        decision.compatibility_result,
        decision.product_enabled,
        decision.verified);
    return decision;
}

} // namespace assetbridge
