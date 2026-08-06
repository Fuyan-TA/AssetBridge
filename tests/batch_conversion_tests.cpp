#include "assetbridge/batch/batch_coordinator.hpp"
#include "assetbridge/core/batch_processor.hpp"
#include "assetbridge/core/batch_serializer.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {

int require(bool condition, std::string_view message) {
    if (condition) return 0;
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

bool has_temporary_directory(const std::filesystem::path& root) {
    std::error_code error;
    if (!std::filesystem::exists(root, error)) return false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
        if (entry.is_directory() && entry.path().filename().native().starts_with(
                L".assetbridge-tmp-")) {
            return true;
        }
    }
    return false;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    using namespace assetbridge;
    using namespace assetbridge::batch;
    if (argc != 3) {
        std::cerr << "Expected asset and output roots.\n";
        return 2;
    }

    const std::filesystem::path assets(argv[1]);
    const std::filesystem::path output(argv[2]);
    std::error_code error;
    std::filesystem::remove_all(output, error);
    std::filesystem::create_directories(output, error);
    int failures = 0;

    BatchCoordinator mixed(make_asset_batch_executor());
    const auto add = mixed.add_inputs({
        assets / "minimal_triangle.obj",
        assets / "invalid.obj",
        assets / "textured" / "rejected" / "unsupported_semantic.obj",
        assets / L"\u4e2d\u6587\u4e09\u89d2\u5f62.obj",
        assets / "textured" / "verified" / "shared_texture.obj"
    });
    failures += require(add.accepted.size() == 5, "five batch assets should be accepted");
    failures += require(mixed.set_output_root(output), "batch output root should be accepted");
    const auto snapshot = mixed.run();
    failures += require(
        snapshot.status == BatchRunStatus::partial
            && snapshot.summary.succeeded == 3
            && snapshot.summary.failed == 1
            && snapshot.summary.not_supported == 1,
        "middle invalid and unsupported assets should not stop later conversions");
    failures += require(
        snapshot.jobs[1].result->error->code == "import_failed",
        "invalid OBJ should keep its structured inspection error");
    failures += require(
        snapshot.jobs[2].status == BatchJobStatus::not_supported
            && snapshot.jobs[2].result->error->code == "texture_semantic_unverified",
        "unsupported texture semantics should remain distinct from runtime failure");
    failures += require(
        snapshot.jobs[4].result->diagnostics.embedded_image_count == 1,
        "shared-texture conversion should embed one deduplicated image");
    failures += require(
        snapshot.jobs[4].result->diagnostics.validation_failure_count == 0,
        "successful texture conversion should have no validation failures");

    std::string write_error;
    failures += require(
        write_batch_report(snapshot, write_error),
        "batch-report.json should be committed: " + write_error);
    const auto report_path = output / "batch-report.json";
    failures += require(std::filesystem::is_regular_file(report_path), "batch report should exist");
    {
        std::ifstream input(report_path, std::ios::binary);
        const auto report = nlohmann::json::parse(input);
        failures += require(
            report.at("schema") == "assetbridge.batch.v1"
                && report.at("status") == "partial"
                && report.at("jobs").size() == 5,
            "batch report should parse with stable schema fields");
        failures += require(
            report.at("summary").at("succeeded").is_number_unsigned()
                && report.at("jobs").at(1).at("output").at("directory").is_null(),
            "batch JSON number and null semantics should be stable");
    }
    failures += require(!has_temporary_directory(output), "batch must leave no transaction directory");

    const auto same_a = output / "inputs-a";
    const auto same_b = output / "inputs-b";
    std::filesystem::create_directories(same_a, error);
    std::filesystem::create_directories(same_b, error);
    std::filesystem::copy_file(
        assets / "minimal_triangle.obj", same_a / "same.obj",
        std::filesystem::copy_options::overwrite_existing, error);
    std::filesystem::copy_file(
        assets / "minimal_triangle.obj", same_b / "same.obj",
        std::filesystem::copy_options::overwrite_existing, error);
    std::filesystem::copy_file(
        assets / "minimal_triangle.mtl", same_a / "minimal_triangle.mtl",
        std::filesystem::copy_options::overwrite_existing, error);
    std::filesystem::copy_file(
        assets / "minimal_triangle.mtl", same_b / "minimal_triangle.mtl",
        std::filesystem::copy_options::overwrite_existing, error);

    const auto same_output = output / "same-stem-output";
    BatchCoordinator same_stem(make_asset_batch_executor());
    (void)same_stem.add_inputs({ same_a / "same.obj", same_b / "same.obj" });
    (void)same_stem.set_output_root(same_output);
    const auto same_snapshot = same_stem.run();
    failures += require(
        same_snapshot.summary.succeeded == 2,
        "same-stem inputs from different roots should both convert");
    failures += require(
        same_snapshot.jobs[0].result->output_directory->filename() == L"same"
            && same_snapshot.jobs[1].result->output_directory->filename() == L"same_2",
        "same-stem output collision suffixes should be deterministic");
    failures += require(!has_temporary_directory(output), "all batch transactions should be cleaned");

    if (failures == 0) {
        std::cout << "All AssetBridge batch conversion tests passed.\n";
        return 0;
    }
    std::cerr << failures << " batch conversion assertion(s) failed.\n";
    return 1;
}
