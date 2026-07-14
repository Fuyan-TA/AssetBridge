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
    return {
        path,
        { assetbridge::FormatId::obj, true, true },
        assetbridge::InspectionErrorCode::none,
        {},
        features,
        std::move(decision)
    };
}

assetbridge::ConversionReport successful_conversion(const std::filesystem::path& root) {
    assetbridge::ConversionReport report;
    report.source_path = std::filesystem::path(L"测试") / L"三角形.obj";
    report.output_directory = root / L"三角形";
    report.output_files = {
        *report.output_directory / L"三角形.glb",
        *report.output_directory / "conversion-report.json"
    };
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

    DesktopAppState success;
    success.select_files({ unicode_path });
    success.complete_inspection(successful_inspection(unicode_path), safe_preflight(unicode_path));
    success.set_output_root(L"输出");
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
