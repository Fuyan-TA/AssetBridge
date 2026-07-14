#pragma once

#include "assetbridge/core/asset_converter.hpp"

#include <string>
#include <string_view>

namespace assetbridge {

inline constexpr const char* conversion_schema = "assetbridge.conversion.v1";

[[nodiscard]] std::string conversion_report_to_text(const ConversionReport& report);
[[nodiscard]] std::string conversion_report_to_json(const ConversionReport& report);
[[nodiscard]] std::string conversion_argument_error_to_json(
    std::string_view code,
    std::string_view message);

} // namespace assetbridge
