#pragma once

#include "assetbridge/core/asset_inspector.hpp"
#include "assetbridge/core/companion_resolver.hpp"
#include "assetbridge/core/runtime_capabilities.hpp"
#include "assetbridge/product/loss_preflight.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace assetbridge {

struct SourceFormatStatus {
    std::optional<FormatId> format;
    bool product_enabled = false;
    bool verified = false;
};

struct PreflightReport {
    std::filesystem::path file_path;
    SourceFormatStatus source;
    InspectionErrorCode error_code = InspectionErrorCode::none;
    std::string error_message;
    std::optional<AssetFeatures> features;
    std::optional<CompanionResolution> companions;
    PreflightDecision decision;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error_code == InspectionErrorCode::none && features.has_value();
    }
};

[[nodiscard]] bool has_runtime_exporter(
    FormatId target,
    const RuntimeCapabilities& runtime_capabilities);

[[nodiscard]] PreflightReport create_preflight_report(
    const std::filesystem::path& file,
    FormatId target);

} // namespace assetbridge
