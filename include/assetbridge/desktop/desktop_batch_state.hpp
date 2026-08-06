#pragma once

#include "assetbridge/batch/batch_coordinator.hpp"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

namespace assetbridge::desktop {

enum class DesktopBatchOperation {
    idle,
    preparing,
    converting
};

class DesktopBatchState {
public:
    DesktopBatchState(
        batch::BatchPreflightExecutor preflight_executor,
        batch::BatchExecutor conversion_executor);

    [[nodiscard]] batch::BatchAddResult add_files(
        const std::vector<std::filesystem::path>& paths);
    [[nodiscard]] bool remove_job(batch::BatchJobId id);
    [[nodiscard]] bool clear();
    [[nodiscard]] bool set_output_root(std::filesystem::path output_root);
    [[nodiscard]] bool select_job(batch::BatchJobId id);
    [[nodiscard]] std::optional<batch::BatchJobId> selected_job() const;

    [[nodiscard]] bool begin_preparation();
    void prepare_all();
    [[nodiscard]] bool begin_batch();
    [[nodiscard]] batch::BatchSnapshot run_batch();
    [[nodiscard]] bool cancel_after_current();

    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] DesktopBatchOperation operation() const noexcept;
    [[nodiscard]] bool can_start_batch() const;
    [[nodiscard]] batch::BatchSnapshot snapshot() const;
    [[nodiscard]] std::vector<batch::BatchInputIssue> input_issues() const;

private:
    batch::BatchCoordinator coordinator_;
    batch::BatchPreflightExecutor preflight_executor_;
    std::atomic<DesktopBatchOperation> operation_ = DesktopBatchOperation::idle;
    mutable std::mutex mutex_;
    std::vector<batch::BatchInputIssue> input_issues_;
    std::optional<batch::BatchJobId> selected_job_;
};

} // namespace assetbridge::desktop
