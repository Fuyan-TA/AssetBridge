#include "assetbridge/core/asset_inspector.hpp"

#include <algorithm>
#include <string>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
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
            std::nullopt
        };
    }

    if (!std::filesystem::is_regular_file(file, file_error) || file_error) {
        return {
            InspectionErrorCode::file_not_found,
            "Path is not a regular file: " + path_to_utf8_for_assimp(file),
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
        std::move(summary)
    };
}

} // namespace assetbridge
