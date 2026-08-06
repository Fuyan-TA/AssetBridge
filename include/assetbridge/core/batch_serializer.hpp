#pragma once

#include "assetbridge/batch/batch_coordinator.hpp"

#include <string>
#include <string_view>

namespace assetbridge {

inline constexpr const char* batch_schema = "assetbridge.batch.v1";

[[nodiscard]] std::string batch_report_to_text(const batch::BatchSnapshot& snapshot);
[[nodiscard]] std::string batch_report_to_json(const batch::BatchSnapshot& snapshot);
[[nodiscard]] std::string batch_argument_error_to_json(
    std::string_view code,
    std::string_view message);

} // namespace assetbridge
