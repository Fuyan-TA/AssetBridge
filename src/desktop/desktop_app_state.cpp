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

std::string diagnostic_message(
    const LossItem& loss,
    const AssetSummary& summary) {
    if (!loss.feature) {
        return loss.reason;
    }

    switch (*loss.feature) {
    case AssetFeature::multiple_meshes:
        return "Multiple meshes: " + std::to_string(summary.mesh_count) + " — not verified yet";
    case AssetFeature::node_hierarchy:
        return "Meaningful hierarchy — not verified yet";
    case AssetFeature::external_textures:
        return "External textures — not verified yet";
    case AssetFeature::embedded_textures:
        return "Embedded textures — not verified yet";
    case AssetFeature::multiple_uv_channels:
        return "Multiple UV channels — not verified yet";
    case AssetFeature::vertex_colors:
        return "Vertex colors — not verified yet";
    case AssetFeature::bones:
        return "Bones — not verified yet";
    case AssetFeature::skin_weights:
        return "Skin weights — not verified yet";
    case AssetFeature::animations:
        return "Animations — not verified yet";
    case AssetFeature::morph_targets:
        return "Morph targets — not verified yet";
    default:
        return std::string(to_string(*loss.feature)) + " — " + loss.reason;
    }
}

std::vector<PreflightDiagnostic> build_diagnostics(
    const PreflightReport& preflight,
    const AssetSummary& summary) {
    std::vector<PreflightDiagnostic> diagnostics;
    diagnostics.reserve(preflight.decision.losses.size());
    for (const auto& loss : preflight.decision.losses) {
        diagnostics.push_back({
            loss.code,
            loss.feature ? std::string(to_string(*loss.feature)) : std::string("none"),
            diagnostic_message(loss, summary),
            loss.severity,
            loss.code == "route_feature_unverified" && loss.feature.has_value()
        });
    }
    return diagnostics;
}

} // namespace

std::string_view to_string(AppStatus status) noexcept {
    switch (status) {
    case AppStatus::no_file: return "Ready";
    case AppStatus::inspecting: return "Inspecting";
    case AppStatus::ready: return "Ready";
    case AppStatus::not_supported: return "Not Supported Yet / 暂未支持";
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

bool DesktopAppState::has_non_triangle_faces() const noexcept {
    return summary_.has_value() && summary_->face_count > summary_->triangle_count;
}

std::string_view DesktopAppState::geometry_notice() const noexcept {
    if (!has_non_triangle_faces()) {
        return {};
    }
    if (status_ == AppStatus::ready) {
        return "Source contains non-triangle faces; the verified OBJ to GLB route will triangulate them during export preparation.";
    }
    return "Source contains non-triangle faces; conversion will triangulate when the route supports this asset.";
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
    diagnostics_.clear();
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
    diagnostics_ = build_diagnostics(*preflight_, *summary_);
    if (preflight_->decision.overall_result != OverallResult::safe) {
        message_ = "This valid asset contains features outside the currently verified OBJ to GLB route.";
        status_ = AppStatus::not_supported;
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
    diagnostics_.clear();
    status_ = AppStatus::no_file;
    message_ = "Ready";
}

} // namespace assetbridge::desktop
