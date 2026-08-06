#include "assetbridge/core/batch_serializer.hpp"

#include <cmath>
#include <sstream>

#include <nlohmann/json.hpp>

namespace assetbridge {
namespace {

std::string path_to_generic_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

nlohmann::json optional_path(const std::optional<std::filesystem::path>& path) {
    return path.has_value()
        ? nlohmann::json(path_to_generic_utf8(*path))
        : nlohmann::json(nullptr);
}

nlohmann::json summary_json(const batch::BatchSummary& summary) {
    return {
        { "total", summary.total },
        { "succeeded", summary.succeeded },
        { "not_supported", summary.not_supported },
        { "failed", summary.failed },
        { "canceled", summary.canceled }
    };
}

nlohmann::json job_json(const batch::BatchJob& job) {
    const batch::BatchJobResult empty;
    const auto& result = job.result.has_value() ? *job.result : empty;
    const double duration = std::isfinite(result.duration_ms) && result.duration_ms >= 0.0
        ? result.duration_ms
        : 0.0;
    return {
        { "id", batch::batch_job_id_string(job.id) },
        { "input_path", path_to_generic_utf8(job.input_path) },
        { "status", batch::to_string(job.status) },
        { "route", {
            { "source_format_id", "obj" },
            { "target_format_id", "glb2" }
        } },
        { "output", {
            { "directory", optional_path(result.output_directory) },
            { "glb", optional_path(result.glb_path) },
            { "conversion_report", optional_path(result.conversion_report_path) }
        } },
        { "error", result.error.has_value()
            ? nlohmann::json({
                { "code", result.error->code },
                { "message", result.error->message }
            })
            : nlohmann::json(nullptr) },
        { "duration_ms", duration },
        { "diagnostics", {
            { "triangle_count", result.diagnostics.triangle_count },
            { "material_count", result.diagnostics.material_count },
            { "embedded_image_count", result.diagnostics.embedded_image_count },
            { "validation_failure_count", result.diagnostics.validation_failure_count }
        } }
    };
}

nlohmann::json snapshot_json(const batch::BatchSnapshot& snapshot) {
    nlohmann::json jobs = nlohmann::json::array();
    for (const auto& job : snapshot.jobs) jobs.push_back(job_json(job));
    return {
        { "schema", batch_schema },
        { "status", batch::to_string(snapshot.status) },
        { "target_format", to_string(snapshot.target_format) },
        { "output_root", path_to_generic_utf8(snapshot.output_root) },
        { "summary", summary_json(snapshot.summary) },
        { "jobs", std::move(jobs) }
    };
}

} // namespace

std::string batch_report_to_text(const batch::BatchSnapshot& snapshot) {
    std::ostringstream output;
    output
        << "Batch Status: " << batch::to_string(snapshot.status) << '\n'
        << "Target: " << to_string(snapshot.target_format) << '\n'
        << "Output Root: " << path_to_generic_utf8(snapshot.output_root) << '\n'
        << "Total: " << snapshot.summary.total << '\n'
        << "Succeeded: " << snapshot.summary.succeeded << '\n'
        << "Not Supported: " << snapshot.summary.not_supported << '\n'
        << "Failed: " << snapshot.summary.failed << '\n'
        << "Canceled: " << snapshot.summary.canceled << '\n';
    for (const auto& job : snapshot.jobs) {
        output << "- [" << batch::batch_job_id_string(job.id) << "] "
               << path_to_generic_utf8(job.input_path) << " | "
               << batch::to_string(job.status);
        if (job.result.has_value() && job.result->error.has_value()) {
            output << " | " << job.result->error->code << ": "
                   << job.result->error->message;
        }
        output << '\n';
    }
    return output.str();
}

std::string batch_report_to_json(const batch::BatchSnapshot& snapshot) {
    return snapshot_json(snapshot).dump(2);
}

std::string batch_argument_error_to_json(
    std::string_view code,
    std::string_view message) {
    nlohmann::json result = {
        { "schema", batch_schema },
        { "status", "failed" },
        { "target_format", nullptr },
        { "output_root", nullptr },
        { "summary", {
            { "total", 0 },
            { "succeeded", 0 },
            { "not_supported", 0 },
            { "failed", 0 },
            { "canceled", 0 }
        } },
        { "jobs", nlohmann::json::array() },
        { "error", { { "code", code }, { "message", message } } }
    };
    return result.dump(2);
}

} // namespace assetbridge
