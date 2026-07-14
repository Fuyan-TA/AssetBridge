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

    auto decision = evaluate_preflight(
        target,
        features,
        runtime_exporter_available,
        static_cast<bool>(inspection));

    return {
        normalized_report_path(file, inspection),
        source_status(file),
        inspection.error_code,
        std::move(inspection.error_message),
        std::move(inspection.features),
        std::move(decision)
    };
}

} // namespace assetbridge
