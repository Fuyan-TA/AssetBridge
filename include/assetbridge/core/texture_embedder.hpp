#pragma once

#include "assetbridge/core/companion_resolver.hpp"

#include <cstddef>
#include <string>

struct aiScene;

namespace assetbridge {

struct TextureEmbeddingResult {
    bool success = false;
    std::size_t embedded_texture_count = 0;
    std::size_t rewritten_material_count = 0;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

[[nodiscard]] TextureEmbeddingResult embed_base_color_textures(
    aiScene& scene,
    const CompanionResolution& companions);

} // namespace assetbridge
