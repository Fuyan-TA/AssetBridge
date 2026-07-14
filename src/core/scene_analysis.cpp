#include "assetbridge/core/scene_analysis.hpp"

#include <unordered_set>

#include <assimp/scene.h>

namespace assetbridge {
namespace {

bool inspect_node(
    const aiNode& node,
    bool is_root,
    std::unordered_set<unsigned int>& referenced_meshes) noexcept {
    if (!is_root
        && (node.mNumChildren > 0 || !node.mTransformation.IsIdentity())) {
        return true;
    }

    if (node.mMeshes != nullptr) {
        for (unsigned int index = 0; index < node.mNumMeshes; ++index) {
            if (!referenced_meshes.insert(node.mMeshes[index]).second) {
                return true;
            }
        }
    }

    if (node.mChildren != nullptr) {
        for (unsigned int index = 0; index < node.mNumChildren; ++index) {
            const aiNode* child = node.mChildren[index];
            if (child != nullptr && inspect_node(*child, false, referenced_meshes)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

bool has_meaningful_node_hierarchy(const aiNode* root) noexcept {
    if (root == nullptr) {
        return false;
    }

    std::unordered_set<unsigned int> referenced_meshes;
    return inspect_node(*root, true, referenced_meshes);
}

} // namespace assetbridge
