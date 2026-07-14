#pragma once

#include "assetbridge/core/asset_inspector.hpp"
#include "assetbridge/core/runtime_capabilities.hpp"

#include <filesystem>
#include <string>

namespace assetbridge {

inline constexpr const char* inspection_schema = "assetbridge.inspect.v1";
inline constexpr const char* capabilities_schema = "assetbridge.capabilities.v1";

[[nodiscard]] std::string inspection_report_to_json(
    const std::filesystem::path& requested_file,
    const InspectionResult& result);

[[nodiscard]] std::string capabilities_to_text(const RuntimeCapabilities& capabilities);
[[nodiscard]] std::string capabilities_to_json(const RuntimeCapabilities& capabilities);

} // namespace assetbridge
