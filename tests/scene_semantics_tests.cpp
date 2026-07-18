#include "assetbridge/core/asset_inspector.hpp"
#include "assetbridge/core/preflight_serializer.hpp"
#include "assetbridge/core/scene_analysis.hpp"
#include "assetbridge/product/format_capabilities.hpp"
#include "assetbridge/product/loss_preflight.hpp"

#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>

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

void set_children(aiNode& parent, std::initializer_list<aiNode*> children) {
    parent.mNumChildren = static_cast<unsigned int>(children.size());
    parent.mChildren = new aiNode*[parent.mNumChildren];
    unsigned int index = 0;
    for (aiNode* child : children) {
        parent.mChildren[index++] = child;
        child->mParent = &parent;
    }
}

void set_meshes(aiNode& node, std::initializer_list<unsigned int> meshes) {
    node.mNumMeshes = static_cast<unsigned int>(meshes.size());
    node.mMeshes = new unsigned int[node.mNumMeshes];
    unsigned int index = 0;
    for (const unsigned int mesh : meshes) {
        node.mMeshes[index++] = mesh;
    }
}

} // namespace

int main() {
    using namespace assetbridge;
    int failures = 0;

    const auto minimal_obj =
        std::filesystem::current_path() / "tests" / "assets" / "minimal_triangle.obj";
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path_to_utf8(minimal_obj),
        aiProcess_ValidateDataStructure);
    failures += require(scene != nullptr, "minimal OBJ should import for node-tree inspection");
    if (scene != nullptr && scene->mRootNode != nullptr) {
        const aiNode& root = *scene->mRootNode;
        failures += require(
            root.mNumChildren == 1,
            "Assimp minimal OBJ should have one flat wrapper child");
        if (root.mNumChildren == 1 && root.mChildren[0] != nullptr) {
            const aiNode& mesh_node = *root.mChildren[0];
            failures += require(
                mesh_node.mNumChildren == 0,
                "minimal OBJ wrapper child should not be nested");
            failures += require(
                mesh_node.mTransformation.IsIdentity(),
                "minimal OBJ wrapper child should have an identity transform");
            failures += require(
                mesh_node.mNumMeshes == 1,
                "minimal OBJ wrapper child should reference one mesh");
        }
        failures += require(
            !has_meaningful_node_hierarchy(scene->mRootNode),
            "root to one identity mesh wrapper is not meaningful hierarchy");
    }

    const AssetInspector inspector;
    const auto inspection = inspector.inspect(minimal_obj);
    failures += require(static_cast<bool>(inspection), "minimal OBJ inspection should succeed");
    failures += require(
        inspection.features.has_value() && !inspection.features->has_node_hierarchy,
        "minimal OBJ AssetFeatures should report meaningful hierarchy as false");

    {
        aiNode root("Root");
        auto* mesh_a = new aiNode("MeshA");
        auto* mesh_b = new aiNode("MeshB");
        set_meshes(*mesh_a, { 0 });
        set_meshes(*mesh_b, { 1 });
        set_children(root, { mesh_a, mesh_b });
        failures += require(
            !has_meaningful_node_hierarchy(&root),
            "flat identity sibling meshes should use multiple_meshes, not hierarchy");
    }

    {
        aiNode root("Root");
        auto* group = new aiNode("Group");
        auto* mesh = new aiNode("Mesh");
        set_meshes(*mesh, { 0 });
        set_children(*group, { mesh });
        set_children(root, { group });
        failures += require(
            has_meaningful_node_hierarchy(&root),
            "nested non-root nodes should be meaningful hierarchy");
    }

    {
        aiNode root("Root");
        auto* mesh = new aiNode("TransformedMesh");
        mesh->mTransformation.a4 = 2.0F;
        set_meshes(*mesh, { 0 });
        set_children(root, { mesh });
        failures += require(
            has_meaningful_node_hierarchy(&root),
            "a non-identity child transform should be meaningful hierarchy");
    }

    {
        aiNode root("Root");
        auto* instance_a = new aiNode("InstanceA");
        auto* instance_b = new aiNode("InstanceB");
        set_meshes(*instance_a, { 0 });
        set_meshes(*instance_b, { 0 });
        set_children(root, { instance_a, instance_b });
        failures += require(
            has_meaningful_node_hierarchy(&root),
            "repeated node references to one mesh should preserve instancing semantics");
    }

    AssetFeatures animation_features;
    animation_features.mesh_count = 1;
    animation_features.animations.push_back({
        "ZeroTickRate",
        animation_duration_seconds(120.0, 0.0),
        120.0,
        0.0
    });
    const auto animation_decision = evaluate_preflight(
        FormatId::glb2,
        animation_features,
        true);
    PreflightReport animation_report;
    animation_report.file_path = "synthetic_animation.fbx";
    animation_report.source = { std::nullopt, false, false };
    animation_report.error_code = InspectionErrorCode::none;
    animation_report.features = animation_features;
    animation_report.decision = animation_decision;

    const auto animation_json = nlohmann::json::parse(preflight_to_json(animation_report));
    const auto& animation = animation_json.at("features").at("animations").at(0);
    failures += require(
        animation.at("duration_seconds").is_null(),
        "zero tick rate should serialize duration_seconds as null");
    failures += require(
        animation.at("duration_ticks") == 120.0,
        "tick duration should remain reportable at zero tick rate");
    failures += require(
        animation.at("ticks_per_second") == 0.0,
        "zero tick rate should remain reportable as zero");

    const auto animation_text = preflight_to_text(animation_report);
    failures += require(
        animation_text.find("duration_seconds=unknown") != std::string::npos,
        "text report should mark seconds as unknown at zero tick rate");
    failures += require(
        animation_text.find("duration_ticks=120") != std::string::npos,
        "text report should retain tick duration");
    failures += require(
        animation_text.find("ticks_per_second=0") != std::string::npos,
        "text report should retain zero tick rate");

    if (failures == 0) {
        std::cout << "All AssetBridge scene semantics tests passed.\n";
        return 0;
    }
    std::cerr << failures << " scene semantics assertion(s) failed.\n";
    return 1;
}
