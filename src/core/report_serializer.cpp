#include "assetbridge/core/report_serializer.hpp"

#include <sstream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace assetbridge {
namespace {

std::string path_to_generic_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

std::filesystem::path report_path(
    const std::filesystem::path& requested_file,
    const InspectionResult& result) {
    if (result.summary.has_value()) {
        return result.summary->file_path;
    }

    std::error_code error;
    const auto absolute = std::filesystem::absolute(requested_file, error);
    return error ? requested_file : absolute.lexically_normal();
}

nlohmann::json format_availability_to_json(const FormatAvailability& availability) {
    return {
        { "extension", availability.extension },
        { "runtime_available", availability.runtime_available },
        { "product_enabled", availability.product_enabled },
        { "verified", availability.verified }
    };
}

} // namespace

std::string inspection_report_to_json(
    const std::filesystem::path& requested_file,
    const InspectionResult& result) {
    const auto file = report_path(requested_file, result);
    nlohmann::json report = {
        { "schema", inspection_schema },
        { "status", result ? "success" : "error" },
        { "file", {
            { "path", path_to_generic_utf8(file) },
            { "extension", path_to_generic_utf8(file.extension()) }
        } },
        { "summary", nullptr },
        { "error", nullptr }
    };

    if (result) {
        const auto& summary = *result.summary;
        report["summary"] = {
            { "mesh_count", summary.mesh_count },
            { "vertex_count", summary.vertex_count },
            { "face_count", summary.face_count },
            { "triangle_count", summary.triangle_count },
            { "material_count", summary.material_count },
            { "uv_channel_count", summary.uv_channel_count },
            { "has_normals", summary.has_normals }
        };
    } else {
        report["error"] = {
            { "code", to_string(result.error_code) },
            { "message", result.error_message }
        };
    }

    return report.dump(2);
}

std::string capabilities_to_text(const RuntimeCapabilities& capabilities) {
    std::ostringstream output;
    output << "Assimp Version: " << capabilities.assimp_version << '\n';
    output << "Importers:\n";
    for (const auto& importer : capabilities.importers) {
        output << "- " << importer.name << '\n';
        for (const auto& format : importer.formats) {
            output
                << "  " << format.extension
                << " | runtime_available=" << (format.runtime_available ? "true" : "false")
                << " | product_enabled=" << (format.product_enabled ? "true" : "false")
                << " | verified=" << (format.verified ? "true" : "false")
                << '\n';
        }
    }

    output << "Exporters:\n";
    for (const auto& exporter : capabilities.exporters) {
        output
            << "- " << exporter.id
            << " | " << exporter.description
            << " | " << exporter.extension
            << " | runtime_available=" << (exporter.runtime_available ? "true" : "false")
            << " | product_enabled=" << (exporter.product_enabled ? "true" : "false")
            << " | verified=" << (exporter.verified ? "true" : "false")
            << '\n';
    }

    return output.str();
}

std::string capabilities_to_json(const RuntimeCapabilities& capabilities) {
    nlohmann::json importers = nlohmann::json::array();
    for (const auto& importer : capabilities.importers) {
        nlohmann::json formats = nlohmann::json::array();
        for (const auto& format : importer.formats) {
            formats.push_back(format_availability_to_json(format));
        }

        importers.push_back({
            { "name", importer.name },
            { "formats", std::move(formats) }
        });
    }

    nlohmann::json exporters = nlohmann::json::array();
    for (const auto& exporter : capabilities.exporters) {
        exporters.push_back({
            { "id", exporter.id },
            { "description", exporter.description },
            { "extension", exporter.extension },
            { "runtime_available", exporter.runtime_available },
            { "product_enabled", exporter.product_enabled },
            { "verified", exporter.verified }
        });
    }

    nlohmann::json report = {
        { "schema", capabilities_schema },
        { "assimp_version", capabilities.assimp_version },
        { "importers", std::move(importers) },
        { "exporters", std::move(exporters) }
    };
    return report.dump(2);
}

} // namespace assetbridge
