#include "assetbridge/core/asset_converter.hpp"
#include "assetbridge/core/asset_inspector.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int require(bool condition, std::string_view message) {
    if (condition) {
        return 0;
    }
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

bool no_temporary_directories(const std::filesystem::path& root) {
    std::error_code error;
    if (!std::filesystem::exists(root, error)) {
        return true;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        const auto name = entry.path().filename().string();
        if (name.starts_with(".assetbridge-tmp-")) {
            return false;
        }
    }
    return !error;
}

bool all_checks_pass(const assetbridge::ConversionReport& report) {
    if (report.validation_checks.empty()) {
        return false;
    }
    for (const auto& check : report.validation_checks) {
        if (!check.passed) {
            return false;
        }
    }
    return true;
}

bool has_passed_check(
    const assetbridge::ConversionReport& report,
    std::string_view name) {
    return std::any_of(
        report.validation_checks.begin(),
        report.validation_checks.end(),
        [name](const assetbridge::ValidationCheck& check) {
            return check.name == name && check.passed;
        });
}

assetbridge::ConversionMeshAnalysis test_mesh(
    std::string name,
    double center_x) {
    assetbridge::ConversionMeshAnalysis mesh;
    mesh.name = std::move(name);
    mesh.vertex_count = 3;
    mesh.triangle_count = 1;
    mesh.bounds.valid = true;
    mesh.bounds.center = { center_x, 0.5, 0.0 };
    mesh.bounds.size = { 1.0, 1.0, 0.0 };
    return mesh;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: assetbridge-conversion-tests <source-root> <output-root>\n";
        return 2;
    }

    using namespace assetbridge;
    const std::filesystem::path source_root(argv[1]);
    const std::filesystem::path output_root(argv[2]);
    std::error_code error;
    std::filesystem::remove_all(output_root, error);
    int failures = 0;
    const AssetConverter converter;

    const std::vector<ConversionMeshAnalysis> reordered_source {
        test_mesh("DuplicateName", 0.5),
        test_mesh("", 2.5)
    };
    const std::vector<ConversionMeshAnalysis> reordered_output {
        test_mesh("", 2.5),
        test_mesh("DuplicateName", 0.5)
    };
    const auto reordered_match = match_conversion_meshes(
        reordered_source,
        reordered_output);
    failures += require(
        reordered_match.complete && reordered_match.matches.size() == 2
            && reordered_match.matches[0].output_index == 1
            && reordered_match.matches[1].output_index == 0,
        "mesh matching must be independent of output array order");

    const std::vector<ConversionMeshAnalysis> duplicate_source {
        test_mesh("", 0.5),
        test_mesh("", 0.5)
    };
    const auto duplicate_match = match_conversion_meshes(
        duplicate_source,
        duplicate_source);
    failures += require(
        duplicate_match.complete && duplicate_match.matches.size() == 2
            && duplicate_match.matches[0].output_index == 0
            && duplicate_match.matches[1].output_index == 1,
        "duplicate and empty mesh names must use a stable output-ordinal tie-break");

    std::vector<ConversionStage> progress_stages;
    const auto ascii = converter.convert(
        source_root / "minimal_triangle.obj",
        FormatId::glb2,
        output_root,
        [&progress_stages](ConversionStage stage) { progress_stages.push_back(stage); });
    failures += require(static_cast<bool>(ascii), "ASCII OBJ to GLB conversion should succeed");
    failures += require(
        ascii.output_directory.has_value()
            && ascii.output_directory->filename() == "minimal_triangle",
        "first output directory should preserve the source stem");
    failures += require(all_checks_pass(ascii), "all ASCII round-trip checks should pass");
    failures += require(
        std::find(progress_stages.begin(), progress_stages.end(), ConversionStage::exporting)
                != progress_stages.end()
            && std::find(progress_stages.begin(), progress_stages.end(), ConversionStage::validating)
                != progress_stages.end(),
        "conversion progress should report real export and validation stages");
    failures += require(
        ascii.source_analysis.has_value() && ascii.output_analysis.has_value(),
        "successful conversion should include both analyses");
    if (ascii.source_analysis && ascii.output_analysis) {
        failures += require(
            ascii.source_analysis->triangle_count == 1
                && ascii.output_analysis->triangle_count == 1,
            "triangle count should survive the round trip");
        failures += require(
            ascii.output_analysis->has_normals && ascii.output_analysis->has_uv0,
            "normals and UV0 should survive the round trip");
        failures += require(
            ascii.output_analysis->diffuse_color.has_value()
                && std::abs(ascii.output_analysis->diffuse_color->red - 0.8) < 1.0e-4,
            "MTL diffuse color should survive the round trip");
    }
    if (ascii.output_directory) {
        const auto glb = *ascii.output_directory / "minimal_triangle.glb";
        failures += require(
            std::filesystem::is_regular_file(glb)
                && std::filesystem::file_size(glb) > 0,
            "generated ASCII GLB should exist and be non-empty");
        const AssetInspector inspector;
        failures += require(
            static_cast<bool>(inspector.inspect(glb)),
            "generated ASCII GLB should be inspectable by Assimp");
        failures += require(
            std::filesystem::is_regular_file(
                *ascii.output_directory / "conversion-report.json"),
            "successful conversion should commit its JSON report");
    }

    const auto conflict = converter.convert(
        source_root / "minimal_triangle.obj",
        FormatId::glb2,
        output_root);
    failures += require(
        conflict && conflict.output_directory.has_value()
            && conflict.output_directory->filename() == "minimal_triangle_2",
        "existing output should cause a _2 directory");

    const auto unicode = converter.convert(
        source_root / L"中文三角形.obj",
        FormatId::glb2,
        output_root);
    failures += require(static_cast<bool>(unicode), "Unicode OBJ and MTL conversion should succeed");
    if (unicode.output_directory) {
        const auto unicode_glb = *unicode.output_directory / L"中文三角形.glb";
        failures += require(
            std::filesystem::is_regular_file(unicode_glb)
                && std::filesystem::file_size(unicode_glb) > 0,
            "Unicode GLB path should exist and be non-empty");
    }

    const auto multi = converter.convert(
        source_root / "multi_two_triangles.obj",
        FormatId::glb2,
        output_root);
    failures += require(static_cast<bool>(multi), "two independent OBJ meshes should convert");
    failures += require(all_checks_pass(multi), "multi-mesh round-trip checks should pass");
    if (multi.source_analysis && multi.output_analysis) {
        failures += require(
            multi.source_analysis->mesh_count == 2
                && multi.output_analysis->mesh_count == 2,
            "two independent source meshes must remain two output meshes");
        failures += require(
            multi.source_analysis->triangle_count == 2
                && multi.output_analysis->triangle_count == 2,
            "two-triangle multi-mesh totals should survive round trip");
        failures += require(
            multi.output_analysis->diffuse_color.has_value()
                && std::abs(multi.output_analysis->diffuse_color->blue - 0.9) < 1.0e-4,
            "shared diffuse material should survive multi-mesh conversion");
    }
    failures += require(
        has_passed_check(multi, "mesh_count")
            && has_passed_check(multi, "mesh_matching_strategy")
            && has_passed_check(multi, "per_mesh_aabb")
            && has_passed_check(multi, "per_mesh_triangle_count"),
        "multi-mesh conversion should report strict per-mesh validation");

    const auto quad = converter.convert(
        source_root / "multi_triangle_quad.obj",
        FormatId::glb2,
        output_root);
    failures += require(static_cast<bool>(quad), "triangle plus quad OBJ should convert after preparation");
    failures += require(all_checks_pass(quad), "triangulated multi-mesh checks should pass");
    if (quad.triangulation && quad.source_analysis && quad.output_analysis) {
        failures += require(
            quad.triangulation->source_face_count == 2
                && quad.triangulation->source_triangle_face_count == 1
                && quad.triangulation->source_non_triangle_face_count == 1
                && quad.triangulation->export_ready_triangle_count == 3,
            "triangulation diagnostics should distinguish source faces from export-ready triangles");
        failures += require(
            quad.source_analysis->mesh_count == 2
                && quad.output_analysis->mesh_count == 2
                && quad.source_analysis->triangle_count == 3
                && quad.output_analysis->triangle_count == 3,
            "triangle plus quad should become three triangles without merging meshes");
    }

    const auto unicode_multi = converter.convert(
        source_root / L"中文多网格.obj",
        FormatId::glb2,
        output_root);
    failures += require(
        unicode_multi && unicode_multi.output_analysis
            && unicode_multi.output_analysis->mesh_count == 2,
        "Unicode OBJ/MTL multi-mesh conversion should preserve two meshes");

    const auto empty_mesh = converter.convert(
        source_root / "empty_mesh.obj",
        FormatId::glb2,
        output_root);
    failures += require(!empty_mesh, "empty or faceless OBJ mesh should be rejected");
    failures += require(
        !std::filesystem::exists(output_root / "empty_mesh"),
        "empty mesh rejection must not leave a final directory");

    const auto invalid = converter.convert(
        source_root / "invalid.obj",
        FormatId::glb2,
        output_root);
    failures += require(!invalid, "invalid OBJ conversion should fail");
    failures += require(
        !std::filesystem::exists(output_root / "invalid"),
        "invalid OBJ must not leave a final directory");

    const auto unverified = converter.convert(
        source_root / "unverified_texture.obj",
        FormatId::glb2,
        output_root);
    failures += require(!unverified, "unverified texture feature should block conversion");
    failures += require(
        unverified.error_code == ConversionErrorCode::route_feature_unverified,
        "unverified texture should return route_feature_unverified");
    failures += require(
        !std::filesystem::exists(output_root / "unverified_texture"),
        "blocked feature must not leave a final directory");
    failures += require(
        no_temporary_directories(output_root),
        "successful and failed transactions must not leave temporary directories");

    if (failures == 0) {
        std::cout << "All AssetBridge conversion tests passed.\n";
        return 0;
    }
    std::cerr << failures << " conversion assertion(s) failed.\n";
    return 1;
}
