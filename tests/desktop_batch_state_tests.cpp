#include "assetbridge/desktop/desktop_batch_state.hpp"
#include "assetbridge/core/batch_serializer.hpp"

#include <iostream>
#include <string_view>

namespace {

int require(bool condition, std::string_view message) {
    if (condition) return 0;
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

assetbridge::batch::BatchPreflightResult synthetic_preflight(
    const std::filesystem::path& input,
    const assetbridge::batch::BatchProgressCallback& progress) {
    using namespace assetbridge::batch;
    progress(BatchJobStatus::preflighting);
    BatchPreflightResult result;
    result.preview.mesh_count = 1;
    result.preview.face_count = 1;
    result.preview.triangle_count = 1;
    result.preview.material_count = 1;
    if (input.filename() == L"unsupported.obj") {
        result.status = BatchJobStatus::not_supported;
        result.error = BatchError {
            "route_feature_unverified", "Synthetic unsupported feature."
        };
        result.preview.diagnostics.push_back(*result.error);
    } else if (input.filename() == L"invalid.obj") {
        result.status = BatchJobStatus::failed;
        result.error = BatchError { "import_failed", "Synthetic invalid OBJ." };
    } else {
        result.status = BatchJobStatus::queued;
        result.preview.preflight_safe = true;
    }
    return result;
}

assetbridge::batch::BatchExecutionResult synthetic_conversion(
    const assetbridge::batch::BatchExecutionRequest& request,
    const assetbridge::batch::BatchProgressCallback& progress) {
    using namespace assetbridge::batch;
    progress(BatchJobStatus::converting);
    progress(BatchJobStatus::validating_geometry);
    BatchExecutionResult result;
    result.status = BatchJobStatus::success;
    result.result.output_directory = request.output_root / request.input_path.stem();
    result.result.glb_path = *result.result.output_directory
        / (request.input_path.stem().wstring() + L".glb");
    result.result.conversion_report_path = *result.result.output_directory
        / "conversion-report.json";
    result.result.diagnostics.triangle_count = 1;
    return result;
}

} // namespace

int main() {
    using namespace assetbridge::batch;
    using namespace assetbridge::desktop;
    int failures = 0;

    DesktopBatchState state(synthetic_preflight, synthetic_conversion);
    failures += require(!state.busy(), "desktop batch should start idle");
    const auto added = state.add_files({
        L"first.obj", L"unsupported.obj", L"invalid.obj", L"first.obj", L"other.glb"
    });
    failures += require(
        added.accepted.size() == 3 && added.issues.size() == 2,
        "desktop input should accept OBJ, deduplicate, and reject non-OBJ");
    failures += require(!state.can_start_batch(), "preflight and output root are required");
    failures += require(state.begin_preparation(), "preparation should begin once");
    failures += require(!state.begin_preparation(), "a second preparation must be rejected");
    state.prepare_all();
    const auto prepared = state.snapshot();
    failures += require(
        prepared.jobs[0].status == BatchJobStatus::queued
            && prepared.jobs[0].preview->preflight_safe,
        "safe desktop job should return to queued after preflight");
    failures += require(
        prepared.jobs[1].status == BatchJobStatus::not_supported
            && prepared.jobs[2].status == BatchJobStatus::failed,
        "not-supported and invalid states must remain distinct");
    failures += require(
        state.set_output_root(L"desktop-output"),
        "output root should be mutable before conversion");
    failures += require(state.can_start_batch(), "safe prepared job should enable Convert All");
    failures += require(state.begin_batch(), "desktop batch should enter converting");
    failures += require(
        !state.set_output_root(L"changed")
            && !state.remove_job(added.accepted.front())
            && state.add_files({ L"late.obj" }).issues.front().code
                == BatchInputIssueCode::batch_locked,
        "queue and output mutations must be disabled while converting");
    const auto completed = state.run_batch();
    failures += require(
        completed.status == BatchRunStatus::partial
            && completed.summary.succeeded == 1
            && completed.summary.not_supported == 1
            && completed.summary.failed == 1,
        "desktop summary should preserve prepared failures and converted success");
    const auto completed_json = assetbridge::batch_report_to_json(completed);
    failures += require(
        completed_json.find("assetbridge.batch.v1") != std::string::npos,
        "a prepared desktop batch snapshot should remain serializable");

    DesktopBatchState editable(synthetic_preflight, synthetic_conversion);
    const auto editable_add = editable.add_files({ L"a.obj", L"b.obj" });
    failures += require(
        editable.remove_job(editable_add.accepted.front()),
        "a queued job should be removable before work starts");
    failures += require(editable.clear(), "queue should be clearable before work starts");
    failures += require(editable.snapshot().jobs.empty(), "clear should remove all jobs");

    DesktopBatchState* cancel_state = nullptr;
    DesktopBatchState canceling(
        synthetic_preflight,
        [&](const BatchExecutionRequest& request, const BatchProgressCallback& progress) {
            if (request.id.value == 1) {
                failures += require(
                    cancel_state->cancel_after_current(),
                    "Cancel After Current should be available during conversion");
            }
            return synthetic_conversion(request, progress);
        });
    cancel_state = &canceling;
    (void)canceling.add_files({ L"a.obj", L"b.obj", L"c.obj" });
    (void)canceling.begin_preparation();
    canceling.prepare_all();
    (void)canceling.set_output_root(L"cancel-output");
    (void)canceling.begin_batch();
    const auto canceled = canceling.run_batch();
    failures += require(
        canceled.summary.succeeded == 1 && canceled.summary.canceled == 2,
        "desktop cancel should finish current and cancel remaining jobs");

    if (failures == 0) {
        std::cout << "All AssetBridge desktop batch state tests passed.\n";
        return 0;
    }
    std::cerr << failures << " desktop batch state assertion(s) failed.\n";
    return 1;
}
