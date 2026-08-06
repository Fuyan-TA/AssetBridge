#include "assetbridge/core/batch_processor.hpp"

#include "assetbridge/core/asset_converter.hpp"
#include "assetbridge/core/asset_inspector.hpp"
#include "assetbridge/core/batch_serializer.hpp"
#include "assetbridge/core/preflight_report.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace assetbridge {
namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

bool is_unverified_loss(std::string_view code) {
    return code == "route_feature_unverified"
        || code == "texture_semantic_unverified"
        || code == "texture_options_unverified"
        || code == "transparency_unverified"
        || code == "multiple_textures_per_material_unverified";
}

batch::BatchJobStatus mapped_stage(ConversionStage stage) {
    switch (stage) {
    case ConversionStage::preflight: return batch::BatchJobStatus::preflighting;
    case ConversionStage::resolving_companions:
        return batch::BatchJobStatus::resolving_textures;
    case ConversionStage::importing:
    case ConversionStage::exporting:
        return batch::BatchJobStatus::converting;
    case ConversionStage::embedding_textures:
        return batch::BatchJobStatus::embedding_textures;
    case ConversionStage::validating_textures:
        return batch::BatchJobStatus::validating_textures;
    case ConversionStage::reimporting:
    case ConversionStage::validating:
    case ConversionStage::committing:
        return batch::BatchJobStatus::validating_geometry;
    }
    return batch::BatchJobStatus::converting;
}

std::optional<std::filesystem::path> output_with_extension(
    const ConversionReport& report,
    std::wstring_view extension) {
    for (const auto& path : report.output_files) {
        if (path.extension().native() == extension) return path;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> named_output(
    const ConversionReport& report,
    std::wstring_view name) {
    for (const auto& path : report.output_files) {
        if (path.filename().native() == name) return path;
    }
    return std::nullopt;
}

batch::BatchExecutionResult execute_asset_job(
    const batch::BatchExecutionRequest& request,
    const batch::BatchProgressCallback& progress) {
    const auto started = Clock::now();
    batch::BatchExecutionResult result;

    const AssetInspector inspector;
    const auto inspection = inspector.inspect(request.input_path);
    if (!inspection) {
        result.status = batch::BatchJobStatus::failed;
        result.result.error = batch::BatchError {
            std::string(to_string(inspection.error_code)),
            inspection.error_message
        };
        result.result.duration_ms = elapsed_ms(started);
        return result;
    }

    if (progress) progress(batch::BatchJobStatus::preflighting);
    const auto preflight = create_preflight_report(
        request.input_path,
        request.target_format);
    if (!preflight) {
        result.status = batch::BatchJobStatus::failed;
        result.result.error = batch::BatchError {
            std::string(to_string(preflight.error_code)),
            preflight.error_message
        };
        result.result.duration_ms = elapsed_ms(started);
        return result;
    }
    if (preflight.decision.overall_result != OverallResult::safe) {
        const auto loss = preflight.decision.losses.empty()
            ? nullptr
            : &preflight.decision.losses.front();
        const std::string code = loss == nullptr ? "preflight_blocked" : loss->code;
        result.status = is_unverified_loss(code)
            ? batch::BatchJobStatus::not_supported
            : batch::BatchJobStatus::failed;
        result.result.error = batch::BatchError {
            code,
            loss == nullptr
                ? "The asset did not pass the verified OBJ-to-GLB2 preflight."
                : loss->reason
        };
        if (inspection.summary.has_value()) {
            result.result.diagnostics.triangle_count = inspection.summary->triangle_count;
            result.result.diagnostics.material_count = inspection.summary->material_count;
        }
        result.result.duration_ms = elapsed_ms(started);
        return result;
    }

    const AssetConverter converter;
    auto conversion = converter.convert(
        request.input_path,
        request.target_format,
        request.output_root,
        [&](ConversionStage stage) {
            if (progress) progress(mapped_stage(stage));
        });

    result.result.duration_ms = elapsed_ms(started);
    result.result.output_directory = conversion.output_directory;
    result.result.glb_path = output_with_extension(conversion, L".glb");
    result.result.conversion_report_path = named_output(
        conversion,
        L"conversion-report.json");
    if (conversion.output_analysis.has_value()) {
        result.result.diagnostics.triangle_count =
            conversion.output_analysis->triangle_count;
        result.result.diagnostics.material_count =
            conversion.output_analysis->referenced_material_count;
    } else if (conversion.source_analysis.has_value()) {
        result.result.diagnostics.triangle_count =
            conversion.source_analysis->triangle_count;
        result.result.diagnostics.material_count =
            conversion.source_analysis->referenced_material_count;
    }
    result.result.diagnostics.embedded_image_count = conversion.embedded_texture_count;
    result.result.diagnostics.validation_failure_count = static_cast<std::uint64_t>(
        std::count_if(
            conversion.validation_checks.begin(),
            conversion.validation_checks.end(),
            [](const ValidationCheck& check) { return !check.passed; }));

    if (conversion) {
        result.status = batch::BatchJobStatus::success;
        return result;
    }

    result.status = conversion.error_code == ConversionErrorCode::route_not_enabled
            || conversion.error_code == ConversionErrorCode::route_feature_unverified
            || conversion.error_code == ConversionErrorCode::unsupported_source_format
        ? batch::BatchJobStatus::not_supported
        : batch::BatchJobStatus::failed;
    result.result.output_directory.reset();
    result.result.glb_path.reset();
    result.result.conversion_report_path.reset();
    result.result.error = batch::BatchError {
        std::string(to_string(conversion.error_code)),
        conversion.error_message
    };
    return result;
}

class TemporaryReportFile {
public:
    explicit TemporaryReportFile(std::filesystem::path path) : path_(std::move(path)) {}
    ~TemporaryReportFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    void release() noexcept { path_.clear(); }

private:
    std::filesystem::path path_;
};

} // namespace

batch::BatchExecutor make_asset_batch_executor() {
    return execute_asset_job;
}

bool write_batch_report(
    const batch::BatchSnapshot& snapshot,
    std::string& error_message) {
    if (snapshot.output_root.empty()) {
        error_message = "The batch output root is empty.";
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(snapshot.output_root, error);
    if (error || !std::filesystem::is_directory(snapshot.output_root, error)) {
        error_message = "Could not create the batch output root: " + error.message();
        return false;
    }

    const auto final_path = snapshot.output_root / "batch-report.json";
    const auto temporary_path = snapshot.output_root / ".assetbridge-batch-report.tmp";
    TemporaryReportFile temporary(temporary_path);
    {
        std::ofstream output(temporary.path(), std::ios::binary | std::ios::trunc);
        if (!output) {
            error_message = "Could not open the temporary batch report.";
            return false;
        }
        output << batch_report_to_json(snapshot) << '\n';
        if (!output) {
            error_message = "Could not finish writing the temporary batch report.";
            return false;
        }
    }

    std::filesystem::remove(final_path, error);
    if (error) {
        error_message = "Could not replace the previous batch report: " + error.message();
        return false;
    }
    std::filesystem::rename(temporary.path(), final_path, error);
    if (error) {
        error_message = "Could not commit the batch report: " + error.message();
        return false;
    }
    temporary.release();
    return true;
}

} // namespace assetbridge
