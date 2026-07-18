#include "assetbridge/core/asset_inspector.hpp"
#include "assetbridge/core/scene_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/scene.h>

namespace assetbridge {
namespace {

// Assimp's public ReadFile API accepts narrow strings. On Windows, keep the
// path as native UTF-16 until this boundary, then pass explicit UTF-8 bytes.
std::string path_to_utf8_for_assimp(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

std::filesystem::path normalized_path(const std::filesystem::path& file) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(file, error);
    if (!error) {
        return canonical;
    }

    const auto absolute = std::filesystem::absolute(file, error);
    return error ? file : absolute;
}

bool material_has_pbr_data(const aiMaterial& material) {
    aiColor4D base_color;
    ai_real factor = 0.0;
    return material.Get(AI_MATKEY_BASE_COLOR, base_color) == AI_SUCCESS
        || material.Get(AI_MATKEY_METALLIC_FACTOR, factor) == AI_SUCCESS
        || material.Get(AI_MATKEY_ROUGHNESS_FACTOR, factor) == AI_SUCCESS
        || material.GetTextureCount(aiTextureType_BASE_COLOR) > 0
        || material.GetTextureCount(aiTextureType_METALNESS) > 0
        || material.GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0
        || material.GetTextureCount(aiTextureType_GLTF_METALLIC_ROUGHNESS) > 0;
}

std::string assimp_string(const aiString& value) {
    return { value.C_Str(), value.length };
}

AssetFeatures analyze_scene(const aiScene& scene) {
    AssetFeatures features;
    features.mesh_count = scene.mNumMeshes;
    features.has_node_hierarchy = has_meaningful_node_hierarchy(scene.mRootNode);
    features.embedded_texture_count = scene.mNumTextures;

    std::set<unsigned int> referenced_materials;
    std::unordered_set<std::string> unique_bones;
    std::set<std::string> external_textures;

    for (unsigned int mesh_index = 0; mesh_index < scene.mNumMeshes; ++mesh_index) {
        const aiMesh* mesh = scene.mMeshes[mesh_index];
        if (mesh == nullptr) {
            continue;
        }

        features.meshes_with_normals += mesh->HasNormals() ? 1U : 0U;
        features.meshes_with_tangents += mesh->HasTangentsAndBitangents() ? 1U : 0U;
        features.max_uv_channel_count = std::max(
            features.max_uv_channel_count,
            static_cast<std::uint32_t>(mesh->GetNumUVChannels()));
        features.meshes_with_vertex_colors += mesh->GetNumColorChannels() > 0 ? 1U : 0U;

        if (mesh->mMaterialIndex < scene.mNumMaterials) {
            referenced_materials.insert(mesh->mMaterialIndex);
        }

        if (mesh->mNumBones > 0) {
            ++features.skinned_mesh_count;
            std::vector<std::uint32_t> weights_per_vertex(mesh->mNumVertices, 0);
            for (unsigned int bone_index = 0; bone_index < mesh->mNumBones; ++bone_index) {
                const aiBone* bone = mesh->mBones[bone_index];
                if (bone == nullptr) {
                    continue;
                }
                unique_bones.insert(assimp_string(bone->mName));
                for (unsigned int weight_index = 0; weight_index < bone->mNumWeights; ++weight_index) {
                    const unsigned int vertex = bone->mWeights[weight_index].mVertexId;
                    if (vertex < weights_per_vertex.size()) {
                        ++weights_per_vertex[vertex];
                    }
                }
            }
            for (const auto weight_count : weights_per_vertex) {
                features.max_weights_per_vertex = std::max(
                    features.max_weights_per_vertex,
                    weight_count);
            }
        }

        for (unsigned int morph_index = 0; morph_index < mesh->mNumAnimMeshes; ++morph_index) {
            const aiAnimMesh* morph = mesh->mAnimMeshes[morph_index];
            if (morph == nullptr) {
                continue;
            }
            auto name = assimp_string(morph->mName);
            if (name.empty()) {
                name = "Mesh" + std::to_string(mesh_index)
                    + "_Morph" + std::to_string(morph_index);
            }
            features.morph_target_names.push_back(std::move(name));
        }
    }

    features.referenced_material_count = referenced_materials.size();
    features.bone_count = unique_bones.size();

    for (const unsigned int material_index : referenced_materials) {
        const aiMaterial* material = scene.mMaterials[material_index];
        if (material == nullptr) {
            continue;
        }
        aiString material_name;
        if (material->Get(AI_MATKEY_NAME, material_name) == AI_SUCCESS) {
            features.referenced_material_names.push_back(assimp_string(material_name));
        } else {
            features.referenced_material_names.emplace_back();
        }
        features.has_pbr_materials =
            features.has_pbr_materials || material_has_pbr_data(*material);

        for (int texture_type = aiTextureType_NONE;
             texture_type <= AI_TEXTURE_TYPE_MAX;
             ++texture_type) {
            const auto type = static_cast<aiTextureType>(texture_type);
            const unsigned int texture_count = material->GetTextureCount(type);
            for (unsigned int texture_index = 0; texture_index < texture_count; ++texture_index) {
                aiString texture_path;
                if (material->GetTexture(type, texture_index, &texture_path) != AI_SUCCESS) {
                    continue;
                }
                const auto path = assimp_string(texture_path);
                if (!path.empty() && path.front() != '*') {
                    external_textures.insert(path);
                }
            }
        }
    }
    features.external_texture_references.assign(
        external_textures.begin(),
        external_textures.end());

    for (unsigned int animation_index = 0;
         animation_index < scene.mNumAnimations;
         ++animation_index) {
        const aiAnimation* animation = scene.mAnimations[animation_index];
        if (animation == nullptr) {
            continue;
        }
        auto name = assimp_string(animation->mName);
        if (name.empty()) {
            name = "Animation_" + std::to_string(animation_index);
        }
        const double duration_ticks = animation->mDuration;
        const double ticks_per_second = animation->mTicksPerSecond;
        features.animations.push_back({
            std::move(name),
            animation_duration_seconds(duration_ticks, ticks_per_second),
            std::isfinite(duration_ticks)
                ? std::optional<double>(duration_ticks)
                : std::nullopt,
            std::isfinite(ticks_per_second)
                ? std::optional<double>(ticks_per_second)
                : std::nullopt
        });
    }

    return features;
}

} // namespace

std::string_view to_string(InspectionErrorCode code) noexcept {
    switch (code) {
    case InspectionErrorCode::none:
        return "none";
    case InspectionErrorCode::file_not_found:
        return "file_not_found";
    case InspectionErrorCode::import_failed:
        return "import_failed";
    case InspectionErrorCode::invalid_scene:
        return "invalid_scene";
    }

    return "unknown";
}

InspectionResult AssetInspector::inspect(const std::filesystem::path& file) const {
    std::error_code file_error;
    if (!std::filesystem::exists(file, file_error) || file_error) {
        return {
            InspectionErrorCode::file_not_found,
            "File does not exist: " + path_to_utf8_for_assimp(file),
            std::nullopt,
            std::nullopt
        };
    }

    if (!std::filesystem::is_regular_file(file, file_error) || file_error) {
        return {
            InspectionErrorCode::file_not_found,
            "Path is not a regular file: " + path_to_utf8_for_assimp(file),
            std::nullopt,
            std::nullopt
        };
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path_to_utf8_for_assimp(file),
        aiProcess_ValidateDataStructure);
    if (scene == nullptr) {
        return {
            InspectionErrorCode::import_failed,
            "Assimp import failed: " + std::string(importer.GetErrorString()),
            std::nullopt,
            std::nullopt
        };
    }

    if ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0
        || scene->mRootNode == nullptr
        || scene->mNumMeshes == 0
        || scene->mMeshes == nullptr) {
        return {
            InspectionErrorCode::invalid_scene,
            "Assimp returned an incomplete scene without inspectable meshes.",
            std::nullopt,
            std::nullopt
        };
    }

    AssetSummary summary;
    summary.file_path = normalized_path(file);
    summary.mesh_count = scene->mNumMeshes;
    summary.material_count = scene->mNumMaterials;

    for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index) {
        const aiMesh* mesh = scene->mMeshes[mesh_index];
        if (mesh == nullptr) {
            continue;
        }

        summary.vertex_count += mesh->mNumVertices;
        summary.face_count += mesh->mNumFaces;
        summary.has_normals = summary.has_normals || mesh->HasNormals();
        summary.uv_channel_count = std::max(
            summary.uv_channel_count,
            static_cast<std::uint32_t>(mesh->GetNumUVChannels()));

        for (unsigned int face_index = 0; face_index < mesh->mNumFaces; ++face_index) {
            if (mesh->mFaces[face_index].mNumIndices == 3) {
                ++summary.triangle_count;
            }
        }
    }

    return {
        InspectionErrorCode::none,
        {},
        std::move(summary),
        analyze_scene(*scene)
    };
}

} // namespace assetbridge
