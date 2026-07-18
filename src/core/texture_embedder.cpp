#include "assetbridge/core/texture_embedder.hpp"

#include <assimp/material.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace assetbridge {
namespace {

std::string assimp_string(const aiString& value) {
    return { value.C_Str(), value.length };
}

std::string material_name(const aiMaterial& material) {
    aiString value;
    return material.Get(AI_MATKEY_NAME, value) == AI_SUCCESS
        ? assimp_string(value)
        : std::string {};
}

std::optional<std::map<std::string, unsigned int>> referenced_materials(
    const aiScene& scene,
    std::string& error) {
    if (scene.mMeshes == nullptr || scene.mMaterials == nullptr) {
        error = "Scene does not contain mesh and material arrays.";
        return std::nullopt;
    }
    std::set<unsigned int> referenced_indices;
    for (unsigned int mesh_index = 0; mesh_index < scene.mNumMeshes; ++mesh_index) {
        const aiMesh* mesh = scene.mMeshes[mesh_index];
        if (mesh == nullptr || mesh->mMaterialIndex >= scene.mNumMaterials
            || scene.mMaterials[mesh->mMaterialIndex] == nullptr) {
            error = "A source mesh references a missing material.";
            return std::nullopt;
        }
        referenced_indices.insert(mesh->mMaterialIndex);
    }

    std::map<std::string, unsigned int> result;
    for (const auto index : referenced_indices) {
        const auto name = material_name(*scene.mMaterials[index]);
        if (name.empty()) {
            error = "A referenced material has no stable name.";
            return std::nullopt;
        }
        if (!result.emplace(name, index).second) {
            error = "Referenced material names are not unique: " + name;
            return std::nullopt;
        }
    }
    return result;
}

std::unique_ptr<aiTexture> make_embedded_texture(const ResolvedTexture& source) {
    if (source.bytes.empty()) return nullptr;
    auto texture = std::make_unique<aiTexture>();
    texture->mWidth = static_cast<unsigned int>(source.bytes.size());
    texture->mHeight = 0;
    const auto texel_count =
        (source.bytes.size() + sizeof(aiTexel) - 1) / sizeof(aiTexel);
    texture->pcData = new aiTexel[texel_count] {};
    std::memcpy(texture->pcData, source.bytes.data(), source.bytes.size());

    const char* hint = source.format == TextureFileFormat::png ? "png" : "jpg";
    std::memcpy(texture->achFormatHint, hint, std::strlen(hint));
    texture->mFilename.Set(source.asset_relative_key);
    return texture;
}

bool rewrite_material_texture(
    aiMaterial& material,
    std::size_t embedded_index) {
    const aiString embedded_reference(
        ("*" + std::to_string(embedded_index)).c_str());
    material.RemoveProperty(AI_MATKEY_TEXTURE_DIFFUSE(0));
    return material.AddProperty(
        &embedded_reference,
        AI_MATKEY_TEXTURE_DIFFUSE(0)) == AI_SUCCESS;
}

} // namespace

TextureEmbeddingResult embed_base_color_textures(
    aiScene& scene,
    const CompanionResolution& companions) {
    TextureEmbeddingResult result;
    if (!companions.safe()) {
        result.error = "Companion resolution contains blocking issues.";
        return result;
    }
    if (scene.mNumTextures != 0 || scene.mTextures != nullptr) {
        result.error = "Source scene already contains embedded textures outside the verified OBJ route.";
        return result;
    }

    auto materials = referenced_materials(scene, result.error);
    if (!materials.has_value()) return result;
    if (materials->size() != companions.referenced_material_count
        || companions.material_bindings.size() != companions.referenced_material_count) {
        result.error = "Resolved companion materials do not match the scene-referenced material set.";
        return result;
    }

    for (const auto& binding : companions.material_bindings) {
        if (!materials->contains(binding.material_name)) {
            result.error = "Resolved material is absent from the export-ready scene: "
                + binding.material_name;
            return result;
        }
        if (binding.resolved_texture_index.has_value()
            && *binding.resolved_texture_index >= companions.textures.size()) {
            result.error = "Resolved material references an invalid texture index.";
            return result;
        }
    }

    std::vector<std::unique_ptr<aiTexture>> prepared;
    prepared.reserve(companions.textures.size());
    for (const auto& source : companions.textures) {
        auto texture = make_embedded_texture(source);
        if (!texture) {
            result.error = "Resolved texture payload is empty.";
            return result;
        }
        prepared.push_back(std::move(texture));
    }

    if (!prepared.empty()) {
        auto textures = std::make_unique<aiTexture*[]>(prepared.size());
        for (std::size_t index = 0; index < prepared.size(); ++index) {
            textures[index] = prepared[index].release();
        }
        scene.mTextures = textures.release();
        scene.mNumTextures = static_cast<unsigned int>(prepared.size());
    }

    for (const auto& binding : companions.material_bindings) {
        if (!binding.resolved_texture_index.has_value()) continue;
        aiMaterial* material = scene.mMaterials[materials->at(binding.material_name)];
        if (!rewrite_material_texture(*material, *binding.resolved_texture_index)) {
            result.error = "Could not rewrite material to an embedded base-color texture: "
                + binding.material_name;
            return result;
        }
        ++result.rewritten_material_count;
    }

    result.success = true;
    result.embedded_texture_count = companions.textures.size();
    return result;
}

} // namespace assetbridge
