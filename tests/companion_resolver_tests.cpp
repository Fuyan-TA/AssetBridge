#include "assetbridge/core/companion_resolver.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using assetbridge::CompanionErrorCode;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void write_text(const std::filesystem::path& path, const std::string& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "Could not create test text file.");
    stream << value;
    require(static_cast<bool>(stream), "Could not finish test text file.");
}

void write_binary(const std::filesystem::path& path, const std::vector<std::uint8_t>& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "Could not create test binary file.");
    stream.write(reinterpret_cast<const char*>(value.data()),
        static_cast<std::streamsize>(value.size()));
    require(static_cast<bool>(stream), "Could not finish test binary file.");
}

std::vector<std::uint8_t> read_binary(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "Could not read test binary file.");
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

void copy_fixture(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::filesystem::create_directories(target.parent_path());
    std::error_code error;
    std::filesystem::copy_file(
        source,
        target,
        std::filesystem::copy_options::overwrite_existing,
        error);
    require(!error, "Could not copy fixture: " + error.message());
}

bool has_issue(
    const assetbridge::CompanionResolution& result,
    CompanionErrorCode code) {
    return std::any_of(result.issues.begin(), result.issues.end(), [code](const auto& issue) {
        return issue.code == code;
    });
}

std::string triangle_obj(const std::string& mtllib, const std::string& materials) {
    return "mtllib " + mtllib + "\n"
        "o Test\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        + materials;
}

void test_committed_fixtures(const std::filesystem::path& fixtures) {
    std::cout << "[resolver] committed PNG/JPEG and multi-material fixtures" << std::endl;
    const assetbridge::CompanionResolver resolver;
    const auto same_dir = resolver.resolve(
        fixtures / "texture_behavior.obj",
        { "PngMaterial", "JpegMaterial" });
    require(same_dir.safe(), "Valid PNG/JPEG fixture was blocked.");
    require(same_dir.referenced_material_count == 2, "Referenced material count is wrong.");
    require(same_dir.external_texture_reference_count == 2, "Texture reference count is wrong.");
    require(same_dir.textures.size() == 2, "Unique texture count is wrong.");
    require(same_dir.texture_bytes == 824, "Texture byte total is wrong.");
    require(!same_dir.textures.front().asset_relative_key.empty(),
        "Resolved texture does not have a stable asset-relative key.");
    require(!std::filesystem::path(same_dir.textures.front().asset_relative_key).is_absolute(),
        "Resolved texture key leaked a machine-absolute path.");

    const auto subdir = resolver.resolve(
        fixtures / "subdir_behavior.obj",
        { "SubdirMaterial" });
    require(subdir.safe() && subdir.textures.size() == 1,
        "MTL-relative child-directory texture did not resolve.");

    const auto unicode = resolver.resolve(
        fixtures / L"中文路径" / L"中文材质.obj",
        { "中文贴图材质" });
    require(unicode.safe() && unicode.textures.size() == 1,
        "Unicode OBJ/MTL/PNG companion chain did not resolve.");
}

void test_space_paths(
    const std::filesystem::path& fixture_png,
    const std::filesystem::path& root) {
    std::cout << "[resolver] paths and material names containing spaces" << std::endl;
    copy_fixture(fixture_png, root / "texture folder" / "base color.png");
    write_text(root / "asset with spaces.obj", triangle_obj(
        "material library.mtl",
        "usemtl Material With Spaces\nf 1/1/1 2/2/1 3/3/1\n"));
    write_text(root / "material library.mtl",
        "newmtl Material With Spaces\nmap_Kd texture folder/base color.png\n");
    const assetbridge::CompanionResolver resolver;
    const auto result = resolver.resolve(
        root / "asset with spaces.obj", { "Material With Spaces" });
    require(result.safe() && result.textures.size() == 1,
        "Companion paths containing spaces did not resolve.");
}

void test_dedup_and_unused_material(
    const std::filesystem::path& fixture_png,
    const std::filesystem::path& root) {
    std::cout << "[resolver] shared texture deduplication and unused materials" << std::endl;
    copy_fixture(fixture_png, root / "textures" / "shared.png");
    write_text(root / "asset.obj", triangle_obj(
        "asset.mtl",
        "usemtl First\nf 1/1/1 2/2/1 3/3/1\n"));
    write_text(root / "asset.mtl",
        "newmtl First\nKd 1 0 0\nmap_Kd textures/shared.png\n"
        "newmtl Second\nKd 0 1 0\nmap_Kd textures/shared.png\n"
        "newmtl Unused\nmap_Bump should_not_block.png\n");
    const assetbridge::CompanionResolver resolver;
    const auto result = resolver.resolve(root / "asset.obj", { "First", "Second" });
    require(result.safe(), "Shared texture fixture was blocked.");
    require(result.textures.size() == 1, "Shared texture was embedded more than once.");
    require(result.shared_texture_deduplication_count == 1, "Deduplication count is wrong.");
    require(!has_issue(result, CompanionErrorCode::texture_semantic_unverified),
        "Unused material expanded the supported feature boundary.");
}

void test_failures(
    const std::filesystem::path& fixture_png,
    const std::filesystem::path& fixture_jpeg,
    const std::filesystem::path& root) {
    std::cout << "[resolver] stable failure codes" << std::endl;
    const assetbridge::CompanionResolver resolver;
    const auto run = [&](const std::string& mtl, CompanionErrorCode expected) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::filesystem::create_directories(root);
        write_text(root / "asset.obj", triangle_obj(
            "asset.mtl",
            "usemtl Material\nf 1/1/1 2/2/1 3/3/1\n"));
        write_text(root / "asset.mtl", "newmtl Material\nKd 1 1 1\n" + mtl + "\n");
        const auto result = resolver.resolve(root / "asset.obj", { "Material" });
        require(has_issue(result, expected),
            "Expected issue was not reported: " + std::string(assetbridge::to_string(expected)));
    };

    run("map_Kd missing.png", CompanionErrorCode::texture_file_missing);
    run("map_Kd ../outside.png", CompanionErrorCode::texture_path_outside_asset_root);
    run("map_Kd C:/outside.png", CompanionErrorCode::texture_path_absolute);
    run("map_Kd \\\\server\\share\\image.png", CompanionErrorCode::texture_path_absolute);
    run("map_Kd https://example.invalid/image.png", CompanionErrorCode::texture_path_absolute);
    run("map_Kd image.bmp", CompanionErrorCode::texture_file_missing);
    run("map_Kd -clamp on image.png", CompanionErrorCode::texture_options_unverified);
    run("map_Kd", CompanionErrorCode::companion_parse_failed);
    run("map_Bump bump.png", CompanionErrorCode::texture_semantic_unverified);
    run("norm normal.png", CompanionErrorCode::texture_semantic_unverified);
    run("map_d opacity.png", CompanionErrorCode::transparency_unverified);
    run("d 0.5", CompanionErrorCode::transparency_unverified);
    run("map_Kd one.png\nmap_Kd two.png",
        CompanionErrorCode::multiple_textures_per_material_unverified);

    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    write_text(root / "asset.obj", triangle_obj(
        "asset.mtl",
        "usemtl Material\nf 1/1/1 2/2/1 3/3/1\n"));
    write_text(root / "asset.mtl", "newmtl Material\nmap_Kd image.bmp\n");
    copy_fixture(fixture_png, root / "image.bmp");
    auto result = resolver.resolve(root / "asset.obj", { "Material" });
    require(has_issue(result, CompanionErrorCode::texture_format_unsupported),
        "Unsupported image extension was not blocked.");

    write_text(root / "asset.mtl", "newmtl Material\nmap_Kd image.png\n");
    copy_fixture(fixture_jpeg, root / "image.png");
    result = resolver.resolve(root / "asset.obj", { "Material" });
    require(has_issue(result, CompanionErrorCode::texture_signature_mismatch),
        "Extension/signature mismatch was not blocked.");

    write_text(root / "asset.mtl", "newmtl Material\nmap_Kd empty.png\n");
    write_text(root / "empty.png", "");
    result = resolver.resolve(root / "asset.obj", { "Material" });
    require(has_issue(result, CompanionErrorCode::texture_signature_mismatch),
        "Empty image was not blocked.");

    std::filesystem::create_directory(root / "directory.png");
    write_text(root / "asset.mtl", "newmtl Material\nmap_Kd directory.png\n");
    result = resolver.resolve(root / "asset.obj", { "Material" });
    require(has_issue(result, CompanionErrorCode::texture_path_not_regular_file),
        "Directory masquerading as a texture was not blocked.");

    auto alpha_png = read_binary(fixture_png);
    require(alpha_png.size() > 25, "PNG fixture is too small for the transparency test.");
    alpha_png[25] = 6;
    write_binary(root / "alpha.png", alpha_png);
    write_text(root / "asset.mtl", "newmtl Material\nmap_Kd alpha.png\n");
    result = resolver.resolve(root / "asset.obj", { "Material" });
    require(has_issue(result, CompanionErrorCode::transparency_unverified),
        "PNG alpha semantics were not blocked.");
}

void test_material_library_failures(const std::filesystem::path& root) {
    std::cout << "[resolver] material-library boundary failures" << std::endl;
    const assetbridge::CompanionResolver resolver;
    const auto run = [&](const std::string& library, CompanionErrorCode expected) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::filesystem::create_directories(root);
        write_text(root / "asset.obj", triangle_obj(
            library,
            "usemtl Material\nf 1/1/1 2/2/1 3/3/1\n"));
        const auto result = resolver.resolve(root / "asset.obj", { "Material" });
        require(has_issue(result, expected),
            "Expected MTL issue was not reported: "
                + std::string(assetbridge::to_string(expected)));
    };

    run("missing.mtl", CompanionErrorCode::material_library_missing);
    run("../outside.mtl", CompanionErrorCode::material_library_path_outside_asset_root);
    run("C:/outside.mtl", CompanionErrorCode::material_library_path_absolute);

    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "directory.mtl");
    write_text(root / "asset.obj", triangle_obj(
        "directory.mtl",
        "usemtl Material\nf 1/1/1 2/2/1 3/3/1\n"));
    auto result = resolver.resolve(root / "asset.obj", { "Material" });
    require(has_issue(result, CompanionErrorCode::material_library_not_regular_file),
        "Directory masquerading as an MTL was not blocked.");

    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    write_text(root / "asset.obj", triangle_obj(
        "asset.mtl",
        "usemtl MissingMaterial\nf 1/1/1 2/2/1 3/3/1\n"));
    write_text(root / "asset.mtl", "newmtl OtherMaterial\nKd 1 1 1\n");
    result = resolver.resolve(root / "asset.obj", { "MissingMaterial" });
    require(has_issue(result, CompanionErrorCode::material_reference_unresolved),
        "Unresolved mesh material was not blocked.");
}

void test_resource_limits(
    const std::filesystem::path& fixture_png,
    const std::filesystem::path& root) {
    std::cout << "[resolver] named texture resource limits" << std::endl;
    std::filesystem::create_directories(root);
    copy_fixture(fixture_png, root / "image.png");
    write_text(root / "asset.obj", triangle_obj(
        "asset.mtl",
        "usemtl Material\nf 1/1/1 2/2/1 3/3/1\n"));
    write_text(root / "asset.mtl", "newmtl Material\nmap_Kd image.png\n");
    const assetbridge::CompanionResolver resolver;
    auto result = resolver.resolve(
        root / "asset.obj",
        { "Material" },
        assetbridge::TextureLimits { 16, 64, 256 });
    require(has_issue(result, CompanionErrorCode::texture_file_too_large),
        "Per-file texture limit was not enforced.");

    result = resolver.resolve(
        root / "asset.obj",
        { "Material" },
        assetbridge::TextureLimits { 1024, 0, 1024 });
    require(has_issue(result, CompanionErrorCode::texture_count_limit_exceeded),
        "Texture-count limit was not enforced.");

    result = resolver.resolve(
        root / "asset.obj",
        { "Material" },
        assetbridge::TextureLimits { 1024, 64, 32 });
    require(has_issue(result, CompanionErrorCode::texture_total_size_limit_exceeded),
        "Total texture-byte limit was not enforced.");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    try {
        const std::filesystem::path fixtures(argv[1]);
        const std::filesystem::path output(argv[2]);
        std::error_code ignored;
        std::filesystem::remove_all(output, ignored);
        std::filesystem::create_directories(output);
        test_committed_fixtures(fixtures);
        test_dedup_and_unused_material(
            fixtures / "textures" / "probe_rgb.png",
            output / "dedup");
        test_space_paths(
            fixtures / "textures" / "probe_rgb.png",
            output / "space paths");
        test_failures(
            fixtures / "textures" / "probe_rgb.png",
            fixtures / "textures" / "probe_rgb.jpg",
            output / "failures");
        test_material_library_failures(output / "material failures");
        test_resource_limits(
            fixtures / "textures" / "probe_rgb.png",
            output / "limits");
        std::cout << "Companion resolver tests passed." << std::endl;
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Companion resolver test failed: " << exception.what() << '\n';
        return 1;
    }
}
