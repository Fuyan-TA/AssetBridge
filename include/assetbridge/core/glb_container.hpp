#pragma once

#include "assetbridge/core/companion_resolver.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace assetbridge {

struct GlbContainerCheck {
    std::string name;
    bool passed = false;
    std::string details;
};

struct GlbContainerValidation {
    bool valid = false;
    std::size_t image_count = 0;
    std::size_t texture_count = 0;
    std::size_t material_count = 0;
    std::vector<GlbContainerCheck> checks;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return valid; }
};

[[nodiscard]] GlbContainerValidation validate_glb_container(
    const std::filesystem::path& file,
    const CompanionResolution& expected);

} // namespace assetbridge
