#include "assetbridge/desktop/desktop_app_state.hpp"

#include <algorithm>
#include <cwctype>

namespace assetbridge::desktop {
namespace {

bool is_obj_extension(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return extension == L".obj";
}

} // namespace

std::string_view to_string(AppStatus status) noexcept {
    switch (status) {
    case AppStatus::no_file: return "Ready";
    case AppStatus::inspecting: return "Inspecting";
    case AppStatus::ready: return "Ready";
    case AppStatus::converting: return "Converting";
    case AppStatus::validating: return "Validating";
    case AppStatus::success: return "Success";
    case AppStatus::failed: return "Failed";
    }
    return "Unknown";
}

bool DesktopAppState::is_busy() const noexcept {
    return status_ == AppStatus::inspecting
        || status_ == AppStatus::converting
        || status_ == AppStatus::validating;
}

bool DesktopAppState::can_convert() const noexcept {
    return status_ == AppStatus::ready
        && input_path_.has_value()
        && !output_root_.empty();
}

FileSelectionResult DesktopAppState::select_files(
    const std::vector<std::filesystem::path>& files) {
    if (is_busy()) {
        message_ = "A task is already running.";
        return FileSelectionResult::busy;
    }
    if (files.size() != 1) {
        message_ = "MVP currently supports one OBJ at a time.";
        status_ = AppStatus::failed;
        return FileSelectionResult::multiple_files;
    }
    if (!is_obj_extension(files.front())) {
        message_ = "Only a real OBJ asset can be selected in this MVP.";
        status_ = AppStatus::failed;
        return FileSelectionResult::not_obj;
    }

    input_path_ = files.front();
    summary_.reset();
    preflight_.reset();
    conversion_.reset();
    status_ = AppStatus::inspecting;
    message_ = "Inspecting the selected OBJ and its MTL references.";
    return FileSelectionResult::accepted;
}

void DesktopAppState::complete_inspection(
    InspectionResult inspection,
    PreflightReport preflight) {
    if (status_ != AppStatus::inspecting) {
        return;
    }
    if (!inspection || !preflight) {
        message_ = !inspection.error_message.empty()
            ? std::move(inspection.error_message)
            : std::move(preflight.error_message);
        if (message_.empty()) {
            message_ = "The file could not be confirmed as a valid OBJ asset.";
        }
        status_ = AppStatus::failed;
        return;
    }

    summary_ = std::move(inspection.summary);
    preflight_ = std::move(preflight);
    if (preflight_->decision.overall_result != OverallResult::safe) {
        message_ = "Preflight rejected a feature outside the verified OBJ to GLB boundary.";
        status_ = AppStatus::failed;
        return;
    }
    status_ = AppStatus::ready;
    message_ = "Verified OBJ to GLB route is ready.";
}

bool DesktopAppState::set_output_root(std::filesystem::path output_root) {
    if (is_busy()) {
        return false;
    }
    output_root_ = std::move(output_root);
    return true;
}

bool DesktopAppState::begin_conversion() {
    if (!can_convert()) {
        if (output_root_.empty()) {
            message_ = "Choose an output folder before converting.";
        }
        return false;
    }
    status_ = AppStatus::converting;
    message_ = "Exporting with the verified OBJ to GLB route.";
    return true;
}

void DesktopAppState::mark_validating() {
    if (status_ == AppStatus::converting) {
        status_ = AppStatus::validating;
        message_ = "Reimporting and validating the generated GLB.";
    }
}

void DesktopAppState::complete_conversion(ConversionReport report) {
    if (status_ != AppStatus::converting && status_ != AppStatus::validating) {
        return;
    }
    const bool succeeded = static_cast<bool>(report);
    if (succeeded) {
        message_ = "Conversion and round-trip validation succeeded.";
        status_ = AppStatus::success;
    } else {
        message_ = report.error_message.empty() ? "Conversion failed." : report.error_message;
        status_ = AppStatus::failed;
    }
    conversion_ = std::move(report);
}

void DesktopAppState::report_failure(std::string message) {
    if (is_busy()) {
        return;
    }
    status_ = AppStatus::failed;
    message_ = std::move(message);
}

void DesktopAppState::convert_another() {
    if (is_busy()) {
        return;
    }
    input_path_.reset();
    summary_.reset();
    preflight_.reset();
    conversion_.reset();
    status_ = AppStatus::no_file;
    message_ = "Ready";
}

} // namespace assetbridge::desktop
