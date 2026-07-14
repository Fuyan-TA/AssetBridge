#pragma once

#include "assetbridge/core/preflight_report.hpp"

#include <string>
#include <string_view>

namespace assetbridge {

inline constexpr const char* preflight_schema = "assetbridge.preflight.v1";

[[nodiscard]] std::string preflight_to_text(const PreflightReport& report);
[[nodiscard]] std::string preflight_to_json(const PreflightReport& report);

[[nodiscard]] std::string unknown_target_to_text(std::string_view requested_target);
[[nodiscard]] std::string unknown_target_to_json(std::string_view requested_target);

} // namespace assetbridge
