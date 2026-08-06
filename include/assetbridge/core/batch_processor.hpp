#pragma once

#include "assetbridge/batch/batch_coordinator.hpp"

#include <string>

namespace assetbridge {

[[nodiscard]] batch::BatchExecutor make_asset_batch_executor();
[[nodiscard]] batch::BatchPreflightExecutor make_asset_batch_preflight_executor();

[[nodiscard]] bool write_batch_report(
    const batch::BatchSnapshot& snapshot,
    std::string& error_message);

} // namespace assetbridge
