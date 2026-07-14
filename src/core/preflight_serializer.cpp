#include "assetbridge/core/preflight_serializer.hpp"

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

nlohmann::json features_to_json(const AssetFeatures& features) {
    nlohmann::json animations = nlohmann::json::array();
    for (const auto& animation : features.animations) {
        animations.push_back({
            { "name", animation.name },
            { "duration_seconds", animation.duration_seconds.has_value()
                ? nlohmann::json(*animation.duration_seconds)
                : nlohmann::json(nullptr) },
            { "duration_ticks", animation.duration_ticks.has_value()
                ? nlohmann::json(*animation.duration_ticks)
                : nlohmann::json(nullptr) },
            { "ticks_per_second", animation.ticks_per_second.has_value()
                ? nlohmann::json(*animation.ticks_per_second)
                : nlohmann::json(nullptr) }
        });
    }

    return {
        { "mesh_count", features.mesh_count },
        { "has_node_hierarchy", features.has_node_hierarchy },
        { "meshes_with_normals", features.meshes_with_normals },
        { "meshes_with_tangents", features.meshes_with_tangents },
        { "max_uv_channel_count", features.max_uv_channel_count },
        { "meshes_with_vertex_colors", features.meshes_with_vertex_colors },
        { "referenced_material_count", features.referenced_material_count },
        { "has_pbr_materials", features.has_pbr_materials },
        { "external_texture_references", features.external_texture_references },
        { "embedded_texture_count", features.embedded_texture_count },
        { "bone_count", features.bone_count },
        { "skinned_mesh_count", features.skinned_mesh_count },
        { "max_weights_per_vertex", features.max_weights_per_vertex },
        { "animation_count", features.animations.size() },
        { "animations", std::move(animations) },
        { "morph_target_count", features.morph_target_names.size() },
        { "morph_target_names", features.morph_target_names }
    };
}

nlohmann::json assessments_to_json(const PreflightDecision& decision) {
    nlohmann::json assessments = nlohmann::json::array();
    for (const auto& assessment : decision.assessments) {
        assessments.push_back({
            { "feature", to_string(assessment.feature) },
            { "support", to_string(assessment.support) },
            { "reason", assessment.reason }
        });
    }
    return assessments;
}

nlohmann::json losses_to_json(const PreflightDecision& decision) {
    nlohmann::json losses = nlohmann::json::array();
    for (const auto& loss : decision.losses) {
        losses.push_back({
            { "code", loss.code },
            { "feature", loss.feature.has_value()
                ? nlohmann::json(to_string(*loss.feature))
                : nlohmann::json(nullptr) },
            { "severity", to_string(loss.severity) },
            { "overrideable", loss.overrideable },
            { "reason", loss.reason }
        });
    }
    return losses;
}

std::string source_format_text(const SourceFormatStatus& source) {
    return source.format.has_value()
        ? std::string(to_string(*source.format))
        : "unknown";
}

nlohmann::json unknown_target_loss(std::string_view requested_target) {
    return {
        { "code", "unknown_target_format" },
        { "feature", nullptr },
        { "severity", "blocking" },
        { "overrideable", false },
        { "reason", "Unknown target format: " + std::string(requested_target) }
    };
}

} // namespace

std::string preflight_to_text(const PreflightReport& report) {
    std::ostringstream output;
    output
        << "Source File: " << path_to_generic_utf8(report.file_path) << '\n'
        << "Source Format: " << source_format_text(report.source) << '\n'
        << "Source Product Enabled: " << (report.source.product_enabled ? "true" : "false") << '\n'
        << "Source Verified: " << (report.source.verified ? "true" : "false") << '\n'
        << "Target Format: " << to_string(report.decision.target) << '\n'
        << "Runtime Exporter Available: "
        << (report.decision.runtime_exporter_available ? "true" : "false") << '\n'
        << "Product Enabled: " << (report.decision.product_enabled ? "true" : "false") << '\n'
        << "Verified: " << (report.decision.verified ? "true" : "false") << '\n';

    if (report.features.has_value()) {
        const auto& features = *report.features;
        output
            << "Features:\n"
            << "- mesh_count: " << features.mesh_count << '\n'
            << "- has_node_hierarchy: " << (features.has_node_hierarchy ? "true" : "false") << '\n'
            << "- meshes_with_normals: " << features.meshes_with_normals << '\n'
            << "- meshes_with_tangents: " << features.meshes_with_tangents << '\n'
            << "- max_uv_channel_count: " << features.max_uv_channel_count << '\n'
            << "- meshes_with_vertex_colors: " << features.meshes_with_vertex_colors << '\n'
            << "- referenced_material_count: " << features.referenced_material_count << '\n'
            << "- external_texture_count: " << features.external_texture_references.size() << '\n'
            << "- embedded_texture_count: " << features.embedded_texture_count << '\n'
            << "- bone_count: " << features.bone_count << '\n'
            << "- skinned_mesh_count: " << features.skinned_mesh_count << '\n'
            << "- max_weights_per_vertex: " << features.max_weights_per_vertex << '\n'
            << "- animation_count: " << features.animations.size() << '\n'
            << "- morph_target_count: " << features.morph_target_names.size() << '\n';

        for (const auto& animation : features.animations) {
            output << "  animation: " << animation.name << " | duration_seconds=";
            if (animation.duration_seconds.has_value()) {
                output << *animation.duration_seconds;
            } else {
                output << "unknown";
            }
            output << " | duration_ticks=";
            if (animation.duration_ticks.has_value()) {
                output << *animation.duration_ticks;
            } else {
                output << "unknown";
            }
            output << " | ticks_per_second=";
            if (animation.ticks_per_second.has_value()) {
                output << *animation.ticks_per_second;
            } else {
                output << "unknown";
            }
            output << '\n';
        }
    }

    output << "Capability Assessments:\n";
    for (const auto& assessment : report.decision.assessments) {
        output
            << "- " << to_string(assessment.feature)
            << ": " << to_string(assessment.support)
            << " | " << assessment.reason << '\n';
    }

    output << "Expected Losses:\n";
    if (report.decision.losses.empty()) {
        output << "- none\n";
    } else {
        for (const auto& loss : report.decision.losses) {
            output
                << "- " << loss.code
                << " | severity=" << to_string(loss.severity)
                << " | overrideable=" << (loss.overrideable ? "true" : "false")
                << " | " << loss.reason << '\n';
        }
    }

    output
        << "Compatibility Result: " << to_string(report.decision.compatibility_result) << '\n'
        << "Overall Result: " << to_string(report.decision.overall_result) << '\n';

    if (!report) {
        output
            << "Error [" << to_string(report.error_code) << "]: "
            << report.error_message << '\n';
    }
    return output.str();
}

std::string preflight_to_json(const PreflightReport& report) {
    const auto extension = report.file_path.extension();
    nlohmann::json result = {
        { "schema", preflight_schema },
        { "status", report ? "success" : "error" },
        { "source", {
            { "file", {
                { "path", path_to_generic_utf8(report.file_path) },
                { "extension", path_to_generic_utf8(extension) }
            } },
            { "format_id", report.source.format.has_value()
                ? nlohmann::json(to_string(*report.source.format))
                : nlohmann::json(nullptr) },
            { "product_enabled", report.source.product_enabled },
            { "verified", report.source.verified },
            { "valid", static_cast<bool>(report) }
        } },
        { "target", {
            { "format_id", to_string(report.decision.target) },
            { "runtime_exporter_available", report.decision.runtime_exporter_available },
            { "product_enabled", report.decision.product_enabled },
            { "verified", report.decision.verified }
        } },
        { "features", report.features.has_value()
            ? features_to_json(*report.features)
            : nlohmann::json(nullptr) },
        { "assessments", assessments_to_json(report.decision) },
        { "losses", losses_to_json(report.decision) },
        { "compatibility_result", to_string(report.decision.compatibility_result) },
        { "product_enabled", report.decision.product_enabled },
        { "verified", report.decision.verified },
        { "overall_result", to_string(report.decision.overall_result) },
        { "error", report ? nlohmann::json(nullptr) : nlohmann::json({
            { "code", to_string(report.error_code) },
            { "message", report.error_message }
        }) }
    };
    return result.dump(2);
}

std::string unknown_target_to_text(std::string_view requested_target) {
    std::ostringstream output;
    output
        << "Target Format: unknown\n"
        << "Product Enabled: false\n"
        << "Verified: false\n"
        << "Expected Losses:\n"
        << "- unknown_target_format | severity=blocking | overrideable=false | "
        << "Unknown target format: " << requested_target << '\n'
        << "Compatibility Result: blocked\n"
        << "Overall Result: blocked\n";
    return output.str();
}

std::string unknown_target_to_json(std::string_view requested_target) {
    nlohmann::json result = {
        { "schema", preflight_schema },
        { "status", "error" },
        { "source", nullptr },
        { "target", nullptr },
        { "features", nullptr },
        { "assessments", nlohmann::json::array() },
        { "losses", nlohmann::json::array({ unknown_target_loss(requested_target) }) },
        { "compatibility_result", "blocked" },
        { "product_enabled", false },
        { "verified", false },
        { "overall_result", "blocked" },
        { "error", {
            { "code", "unknown_target_format" },
            { "message", "Unknown target format: " + std::string(requested_target) }
        } }
    };
    return result.dump(2);
}

} // namespace assetbridge
