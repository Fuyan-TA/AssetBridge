#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace assetbridge {

struct AssetSummary {
    std::filesystem::path file_path;
    std::uint64_t mesh_count = 0;
    std::uint64_t vertex_count = 0;
    std::uint64_t face_count = 0;
    std::uint64_t triangle_count = 0;
    std::uint64_t material_count = 0;
    std::uint32_t uv_channel_count = 0;
    bool has_normals = false;
};

struct InspectionResult {
    std::optional<AssetSummary> summary;
    std::string error_message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return summary.has_value();
    }
};

class AssetInspector {
public:
    [[nodiscard]] InspectionResult inspect(const std::filesystem::path& file) const;
};

} // namespace assetbridge
