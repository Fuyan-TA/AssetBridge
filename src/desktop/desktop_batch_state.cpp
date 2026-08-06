#include "assetbridge/desktop/desktop_batch_state.hpp"

#include <algorithm>
#include <utility>

namespace assetbridge::desktop {

DesktopBatchState::DesktopBatchState(
    batch::BatchPreflightExecutor preflight_executor,
    batch::BatchExecutor conversion_executor)
    : coordinator_(std::move(conversion_executor)),
      preflight_executor_(std::move(preflight_executor)) {}

batch::BatchAddResult DesktopBatchState::add_files(
    const std::vector<std::filesystem::path>& paths) {
    if (busy()) {
        batch::BatchAddResult result;
        for (const auto& path : paths) {
            result.issues.push_back({
                path,
                batch::BatchInputIssueCode::batch_locked,
                "The batch cannot be modified while work is running."
            });
        }
        return result;
    }
    auto result = coordinator_.add_inputs(paths);
    {
        std::scoped_lock lock(mutex_);
        input_issues_ = result.issues;
        if (!selected_job_.has_value() && !result.accepted.empty()) {
            selected_job_ = result.accepted.front();
        }
    }
    return result;
}

bool DesktopBatchState::remove_job(batch::BatchJobId id) {
    if (busy() || !coordinator_.remove_job(id)) return false;
    std::scoped_lock lock(mutex_);
    if (selected_job_ == id) selected_job_.reset();
    return true;
}

bool DesktopBatchState::clear() {
    if (busy() || !coordinator_.clear()) return false;
    std::scoped_lock lock(mutex_);
    selected_job_.reset();
    input_issues_.clear();
    return true;
}

bool DesktopBatchState::set_output_root(std::filesystem::path output_root) {
    return !busy() && coordinator_.set_output_root(std::move(output_root));
}

bool DesktopBatchState::select_job(batch::BatchJobId id) {
    const auto current = coordinator_.snapshot();
    if (std::none_of(current.jobs.begin(), current.jobs.end(), [&](const auto& job) {
            return job.id == id;
        })) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    selected_job_ = id;
    return true;
}

std::optional<batch::BatchJobId> DesktopBatchState::selected_job() const {
    std::scoped_lock lock(mutex_);
    return selected_job_;
}

bool DesktopBatchState::begin_preparation() {
    if (!preflight_executor_) return false;
    const auto current = coordinator_.snapshot();
    const bool has_unprepared = std::any_of(
        current.jobs.begin(), current.jobs.end(), [](const auto& job) {
            return job.status == batch::BatchJobStatus::queued && !job.preview.has_value();
        });
    if (!has_unprepared) return false;
    auto expected = DesktopBatchOperation::idle;
    return operation_.compare_exchange_strong(
        expected,
        DesktopBatchOperation::preparing);
}

void DesktopBatchState::prepare_all() {
    if (operation() != DesktopBatchOperation::preparing) return;
    const auto current = coordinator_.snapshot();
    for (const auto& job : current.jobs) {
        if (job.status == batch::BatchJobStatus::queued && !job.preview.has_value()) {
            (void)coordinator_.prepare_job(job.id, preflight_executor_);
        }
    }
    operation_.store(DesktopBatchOperation::idle);
}

bool DesktopBatchState::begin_batch() {
    if (!can_start_batch()) return false;
    auto expected = DesktopBatchOperation::idle;
    return operation_.compare_exchange_strong(
        expected,
        DesktopBatchOperation::converting);
}

batch::BatchSnapshot DesktopBatchState::run_batch() {
    if (operation() != DesktopBatchOperation::converting) {
        return coordinator_.snapshot();
    }
    const auto completed = coordinator_.run();
    operation_.store(DesktopBatchOperation::idle);
    return completed;
}

bool DesktopBatchState::cancel_after_current() {
    return operation() == DesktopBatchOperation::converting
        && coordinator_.cancel_after_current();
}

bool DesktopBatchState::busy() const noexcept {
    return operation() != DesktopBatchOperation::idle;
}

DesktopBatchOperation DesktopBatchState::operation() const noexcept {
    return operation_.load();
}

bool DesktopBatchState::can_start_batch() const {
    if (busy() || !coordinator_.can_run()) return false;
    const auto current = coordinator_.snapshot();
    return std::all_of(current.jobs.begin(), current.jobs.end(), [](const auto& job) {
        return job.status != batch::BatchJobStatus::queued || job.preview.has_value();
    });
}

batch::BatchSnapshot DesktopBatchState::snapshot() const {
    return coordinator_.snapshot();
}

std::vector<batch::BatchInputIssue> DesktopBatchState::input_issues() const {
    std::scoped_lock lock(mutex_);
    return input_issues_;
}

} // namespace assetbridge::desktop
