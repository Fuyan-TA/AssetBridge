#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;

struct GlbDocument {
    Json json;
    std::vector<std::uint8_t> binary;
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "Could not open fixture: " + path_to_utf8(path));
    return {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    };
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    require(offset + 4 <= bytes.size(), "GLB u32 read is out of range.");
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

GlbDocument read_glb(const std::filesystem::path& path) {
    const auto bytes = read_bytes(path);
    require(bytes.size() >= 20, "GLB is too small.");
    require(read_u32(bytes, 0) == 0x46546C67U, "GLB magic is invalid.");
    require(read_u32(bytes, 4) == 2U, "GLB version is not 2.");
    require(read_u32(bytes, 8) == bytes.size(), "GLB declared length does not match file size.");

    std::size_t offset = 12;
    const auto json_length = read_u32(bytes, offset);
    const auto json_type = read_u32(bytes, offset + 4);
    require(json_type == 0x4E4F534AU, "First GLB chunk is not JSON.");
    offset += 8;
    require(offset + json_length <= bytes.size(), "GLB JSON chunk is out of range.");
    std::string json_text(
        reinterpret_cast<const char*>(bytes.data() + offset),
        json_length);
    while (!json_text.empty() && (json_text.back() == ' ' || json_text.back() == '\0')) {
        json_text.pop_back();
    }
    GlbDocument result;
    result.json = Json::parse(json_text);
    offset += json_length;

    require(offset + 8 <= bytes.size(), "GLB does not contain a BIN chunk.");
    const auto binary_length = read_u32(bytes, offset);
    const auto binary_type = read_u32(bytes, offset + 4);
    require(binary_type == 0x004E4942U, "Second GLB chunk is not BIN.");
    offset += 8;
    require(offset + binary_length <= bytes.size(), "GLB BIN chunk is out of range.");
    result.binary.assign(bytes.begin() + offset, bytes.begin() + offset + binary_length);
    return result;
}

std::string material_name(const aiMaterial& material) {
    aiString name;
    require(material.Get(AI_MATKEY_NAME, name) == AI_SUCCESS, "Material has no name.");
    return { name.C_Str(), name.length };
}

std::string diffuse_texture_path(const aiMaterial& material) {
    aiString path;
    require(
        material.GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS,
        "Material has no diffuse texture.");
    return { path.C_Str(), path.length };
}

std::map<std::string, std::string> imported_material_textures(const aiScene& scene) {
    std::set<unsigned int> referenced;
    for (unsigned int index = 0; index < scene.mNumMeshes; ++index) {
        require(scene.mMeshes[index] != nullptr, "Imported scene has a null mesh.");
        referenced.insert(scene.mMeshes[index]->mMaterialIndex);
    }

    std::map<std::string, std::string> result;
    for (const auto index : referenced) {
        require(index < scene.mNumMaterials && scene.mMaterials[index] != nullptr,
            "Mesh references an invalid material.");
        const auto& material = *scene.mMaterials[index];
        require(material.GetTextureCount(aiTextureType_BASE_COLOR) == 0,
            "OBJ map_Kd unexpectedly imported as BASE_COLOR.");
        require(material.GetTextureCount(aiTextureType_DIFFUSE) == 1,
            "OBJ map_Kd did not import as exactly one DIFFUSE texture.");
        result.emplace(material_name(material), diffuse_texture_path(material));
    }
    return result;
}

std::map<std::string, std::string> reimported_base_color_textures(const aiScene& scene) {
    std::set<unsigned int> referenced;
    for (unsigned int index = 0; index < scene.mNumMeshes; ++index) {
        require(scene.mMeshes[index] != nullptr, "Reimported scene has a null mesh.");
        referenced.insert(scene.mMeshes[index]->mMaterialIndex);
    }
    std::map<std::string, std::string> result;
    for (const auto index : referenced) {
        require(index < scene.mNumMaterials && scene.mMaterials[index] != nullptr,
            "Reimported mesh references an invalid material.");
        const auto& material = *scene.mMaterials[index];
        aiString path;
        require(
            material.GetTexture(aiTextureType_BASE_COLOR, 0, &path) == AI_SUCCESS,
            "Reimported GLB material has no base-color texture.");
        result.emplace(material_name(material), std::string(path.C_Str(), path.length));
    }
    return result;
}

const aiScene* import_obj(Assimp::Importer& importer, const std::filesystem::path& path) {
    const aiScene* scene = importer.ReadFile(
        path_to_utf8(path),
        aiProcess_Triangulate | aiProcess_ValidateDataStructure);
    require(scene != nullptr, "Assimp OBJ import failed: " + std::string(importer.GetErrorString()));
    return scene;
}

void set_embedded_texture(
    aiScene& scene,
    unsigned int texture_index,
    const std::filesystem::path& source_path,
    const char* format_hint) {
    const auto bytes = read_bytes(source_path);
    auto* texture = new aiTexture();
    texture->mWidth = static_cast<unsigned int>(bytes.size());
    texture->mHeight = 0;
    const auto texel_count = (bytes.size() + sizeof(aiTexel) - 1) / sizeof(aiTexel);
    texture->pcData = new aiTexel[texel_count] {};
    std::memcpy(texture->pcData, bytes.data(), bytes.size());
    const auto hint_length = std::min(
        std::strlen(format_hint),
        sizeof(texture->achFormatHint) - 1);
    std::memcpy(texture->achFormatHint, format_hint, hint_length);
    const auto filename = path_to_utf8(source_path.filename());
    texture->mFilename.Set(filename);
    scene.mTextures[texture_index] = texture;
}

void rewrite_diffuse_texture(aiMaterial& material, unsigned int embedded_index) {
    const aiString embedded_path(("*" + std::to_string(embedded_index)).c_str());
    material.RemoveProperty(AI_MATKEY_TEXTURE_DIFFUSE(0));
    require(
        material.AddProperty(&embedded_path, AI_MATKEY_TEXTURE_DIFFUSE(0)) == AI_SUCCESS,
        "Could not rewrite material texture reference to an embedded texture.");
}

void assert_embedded_bytes(
    const GlbDocument& glb,
    const std::vector<std::vector<std::uint8_t>>& expected) {
    require(glb.json.contains("images") && glb.json["images"].is_array(),
        "GLB JSON has no images array.");
    require(glb.json["images"].size() == expected.size(),
        "GLB embedded image count does not match expected count.");
    for (std::size_t image_index = 0; image_index < expected.size(); ++image_index) {
        const auto& image = glb.json["images"][image_index];
        require(image.contains("bufferView") && image["bufferView"].is_number_unsigned(),
            "Embedded GLB image does not use a bufferView.");
        require(!image.contains("uri"), "Embedded GLB image unexpectedly has an external URI.");
        const auto view_index = image["bufferView"].get<std::size_t>();
        require(view_index < glb.json["bufferViews"].size(), "Image bufferView index is invalid.");
        const auto& view = glb.json["bufferViews"][view_index];
        const auto offset = view.value("byteOffset", std::size_t { 0 });
        const auto length = view.at("byteLength").get<std::size_t>();
        require(offset + length <= glb.binary.size(), "Image bufferView exceeds BIN chunk.");
        const std::vector<std::uint8_t> actual(
            glb.binary.begin() + offset,
            glb.binary.begin() + offset + length);
        require(actual == expected[image_index],
            "Assimp did not preserve the original compressed image bytes.");
    }
}

void probe_import_paths(const std::filesystem::path& fixture_root) {
    std::cout << "[probe] import map_Kd type and same-directory paths" << std::endl;
    Assimp::Importer importer;
    const aiScene* scene = import_obj(importer, fixture_root / "texture_behavior.obj");
    require(scene->mNumMeshes == 2, "Multi-material OBJ was not split into two meshes.");
    const auto textures = imported_material_textures(*scene);
    require(textures.at("PngMaterial") == "textures/probe_rgb.png",
        "Assimp changed the PNG map_Kd path.");
    require(textures.at("JpegMaterial") == "textures/probe_rgb.jpg",
        "Assimp changed the JPEG map_Kd path.");

    std::cout << "[probe] child MTL and texture-relative path" << std::endl;
    Assimp::Importer subdir_importer;
    const aiScene* subdir_scene = import_obj(
        subdir_importer,
        fixture_root / "subdir_behavior.obj");
    const auto subdir_textures = imported_material_textures(*subdir_scene);
    std::cout << "[probe] child MTL returned path: "
              << subdir_textures.at("SubdirMaterial") << std::endl;
    require(subdir_textures.at("SubdirMaterial") == "materials/../textures/probe_rgb.png",
        "Assimp did not prefix the child MTL directory while preserving dot segments.");

    std::cout << "[probe] Unicode MTL and PNG paths" << std::endl;
    Assimp::Importer unicode_importer;
    const aiScene* unicode_scene = import_obj(
        unicode_importer,
        fixture_root / L"中文路径" / L"中文材质.obj");
    const auto unicode_textures = imported_material_textures(*unicode_scene);
    std::cout << "[probe] Unicode MTL returned path: "
              << unicode_textures.at("中文贴图材质") << std::endl;
    require(unicode_textures.at("中文贴图材质") == "材质/../贴图/颜色.png",
        "Assimp did not preserve the UTF-8 Unicode MTL-relative texture path.");
}

void probe_external_export(
    const std::filesystem::path& fixture_root,
    const std::filesystem::path& output_root) {
    std::cout << "[probe] external PNG/JPEG direct GLB export" << std::endl;
    Assimp::Importer importer;
    const aiScene* scene = import_obj(importer, fixture_root / "texture_behavior.obj");
    const auto output = output_root / "external-textures.glb";
    Assimp::Exporter exporter;
    require(
        exporter.Export(scene, "glb2", path_to_utf8(output)) == AI_SUCCESS,
        "Assimp external-texture GLB export failed: " + std::string(exporter.GetErrorString()));
    const auto glb = read_glb(output);
    require(glb.json.contains("images") && glb.json["images"].size() == 2,
        "Direct GLB export did not write two image records.");
    for (const auto& image : glb.json["images"]) {
        require(image.contains("uri"), "Assimp unexpectedly embedded an external image.");
        require(!image.contains("bufferView"), "External image unexpectedly uses a bufferView.");
    }

    Assimp::Importer reimporter;
    const aiScene* output_scene = reimporter.ReadFile(
        path_to_utf8(output),
        aiProcess_ValidateDataStructure);
    require(output_scene != nullptr, "Assimp could not reimport direct-export GLB.");
    require(output_scene->mNumTextures == 0,
        "Direct-export GLB unexpectedly reimported external images as embedded textures.");
}

void probe_explicit_embedding(
    const std::filesystem::path& fixture_root,
    const std::filesystem::path& output_root) {
    std::cout << "[probe] explicit PNG/JPEG embedding and byte preservation" << std::endl;
    Assimp::Importer importer;
    const aiScene* imported = import_obj(importer, fixture_root / "texture_behavior.obj");
    auto* scene = const_cast<aiScene*>(imported);
    scene->mNumTextures = 2;
    scene->mTextures = new aiTexture*[2] {};
    const auto png_path = fixture_root / "textures" / "probe_rgb.png";
    const auto jpeg_path = fixture_root / "textures" / "probe_rgb.jpg";
    set_embedded_texture(*scene, 0, png_path, "png");
    set_embedded_texture(*scene, 1, jpeg_path, "jpg");

    for (unsigned int index = 0; index < scene->mNumMaterials; ++index) {
        auto* material = scene->mMaterials[index];
        if (material == nullptr || material->GetTextureCount(aiTextureType_DIFFUSE) == 0) {
            continue;
        }
        const auto name = material_name(*material);
        if (name == "PngMaterial") rewrite_diffuse_texture(*material, 0);
        if (name == "JpegMaterial") rewrite_diffuse_texture(*material, 1);
    }

    const auto output = output_root / "embedded-textures.glb";
    Assimp::Exporter exporter;
    require(
        exporter.Export(scene, "glb2", path_to_utf8(output)) == AI_SUCCESS,
        "Assimp embedded-texture GLB export failed: " + std::string(exporter.GetErrorString()));
    const auto glb = read_glb(output);
    assert_embedded_bytes(glb, { read_bytes(png_path), read_bytes(jpeg_path) });

    Assimp::Importer reimporter;
    const aiScene* output_scene = reimporter.ReadFile(
        path_to_utf8(output),
        aiProcess_ValidateDataStructure);
    require(output_scene != nullptr, "Assimp could not reimport embedded-texture GLB.");
    require(output_scene->mNumTextures == 2,
        "Embedded-texture GLB did not reimport two aiTexture entries.");

    const auto output_bindings = reimported_base_color_textures(*output_scene);
    require(output_bindings.at("PngMaterial") == "*0", "PNG material binding changed after GLB reimport.");
    require(output_bindings.at("JpegMaterial") == "*1", "JPEG material binding changed after GLB reimport.");
    require(output_scene->mNumMeshes == 2, "GLB reimport changed the mesh count.");
}

void probe_shared_texture_deduplication(
    const std::filesystem::path& fixture_root,
    const std::filesystem::path& output_root) {
    std::cout << "[probe] shared embedded texture deduplication" << std::endl;
    Assimp::Importer importer;
    const aiScene* imported = import_obj(importer, fixture_root / "texture_behavior.obj");
    auto* scene = const_cast<aiScene*>(imported);
    scene->mNumTextures = 1;
    scene->mTextures = new aiTexture*[1] {};
    const auto png_path = fixture_root / "textures" / "probe_rgb.png";
    set_embedded_texture(*scene, 0, png_path, "png");
    for (unsigned int index = 0; index < scene->mNumMaterials; ++index) {
        auto* material = scene->mMaterials[index];
        if (material != nullptr && material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            rewrite_diffuse_texture(*material, 0);
        }
    }

    const auto output = output_root / "shared-texture.glb";
    Assimp::Exporter exporter;
    require(
        exporter.Export(scene, "glb2", path_to_utf8(output)) == AI_SUCCESS,
        "Assimp shared-texture GLB export failed: " + std::string(exporter.GetErrorString()));
    const auto glb = read_glb(output);
    assert_embedded_bytes(glb, { read_bytes(png_path) });
    require(glb.json.contains("textures") && glb.json["textures"].size() == 1,
        "Shared image reference was not deduplicated to one glTF texture.");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::cerr << "Usage: assetbridge-assimp-texture-probe-tests <fixture-root> <output-root>\n";
        return 2;
    }
    try {
        const std::filesystem::path fixture_root(argv[1]);
        const std::filesystem::path output_root(argv[2]);
        std::error_code error;
        std::filesystem::remove_all(output_root, error);
        error.clear();
        std::filesystem::create_directories(output_root, error);
        require(!error, "Could not create probe output directory: " + error.message());

        probe_import_paths(fixture_root);
        probe_external_export(fixture_root, output_root);
        probe_explicit_embedding(fixture_root, output_root);
        probe_shared_texture_deduplication(fixture_root, output_root);
        std::cout << "[probe] all Assimp 6.0.4 texture behavior checks passed" << std::endl;
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Assimp texture probe failed: " << exception.what() << '\n';
        return 1;
    }
}
