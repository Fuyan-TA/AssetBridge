#include "assetbridge/desktop/desktop_app_state.hpp"

#include <iostream>
#include <string_view>

namespace {

int require(bool condition, std::string_view message) {
    if (condition) return 0;
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

assetbridge::InspectionResult successful_inspection(const std::filesystem::path& path) {
    assetbridge::AssetSummary summary;
    summary.file_path = path;
    summary.mesh_count = 1;
    summary.triangle_count = 1;
    summary.material_count = 1;
    summary.uv_channel_count = 1;
    summary.has_normals = true;
    assetbridge::AssetFeatures features;
    features.mesh_count = 1;
    features.meshes_with_normals = 1;
    features.max_uv_channel_count = 1;
    features.referenced_material_count = 1;
    return { assetbridge::InspectionErrorCode::none, {}, summary, features };
}

assetbridge::PreflightReport safe_preflight(const std::filesystem::path& path) {
    assetbridge::AssetFeatures features;
    features.mesh_count = 1;
    assetbridge::PreflightDecision decision{
        assetbridge::FormatId::glb2,
        true,
        true,
        true,
        assetbridge::CompatibilityResult::safe,
        assetbridge::OverallResult::safe,
        {},
        {}
    };
    assetbridge::PreflightReport report;
    report.file_path = path;
    report.source = { assetbridge::FormatId::obj, true, true };
    report.features = std::move(features);
    report.companions = assetbridge::CompanionResolution {};
    report.companions->referenced_material_count = 1;
    report.decision = std::move(decision);
    return report;
}

assetbridge::PreflightReport blocked_preflight(
    const std::filesystem::path& path,
    std::vector<assetbridge::LossItem> losses) {
    assetbridge::AssetFeatures features;
    features.mesh_count = 29;
    assetbridge::PreflightDecision decision{
        assetbridge::FormatId::glb2,
        true,
        true,
        true,
        assetbridge::CompatibilityResult::blocked,
        assetbridge::OverallResult::blocked,
        {},
        std::move(losses)
    };
    assetbridge::PreflightReport report;
    report.file_path = path;
    report.source = { assetbridge::FormatId::obj, true, true };
    report.features = std::move(features);
    report.companions = assetbridge::CompanionResolution {};
    report.companions->referenced_material_count = 1;
    report.decision = std::move(decision);
    return report;
}

assetbridge::ConversionReport successful_conversion(const std::filesystem::path& root) {
    assetbridge::ConversionReport report;
    report.source_path = std::filesystem::path(L"测试") / L"三角形.obj";
    report.output_directory = root / L"三角形";
    report.output_files = {
        *report.output_directory / L"三角形.glb",
        *report.output_directory / "conversion-report.json"
    };
    report.embedded_texture_count = 1;
    return report;
}

} // namespace

int main() {
    using namespace assetbridge::desktop;
    const std::filesystem::path unicode_path = std::filesystem::path(L"测试") / L"中文三角形.obj";
    int failures = 0;

    DesktopAppState initial;
    failures += require(initial.status() == AppStatus::no_file, "initial state should be no_file");

    DesktopAppState accepted;
    failures += require(
        accepted.select_files({ unicode_path }) == FileSelectionResult::accepted,
        "one Unicode OBJ should be accepted for inspection");
    failures += require(accepted.status() == AppStatus::inspecting, "accepted OBJ should enter inspecting");
    failures += require(accepted.input_path() == unicode_path, "Unicode input path should be preserved exactly");

    DesktopAppState non_obj;
    failures += require(
        non_obj.select_files({ std::filesystem::path(L"测试") / L"fake.glb" })
            == FileSelectionResult::not_obj,
        "non-OBJ extension should be rejected");

    DesktopAppState multiple;
    failures += require(
        multiple.select_files({ L"a.obj", L"b.obj" }) == FileSelectionResult::multiple_files,
        "multiple files should be rejected");
    failures += require(
        multiple.message() == "MVP currently supports one OBJ at a time.",
        "multiple-file error text should remain stable");

    DesktopAppState no_output;
    no_output.select_files({ unicode_path });
    no_output.complete_inspection(successful_inspection(unicode_path), safe_preflight(unicode_path));
    failures += require(no_output.status() == AppStatus::ready, "successful inspection should enter ready");
    failures += require(!no_output.can_convert(), "conversion should require an output directory");
    failures += require(!no_output.begin_conversion(), "conversion must not start without output directory");

    DesktopAppState blocked;
    blocked.select_files({ unicode_path });
    auto blocked_inspection = successful_inspection(unicode_path);
    blocked_inspection.summary->mesh_count = 29;
    blocked_inspection.summary->face_count = 87428;
    blocked_inspection.summary->triangle_count = 0;
    blocked.complete_inspection(
        std::move(blocked_inspection),
        blocked_preflight(unicode_path, {
            {
                "route_feature_unverified",
                assetbridge::AssetFeature::external_textures,
                assetbridge::LossSeverity::blocking,
                false,
                "Feature has not been verified for this AssetBridge conversion route."
            },
            {
                "route_feature_unverified",
                assetbridge::AssetFeature::node_hierarchy,
                assetbridge::LossSeverity::blocking,
                false,
                "Feature has not been verified for this AssetBridge conversion route."
            }
        }));
    blocked.set_output_root(L"输出");
    failures += require(
        blocked.status() == AppStatus::not_supported,
        "blocked preflight should enter the distinct not-supported state");
    failures += require(
        !blocked.is_runtime_failure(),
        "blocked preflight is a product-boundary result, not a runtime failure");
    failures += require(!blocked.can_convert(), "blocked preflight must disable conversion");
    failures += require(!blocked.begin_conversion(), "blocked preflight must not start conversion");
    failures += require(
        blocked.diagnostics().size() == 2,
        "all detected blocking reasons should be available to the UI");
    failures += require(
        blocked.diagnostics()[0].code == "route_feature_unverified"
            && blocked.diagnostics()[0].feature_code == "external_textures"
            && blocked.diagnostics()[0].message == "External textures — not verified yet"
            && blocked.diagnostics()[0].future_support_candidate,
        "external-texture diagnostic should preserve stable codes and future scope");
    failures += require(
        blocked.diagnostics()[1].feature_code == "node_hierarchy"
            && blocked.diagnostics()[1].message == "Meaningful hierarchy — not verified yet",
        "multiple concrete diagnostics should retain distinct user messages");
    failures += require(
        blocked.has_non_triangle_faces(),
        "faces greater than source triangles should identify non-triangle source geometry");
    failures += require(
        blocked.geometry_notice()
            == "Source contains non-triangle faces; conversion will triangulate when the route supports this asset.",
        "non-triangle geometry notice should remain explicit and stable");

    DesktopAppState safe_multi;
    safe_multi.select_files({ unicode_path });
    auto safe_multi_inspection = successful_inspection(unicode_path);
    safe_multi_inspection.summary->mesh_count = 29;
    safe_multi_inspection.summary->face_count = 87428;
    safe_multi_inspection.summary->triangle_count = 0;
    auto safe_multi_preflight = safe_preflight(unicode_path);
    safe_multi_preflight.features->mesh_count = 29;
    safe_multi.complete_inspection(
        std::move(safe_multi_inspection),
        std::move(safe_multi_preflight));
    safe_multi.set_output_root(L"输出");
    failures += require(
        safe_multi.status() == AppStatus::ready
            && safe_multi.can_convert()
            && safe_multi.diagnostics().empty(),
        "safe flat multi-mesh preflight should enable conversion without diagnostics");
    failures += require(
        safe_multi.geometry_notice()
            == "Source contains non-triangle faces; the verified OBJ to GLB route will triangulate them during export preparation.",
        "safe multi-mesh geometry should explain export-preparation triangulation");
    failures += require(
        safe_multi.begin_conversion(),
        "safe multi-mesh state should enter conversion");
    safe_multi.mark_validating();
    safe_multi.complete_conversion(successful_conversion(L"输出"));
    failures += require(
        safe_multi.geometry_notice()
            == "Source contained non-triangle faces; the verified OBJ to GLB route triangulated them during export preparation.",
        "successful multi-mesh conversion should describe triangulation in past tense");

    DesktopAppState success;
    success.select_files({ unicode_path });
    success.complete_inspection(successful_inspection(unicode_path), safe_preflight(unicode_path));
    success.set_output_root(L"输出");
    failures += require(success.can_convert(), "safe preflight should remain convertible");
    failures += require(success.begin_conversion(), "ready state with output should enter converting");
    failures += require(success.status() == AppStatus::converting, "state should be converting");
    failures += require(
        success.select_files({ L"second.obj" }) == FileSelectionResult::busy,
        "a second task must be rejected while converting");
    success.mark_validating();
    failures += require(success.status() == AppStatus::validating, "conversion should expose validating stage");
    success.complete_conversion(successful_conversion(L"输出"));
    failures += require(success.status() == AppStatus::success, "successful report should enter success");
    failures += require(
        success.message().find("embedded base-color textures") != std::string::npos,
        "successful textured conversion should state that the GLB contains embedded textures");
    failures += require(
        success.conversion()->output_directory->filename() == L"三角形",
        "Unicode output path should be preserved");

    DesktopAppState failed;
    failed.select_files({ unicode_path });
    failed.complete_inspection(successful_inspection(unicode_path), safe_preflight(unicode_path));
    failed.set_output_root(L"输出");
    failed.begin_conversion();
    assetbridge::ConversionReport error_report;
    error_report.error_code = assetbridge::ConversionErrorCode::validation_failed;
    error_report.error_message = "round-trip validation failed";
    failed.complete_conversion(std::move(error_report));
    failures += require(failed.status() == AppStatus::failed, "failed report should enter failed");
    failures += require(
        failed.message() == "round-trip validation failed",
        "structured conversion error should be preserved");

    DesktopAppState invalid_content;
    invalid_content.select_files({ L"renamed.obj" });
    assetbridge::InspectionResult invalid;
    invalid.error_code = assetbridge::InspectionErrorCode::import_failed;
    invalid.error_message = "Assimp rejected non-OBJ content";
    invalid_content.complete_inspection(std::move(invalid), safe_preflight(L"renamed.obj"));
    failures += require(
        invalid_content.status() == AppStatus::failed,
        "an OBJ extension must not bypass actual importer validation");
    failures += require(
        invalid_content.message() == "Assimp rejected non-OBJ content",
        "inspection error text should be preserved");

    if (failures == 0) {
        std::cout << "All AssetBridge desktop state tests passed.\n";
        return 0;
    }
    std::cerr << failures << " desktop state assertion(s) failed.\n";
    return 1;
}
