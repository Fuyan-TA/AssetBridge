#pragma once

#include "assetbridge/core/preflight_report.hpp"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace assetbridge {

enum class ConversionErrorCode {
    none,
    file_not_found,
    unsupported_source_format,
    route_not_enabled,
    route_feature_unverified,
    companion_resolution_failed,
    output_root_error,
    import_failed,
    texture_embedding_failed,
    export_failed,
    reimport_failed,
    validation_failed,
    report_write_failed,
    commit_failed
};

enum class ConversionStage {
    preflight,
    importing,
    embedding_textures,
    exporting,
    validating_textures,
    reimporting,
    validating,
    committing
};

using ConversionProgressCallback = std::function<void(ConversionStage)>;

[[nodiscard]] std::string_view to_string(ConversionErrorCode code) noexcept;

struct Vector3Value {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct BoundsValue {
    bool valid = false;
    Vector3Value minimum;
    Vector3Value maximum;
    Vector3Value center;
    Vector3Value size;
};

struct ColorValue {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double alpha = 1.0;
};

struct ConversionMeshAnalysis {
    std::string name;
    std::uint64_t vertex_count = 0;
    std::uint64_t triangle_count = 0;
    bool has_normals = false;
    bool has_uv0 = false;
    BoundsValue bounds;
    std::optional<ColorValue> diffuse_color;
};

struct ConversionSceneAnalysis {
    std::uint64_t mesh_count = 0;
    std::uint64_t vertex_count = 0;
    std::uint64_t triangle_count = 0;
    std::uint64_t referenced_material_count = 0;
    bool has_normals = false;
    bool has_uv0 = false;
    BoundsValue bounds;
    std::optional<ColorValue> diffuse_color;
    // Kept in scene mesh order for diagnostics only. Round-trip validation
    // uses match_conversion_meshes and never assumes source/output order.
    std::vector<ConversionMeshAnalysis> meshes;
};

struct MeshMatch {
    std::size_t source_index = 0;
    std::size_t output_index = 0;
    std::string method;
};

struct MeshMatchingResult {
    bool complete = false;
    std::vector<MeshMatch> matches;
    std::string details;
};

[[nodiscard]] MeshMatchingResult match_conversion_meshes(
    const std::vector<ConversionMeshAnalysis>& source,
    const std::vector<ConversionMeshAnalysis>& output);

struct TriangulationDiagnostics {
    std::uint64_t source_face_count = 0;
    std::uint64_t source_triangle_face_count = 0;
    std::uint64_t source_non_triangle_face_count = 0;
    std::uint64_t export_ready_triangle_count = 0;
};

struct ValidationCheck {
    std::string name;
    bool passed = false;
    std::string details;
};

struct ConversionTimings {
    double preflight_ms = 0.0;
    double import_ms = 0.0;
    double export_ms = 0.0;
    double reimport_ms = 0.0;
    double validation_ms = 0.0;
    double total_ms = 0.0;
};

struct ConversionReport {
    std::filesystem::path source_path;
    std::optional<FormatId> source_format;
    FormatId target_format = FormatId::glb2;
    bool route_product_enabled = false;
    bool route_verified = false;
    std::optional<std::filesystem::path> output_directory;
    std::vector<std::filesystem::path> output_files;
    std::optional<ConversionSceneAnalysis> source_analysis;
    std::optional<TriangulationDiagnostics> triangulation;
    std::optional<PreflightReport> preflight;
    std::vector<std::string> processing_steps;
    std::optional<ConversionSceneAnalysis> output_analysis;
    std::vector<ValidationCheck> validation_checks;
    std::vector<std::string> warnings;
    ConversionErrorCode error_code = ConversionErrorCode::none;
    std::string error_message;
    std::string assimp_version;
    ConversionTimings timings;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error_code == ConversionErrorCode::none
            && output_directory.has_value();
    }
};

class AssetConverter {
public:
    [[nodiscard]] ConversionReport convert(
        const std::filesystem::path& input,
        FormatId target,
        const std::filesystem::path& output_root,
        const ConversionProgressCallback& progress = {}) const;
};

} // namespace assetbridge
