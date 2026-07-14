#include "assetbridge/core/asset_converter.hpp"

#include "assetbridge/core/asset_inspector.hpp"
#include "assetbridge/core/conversion_serializer.hpp"
#include "assetbridge/core/runtime_capabilities.hpp"
#include "assetbridge/product/conversion_routes.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace assetbridge {
namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

std::filesystem::path normalized_absolute(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return error ? path.lexically_normal() : absolute.lexically_normal();
}

bool finite(double value) {
    return std::isfinite(value);
}

bool finite(const aiVector3D& value) {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

bool finite(const aiMatrix4x4& value) {
    return finite(value.a1) && finite(value.a2) && finite(value.a3) && finite(value.a4)
        && finite(value.b1) && finite(value.b2) && finite(value.b3) && finite(value.b4)
        && finite(value.c1) && finite(value.c2) && finite(value.c3) && finite(value.c4)
        && finite(value.d1) && finite(value.d2) && finite(value.d3) && finite(value.d4);
}

aiVector3D transform_point(const aiMatrix4x4& matrix, const aiVector3D& point) {
    return {
        matrix.a1 * point.x + matrix.a2 * point.y + matrix.a3 * point.z + matrix.a4,
        matrix.b1 * point.x + matrix.b2 * point.y + matrix.b3 * point.z + matrix.b4,
        matrix.c1 * point.x + matrix.c2 * point.y + matrix.c3 * point.z + matrix.c4
    };
}

struct SceneAnalysisResult {
    bool valid = false;
    std::string error;
    ConversionSceneAnalysis analysis;
};

void extend_bounds(BoundsValue& bounds, const aiVector3D& point) {
    if (!bounds.valid) {
        bounds.valid = true;
        bounds.minimum = { point.x, point.y, point.z };
        bounds.maximum = bounds.minimum;
        return;
    }
    bounds.minimum.x = std::min(bounds.minimum.x, static_cast<double>(point.x));
    bounds.minimum.y = std::min(bounds.minimum.y, static_cast<double>(point.y));
    bounds.minimum.z = std::min(bounds.minimum.z, static_cast<double>(point.z));
    bounds.maximum.x = std::max(bounds.maximum.x, static_cast<double>(point.x));
    bounds.maximum.y = std::max(bounds.maximum.y, static_cast<double>(point.y));
    bounds.maximum.z = std::max(bounds.maximum.z, static_cast<double>(point.z));
}

bool collect_node_bounds(
    const aiScene& scene,
    const aiNode* node,
    const aiMatrix4x4& parent_transform,
    BoundsValue& bounds,
    std::vector<BoundsValue>& mesh_bounds,
    std::vector<unsigned int>& mesh_reference_counts,
    std::string& error) {
    if (node == nullptr || !finite(node->mTransformation)) {
        error = "Scene contains a missing or non-finite node transform.";
        return false;
    }

    const aiMatrix4x4 world_transform = parent_transform * node->mTransformation;
    if (!finite(world_transform)) {
        error = "Scene contains a non-finite cumulative node transform.";
        return false;
    }

    for (unsigned int index = 0; index < node->mNumMeshes; ++index) {
        const unsigned int mesh_index = node->mMeshes[index];
        if (mesh_index >= scene.mNumMeshes || mesh_index >= mesh_bounds.size()
            || scene.mMeshes[mesh_index] == nullptr) {
            error = "Node references an invalid mesh index.";
            return false;
        }
        ++mesh_reference_counts[mesh_index];
        const aiMesh& mesh = *scene.mMeshes[mesh_index];
        for (unsigned int vertex_index = 0; vertex_index < mesh.mNumVertices; ++vertex_index) {
            const aiVector3D transformed = transform_point(world_transform, mesh.mVertices[vertex_index]);
            if (!finite(transformed)) {
                error = "Scene contains a non-finite transformed vertex.";
                return false;
            }
            extend_bounds(bounds, transformed);
            extend_bounds(mesh_bounds[mesh_index], transformed);
        }
    }

    for (unsigned int index = 0; index < node->mNumChildren; ++index) {
        if (!collect_node_bounds(
                scene,
                node->mChildren[index],
                world_transform,
                bounds,
                mesh_bounds,
                mesh_reference_counts,
                error)) {
            return false;
        }
    }
    return true;
}

std::optional<ColorValue> material_diffuse_color(
    const aiScene& scene,
    unsigned int index) {
    if (index >= scene.mNumMaterials || scene.mMaterials[index] == nullptr) {
        return std::nullopt;
    }

    aiColor4D color;
    const aiMaterial& material = *scene.mMaterials[index];
    if (material.Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS
        && material.Get(AI_MATKEY_COLOR_DIFFUSE, color) != AI_SUCCESS) {
        return std::nullopt;
    }
    if (!finite(color.r) || !finite(color.g) || !finite(color.b) || !finite(color.a)) {
        return std::nullopt;
    }
    return ColorValue { color.r, color.g, color.b, color.a };
}

std::optional<ColorValue> referenced_diffuse_color(
    const aiScene& scene,
    const std::set<unsigned int>& referenced_materials) {
    return referenced_materials.empty()
        ? std::nullopt
        : material_diffuse_color(scene, *referenced_materials.begin());
}

void finalize_bounds(BoundsValue& bounds) {
    bounds.center = {
        (bounds.minimum.x + bounds.maximum.x) * 0.5,
        (bounds.minimum.y + bounds.maximum.y) * 0.5,
        (bounds.minimum.z + bounds.maximum.z) * 0.5
    };
    bounds.size = {
        bounds.maximum.x - bounds.minimum.x,
        bounds.maximum.y - bounds.minimum.y,
        bounds.maximum.z - bounds.minimum.z
    };
}

SceneAnalysisResult analyze_scene_for_conversion(const aiScene* scene) {
    SceneAnalysisResult result;
    if (scene == nullptr || scene->mRootNode == nullptr || scene->mMeshes == nullptr
        || scene->mNumMeshes == 0 || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0) {
        result.error = "Scene is incomplete or has no meshes.";
        return result;
    }

    std::set<unsigned int> referenced_materials;
    result.analysis.mesh_count = scene->mNumMeshes;
    result.analysis.meshes.resize(scene->mNumMeshes);
    for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index) {
        const aiMesh* mesh = scene->mMeshes[mesh_index];
        if (mesh == nullptr || mesh->mNumVertices == 0 || mesh->mVertices == nullptr
            || mesh->mNumFaces == 0 || mesh->mFaces == nullptr) {
            result.error = "Scene contains an empty or missing mesh.";
            return result;
        }
        auto& mesh_analysis = result.analysis.meshes[mesh_index];
        mesh_analysis.name.assign(mesh->mName.C_Str(), mesh->mName.length);
        mesh_analysis.vertex_count = mesh->mNumVertices;
        mesh_analysis.has_normals = mesh->HasNormals();
        mesh_analysis.has_uv0 = mesh->HasTextureCoords(0);
        result.analysis.vertex_count += mesh->mNumVertices;
        result.analysis.has_normals = result.analysis.has_normals || mesh->HasNormals();
        result.analysis.has_uv0 = result.analysis.has_uv0 || mesh->HasTextureCoords(0);

        for (unsigned int vertex_index = 0; vertex_index < mesh->mNumVertices; ++vertex_index) {
            if (!finite(mesh->mVertices[vertex_index])) {
                result.error = "Mesh contains a NaN or infinite vertex position.";
                return result;
            }
            if (mesh->HasNormals() && !finite(mesh->mNormals[vertex_index])) {
                result.error = "Mesh contains a NaN or infinite normal.";
                return result;
            }
            if (mesh->HasTextureCoords(0) && !finite(mesh->mTextureCoords[0][vertex_index])) {
                result.error = "Mesh contains a NaN or infinite UV coordinate.";
                return result;
            }
        }

        for (unsigned int face_index = 0; face_index < mesh->mNumFaces; ++face_index) {
            const aiFace& face = mesh->mFaces[face_index];
            if (face.mNumIndices != 3 || face.mIndices == nullptr) {
                result.error = "Mesh contains a non-triangle or missing index buffer.";
                return result;
            }
            for (unsigned int index = 0; index < face.mNumIndices; ++index) {
                if (face.mIndices[index] >= mesh->mNumVertices) {
                    result.error = "Mesh contains an out-of-range vertex index.";
                    return result;
                }
            }
            ++result.analysis.triangle_count;
            ++mesh_analysis.triangle_count;
        }

        if (mesh->mMaterialIndex >= scene->mNumMaterials || scene->mMaterials == nullptr
            || scene->mMaterials[mesh->mMaterialIndex] == nullptr) {
            result.error = "Mesh references a missing material.";
            return result;
        }
        referenced_materials.insert(mesh->mMaterialIndex);
        mesh_analysis.diffuse_color = material_diffuse_color(*scene, mesh->mMaterialIndex);
    }

    aiMatrix4x4 identity;
    std::vector<BoundsValue> mesh_bounds(scene->mNumMeshes);
    std::vector<unsigned int> mesh_reference_counts(scene->mNumMeshes, 0);
    if (!collect_node_bounds(
            *scene,
            scene->mRootNode,
            identity,
            result.analysis.bounds,
            mesh_bounds,
            mesh_reference_counts,
            result.error)
        || !result.analysis.bounds.valid) {
        if (result.error.empty()) {
            result.error = "Could not calculate scene bounds.";
        }
        return result;
    }
    for (std::size_t index = 0; index < mesh_bounds.size(); ++index) {
        if (mesh_reference_counts[index] != 1 || !mesh_bounds[index].valid) {
            result.error = mesh_reference_counts[index] > 1
                ? "Scene contains mesh instancing, which is outside the verified route."
                : "Every non-empty mesh must be referenced by exactly one node.";
            return result;
        }
        finalize_bounds(mesh_bounds[index]);
        result.analysis.meshes[index].bounds = mesh_bounds[index];
    }
    finalize_bounds(result.analysis.bounds);
    result.analysis.referenced_material_count = referenced_materials.size();
    result.analysis.diffuse_color = referenced_diffuse_color(*scene, referenced_materials);
    result.valid = true;
    return result;
}

bool nearly_equal(double left, double right, double absolute, double relative) {
    return std::abs(left - right)
        <= absolute + relative * std::max(std::abs(left), std::abs(right));
}

bool vector_nearly_equal(const Vector3Value& left, const Vector3Value& right) {
    return nearly_equal(left.x, right.x, 1.0e-5, 1.0e-5)
        && nearly_equal(left.y, right.y, 1.0e-5, 1.0e-5)
        && nearly_equal(left.z, right.z, 1.0e-5, 1.0e-5);
}

bool color_nearly_equal(
    const std::optional<ColorValue>& source,
    const std::optional<ColorValue>& output) {
    if (!source.has_value()) {
        return true;
    }
    if (!output.has_value()) {
        return false;
    }
    return nearly_equal(source->red, output->red, 1.0e-4, 1.0e-4)
        && nearly_equal(source->green, output->green, 1.0e-4, 1.0e-4)
        && nearly_equal(source->blue, output->blue, 1.0e-4, 1.0e-4);
}

std::vector<ValidationCheck> validate_round_trip(
    const ConversionSceneAnalysis& source,
    const ConversionSceneAnalysis& output,
    const std::filesystem::path& output_file) {
    std::vector<ValidationCheck> checks;
    std::error_code error;
    const auto file_size = std::filesystem::file_size(output_file, error);
    checks.push_back({
        "output_file_nonempty",
        !error && file_size > 0,
        !error && file_size > 0 ? "Output GLB exists and is non-empty." : "Output GLB is missing or empty."
    });
    checks.push_back({ "reimport_scene_valid", true, "Assimp reimported a structurally valid scene." });
    checks.push_back({
        "mesh_indices_valid",
        true,
        "Every output mesh, triangle index, material reference, vertex, normal, UV, and transform is valid."
    });
    const bool mesh_count_matches = source.mesh_count > 0
        && source.mesh_count == output.mesh_count
        && source.meshes.size() == source.mesh_count
        && output.meshes.size() == output.mesh_count;
    checks.push_back({
        "mesh_count",
        mesh_count_matches,
        "Export-ready source meshes=" + std::to_string(source.mesh_count)
            + ", output meshes=" + std::to_string(output.mesh_count)
            + "; independent meshes must not be merged."
    });
    checks.push_back({
        "triangle_count",
        source.triangle_count == output.triangle_count,
        "Source triangles=" + std::to_string(source.triangle_count)
            + ", output triangles=" + std::to_string(output.triangle_count) + "."
    });
    checks.push_back({
        "all_faces_triangles",
        true,
        "The export-ready source and reimported GLB contain only valid three-index faces."
    });
    checks.push_back({
        "aabb_center",
        vector_nearly_equal(source.bounds.center, output.bounds.center),
        "AABB centers are compared with absolute and relative tolerance 1e-5."
    });
    checks.push_back({
        "aabb_size",
        vector_nearly_equal(source.bounds.size, output.bounds.size),
        "AABB sizes are compared with absolute and relative tolerance 1e-5."
    });
    const auto matching = match_conversion_meshes(source.meshes, output.meshes);
    checks.push_back({ "mesh_matching_strategy", matching.complete, matching.details });

    bool per_mesh_triangles = matching.complete;
    bool per_mesh_bounds = matching.complete;
    bool per_mesh_normals = matching.complete;
    bool per_mesh_uv0 = matching.complete;
    bool per_mesh_material = matching.complete;
    for (const auto& match : matching.matches) {
        if (match.source_index >= source.meshes.size()
            || match.output_index >= output.meshes.size()) {
            per_mesh_triangles = false;
            per_mesh_bounds = false;
            per_mesh_normals = false;
            per_mesh_uv0 = false;
            per_mesh_material = false;
            continue;
        }
        const auto& before = source.meshes[match.source_index];
        const auto& after = output.meshes[match.output_index];
        per_mesh_triangles = per_mesh_triangles
            && before.triangle_count == after.triangle_count;
        per_mesh_bounds = per_mesh_bounds
            && vector_nearly_equal(before.bounds.center, after.bounds.center)
            && vector_nearly_equal(before.bounds.size, after.bounds.size);
        per_mesh_normals = per_mesh_normals && before.has_normals == after.has_normals;
        per_mesh_uv0 = per_mesh_uv0 && before.has_uv0 == after.has_uv0;
        per_mesh_material = per_mesh_material
            && color_nearly_equal(before.diffuse_color, after.diffuse_color);
    }
    checks.push_back({
        "per_mesh_triangle_count",
        per_mesh_triangles,
        "Each matched mesh preserves its export-ready triangle count."
    });
    checks.push_back({
        "per_mesh_aabb",
        per_mesh_bounds,
        "Each matched mesh preserves its world-space AABB with tolerance 1e-5."
    });
    checks.push_back({
        "per_mesh_normals",
        per_mesh_normals,
        "Each matched mesh preserves normal-channel existence."
    });
    checks.push_back({
        "per_mesh_uv0",
        per_mesh_uv0,
        "Each matched mesh preserves UV0-channel existence."
    });
    checks.push_back({
        "per_mesh_material",
        per_mesh_material,
        "Each matched mesh preserves the currently verified diffuse material color."
    });
    checks.push_back({
        "normals_preserved",
        per_mesh_normals,
        "Normal existence is validated per matched mesh."
    });
    checks.push_back({
        "uv0_preserved",
        per_mesh_uv0,
        "UV0 existence is validated per matched mesh."
    });
    checks.push_back({
        "referenced_material_present",
        output.referenced_material_count > 0
            && output.referenced_material_count == source.referenced_material_count,
        "The output must preserve the verified referenced-material count."
    });

    checks.push_back({
        "diffuse_color",
        color_nearly_equal(source.diffuse_color, output.diffuse_color),
        source.diffuse_color.has_value()
            ? "MTL diffuse RGB is compared after mapping with tolerance 1e-4."
            : "Source material has no diffuse color to compare."
    });
    return checks;
}

bool all_checks_pass(const std::vector<ValidationCheck>& checks) {
    return std::all_of(checks.begin(), checks.end(), [](const ValidationCheck& check) {
        return check.passed;
    });
}

bool contains_route_feature_block(const PreflightReport& preflight) {
    return std::any_of(
        preflight.decision.losses.begin(),
        preflight.decision.losses.end(),
        [](const LossItem& loss) { return loss.code == "route_feature_unverified"; });
}

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::filesystem::path path)
        : path_(std::move(path)) {}

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        if (!committed_) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    void mark_committed() noexcept { committed_ = true; }

private:
    std::filesystem::path path_;
    bool committed_ = false;
};

std::optional<std::filesystem::path> create_temporary_directory(
    const std::filesystem::path& root,
    std::string& error_message) {
    static std::atomic_uint64_t counter = 0;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto id = ++counter;
        const auto candidate = root / (".assetbridge-tmp-" + std::to_string(id));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) {
            return candidate;
        }
        if (error) {
            error_message = "Could not create temporary directory: " + error.message();
            return std::nullopt;
        }
    }
    error_message = "Could not allocate a unique temporary directory.";
    return std::nullopt;
}

std::optional<std::filesystem::path> choose_final_directory(
    const std::filesystem::path& root,
    const std::filesystem::path& stem,
    std::string& error_message) {
    for (std::uint64_t suffix = 1; ; ++suffix) {
        auto name = stem;
        if (suffix > 1) {
            name += std::filesystem::path("_" + std::to_string(suffix));
        }
        const auto candidate = root / name;
        std::error_code error;
        const bool exists = std::filesystem::exists(candidate, error);
        if (error) {
            error_message = "Could not check output directory availability: " + error.message();
            return std::nullopt;
        }
        if (!exists) {
            return candidate;
        }
    }
}

void set_error(
    ConversionReport& report,
    ConversionErrorCode code,
    std::string message,
    Clock::time_point total_start) {
    report.error_code = code;
    report.error_message = std::move(message);
    report.output_directory.reset();
    report.output_files.clear();
    report.timings.total_ms = elapsed_ms(total_start);
}

} // namespace

MeshMatchingResult match_conversion_meshes(
    const std::vector<ConversionMeshAnalysis>& source,
    const std::vector<ConversionMeshAnalysis>& output) {
    MeshMatchingResult result;
    if (source.size() != output.size()) {
        result.details = "Cannot match meshes one-to-one: source count="
            + std::to_string(source.size()) + ", output count="
            + std::to_string(output.size()) + ".";
        return result;
    }

    std::vector<bool> output_used(output.size(), false);
    std::size_t name_matches = 0;
    for (std::size_t source_index = 0; source_index < source.size(); ++source_index) {
        const auto& source_mesh = source[source_index];
        std::vector<std::size_t> candidates;
        for (std::size_t output_index = 0; output_index < output.size(); ++output_index) {
            if (output_used[output_index]) {
                continue;
            }
            const auto& output_mesh = output[output_index];
            if (source_mesh.bounds.valid && output_mesh.bounds.valid
                && source_mesh.triangle_count == output_mesh.triangle_count
                && vector_nearly_equal(source_mesh.bounds.center, output_mesh.bounds.center)
                && vector_nearly_equal(source_mesh.bounds.size, output_mesh.bounds.size)) {
                candidates.push_back(output_index);
            }
        }
        if (candidates.empty()) {
            result.details = "No unmatched output mesh has the same triangle count and AABB as source mesh "
                + std::to_string(source_index) + ".";
            return result;
        }

        auto selected = candidates.front();
        std::string method = "geometry_signature_then_lowest_unmatched_output_ordinal";
        if (!source_mesh.name.empty()) {
            const auto name_match = std::find_if(
                candidates.begin(),
                candidates.end(),
                [&source_mesh, &output](std::size_t output_index) {
                    return output[output_index].name == source_mesh.name;
                });
            if (name_match != candidates.end()) {
                selected = *name_match;
                method = "exact_nonempty_name_and_geometry_signature";
                ++name_matches;
            }
        }
        output_used[selected] = true;
        result.matches.push_back({ source_index, selected, std::move(method) });
    }

    result.complete = result.matches.size() == source.size();
    result.details = "Matched " + std::to_string(result.matches.size())
        + " meshes one-to-one by triangle count and AABB; exact non-empty names were preferred for "
        + std::to_string(name_matches)
        + " matches, with the lowest unmatched output ordinal as the stable tie-break for duplicate or empty names."
        + " Vertex counts are diagnostic only.";
    return result;
}

std::string_view to_string(ConversionErrorCode code) noexcept {
    switch (code) {
    case ConversionErrorCode::none: return "none";
    case ConversionErrorCode::file_not_found: return "file_not_found";
    case ConversionErrorCode::unsupported_source_format: return "unsupported_source_format";
    case ConversionErrorCode::route_not_enabled: return "route_not_enabled";
    case ConversionErrorCode::route_feature_unverified: return "route_feature_unverified";
    case ConversionErrorCode::output_root_error: return "output_root_error";
    case ConversionErrorCode::import_failed: return "import_failed";
    case ConversionErrorCode::export_failed: return "export_failed";
    case ConversionErrorCode::reimport_failed: return "reimport_failed";
    case ConversionErrorCode::validation_failed: return "validation_failed";
    case ConversionErrorCode::report_write_failed: return "report_write_failed";
    case ConversionErrorCode::commit_failed: return "commit_failed";
    }
    return "unknown";
}

ConversionReport AssetConverter::convert(
    const std::filesystem::path& input,
    FormatId target,
    const std::filesystem::path& output_root,
    const ConversionProgressCallback& progress) const {
    const auto total_start = Clock::now();
    ConversionReport report;
    report.source_path = normalized_absolute(input);
    report.target_format = target;
    report.assimp_version = query_runtime_capabilities().assimp_version;

    const auto extension = input.extension().u8string();
    const std::string extension_utf8(
        reinterpret_cast<const char*>(extension.data()), extension.size());
    report.source_format = parse_format_id(extension_utf8);
    if (report.source_format != FormatId::obj) {
        set_error(
            report,
            ConversionErrorCode::unsupported_source_format,
            "The verified conversion product accepts OBJ input only.",
            total_start);
        return report;
    }

    const auto& route = conversion_route(*report.source_format, target);
    report.route_product_enabled = route.product_enabled;
    report.route_verified = route.verified;
    if (!route.product_enabled || !route.verified) {
        set_error(
            report,
            ConversionErrorCode::route_not_enabled,
            "The requested conversion route is not enabled and verified.",
            total_start);
        return report;
    }

    if (progress) progress(ConversionStage::preflight);
    const auto preflight_start = Clock::now();
    report.preflight = create_preflight_report(input, target);
    const AssetInspector inspector;
    const auto inspection = inspector.inspect(input);
    report.timings.preflight_ms = elapsed_ms(preflight_start);
    if (!*report.preflight || !inspection) {
        const auto code = inspection.error_code == InspectionErrorCode::file_not_found
            ? ConversionErrorCode::file_not_found
            : ConversionErrorCode::import_failed;
        set_error(report, code, inspection.error_message, total_start);
        return report;
    }
    if (report.preflight->decision.overall_result != OverallResult::safe) {
        const bool feature_block = contains_route_feature_block(*report.preflight);
        set_error(
            report,
            feature_block
                ? ConversionErrorCode::route_feature_unverified
                : ConversionErrorCode::route_not_enabled,
            feature_block
                ? "The source contains a feature outside the verified OBJ to GLB2 boundary."
                : "Conversion preflight blocked the requested route.",
            total_start);
        return report;
    }
    if (!inspection.summary.has_value()) {
        set_error(
            report,
            ConversionErrorCode::import_failed,
            "The inspected OBJ did not provide source geometry diagnostics.",
            total_start);
        return report;
    }
    report.triangulation = TriangulationDiagnostics {
        inspection.summary->face_count,
        inspection.summary->triangle_count,
        inspection.summary->face_count >= inspection.summary->triangle_count
            ? inspection.summary->face_count - inspection.summary->triangle_count
            : 0,
        0
    };

    const auto stem = input.filename().stem();
    if (stem.empty() || stem == "." || stem == ".." || stem.has_parent_path()) {
        set_error(
            report,
            ConversionErrorCode::output_root_error,
            "The source file name cannot form a safe output directory.",
            total_start);
        return report;
    }

    std::error_code filesystem_error;
    std::filesystem::create_directories(output_root, filesystem_error);
    if (filesystem_error || !std::filesystem::is_directory(output_root, filesystem_error)) {
        set_error(
            report,
            ConversionErrorCode::output_root_error,
            "Could not create or access output root: " + filesystem_error.message(),
            total_start);
        return report;
    }
    const auto normalized_root = normalized_absolute(output_root);
    std::string final_directory_error;
    const auto final_directory = choose_final_directory(
        normalized_root,
        stem,
        final_directory_error);
    if (!final_directory.has_value()) {
        set_error(
            report,
            ConversionErrorCode::output_root_error,
            std::move(final_directory_error),
            total_start);
        return report;
    }
    if (final_directory->parent_path() != normalized_root) {
        set_error(
            report,
            ConversionErrorCode::output_root_error,
            "Output directory escaped the requested output root.",
            total_start);
        return report;
    }

    std::string temporary_error;
    const auto temporary_path = create_temporary_directory(normalized_root, temporary_error);
    if (!temporary_path.has_value()) {
        set_error(
            report,
            ConversionErrorCode::output_root_error,
            std::move(temporary_error),
            total_start);
        return report;
    }
    TemporaryDirectory temporary(*temporary_path);
    const auto temporary_glb = temporary.path() / (stem.native() + L".glb");

    report.processing_steps = {
        "aiProcess_Triangulate",
        "aiProcess_ValidateDataStructure"
    };
    if (progress) progress(ConversionStage::importing);
    const auto import_start = Clock::now();
    Assimp::Importer source_importer;
    const aiScene* source_scene = source_importer.ReadFile(
        path_to_utf8(input),
        aiProcess_Triangulate | aiProcess_ValidateDataStructure);
    report.timings.import_ms = elapsed_ms(import_start);
    if (source_scene == nullptr) {
        set_error(
            report,
            ConversionErrorCode::import_failed,
            "Assimp source import failed: " + std::string(source_importer.GetErrorString()),
            total_start);
        return report;
    }
    const auto source_analysis = analyze_scene_for_conversion(source_scene);
    if (!source_analysis.valid) {
        set_error(
            report,
            ConversionErrorCode::import_failed,
            "Source scene validation failed: " + source_analysis.error,
            total_start);
        return report;
    }
    report.source_analysis = source_analysis.analysis;
    report.triangulation->export_ready_triangle_count = source_analysis.analysis.triangle_count;
    if (source_analysis.analysis.mesh_count != inspection.summary->mesh_count) {
        set_error(
            report,
            ConversionErrorCode::validation_failed,
            "Export preparation changed the number of non-empty meshes.",
            total_start);
        return report;
    }

    if (progress) progress(ConversionStage::exporting);
    const auto export_start = Clock::now();
    Assimp::Exporter exporter;
    const aiReturn export_result = exporter.Export(
        source_scene,
        "glb2",
        path_to_utf8(temporary_glb));
    report.timings.export_ms = elapsed_ms(export_start);
    if (export_result != AI_SUCCESS) {
        set_error(
            report,
            ConversionErrorCode::export_failed,
            "Assimp GLB2 export failed: " + std::string(exporter.GetErrorString()),
            total_start);
        return report;
    }

    if (progress) progress(ConversionStage::reimporting);
    const auto reimport_start = Clock::now();
    Assimp::Importer output_importer;
    const aiScene* output_scene = output_importer.ReadFile(
        path_to_utf8(temporary_glb),
        aiProcess_ValidateDataStructure);
    report.timings.reimport_ms = elapsed_ms(reimport_start);
    if (output_scene == nullptr) {
        set_error(
            report,
            ConversionErrorCode::reimport_failed,
            "Assimp could not reimport the generated GLB: "
                + std::string(output_importer.GetErrorString()),
            total_start);
        return report;
    }

    if (progress) progress(ConversionStage::validating);
    const auto validation_start = Clock::now();
    const auto output_analysis = analyze_scene_for_conversion(output_scene);
    if (!output_analysis.valid) {
        report.timings.validation_ms = elapsed_ms(validation_start);
        set_error(
            report,
            ConversionErrorCode::validation_failed,
            "Generated GLB validation failed: " + output_analysis.error,
            total_start);
        return report;
    }
    report.output_analysis = output_analysis.analysis;
    report.validation_checks = validate_round_trip(
        *report.source_analysis,
        *report.output_analysis,
        temporary_glb);
    report.timings.validation_ms = elapsed_ms(validation_start);
    if (!all_checks_pass(report.validation_checks)) {
        set_error(
            report,
            ConversionErrorCode::validation_failed,
            "One or more GLB round-trip validation checks failed.",
            total_start);
        return report;
    }

    const auto final_glb = *final_directory / (stem.native() + L".glb");
    const auto final_report = *final_directory / "conversion-report.json";
    report.output_directory = *final_directory;
    report.output_files = { final_glb, final_report };
    report.timings.total_ms = elapsed_ms(total_start);

    const auto temporary_report = temporary.path() / "conversion-report.json";
    std::ofstream report_file(temporary_report, std::ios::binary);
    if (!report_file) {
        set_error(
            report,
            ConversionErrorCode::report_write_failed,
            "Could not create conversion-report.json.",
            total_start);
        return report;
    }
    report_file << conversion_report_to_json(report) << '\n';
    report_file.close();
    if (!report_file) {
        set_error(
            report,
            ConversionErrorCode::report_write_failed,
            "Could not finish writing conversion-report.json.",
            total_start);
        return report;
    }

    if (progress) progress(ConversionStage::committing);
    std::filesystem::rename(temporary.path(), *final_directory, filesystem_error);
    if (filesystem_error) {
        set_error(
            report,
            ConversionErrorCode::commit_failed,
            "Could not commit the validated conversion: " + filesystem_error.message(),
            total_start);
        return report;
    }
    temporary.mark_committed();
    return report;
}

} // namespace assetbridge
