#pragma once

#include "assetbridge/core/asset_converter.hpp"
#include "assetbridge/core/asset_inspector.hpp"
#include "assetbridge/core/preflight_report.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace assetbridge::desktop {

enum class AppStatus {
    no_file,
    inspecting,
    ready,
    not_supported,
    converting,
    validating,
    success,
    failed
};

struct PreflightDiagnostic {
    std::string code;
    std::string feature_code;
    std::string message;
    LossSeverity severity = LossSeverity::warning;
    bool future_support_candidate = false;
};

enum class FileSelectionResult {
    accepted,
    busy,
    multiple_files,
    not_obj
};

[[nodiscard]] std::string_view to_string(AppStatus status) noexcept;

class DesktopAppState {
public:
    [[nodiscard]] AppStatus status() const noexcept { return status_; }
    [[nodiscard]] bool is_busy() const noexcept;
    [[nodiscard]] bool is_runtime_failure() const noexcept {
        return status_ == AppStatus::failed;
    }
    [[nodiscard]] bool can_convert() const noexcept;
    [[nodiscard]] bool has_non_triangle_faces() const noexcept;
    [[nodiscard]] std::string_view geometry_notice() const noexcept;

    [[nodiscard]] const std::optional<std::filesystem::path>& input_path() const noexcept {
        return input_path_;
    }
    [[nodiscard]] const std::filesystem::path& output_root() const noexcept {
        return output_root_;
    }
    [[nodiscard]] const std::optional<AssetSummary>& summary() const noexcept {
        return summary_;
    }
    [[nodiscard]] const std::optional<PreflightReport>& preflight() const noexcept {
        return preflight_;
    }
    [[nodiscard]] const std::optional<ConversionReport>& conversion() const noexcept {
        return conversion_;
    }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] const std::vector<PreflightDiagnostic>& diagnostics() const noexcept {
        return diagnostics_;
    }

    FileSelectionResult select_files(const std::vector<std::filesystem::path>& files);
    void complete_inspection(InspectionResult inspection, PreflightReport preflight);
    bool set_output_root(std::filesystem::path output_root);
    bool begin_conversion();
    void mark_validating();
    void complete_conversion(ConversionReport report);
    void report_failure(std::string message);
    void convert_another();

private:
    AppStatus status_ = AppStatus::no_file;
    std::optional<std::filesystem::path> input_path_;
    std::filesystem::path output_root_;
    std::optional<AssetSummary> summary_;
    std::optional<PreflightReport> preflight_;
    std::optional<ConversionReport> conversion_;
    std::vector<PreflightDiagnostic> diagnostics_;
    std::string message_ = "Ready";
};

} // namespace assetbridge::desktop
