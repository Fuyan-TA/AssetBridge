#include "assetbridge/core/asset_converter.hpp"
#include "assetbridge/core/asset_inspector.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string_view>
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
