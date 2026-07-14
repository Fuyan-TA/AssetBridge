#pragma once

#include "assetbridge/product/format_capabilities.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace assetbridge {

enum class InspectionErrorCode {
    none,
    file_not_found,
    import_failed,
    invalid_scene
};

[[nodiscard]] std::string_view to_string(InspectionErrorCode code) noexcept;

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
    InspectionErrorCode error_code = InspectionErrorCode::none;
    std::string error_message;
    std::optional<AssetSummary> summary;
    std::optional<AssetFeatures> features;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error_code == InspectionErrorCode::none && summary.has_value();
    }
};

class AssetInspector {
public:
    [[nodiscard]] InspectionResult inspect(const std::filesystem::path& file) const;
};

} // namespace assetbridge
