#include "assetbridge/core/asset_inspector.hpp"

#include <algorithm>
#include <string>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace assetbridge {
namespace {

std::string to_utf8(const std::filesystem::path& path) {
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

} // namespace

InspectionResult AssetInspector::inspect(const std::filesystem::path& file) const {
    std::error_code file_error;
    if (!std::filesystem::exists(file, file_error) || file_error) {
        return { std::nullopt, "File does not exist: " + to_utf8(file) };
    }

    if (!std::filesystem::is_regular_file(file, file_error) || file_error) {
        return { std::nullopt, "Path is not a regular file: " + to_utf8(file) };
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(to_utf8(file), aiProcess_ValidateDataStructure);
    if (scene == nullptr) {
        return { std::nullopt, "Assimp import failed: " + std::string(importer.GetErrorString()) };
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

    return { std::move(summary), {} };
}

} // namespace assetbridge
