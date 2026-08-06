#pragma once

#include "assetbridge/product/format_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace assetbridge::batch {

inline constexpr std::size_t maximum_batch_jobs = 256;

struct BatchJobId {
    std::uint64_t value = 0;

    friend bool operator==(BatchJobId, BatchJobId) = default;
};

enum class BatchJobStatus {
    queued,
    inspecting,
    preflighting,
    resolving_textures,
    converting,
    embedding_textures,
    validating_geometry,
    validating_textures,
    success,
    not_supported,
    failed,
    canceled
};

enum class BatchRunStatus {
    not_started,
    running,
    success,
    partial,
    failed,
    canceled
};

enum class BatchInputIssueCode {
    duplicate_input,
    not_obj,
    job_limit_exceeded,
    batch_locked
};

[[nodiscard]] std::string_view to_string(BatchJobStatus status) noexcept;
[[nodiscard]] std::string_view to_string(BatchRunStatus status) noexcept;
[[nodiscard]] std::string_view to_string(BatchInputIssueCode code) noexcept;
[[nodiscard]] std::string batch_job_id_string(BatchJobId id);

struct BatchError {
    std::string code;
    std::string message;
};

struct BatchJobDiagnostics {
    std::uint64_t triangle_count = 0;
    std::uint64_t material_count = 0;
    std::uint64_t embedded_image_count = 0;
    std::uint64_t validation_failure_count = 0;
};

struct BatchJobResult {
    std::optional<std::filesystem::path> output_directory;
    std::optional<std::filesystem::path> glb_path;
    std::optional<std::filesystem::path> conversion_report_path;
    std::optional<BatchError> error;
    double duration_ms = 0.0;
    BatchJobDiagnostics diagnostics;
};

struct BatchJobPreview {
    std::uint64_t mesh_count = 0;
    std::uint64_t face_count = 0;
    std::uint64_t triangle_count = 0;
    std::uint64_t material_count = 0;
    std::uint64_t texture_count = 0;
    bool preflight_safe = false;
    std::vector<BatchError> diagnostics;
};

struct BatchJob {
    BatchJobId id;
    std::filesystem::path input_path;
    BatchJobStatus status = BatchJobStatus::queued;
    std::optional<BatchJobResult> result;
    std::optional<BatchJobPreview> preview;
};

struct BatchSummary {
    std::size_t total = 0;
    std::size_t succeeded = 0;
    std::size_t not_supported = 0;
    std::size_t failed = 0;
    std::size_t canceled = 0;
};

struct BatchInputIssue {
    std::filesystem::path input_path;
    BatchInputIssueCode code = BatchInputIssueCode::not_obj;
    std::string message;
};

struct BatchAddResult {
    std::vector<BatchJobId> accepted;
    std::vector<BatchInputIssue> issues;
};

struct BatchSnapshot {
    BatchRunStatus status = BatchRunStatus::not_started;
    FormatId target_format = FormatId::glb2;
    std::filesystem::path output_root;
    std::vector<BatchJob> jobs;
    BatchSummary summary;
    bool cancel_after_current_requested = false;
};

struct BatchExecutionRequest {
    BatchJobId id;
    std::filesystem::path input_path;
    FormatId target_format = FormatId::glb2;
    std::filesystem::path output_root;
};

struct BatchExecutionResult {
    BatchJobStatus status = BatchJobStatus::failed;
    BatchJobResult result;
};

struct BatchPreflightResult {
    BatchJobStatus status = BatchJobStatus::failed;
    BatchJobPreview preview;
    std::optional<BatchError> error;
};

using BatchProgressCallback = std::function<void(BatchJobStatus)>;
using BatchExecutor = std::function<BatchExecutionResult(
    const BatchExecutionRequest&,
    const BatchProgressCallback&)>;
using BatchStateCallback = std::function<void(const BatchJob&)>;
using BatchPreflightExecutor = std::function<BatchPreflightResult(
    const std::filesystem::path&,
    const BatchProgressCallback&)>;

class BatchCoordinator {
public:
    explicit BatchCoordinator(BatchExecutor executor);

    [[nodiscard]] BatchAddResult add_inputs(
        const std::vector<std::filesystem::path>& paths);
    [[nodiscard]] bool remove_job(BatchJobId id);
    [[nodiscard]] bool clear();
    [[nodiscard]] bool set_output_root(std::filesystem::path output_root);
    [[nodiscard]] bool can_run() const;
    [[nodiscard]] bool running() const;
    [[nodiscard]] bool cancel_after_current();
    [[nodiscard]] BatchSnapshot snapshot() const;
    [[nodiscard]] bool prepare_job(
        BatchJobId id,
        const BatchPreflightExecutor& preflight_executor,
        const BatchStateCallback& state_callback = {});

    BatchSnapshot run(const BatchStateCallback& state_callback = {});

private:
    [[nodiscard]] static std::filesystem::path normalized_input_key(
        const std::filesystem::path& path);
    [[nodiscard]] static bool is_obj_path(const std::filesystem::path& path);
    [[nodiscard]] std::optional<std::size_t> job_index(BatchJobId id) const;
    void update_status(
        BatchJobId id,
        BatchJobStatus status,
        const BatchStateCallback& state_callback);
    void complete_job(
        BatchJobId id,
        BatchExecutionResult result,
        const BatchStateCallback& state_callback);
    void cancel_queued_jobs(const BatchStateCallback& state_callback);

    BatchExecutor executor_;
    mutable std::mutex mutex_;
    std::vector<BatchJob> jobs_;
    std::filesystem::path output_root_;
    std::uint64_t next_id_ = 1;
    bool running_ = false;
    bool has_run_ = false;
    bool cancel_requested_ = false;
};

} // namespace assetbridge::batch
