#include "assetbridge/core/conversion_serializer.hpp"

#include "assetbridge/core/preflight_serializer.hpp"
#include "assetbridge/core/runtime_capabilities.hpp"

#include <sstream>

#include <nlohmann/json.hpp>

namespace assetbridge {
namespace {

std::string path_to_generic_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

nlohmann::json vector_to_json(const Vector3Value& value) {
    return {
        { "x", value.x },
        { "y", value.y },
        { "z", value.z }
    };
}

nlohmann::json bounds_to_json(const BoundsValue& bounds) {
    return {
        { "valid", bounds.valid },
        { "minimum", vector_to_json(bounds.minimum) },
        { "maximum", vector_to_json(bounds.maximum) },
        { "center", vector_to_json(bounds.center) },
        { "size", vector_to_json(bounds.size) }
    };
}

nlohmann::json analysis_to_json(const ConversionSceneAnalysis& analysis) {
    return {
        { "mesh_count", analysis.mesh_count },
        { "vertex_count", analysis.vertex_count },
        { "triangle_count", analysis.triangle_count },
        { "referenced_material_count", analysis.referenced_material_count },
        { "has_normals", analysis.has_normals },
        { "has_uv0", analysis.has_uv0 },
        { "bounds", bounds_to_json(analysis.bounds) },
        { "diffuse_color", analysis.diffuse_color.has_value()
            ? nlohmann::json({
                { "red", analysis.diffuse_color->red },
                { "green", analysis.diffuse_color->green },
                { "blue", analysis.diffuse_color->blue },
                { "alpha", analysis.diffuse_color->alpha }
            })
            : nlohmann::json(nullptr) }
    };
}

nlohmann::json triangulation_to_json(const TriangulationDiagnostics& diagnostics) {
    return {
        { "source_face_count", diagnostics.source_face_count },
        { "source_triangle_face_count", diagnostics.source_triangle_face_count },
        { "source_non_triangle_face_count", diagnostics.source_non_triangle_face_count },
        { "export_ready_triangle_count", diagnostics.export_ready_triangle_count }
    };
}

nlohmann::json validation_to_json(const std::vector<ValidationCheck>& checks) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& check : checks) {
        result.push_back({
            { "name", check.name },
            { "passed", check.passed },
            { "details", check.details }
        });
    }
    return result;
}

nlohmann::json paths_to_json(const std::vector<std::filesystem::path>& paths) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& path : paths) {
        result.push_back(path_to_generic_utf8(path));
    }
    return result;
}

nlohmann::json report_json(const ConversionReport& report) {
    return {
        { "schema", conversion_schema },
        { "status", report ? "success" : "error" },
        { "source", {
            { "path", path_to_generic_utf8(report.source_path) },
            { "format_id", report.source_format.has_value()
                ? nlohmann::json(to_string(*report.source_format))
                : nlohmann::json(nullptr) }
        } },
        { "route", {
            { "source_format_id", report.source_format.has_value()
                ? nlohmann::json(to_string(*report.source_format))
                : nlohmann::json(nullptr) },
            { "target_format_id", to_string(report.target_format) },
            { "product_enabled", report.route_product_enabled },
            { "verified", report.route_verified }
        } },
        { "output", {
            { "directory", report.output_directory.has_value()
                ? nlohmann::json(path_to_generic_utf8(*report.output_directory))
                : nlohmann::json(nullptr) },
            { "files", paths_to_json(report.output_files) }
        } },
        { "source_analysis", report.source_analysis.has_value()
            ? analysis_to_json(*report.source_analysis)
            : nlohmann::json(nullptr) },
        { "triangulation", report.triangulation.has_value()
            ? triangulation_to_json(*report.triangulation)
            : nlohmann::json(nullptr) },
        { "preflight", report.preflight.has_value()
            ? nlohmann::json::parse(preflight_to_json(*report.preflight))
            : nlohmann::json(nullptr) },
        { "processing_steps", report.processing_steps },
        { "output_analysis", report.output_analysis.has_value()
            ? analysis_to_json(*report.output_analysis)
            : nlohmann::json(nullptr) },
        { "validation_checks", validation_to_json(report.validation_checks) },
        { "warnings", report.warnings },
        { "error", report ? nlohmann::json(nullptr) : nlohmann::json({
            { "code", to_string(report.error_code) },
            { "message", report.error_message }
        }) },
        { "assimp_version", report.assimp_version },
        { "timings_ms", {
            { "preflight", report.timings.preflight_ms },
            { "import", report.timings.import_ms },
            { "export", report.timings.export_ms },
            { "reimport", report.timings.reimport_ms },
            { "validation", report.timings.validation_ms },
            { "total", report.timings.total_ms }
        } }
    };
}

} // namespace

std::string conversion_report_to_text(const ConversionReport& report) {
    std::ostringstream output;
    output
        << "Status: " << (report ? "success" : "error") << '\n'
        << "Source: " << path_to_generic_utf8(report.source_path) << '\n'
        << "Route: "
        << (report.source_format.has_value() ? to_string(*report.source_format) : "unknown")
        << " -> " << to_string(report.target_format) << '\n'
        << "Product Enabled: " << (report.route_product_enabled ? "true" : "false") << '\n'
        << "Verified: " << (report.route_verified ? "true" : "false") << '\n';

    if (report.output_directory.has_value()) {
        output << "Output Directory: " << path_to_generic_utf8(*report.output_directory) << '\n';
        for (const auto& file : report.output_files) {
            output << "Output File: " << path_to_generic_utf8(file) << '\n';
        }
    }

    if (report.source_analysis.has_value()) {
        output
            << "Source Meshes: " << report.source_analysis->mesh_count << '\n'
            << "Export-ready Triangles: " << report.source_analysis->triangle_count << '\n'
            << "Source Vertices (diagnostic): " << report.source_analysis->vertex_count << '\n';
    }
    if (report.triangulation.has_value()) {
        output
            << "Source Faces: " << report.triangulation->source_face_count << '\n'
            << "Source Triangle Faces: " << report.triangulation->source_triangle_face_count << '\n'
            << "Source Non-triangle Faces: " << report.triangulation->source_non_triangle_face_count << '\n'
            << "Export-ready Triangle Count: "
            << report.triangulation->export_ready_triangle_count << '\n';
    }
    if (report.output_analysis.has_value()) {
        output
            << "Output Meshes: " << report.output_analysis->mesh_count << '\n'
            << "Output Triangles: " << report.output_analysis->triangle_count << '\n'
            << "Output Vertices (diagnostic): " << report.output_analysis->vertex_count << '\n';
    }

    output << "Validation Checks:\n";
    for (const auto& check : report.validation_checks) {
        output << "- " << check.name << ": " << (check.passed ? "passed" : "failed")
               << " | " << check.details << '\n';
    }
    output << "Total Time: " << report.timings.total_ms << " ms\n";

    if (!report) {
        output << "Error [" << to_string(report.error_code) << "]: "
               << report.error_message << '\n';
    }
    return output.str();
}

std::string conversion_report_to_json(const ConversionReport& report) {
    return report_json(report).dump(2);
}

std::string conversion_argument_error_to_json(
    std::string_view code,
    std::string_view message) {
    nlohmann::json result = {
        { "schema", conversion_schema },
        { "status", "error" },
        { "source", nullptr },
        { "route", nullptr },
        { "output", { { "directory", nullptr }, { "files", nlohmann::json::array() } } },
        { "source_analysis", nullptr },
        { "triangulation", nullptr },
        { "preflight", nullptr },
        { "processing_steps", nlohmann::json::array() },
        { "output_analysis", nullptr },
        { "validation_checks", nlohmann::json::array() },
        { "warnings", nlohmann::json::array() },
        { "error", { { "code", code }, { "message", message } } },
        { "assimp_version", query_runtime_capabilities().assimp_version },
        { "timings_ms", {
            { "preflight", 0.0 },
            { "import", 0.0 },
            { "export", 0.0 },
            { "reimport", 0.0 },
            { "validation", 0.0 },
            { "total", 0.0 }
        } }
    };
    return result.dump(2);
}

} // namespace assetbridge
