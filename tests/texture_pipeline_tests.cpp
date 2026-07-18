#include "assetbridge/core/companion_resolver.hpp"
#include "assetbridge/core/glb_container.hpp"
#include "assetbridge/core/texture_embedder.hpp"

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "Could not create pipeline fixture.");
    stream << text;
    require(static_cast<bool>(stream), "Could not finish pipeline fixture.");
}

void copy_fixture(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::filesystem::create_directories(target.parent_path());
    std::error_code error;
    std::filesystem::copy_file(
        source, target, std::filesystem::copy_options::overwrite_existing, error);
    require(!error, "Could not copy pipeline texture fixture: " + error.message());
}

const aiScene* import_obj(
    Assimp::Importer& importer,
    const std::filesystem::path& input) {
    const aiScene* scene = importer.ReadFile(
        path_to_utf8(input),
        aiProcess_Triangulate | aiProcess_ValidateDataStructure);
    require(scene != nullptr, "OBJ import failed: " + std::string(importer.GetErrorString()));
    return scene;
}

void export_glb(const aiScene& scene, const std::filesystem::path& output) {
    Assimp::Exporter exporter;
    require(
        exporter.Export(&scene, "glb2", path_to_utf8(output)) == AI_SUCCESS,
        "GLB export failed: " + std::string(exporter.GetErrorString()));
}

void verify_pipeline(
    const std::filesystem::path& input,
    const std::vector<std::string>& material_names,
    const std::filesystem::path& output,
    std::size_t expected_unique_textures,
    std::size_t expected_rewritten_materials) {
    const assetbridge::CompanionResolver resolver;
    const auto companions = resolver.resolve(input, material_names);
    require(companions.safe(), "Companion resolution blocked a valid pipeline fixture.");
    require(companions.textures.size() == expected_unique_textures,
        "Resolved unique texture count is wrong.");

    Assimp::Importer importer;
    const aiScene* imported = import_obj(importer, input);
    auto* scene = const_cast<aiScene*>(imported);
    const auto embedding = assetbridge::embed_base_color_textures(*scene, companions);
    require(static_cast<bool>(embedding), "Texture embedding failed: " + embedding.error);
    require(embedding.embedded_texture_count == expected_unique_textures,
        "Embedded texture count is wrong.");
    require(embedding.rewritten_material_count == expected_rewritten_materials,
        "Rewritten material count is wrong.");
    export_glb(*scene, output);

    const auto container = assetbridge::validate_glb_container(output, companions);
    require(static_cast<bool>(container), "GLB container validation failed: " + container.error);
    require(container.image_count == expected_unique_textures,
        "GLB image count is wrong.");

    Assimp::Importer reimporter;
    const aiScene* output_scene = reimporter.ReadFile(
        path_to_utf8(output), aiProcess_ValidateDataStructure);
    require(output_scene != nullptr,
        "Generated GLB could not be reimported: " + std::string(reimporter.GetErrorString()));
    require(output_scene->mNumTextures == expected_unique_textures,
        "Reimported GLB embedded texture count is wrong.");
}

void test_distinct_png_jpeg(
    const std::filesystem::path& fixtures,
    const std::filesystem::path& output) {
    std::cout << "[texture-pipeline] distinct PNG/JPEG embedding" << std::endl;
    verify_pipeline(
        fixtures / "texture_behavior.obj",
        { "PngMaterial", "JpegMaterial" },
        output / "distinct.glb",
        2,
        2);
}

void test_shared_texture(
    const std::filesystem::path& fixtures,
    const std::filesystem::path& root) {
    std::cout << "[texture-pipeline] shared image deduplication" << std::endl;
    copy_fixture(fixtures / "textures" / "probe_rgb.png", root / "shared image.png");
    write_text(root / "shared.obj",
        "mtllib shared.mtl\n"
        "o First\n"
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "v 2 0 0\nv 3 0 0\nv 2 1 0\n"
        "vt 0 0\nvt 1 0\nvt 0 1\n"
        "vn 0 0 1\n"
        "usemtl FirstMaterial\nf 1/1/1 2/2/1 3/3/1\n"
        "o Second\nusemtl SecondMaterial\nf 4/1/1 5/2/1 6/3/1\n");
    write_text(root / "shared.mtl",
        "newmtl FirstMaterial\nKd 1 0 0\nmap_Kd shared image.png\n"
        "newmtl SecondMaterial\nKd 0 1 0\nmap_Kd shared image.png\n");
    verify_pipeline(
        root / "shared.obj",
        { "FirstMaterial", "SecondMaterial" },
        root / "shared.glb",
        1,
        2);
}

void test_external_uri_rejected(
    const std::filesystem::path& fixtures,
    const std::filesystem::path& output) {
    std::cout << "[texture-pipeline] direct external URI export rejection" << std::endl;
    const assetbridge::CompanionResolver resolver;
    const auto companions = resolver.resolve(
        fixtures / "texture_behavior.obj",
        { "PngMaterial", "JpegMaterial" });
    require(companions.safe(), "Valid external fixture did not resolve.");
    Assimp::Importer importer;
    const aiScene* scene = import_obj(importer, fixtures / "texture_behavior.obj");
    const auto glb = output / "external-uri.glb";
    export_glb(*scene, glb);
    const auto validation = assetbridge::validate_glb_container(glb, companions);
    require(!validation, "Container validator accepted external image URIs.");
}

void test_untextured_regression(const std::filesystem::path& root) {
    std::cout << "[texture-pipeline] untextured v0.1 regression" << std::endl;
    write_text(root / "plain.obj",
        "mtllib plain.mtl\n"
        "o Plain\n"
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "vt 0 0\nvt 1 0\nvt 0 1\n"
        "vn 0 0 1\n"
        "usemtl PlainMaterial\nf 1/1/1 2/2/1 3/3/1\n");
    write_text(root / "plain.mtl", "newmtl PlainMaterial\nKd 0.5 0.5 0.5\n");
    verify_pipeline(
        root / "plain.obj",
        { "PlainMaterial" },
        root / "plain.glb",
        0,
        0);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    try {
        const std::filesystem::path fixtures(argv[1]);
        const std::filesystem::path output(argv[2]);
        std::error_code error;
        std::filesystem::remove_all(output, error);
        error.clear();
        std::filesystem::create_directories(output, error);
        require(!error, "Could not create texture pipeline output directory.");
        test_distinct_png_jpeg(fixtures, output);
        test_shared_texture(fixtures, output / "shared fixture");
        test_external_uri_rejected(fixtures, output);
        test_untextured_regression(output / "plain fixture");
        std::cout << "Texture embedding and GLB container tests passed." << std::endl;
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Texture pipeline test failed: " << exception.what() << '\n';
        return 1;
    }
}
