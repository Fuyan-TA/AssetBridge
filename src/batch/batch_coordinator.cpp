#include "assetbridge/batch/batch_coordinator.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <exception>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace assetbridge::batch {
namespace {

bool is_active_status(BatchJobStatus status) {
    switch (status) {
    case BatchJobStatus::inspecting:
    case BatchJobStatus::preflighting:
    case BatchJobStatus::resolving_textures:
    case BatchJobStatus::converting:
    case BatchJobStatus::embedding_textures:
    case BatchJobStatus::validating_geometry:
    case BatchJobStatus::validating_textures:
        return true;
    default:
        return false;
    }
}

bool is_executor_terminal_status(BatchJobStatus status) {
    return status == BatchJobStatus::success
        || status == BatchJobStatus::not_supported
        || status == BatchJobStatus::failed;
}

std::wstring case_folded(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

BatchSummary summarize(const std::vector<BatchJob>& jobs) {
    BatchSummary summary;
    summary.total = jobs.size();
    for (const auto& job : jobs) {
        switch (job.status) {
        case BatchJobStatus::success: ++summary.succeeded; break;
        case BatchJobStatus::not_supported: ++summary.not_supported; break;
        case BatchJobStatus::failed: ++summary.failed; break;
        case BatchJobStatus::canceled: ++summary.canceled; break;
        default: break;
        }
    }
    return summary;
}

BatchRunStatus run_status(
    const std::vector<BatchJob>& jobs,
    bool running,
    bool has_run) {
    if (running) return BatchRunStatus::running;
    if (!has_run) return BatchRunStatus::not_started;

    const auto summary = summarize(jobs);
    if (summary.total > 0 && summary.succeeded == summary.total) {
        return BatchRunStatus::success;
    }
    if (summary.succeeded > 0) {
        return BatchRunStatus::partial;
    }
    if (summary.failed > 0 || summary.not_supported > 0) {
        return BatchRunStatus::failed;
    }
    if (summary.canceled > 0) {
        return BatchRunStatus::canceled;
    }
    return BatchRunStatus::failed;
}

BatchJobResult canceled_result() {
    BatchJobResult result;
    result.error = BatchError {
        "canceled_after_current",
        "Canceled before execution because Cancel After Current was requested."
    };
    return result;
}

} // namespace

std::string_view to_string(BatchJobStatus status) noexcept {
    switch (status) {
    case BatchJobStatus::queued: return "queued";
    case BatchJobStatus::inspecting: return "inspecting";
    case BatchJobStatus::preflighting: return "preflighting";
    case BatchJobStatus::resolving_textures: return "resolving_textures";
    case BatchJobStatus::converting: return "converting";
    case BatchJobStatus::embedding_textures: return "embedding_textures";
    case BatchJobStatus::validating_geometry: return "validating_geometry";
    case BatchJobStatus::validating_textures: return "validating_textures";
    case BatchJobStatus::success: return "success";
    case BatchJobStatus::not_supported: return "not_supported";
    case BatchJobStatus::failed: return "failed";
    case BatchJobStatus::canceled: return "canceled";
    }
    return "failed";
}

std::string_view to_string(BatchRunStatus status) noexcept {
    switch (status) {
    case BatchRunStatus::not_started: return "not_started";
    case BatchRunStatus::running: return "running";
    case BatchRunStatus::success: return "success";
    case BatchRunStatus::partial: return "partial";
    case BatchRunStatus::failed: return "failed";
    case BatchRunStatus::canceled: return "canceled";
    }
    return "failed";
}

std::string_view to_string(BatchInputIssueCode code) noexcept {
    switch (code) {
    case BatchInputIssueCode::duplicate_input: return "duplicate_input";
    case BatchInputIssueCode::not_obj: return "not_obj";
    case BatchInputIssueCode::job_limit_exceeded: return "job_limit_exceeded";
    }
    return "not_obj";
}

std::string batch_job_id_string(BatchJobId id) {
    std::ostringstream output;
    output << "job-" << std::setfill('0') << std::setw(6) << id.value;
    return output.str();
}

BatchCoordinator::BatchCoordinator(BatchExecutor executor)
    : executor_(std::move(executor)) {
    if (!executor_) {
        throw std::invalid_argument("BatchCoordinator requires an executor.");
    }
}

std::filesystem::path BatchCoordinator::normalized_input_key(
    const std::filesystem::path& path) {
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error) {
        error.clear();
        normalized = std::filesystem::absolute(path, error);
        if (error) normalized = path;
        normalized = normalized.lexically_normal();
    }
    return std::filesystem::path(case_folded(normalized.native()));
}

bool BatchCoordinator::is_obj_path(const std::filesystem::path& path) {
    return case_folded(path.extension().native()) == L".obj";
}

BatchAddResult BatchCoordinator::add_inputs(
    const std::vector<std::filesystem::path>& paths) {
    std::scoped_lock lock(mutex_);
    BatchAddResult result;
    if (running_ || has_run_) {
        for (const auto& path : paths) {
            result.issues.push_back({
                path,
                BatchInputIssueCode::job_limit_exceeded,
                "The current batch cannot be modified after execution starts."
            });
        }
        return result;
    }

    std::vector<std::filesystem::path> known;
    known.reserve(jobs_.size() + paths.size());
    for (const auto& job : jobs_) known.push_back(normalized_input_key(job.input_path));

    for (const auto& path : paths) {
        if (!is_obj_path(path)) {
            result.issues.push_back({
                path,
                BatchInputIssueCode::not_obj,
                "Only OBJ files can be added to the verified batch route."
            });
            continue;
        }
        const auto key = normalized_input_key(path);
        if (std::find(known.begin(), known.end(), key) != known.end()) {
            result.issues.push_back({
                path,
                BatchInputIssueCode::duplicate_input,
                "This canonical input path is already in the batch."
            });
            continue;
        }
        if (jobs_.size() >= maximum_batch_jobs) {
            result.issues.push_back({
                path,
                BatchInputIssueCode::job_limit_exceeded,
                "The batch limit of 256 jobs has been reached."
            });
            continue;
        }

        const BatchJobId id { next_id_++ };
        jobs_.push_back({ id, path, BatchJobStatus::queued, std::nullopt });
        known.push_back(key);
        result.accepted.push_back(id);
    }
    return result;
}

std::optional<std::size_t> BatchCoordinator::job_index(BatchJobId id) const {
    for (std::size_t index = 0; index < jobs_.size(); ++index) {
        if (jobs_[index].id == id) return index;
    }
    return std::nullopt;
}

bool BatchCoordinator::remove_job(BatchJobId id) {
    std::scoped_lock lock(mutex_);
    if (running_ || has_run_) return false;
    const auto index = job_index(id);
    if (!index.has_value()) return false;
    jobs_.erase(jobs_.begin() + static_cast<std::ptrdiff_t>(*index));
    return true;
}

bool BatchCoordinator::clear() {
    std::scoped_lock lock(mutex_);
    if (running_ || has_run_) return false;
    jobs_.clear();
    return true;
}

bool BatchCoordinator::set_output_root(std::filesystem::path output_root) {
    std::scoped_lock lock(mutex_);
    if (running_ || has_run_) return false;
    output_root_ = std::move(output_root);
    return true;
}

bool BatchCoordinator::can_run() const {
    std::scoped_lock lock(mutex_);
    return !running_ && !has_run_ && !jobs_.empty() && !output_root_.empty();
}

bool BatchCoordinator::running() const {
    std::scoped_lock lock(mutex_);
    return running_;
}

bool BatchCoordinator::cancel_after_current() {
    std::scoped_lock lock(mutex_);
    if (!running_ || cancel_requested_) return false;
    cancel_requested_ = true;
    return true;
}

BatchSnapshot BatchCoordinator::snapshot() const {
    std::scoped_lock lock(mutex_);
    return {
        run_status(jobs_, running_, has_run_),
        FormatId::glb2,
        output_root_,
        jobs_,
        summarize(jobs_),
        cancel_requested_
    };
}

void BatchCoordinator::update_status(
    BatchJobId id,
    BatchJobStatus status,
    const BatchStateCallback& state_callback) {
    if (!is_active_status(status)) return;
    std::optional<BatchJob> changed;
    {
        std::scoped_lock lock(mutex_);
        const auto index = job_index(id);
        if (!index.has_value()) return;
        auto& job = jobs_[*index];
        if (job.status == BatchJobStatus::success
            || job.status == BatchJobStatus::not_supported
            || job.status == BatchJobStatus::failed
            || job.status == BatchJobStatus::canceled) {
            return;
        }
        job.status = status;
        changed = job;
    }
    if (changed && state_callback) state_callback(*changed);
}

void BatchCoordinator::complete_job(
    BatchJobId id,
    BatchExecutionResult result,
    const BatchStateCallback& state_callback) {
    if (!is_executor_terminal_status(result.status)) {
        result.status = BatchJobStatus::failed;
        result.result.error = BatchError {
            "invalid_executor_result",
            "The batch executor returned a non-terminal result."
        };
    }
    if (!std::isfinite(result.result.duration_ms) || result.result.duration_ms < 0.0) {
        result.result.duration_ms = 0.0;
    }

    std::optional<BatchJob> changed;
    {
        std::scoped_lock lock(mutex_);
        const auto index = job_index(id);
        if (!index.has_value()) return;
        auto& job = jobs_[*index];
        job.status = result.status;
        job.result = std::move(result.result);
        changed = job;
    }
    if (changed && state_callback) state_callback(*changed);
}

void BatchCoordinator::cancel_queued_jobs(
    const BatchStateCallback& state_callback) {
    std::vector<BatchJob> changed;
    {
        std::scoped_lock lock(mutex_);
        for (auto& job : jobs_) {
            if (job.status != BatchJobStatus::queued) continue;
            job.status = BatchJobStatus::canceled;
            job.result = canceled_result();
            changed.push_back(job);
        }
    }
    if (state_callback) {
        for (const auto& job : changed) state_callback(job);
    }
}

BatchSnapshot BatchCoordinator::run(const BatchStateCallback& state_callback) {
    std::vector<BatchJobId> execution_order;
    std::filesystem::path output_root;
    {
        std::scoped_lock lock(mutex_);
        if (running_ || has_run_ || jobs_.empty() || output_root_.empty()) {
            return {
                run_status(jobs_, running_, has_run_),
                FormatId::glb2,
                output_root_,
                jobs_,
                summarize(jobs_),
                cancel_requested_
            };
        }
        running_ = true;
        output_root = output_root_;
        execution_order.reserve(jobs_.size());
        for (const auto& job : jobs_) execution_order.push_back(job.id);
    }

    for (const auto id : execution_order) {
        {
            std::scoped_lock lock(mutex_);
            if (cancel_requested_) break;
        }

        update_status(id, BatchJobStatus::inspecting, state_callback);
        BatchExecutionResult execution;
        try {
            std::filesystem::path input;
            {
                std::scoped_lock lock(mutex_);
                const auto index = job_index(id);
                if (!index.has_value()) continue;
                input = jobs_[*index].input_path;
            }
            execution = executor_(
                { id, std::move(input), FormatId::glb2, output_root },
                [this, id, &state_callback](BatchJobStatus status) {
                    update_status(id, status, state_callback);
                });
        } catch (const std::exception& exception) {
            execution.status = BatchJobStatus::failed;
            execution.result.error = BatchError { "executor_exception", exception.what() };
        } catch (...) {
            execution.status = BatchJobStatus::failed;
            execution.result.error = BatchError {
                "executor_exception",
                "The batch executor threw an unknown exception."
            };
        }
        complete_job(id, std::move(execution), state_callback);
    }

    cancel_queued_jobs(state_callback);
    {
        std::scoped_lock lock(mutex_);
        running_ = false;
        has_run_ = true;
    }
    return snapshot();
}

} // namespace assetbridge::batch
