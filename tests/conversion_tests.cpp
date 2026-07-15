#include "assetbridge/core/asset_converter.hpp"
#include "assetbridge/core/asset_inspector.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
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
    std::cout << "DIAG PROBE_BEGIN subtest=transaction_cleanup operation=root_exists"
              << std::endl;
    const bool root_exists = std::filesystem::exists(root, error);
    std::cout << "DIAG PROBE_END subtest=transaction_cleanup operation=root_exists"
              << " result=" << (root_exists ? "true" : "false")
              << " error=" << error.value() << std::endl;
    if (!root_exists) {
        return true;
    }
    std::cout << "DIAG PROBE_BEGIN subtest=transaction_cleanup operation=directory_scan"
              << std::endl;
    const auto temporary_prefix = std::filesystem::path(".assetbridge-tmp-").native();
    std::size_t entry_index = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        const auto name = entry.path().filename().native();
        std::cout << "DIAG PROBE_ENTRY subtest=transaction_cleanup operation=directory_scan"
                  << " index=" << entry_index++ << std::endl;
        if (name.starts_with(temporary_prefix)) {
            return false;
        }
    }
    std::cout << "DIAG PROBE_END subtest=transaction_cleanup operation=directory_scan"
              << " error=" << error.value() << std::endl;
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

std::string_view stage_name(assetbridge::ConversionStage stage) {
    using assetbridge::ConversionStage;
    switch (stage) {
    case ConversionStage::preflight: return "preflight";
    case ConversionStage::importing: return "importing";
    case ConversionStage::exporting: return "exporting";
    case ConversionStage::reimporting: return "reimporting";
    case ConversionStage::validating: return "validating";
    case ConversionStage::committing: return "committing";
    }
    return "unknown";
}

void diagnostic(
    std::string_view marker,
    std::string_view subtest,
    std::optional<assetbridge::ConversionStage> stage = std::nullopt) {
    std::cout << "DIAG " << marker << " subtest=" << subtest;
    if (stage) {
        std::cout << " stage=" << stage_name(*stage);
    }
    std::cout << std::endl;
}

void diagnostic_named_stage(
    std::string_view marker,
    std::string_view subtest,
    std::string_view stage) {
    std::cout << "DIAG " << marker << " subtest=" << subtest
              << " stage=" << stage << std::endl;
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

    const auto run_conversion = [&converter, &output_root](
                                    std::string_view subtest,
                                    const std::filesystem::path& input,
                                    std::vector<ConversionStage>* observed_stages = nullptr) {
        diagnostic("SUBTEST_BEGIN", subtest);
        std::optional<ConversionStage> active_stage;
        auto report = converter.convert(
            input,
            FormatId::glb2,
            output_root,
            [&](ConversionStage stage) {
                if (active_stage) {
                    diagnostic("STAGE_END", subtest, active_stage);
                }
                active_stage = stage;
                diagnostic("STAGE_BEGIN", subtest, active_stage);
                if (observed_stages) {
                    observed_stages->push_back(stage);
                }
            });
        if (active_stage) {
            diagnostic("STAGE_END", subtest, active_stage);
        }
        diagnostic("SUBTEST_END", subtest);
        return report;
    };

    diagnostic("SUBTEST_BEGIN", "mesh_matching_reordered");
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
    diagnostic("SUBTEST_END", "mesh_matching_reordered");

    diagnostic("SUBTEST_BEGIN", "mesh_matching_duplicate_names");
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
    diagnostic("SUBTEST_END", "mesh_matching_duplicate_names");

    std::vector<ConversionStage> progress_stages;
    const auto ascii = run_conversion(
        "ascii_minimal_triangle",
        source_root / "minimal_triangle.obj",
        &progress_stages);
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
        diagnostic_named_stage("STAGE_BEGIN", "ascii_output_inspect", "inspect");
        const auto output_inspection = inspector.inspect(glb);
        diagnostic_named_stage("STAGE_END", "ascii_output_inspect", "inspect");
        failures += require(
            static_cast<bool>(output_inspection),
            "generated ASCII GLB should be inspectable by Assimp");
        failures += require(
            std::filesystem::is_regular_file(
                *ascii.output_directory / "conversion-report.json"),
            "successful conversion should commit its JSON report");
    }

    const auto conflict = run_conversion(
        "ascii_output_conflict",
        source_root / "minimal_triangle.obj");
    failures += require(
        conflict && conflict.output_directory.has_value()
            && conflict.output_directory->filename() == "minimal_triangle_2",
        "existing output should cause a _2 directory");

    const auto unicode = run_conversion(
        "unicode_minimal_triangle",
        source_root / L"中文三角形.obj");
    failures += require(static_cast<bool>(unicode), "Unicode OBJ and MTL conversion should succeed");
    if (unicode.output_directory) {
        const auto unicode_glb = *unicode.output_directory / L"中文三角形.glb";
        failures += require(
            std::filesystem::is_regular_file(unicode_glb)
                && std::filesystem::file_size(unicode_glb) > 0,
            "Unicode GLB path should exist and be non-empty");
    }

    const auto multi = run_conversion(
        "ascii_multi_two_triangles",
        source_root / "multi_two_triangles.obj");
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

    const auto quad = run_conversion(
        "ascii_multi_triangle_quad",
        source_root / "multi_triangle_quad.obj");
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

    const auto unicode_multi = run_conversion(
        "unicode_multi_mesh",
        source_root / L"中文多网格.obj");
    failures += require(
        unicode_multi && unicode_multi.output_analysis
            && unicode_multi.output_analysis->mesh_count == 2,
        "Unicode OBJ/MTL multi-mesh conversion should preserve two meshes");

    const auto empty_mesh = run_conversion(
        "reject_empty_mesh",
        source_root / "empty_mesh.obj");
    failures += require(!empty_mesh, "empty or faceless OBJ mesh should be rejected");
    failures += require(
        !std::filesystem::exists(output_root / "empty_mesh"),
        "empty mesh rejection must not leave a final directory");

    const auto invalid = run_conversion(
        "reject_invalid_obj",
        source_root / "invalid.obj");
    failures += require(!invalid, "invalid OBJ conversion should fail");
    failures += require(
        !std::filesystem::exists(output_root / "invalid"),
        "invalid OBJ must not leave a final directory");

    const auto unverified = run_conversion(
        "reject_unverified_texture",
        source_root / "unverified_texture.obj");
    failures += require(!unverified, "unverified texture feature should block conversion");
    failures += require(
        unverified.error_code == ConversionErrorCode::route_feature_unverified,
        "unverified texture should return route_feature_unverified");
    failures += require(
        !std::filesystem::exists(output_root / "unverified_texture"),
        "blocked feature must not leave a final directory");
    diagnostic_named_stage("STAGE_BEGIN", "transaction_cleanup", "cleanup");
    const bool cleanup_complete = no_temporary_directories(output_root);
    diagnostic_named_stage("STAGE_END", "transaction_cleanup", "cleanup");
    failures += require(
        cleanup_complete,
        "successful and failed transactions must not leave temporary directories");

    if (failures == 0) {
        std::cout << "All AssetBridge conversion tests passed.\n";
        return 0;
    }
    std::cerr << failures << " conversion assertion(s) failed.\n";
    return 1;
}
