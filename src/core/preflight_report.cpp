#include "assetbridge/core/preflight_report.hpp"

#include <algorithm>
#include <string_view>

namespace assetbridge {
namespace {

std::filesystem::path normalized_report_path(
    const std::filesystem::path& requested_file,
    const InspectionResult& inspection) {
    if (inspection.summary.has_value()) {
        return inspection.summary->file_path;
    }

    std::error_code error;
    const auto absolute = std::filesystem::absolute(requested_file, error);
    return error ? requested_file : absolute.lexically_normal();
}

SourceFormatStatus source_status(const std::filesystem::path& file) {
    const auto utf8_extension = file.extension().u8string();
    const std::string extension(
        reinterpret_cast<const char*>(utf8_extension.data()),
        utf8_extension.size());
    const auto format = parse_format_id(extension);
    const bool verified_obj = format == FormatId::obj;
    return { format, verified_obj, verified_obj };
}

bool exporter_id_matches(FormatId target, std::string_view exporter_id) {
    switch (target) {
    case FormatId::obj:
        return exporter_id == "obj";
    case FormatId::gltf2:
        return exporter_id == "gltf2";
    case FormatId::glb2:
        return exporter_id == "glb2";
    case FormatId::stl:
        return exporter_id == "stl" || exporter_id == "stlb";
    case FormatId::ply:
        return exporter_id == "ply" || exporter_id == "plyb";
    }
    return false;
}

std::optional<AssetFeature> companion_issue_feature(CompanionErrorCode code) {
    switch (code) {
    case CompanionErrorCode::material_library_missing:
    case CompanionErrorCode::material_library_path_absolute:
    case CompanionErrorCode::material_library_path_outside_asset_root:
    case CompanionErrorCode::material_library_not_regular_file:
    case CompanionErrorCode::material_reference_unresolved:
        return AssetFeature::material_slots;
    case CompanionErrorCode::texture_file_missing:
    case CompanionErrorCode::texture_path_absolute:
    case CompanionErrorCode::texture_path_outside_asset_root:
    case CompanionErrorCode::texture_path_not_regular_file:
    case CompanionErrorCode::texture_format_unsupported:
    case CompanionErrorCode::texture_signature_mismatch:
    case CompanionErrorCode::texture_semantic_unverified:
    case CompanionErrorCode::texture_options_unverified:
    case CompanionErrorCode::transparency_unverified:
    case CompanionErrorCode::multiple_textures_per_material_unverified:
    case CompanionErrorCode::texture_file_too_large:
    case CompanionErrorCode::texture_count_limit_exceeded:
    case CompanionErrorCode::texture_total_size_limit_exceeded:
        return AssetFeature::external_textures;
    case CompanionErrorCode::none:
    case CompanionErrorCode::companion_parse_failed:
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<LossItem> companion_losses(const CompanionResolution& resolution) {
    std::vector<LossItem> losses;
    losses.reserve(resolution.issues.size());
    for (const auto& issue : resolution.issues) {
        losses.push_back({
            std::string(to_string(issue.code)),
            companion_issue_feature(issue.code),
            LossSeverity::blocking,
            false,
            issue.message
        });
    }
    return losses;
}

} // namespace

bool has_runtime_exporter(
    FormatId target,
    const RuntimeCapabilities& runtime_capabilities) {
    return std::any_of(
        runtime_capabilities.exporters.begin(),
        runtime_capabilities.exporters.end(),
        [target](const ExporterCapability& exporter) {
            return exporter.runtime_available && exporter_id_matches(target, exporter.id);
        });
}

PreflightReport create_preflight_report(
    const std::filesystem::path& file,
    FormatId target) {
    const AssetInspector inspector;
    auto inspection = inspector.inspect(file);
    const auto runtime_capabilities = query_runtime_capabilities();
    const bool runtime_exporter_available = has_runtime_exporter(
        target,
        runtime_capabilities);

    AssetFeatures features;
    if (inspection.features.has_value()) {
        features = *inspection.features;
    }

    const auto source = source_status(file);
    std::optional<CompanionResolution> companions;
    if (source.format == FormatId::obj && inspection.features.has_value()) {
        const CompanionResolver resolver;
        companions = resolver.resolve(
            file,
            inspection.features->referenced_material_names);
    }
    const RoutePreflightEvidence evidence {
        companions.has_value()
    };
    auto decision = source.format.has_value()
        ? evaluate_route_preflight(
            *source.format,
            target,
            features,
            runtime_exporter_available,
            static_cast<bool>(inspection),
            evidence)
        : evaluate_preflight(
            target,
            features,
            runtime_exporter_available,
            static_cast<bool>(inspection));

    if (companions.has_value()) {
        append_preflight_losses(decision, companion_losses(*companions));
    }

    return {
        normalized_report_path(file, inspection),
        source,
        inspection.error_code,
        std::move(inspection.error_message),
        std::move(inspection.features),
        std::move(companions),
        std::move(decision)
    };
}

} // namespace assetbridge
