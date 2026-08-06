#include "assetbridge/batch/batch_coordinator.hpp"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

int require(bool condition, std::string_view message) {
    if (condition) return 0;
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

assetbridge::batch::BatchExecutionResult success_result(
    const assetbridge::batch::BatchExecutionRequest& request) {
    assetbridge::batch::BatchExecutionResult result;
    result.status = assetbridge::batch::BatchJobStatus::success;
    result.result.output_directory = request.output_root / request.input_path.stem();
    result.result.glb_path = *result.result.output_directory
        / (request.input_path.stem().wstring() + L".glb");
    result.result.conversion_report_path = *result.result.output_directory
        / "conversion-report.json";
    result.result.duration_ms = 1.0;
    result.result.diagnostics.triangle_count = 1;
    return result;
}

} // namespace

int main() {
    using namespace assetbridge::batch;
    int failures = 0;

    std::atomic_int active = 0;
    std::atomic_int peak_active = 0;
    std::vector<std::wstring> execution_order;
    BatchCoordinator sequential([&](
        const BatchExecutionRequest& request,
        const BatchProgressCallback& progress) {
        const int now_active = ++active;
        peak_active.store(std::max(peak_active.load(), now_active));
        execution_order.push_back(request.input_path.filename().wstring());
        progress(BatchJobStatus::preflighting);
        progress(BatchJobStatus::converting);
        --active;
        return success_result(request);
    });

    const auto add = sequential.add_inputs({ L"first.obj", L"second.OBJ" });
    failures += require(add.accepted.size() == 2, "two OBJ inputs should be accepted");
    failures += require(
        add.accepted[0].value == 1 && add.accepted[1].value == 2,
        "job IDs should be stable and monotonic");
    failures += require(
        batch_job_id_string(add.accepted[0]) == "job-000001",
        "job ID text should be stable");
    failures += require(!sequential.can_run(), "an output root is required");
    failures += require(
        sequential.set_output_root(L"batch-output"),
        "output root should be mutable before execution");
    failures += require(sequential.can_run(), "a populated batch should be runnable");

    std::vector<BatchJobStatus> callbacks;
    const auto completed = sequential.run([&](const BatchJob& job) {
        callbacks.push_back(job.status);
    });
    failures += require(
        completed.status == BatchRunStatus::success,
        "an all-success batch should report success");
    failures += require(
        completed.summary.total == 2 && completed.summary.succeeded == 2,
        "success summary should match terminal jobs");
    failures += require(
        execution_order == std::vector<std::wstring>{ L"first.obj", L"second.OBJ" },
        "input order should be preserved");
    failures += require(peak_active.load() == 1, "the coordinator must execute sequentially");
    failures += require(
        std::count(callbacks.begin(), callbacks.end(), BatchJobStatus::success) == 2,
        "each successful job should emit one terminal callback");
    failures += require(!sequential.can_run(), "a completed batch must not start twice");

    BatchCoordinator input_rules([](const auto& request, const auto&) {
        return success_result(request);
    });
    const auto mixed_add = input_rules.add_inputs({
        L"duplicate.obj", L".\\duplicate.obj", L"not-an-obj.glb"
    });
    failures += require(mixed_add.accepted.size() == 1, "duplicates must be deduplicated");
    failures += require(
        mixed_add.issues.size() == 2
            && mixed_add.issues[0].code == BatchInputIssueCode::duplicate_input
            && mixed_add.issues[1].code == BatchInputIssueCode::not_obj,
        "duplicate and non-OBJ inputs need stable issue codes");

    std::vector<std::filesystem::path> over_limit;
    over_limit.reserve(maximum_batch_jobs + 1);
    for (std::size_t index = 0; index <= maximum_batch_jobs; ++index) {
        over_limit.push_back(L"limit-" + std::to_wstring(index) + L".obj");
    }
    BatchCoordinator limited([](const auto& request, const auto&) {
        return success_result(request);
    });
    const auto limit_add = limited.add_inputs(over_limit);
    failures += require(
        limit_add.accepted.size() == maximum_batch_jobs
            && limit_add.issues.size() == 1
            && limit_add.issues.front().code == BatchInputIssueCode::job_limit_exceeded,
        "the 256-job limit should reject only the excess input");

    BatchCoordinator failure_isolation([](
        const BatchExecutionRequest& request,
        const BatchProgressCallback& progress) {
        progress(BatchJobStatus::preflighting);
        if (request.input_path.filename() == L"unsupported.obj") {
            BatchExecutionResult result;
            result.status = BatchJobStatus::not_supported;
            result.result.error = BatchError {
                "route_feature_unverified", "Unsupported test feature."
            };
            return result;
        }
        if (request.input_path.filename() == L"throws.obj") {
            throw std::runtime_error("synthetic executor failure");
        }
        return success_result(request);
    });
    (void)failure_isolation.add_inputs({
        L"first.obj", L"unsupported.obj", L"throws.obj", L"last.obj"
    });
    (void)failure_isolation.set_output_root(L"isolation-output");
    const auto isolated = failure_isolation.run();
    failures += require(
        isolated.status == BatchRunStatus::partial
            && isolated.summary.succeeded == 2
            && isolated.summary.not_supported == 1
            && isolated.summary.failed == 1,
        "unsupported and failed jobs must not prevent later execution");
    failures += require(
        isolated.jobs[2].result->error->code == "executor_exception",
        "executor exceptions should become structured failures");

    BatchCoordinator* cancel_pointer = nullptr;
    BatchCoordinator canceling([&](
        const BatchExecutionRequest& request,
        const BatchProgressCallback&) {
        if (request.id.value == 1) {
            failures += require(
                cancel_pointer->cancel_after_current(),
                "Cancel After Current should be accepted during execution");
        }
        return success_result(request);
    });
    cancel_pointer = &canceling;
    (void)canceling.add_inputs({ L"current.obj", L"queued-a.obj", L"queued-b.obj" });
    (void)canceling.set_output_root(L"cancel-output");
    const auto canceled = canceling.run();
    failures += require(
        canceled.status == BatchRunStatus::partial
            && canceled.summary.succeeded == 1
            && canceled.summary.canceled == 2,
        "Cancel After Current should preserve current success and cancel queued jobs");
    failures += require(
        canceled.jobs[1].result->error->code == "canceled_after_current",
        "canceled jobs should have a stable non-failure reason");

    if (failures == 0) {
        std::cout << "All AssetBridge batch coordinator tests passed.\n";
        return 0;
    }
    std::cerr << failures << " batch coordinator assertion(s) failed.\n";
    return 1;
}
