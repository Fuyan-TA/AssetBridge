#include "assetbridge/product/loss_preflight.hpp"

#include <iterator>
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

PreflightDecision evaluate_route_preflight(
    FormatId source,
    FormatId target,
    const AssetFeatures& source_features,
    bool runtime_exporter_available,
    bool asset_valid) {
    auto decision = evaluate_preflight(
        target,
        source_features,
        runtime_exporter_available,
        asset_valid);
    const auto& route = conversion_route(source, target);
    decision.product_enabled = route.product_enabled;
    decision.verified = route.verified;

    if (route.product_enabled && route.verified && asset_valid) {
        for (auto& assessment : decision.assessments) {
            if (route_feature_verified(route, assessment.feature)) {
                assessment.support = SupportLevel::supported;
                assessment.reason = "Feature is verified for this AssetBridge conversion route.";
            } else {
                assessment.support = SupportLevel::unverified;
                assessment.reason = "Feature has not been verified for this AssetBridge conversion route.";
                decision.losses.push_back({
                    "route_feature_unverified",
                    assessment.feature,
                    LossSeverity::blocking,
                    false,
                    assessment.reason
                });
            }
        }

        if (source_features.referenced_material_count > 1) {
            decision.losses.push_back({
                "route_feature_unverified",
                AssetFeature::material_slots,
                LossSeverity::blocking,
                false,
                "The verified OBJ to GLB2 route supports at most one referenced material."
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

void append_preflight_losses(
    PreflightDecision& decision,
    std::vector<LossItem> losses) {
    decision.losses.insert(
        decision.losses.end(),
        std::make_move_iterator(losses.begin()),
        std::make_move_iterator(losses.end()));
    decision.compatibility_result = compatibility_from(decision.losses);
    decision.overall_result = overall_from(
        decision.compatibility_result,
        decision.product_enabled,
        decision.verified);
}

} // namespace assetbridge
