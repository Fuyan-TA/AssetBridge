#include "assetbridge/core/asset_inspector.hpp"

#include <filesystem>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace {

int require(bool condition, std::string_view message) {
    if (condition) {
        return 0;
    }

    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

bool nearly_equal(float left, float right) {
    return std::fabs(left - right) < 0.0001F;
}

bool has_expected_unicode_material(const aiScene& scene) {
    constexpr std::string_view expected_name = "\xE4\xB8\xAD\xE6\x96\x87\xE6\x9D\x90\xE8\xB4\xA8";

    for (unsigned int index = 0; index < scene.mNumMaterials; ++index) {
        const aiMaterial* material = scene.mMaterials[index];
        if (material == nullptr) {
            continue;
        }

        aiString name;
        aiColor3D diffuse;
        if (material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS
            && name.C_Str() == expected_name
            && material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS
            && nearly_equal(diffuse.r, 0.125F)
            && nearly_equal(diffuse.g, 0.5F)
            && nearly_equal(diffuse.b, 0.875F)) {
            return true;
        }
    }

    return false;
}

} // namespace

int main() {
    const auto asset_directory = std::filesystem::current_path() / "tests" / "assets";
    const auto ascii_obj_path = asset_directory / "minimal_triangle.obj";
    const auto unicode_obj_path = asset_directory / L"中文三角形.obj";
    const auto invalid_obj_path = asset_directory / "invalid.obj";

    const assetbridge::AssetInspector inspector;
    const auto ascii_result = inspector.inspect(ascii_obj_path);

    int failures = 0;
    failures += require(static_cast<bool>(ascii_result), "ASCII OBJ should import successfully");
    failures += require(
        ascii_result.error_code == assetbridge::InspectionErrorCode::none,
        "successful ASCII inspection should have error code none");
    failures += require(
        ascii_result.error_message.empty(),
        "successful ASCII inspection should not have an error message");
    if (ascii_result) {
        const auto& summary = *ascii_result.summary;
        failures += require(summary.mesh_count == 1, "mesh count should be 1");
        failures += require(summary.vertex_count == 3, "vertex count should be 3");
        failures += require(summary.face_count == 1, "face count should be 1");
        failures += require(summary.triangle_count == 1, "triangle count should be 1");
        failures += require(summary.material_count >= 1, "material count should be at least 1");
        failures += require(summary.uv_channel_count == 1, "UV channel count should be 1");
        failures += require(summary.has_normals, "mesh should have normals");
        failures += require(ascii_result.features.has_value(), "successful inspection should include AssetFeatures");
        if (ascii_result.features.has_value()) {
            const auto& features = *ascii_result.features;
            failures += require(features.mesh_count == 1, "feature mesh count should be 1");
            failures += require(
                !features.has_node_hierarchy,
                "minimal OBJ should not report Assimp's flat wrapper as meaningful hierarchy");
            failures += require(features.meshes_with_normals == 1, "one mesh should have normals");
            failures += require(features.meshes_with_tangents == 0, "OBJ should not invent tangents");
            failures += require(features.max_uv_channel_count == 1, "maximum UV channel count should be 1");
            failures += require(features.referenced_material_count == 1, "one material should be referenced by a mesh");
            failures += require(features.bone_count == 0, "OBJ should not invent bones");
            failures += require(features.animations.empty(), "OBJ should not invent animations");
            failures += require(features.morph_target_names.empty(), "OBJ should not invent morph targets");
        }
    } else {
        std::cerr << "ASCII import error: " << ascii_result.error_message << '\n';
    }

    const auto unicode_result = inspector.inspect(unicode_obj_path);
    failures += require(static_cast<bool>(unicode_result), "Unicode OBJ should import successfully");
    failures += require(
        unicode_result.error_code == assetbridge::InspectionErrorCode::none,
        "successful Unicode inspection should have error code none");
    if (unicode_result) {
        failures += require(unicode_result.summary->mesh_count == 1, "Unicode OBJ mesh count should be 1");
        failures += require(unicode_result.summary->triangle_count == 1, "Unicode OBJ triangle count should be 1");
    } else {
        std::cerr << "Unicode import error: " << unicode_result.error_message << '\n';
    }

    Assimp::Importer unicode_importer;
    const aiScene* unicode_scene = unicode_importer.ReadFile(
        path_to_utf8(unicode_obj_path),
        aiProcess_ValidateDataStructure);
    failures += require(unicode_scene != nullptr, "Assimp should directly read the Unicode OBJ path");
    if (unicode_scene != nullptr) {
        failures += require(
            has_expected_unicode_material(*unicode_scene),
            "Unicode MTL reference should resolve its material name and diffuse color");
    }

    const auto missing_result = inspector.inspect(asset_directory / "does_not_exist.obj");
    failures += require(!missing_result, "missing file should fail inspection");
    failures += require(
        missing_result.error_code == assetbridge::InspectionErrorCode::file_not_found,
        "missing file should return file_not_found");
    failures += require(!missing_result.summary.has_value(), "missing file should not return a summary");
    failures += require(!missing_result.features.has_value(), "missing file should not return AssetFeatures");
    failures += require(!missing_result.error_message.empty(), "missing file should return an error message");

    const auto invalid_result = inspector.inspect(invalid_obj_path);
    failures += require(!invalid_result, "invalid OBJ should fail inspection");
    failures += require(
        invalid_result.error_code == assetbridge::InspectionErrorCode::import_failed
            || invalid_result.error_code == assetbridge::InspectionErrorCode::invalid_scene,
        "invalid OBJ should return import_failed or invalid_scene");
    failures += require(!invalid_result.summary.has_value(), "invalid OBJ should not return a summary");
    failures += require(!invalid_result.error_message.empty(), "invalid OBJ should return an error message");

    if (failures == 0) {
        std::cout << "All AssetBridge Phase 1B.1 core tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
