#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

#include <imgui_impl_dx11.h>
#include <imgui_impl_sdl3.h>
#include <imgui.h>

#include "assetbridge/batch/batch_coordinator.hpp"
#include "assetbridge/core/batch_processor.hpp"
#include "assetbridge/desktop/desktop_batch_state.hpp"
#include "assetbridge/platform/windows/windows_platform.hpp"
#include "assetbridge/version.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using assetbridge::batch::BatchJob;
using assetbridge::batch::BatchJobId;
using assetbridge::batch::BatchJobStatus;
using assetbridge::batch::BatchRunStatus;
using assetbridge::desktop::DesktopBatchOperation;
using assetbridge::desktop::DesktopBatchState;

struct Dx11Context {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* render_target = nullptr;

    void release_render_target() {
        if (render_target != nullptr) {
            render_target->Release();
            render_target = nullptr;
        }
    }

    bool create_render_target() {
        ID3D11Texture2D* back_buffer = nullptr;
        if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) return false;
        const HRESULT result = device->CreateRenderTargetView(
            back_buffer, nullptr, &render_target);
        back_buffer->Release();
        return SUCCEEDED(result);
    }

    bool initialize(HWND window) {
        DXGI_SWAP_CHAIN_DESC description{};
        description.BufferCount = 2;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.OutputWindow = window;
        description.SampleDesc.Count = 1;
        description.Windowed = TRUE;
        description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        constexpr D3D_FEATURE_LEVEL requested[] = {
            D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL created{};
        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            requested, static_cast<UINT>(std::size(requested)), D3D11_SDK_VERSION,
            &description, &swap_chain, &device, &created, &context);
#ifdef _DEBUG
        if (FAILED(result)) {
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            result = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                requested, static_cast<UINT>(std::size(requested)), D3D11_SDK_VERSION,
                &description, &swap_chain, &device, &created, &context);
        }
#endif
        return SUCCEEDED(result) && create_render_target();
    }

    void resize(int width, int height) {
        if (swap_chain == nullptr || width <= 0 || height <= 0) return;
        release_render_target();
        if (SUCCEEDED(swap_chain->ResizeBuffers(
                0, static_cast<UINT>(width), static_cast<UINT>(height),
                DXGI_FORMAT_UNKNOWN, 0))) {
            create_render_target();
        }
    }

    void shutdown() {
        release_render_target();
        if (swap_chain != nullptr) swap_chain->Release();
        if (context != nullptr) context->Release();
        if (device != nullptr) device->Release();
        swap_chain = nullptr;
        context = nullptr;
        device = nullptr;
    }
};

class DesktopBatchWorker {
public:
    ~DesktopBatchWorker() { wait(); }

    bool start_preparation(DesktopBatchState& state) {
        if (busy_.exchange(true)) return false;
        wait_finished_thread();
        if (!state.begin_preparation()) {
            busy_.store(false);
            return false;
        }
        clear_error();
        thread_ = std::jthread([this, &state] {
            state.prepare_all();
            busy_.store(false);
        });
        return true;
    }

    bool start_batch(DesktopBatchState& state) {
        if (busy_.exchange(true)) return false;
        wait_finished_thread();
        if (!state.begin_batch()) {
            busy_.store(false);
            return false;
        }
        clear_error();
        thread_ = std::jthread([this, &state] {
            auto snapshot = state.run_batch();
            {
                std::scoped_lock lock(mutex_);
                completed_ = std::move(snapshot);
            }
            busy_.store(false);
        });
        return true;
    }

    [[nodiscard]] bool busy() const noexcept { return busy_.load(); }

    [[nodiscard]] std::string error() const {
        std::scoped_lock lock(mutex_);
        return error_;
    }

    std::optional<assetbridge::batch::BatchSnapshot> take_completed() {
        std::scoped_lock lock(mutex_);
        auto result = std::move(completed_);
        completed_.reset();
        return result;
    }

    void set_report_error(std::string error) {
        std::scoped_lock lock(mutex_);
        error_ = std::move(error);
    }

    void wait() {
        if (thread_.joinable()) thread_.join();
    }

private:
    void wait_finished_thread() {
        if (thread_.joinable()) thread_.join();
    }

    void clear_error() {
        std::scoped_lock lock(mutex_);
        error_.clear();
    }

    std::jthread thread_;
    std::atomic_bool busy_ = false;
    mutable std::mutex mutex_;
    std::string error_;
    std::optional<assetbridge::batch::BatchSnapshot> completed_;
};

bool command_line_has(std::wstring_view option) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) return false;
    bool found = false;
    for (int index = 1; index < count; ++index) {
        if (option == arguments[index]) found = true;
    }
    LocalFree(arguments);
    return found;
}

std::vector<std::filesystem::path> command_line_paths(std::wstring_view option) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) return {};
    std::vector<std::filesystem::path> result;
    for (int index = 1; index + 1 < count; ++index) {
        if (option == arguments[index]) result.emplace_back(arguments[index + 1]);
    }
    LocalFree(arguments);
    return result;
}

std::optional<std::filesystem::path> command_line_path(std::wstring_view option) {
    const auto paths = command_line_paths(option);
    return paths.empty() ? std::nullopt : std::optional<std::filesystem::path>(paths.front());
}

void apply_style(float ui_scale) {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 0.0F;
    style.FrameRounding = 5.0F;
    style.ChildRounding = 7.0F;
    style.GrabRounding = 4.0F;
    style.WindowPadding = { 22.0F, 18.0F };
    style.ItemSpacing = { 10.0F, 9.0F };
    style.Colors[ImGuiCol_WindowBg] = { 0.055F, 0.063F, 0.075F, 1.0F };
    style.Colors[ImGuiCol_ChildBg] = { 0.080F, 0.090F, 0.106F, 1.0F };
    style.Colors[ImGuiCol_Button] = { 0.075F, 0.36F, 0.72F, 1.0F };
    style.Colors[ImGuiCol_ButtonHovered] = { 0.095F, 0.43F, 0.84F, 1.0F };
    style.Colors[ImGuiCol_ButtonActive] = { 0.060F, 0.30F, 0.62F, 1.0F };
    style.Colors[ImGuiCol_Header] = { 0.075F, 0.36F, 0.72F, 0.55F };
    style.ScaleAllSizes(ui_scale);
}

ImVec4 status_color(BatchJobStatus status) {
    switch (status) {
    case BatchJobStatus::success: return { 0.25F, 0.82F, 0.52F, 1.0F };
    case BatchJobStatus::failed: return { 0.96F, 0.38F, 0.38F, 1.0F };
    case BatchJobStatus::not_supported: return { 0.98F, 0.70F, 0.25F, 1.0F };
    case BatchJobStatus::canceled: return { 0.52F, 0.56F, 0.62F, 1.0F };
    case BatchJobStatus::queued: return { 0.62F, 0.68F, 0.76F, 1.0F };
    default: return { 0.38F, 0.66F, 1.0F, 1.0F };
    }
}

std::string_view status_label(BatchJobStatus status) {
    switch (status) {
    case BatchJobStatus::queued: return "Queued";
    case BatchJobStatus::inspecting: return "Inspecting";
    case BatchJobStatus::preflighting: return "Preflighting";
    case BatchJobStatus::resolving_textures: return "Resolving Textures";
    case BatchJobStatus::converting: return "Converting";
    case BatchJobStatus::embedding_textures: return "Embedding Textures";
    case BatchJobStatus::validating_geometry: return "Validating Geometry";
    case BatchJobStatus::validating_textures: return "Validating Textures";
    case BatchJobStatus::success: return "Success";
    case BatchJobStatus::not_supported: return "Not Supported";
    case BatchJobStatus::failed: return "Failed";
    case BatchJobStatus::canceled: return "Canceled";
    }
    return "Failed";
}

void disabled_begin(bool disabled) {
    if (disabled) ImGui::BeginDisabled();
}

void disabled_end(bool disabled) {
    if (disabled) ImGui::EndDisabled();
}

const BatchJob* find_selected_job(
    const assetbridge::batch::BatchSnapshot& snapshot,
    std::optional<BatchJobId> selected) {
    if (!selected.has_value()) return nullptr;
    const auto found = std::find_if(snapshot.jobs.begin(), snapshot.jobs.end(),
        [&](const BatchJob& job) { return job.id == *selected; });
    return found == snapshot.jobs.end() ? nullptr : &*found;
}

bool has_active_job(const assetbridge::batch::BatchSnapshot& snapshot) {
    return std::any_of(snapshot.jobs.begin(), snapshot.jobs.end(), [](const BatchJob& job) {
        return job.status == BatchJobStatus::inspecting
            || job.status == BatchJobStatus::preflighting
            || job.status == BatchJobStatus::resolving_textures
            || job.status == BatchJobStatus::converting
            || job.status == BatchJobStatus::embedding_textures
            || job.status == BatchJobStatus::validating_geometry
            || job.status == BatchJobStatus::validating_textures;
    });
}

void draw_ui(
    DesktopBatchState& state,
    DesktopBatchWorker& worker,
    HWND native_window,
    bool cjk_font_available,
    float ui_scale) {
    const auto snapshot = state.snapshot();
    const bool busy = worker.busy() || state.busy();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("AssetBridgeRoot", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetWindowFontScale(1.22F);
    ImGui::TextUnformatted("AssetBridge");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextColored({ 0.62F, 0.68F, 0.76F, 1.0F },
        "Verified OBJ to GLB Batch Converter");
    ImGui::SameLine();
    ImGui::TextDisabled("  v%s", assetbridge::version.data());
    if (!cjk_font_available) {
        ImGui::TextColored({ 0.98F, 0.70F, 0.25F, 1.0F },
            "CJK display font was not found; Unicode file operations remain enabled.");
    }
    ImGui::Separator();

    ImGui::BeginChild("DropZone", { 0.0F, 82.0F * ui_scale },
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::Dummy({ 0.0F, 5.0F * ui_scale });
    ImGui::TextUnformatted("Drop one or more OBJ files here / 拖入一个或多个 OBJ 模型");
    disabled_begin(busy || snapshot.status != BatchRunStatus::not_started);
    if (ImGui::Button("Choose OBJ Files")) {
        const auto paths = assetbridge::platform::windows::choose_obj_files(native_window);
        if (!paths.empty()) {
            const auto added = state.add_files(paths);
            if (!added.accepted.empty()) (void)worker.start_preparation(state);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Queue")) (void)state.clear();
    disabled_end(busy || snapshot.status != BatchRunStatus::not_started);
    ImGui::SameLine();
    ImGui::TextDisabled("%llu / %llu jobs",
        static_cast<unsigned long long>(snapshot.jobs.size()),
        static_cast<unsigned long long>(assetbridge::batch::maximum_batch_jobs));
    ImGui::EndChild();

    for (const auto& issue : state.input_issues()) {
        ImGui::TextColored({ 0.98F, 0.70F, 0.25F, 1.0F }, "[%s] %s",
            std::string(assetbridge::batch::to_string(issue.code)).c_str(),
            issue.message.c_str());
    }

    ImGui::SeparatorText("Conversion Queue");
    const ImGuiTableFlags table_flags = ImGuiTableFlags_BordersInnerH
        | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("BatchQueue", 7, table_flags, { 0.0F, 210.0F * ui_scale })) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch, 2.5F);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.4F);
        ImGui::TableSetupColumn("Meshes", ImGuiTableColumnFlags_WidthFixed, 62.0F * ui_scale);
        ImGui::TableSetupColumn("Faces", ImGuiTableColumnFlags_WidthFixed, 68.0F * ui_scale);
        ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_WidthFixed, 76.0F * ui_scale);
        ImGui::TableSetupColumn("Materials", ImGuiTableColumnFlags_WidthFixed, 72.0F * ui_scale);
        ImGui::TableSetupColumn("Textures", ImGuiTableColumnFlags_WidthFixed, 66.0F * ui_scale);
        ImGui::TableHeadersRow();
        for (const auto& job : snapshot.jobs) {
            ImGui::PushID(static_cast<int>(job.id.value));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const auto filename = assetbridge::platform::windows::path_to_utf8(
                job.input_path.filename());
            const bool selected = state.selected_job() == job.id;
            if (ImGui::Selectable(filename.c_str(), selected,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                (void)state.select_job(job.id);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(status_color(job.status), "%s",
                std::string(status_label(job.status)).c_str());
            const auto value = [&](std::uint64_t count) {
                ImGui::Text("%llu", static_cast<unsigned long long>(count));
            };
            ImGui::TableSetColumnIndex(2);
            job.preview ? value(job.preview->mesh_count) : ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(3);
            job.preview ? value(job.preview->face_count) : ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(4);
            job.preview ? value(job.preview->triangle_count) : ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(5);
            job.preview ? value(job.preview->material_count) : ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(6);
            job.preview ? value(job.preview->texture_count) : ImGui::TextDisabled("-");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    const BatchJob* selected = find_selected_job(snapshot, state.selected_job());
    if (selected != nullptr) {
        const auto path = assetbridge::platform::windows::path_to_utf8(selected->input_path);
        ImGui::TextWrapped("Selected: %s", path.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy Path")) ImGui::SetClipboardText(path.c_str());
        disabled_begin(busy || snapshot.status != BatchRunStatus::not_started);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) (void)state.remove_job(selected->id);
        disabled_end(busy || snapshot.status != BatchRunStatus::not_started);
        if (selected->preview.has_value()) {
            for (const auto& diagnostic : selected->preview->diagnostics) {
                ImGui::TextColored({ 0.98F, 0.70F, 0.25F, 1.0F }, "[%s]",
                    diagnostic.code.c_str());
                ImGui::SameLine();
                ImGui::TextWrapped("%s", diagnostic.message.c_str());
            }
        }
        if (selected->result.has_value() && selected->result->error.has_value()) {
            ImGui::TextColored(status_color(selected->status), "[%s] %s",
                selected->result->error->code.c_str(),
                selected->result->error->message.c_str());
        }
    } else if (snapshot.jobs.empty()) {
        ImGui::TextDisabled("Queue is empty. Single-file conversion remains a one-item batch.");
    }

    ImGui::SeparatorText("Output and Progress");
    const std::string output = snapshot.output_root.empty()
        ? "Not selected"
        : assetbridge::platform::windows::path_to_utf8(snapshot.output_root);
    ImGui::TextWrapped("Output directory: %s", output.c_str());
    disabled_begin(busy || snapshot.status != BatchRunStatus::not_started);
    ImGui::SameLine();
    if (ImGui::SmallButton("Choose Folder")) {
        if (auto path = assetbridge::platform::windows::choose_folder(native_window)) {
            (void)state.set_output_root(*path);
        }
    }
    disabled_end(busy || snapshot.status != BatchRunStatus::not_started);

    ImGui::Text("Total %llu  |  Success %llu  |  Not Supported %llu  |  Failed %llu  |  Canceled %llu",
        static_cast<unsigned long long>(snapshot.summary.total),
        static_cast<unsigned long long>(snapshot.summary.succeeded),
        static_cast<unsigned long long>(snapshot.summary.not_supported),
        static_cast<unsigned long long>(snapshot.summary.failed),
        static_cast<unsigned long long>(snapshot.summary.canceled));

    disabled_begin(!state.can_start_batch() || busy);
    if (ImGui::Button("Convert All to GLB", { 188.0F * ui_scale, 38.0F * ui_scale })) {
        (void)worker.start_batch(state);
    }
    disabled_end(!state.can_start_batch() || busy);
    if (state.operation() == DesktopBatchOperation::converting) {
        ImGui::SameLine();
        disabled_begin(snapshot.cancel_after_current_requested);
        if (ImGui::Button("Cancel After Current")) (void)state.cancel_after_current();
        disabled_end(snapshot.cancel_after_current_requested);
    }
    ImGui::SameLine();
    if (state.operation() == DesktopBatchOperation::preparing) {
        ImGui::TextColored({ 0.38F, 0.66F, 1.0F, 1.0F }, "Inspecting and preflighting queue...");
    } else if (has_active_job(snapshot)) {
        const auto active = std::find_if(snapshot.jobs.begin(), snapshot.jobs.end(),
            [](const BatchJob& job) {
                return job.status != BatchJobStatus::queued
                    && job.status != BatchJobStatus::success
                    && job.status != BatchJobStatus::not_supported
                    && job.status != BatchJobStatus::failed
                    && job.status != BatchJobStatus::canceled;
            });
        if (active != snapshot.jobs.end()) {
            ImGui::TextColored(status_color(active->status), "%s: %s",
                assetbridge::platform::windows::path_to_utf8(
                    active->input_path.filename()).c_str(),
                std::string(status_label(active->status)).c_str());
        }
    } else {
        ImGui::Text("Batch: %s", std::string(assetbridge::batch::to_string(snapshot.status)).c_str());
    }

    const auto report_error = worker.error();
    if (!report_error.empty()) {
        ImGui::TextColored({ 0.96F, 0.38F, 0.38F, 1.0F },
            "Batch report error: %s", report_error.c_str());
    }
    if (snapshot.status != BatchRunStatus::not_started && !snapshot.output_root.empty()) {
        if (ImGui::Button("Open Output Folder")) {
            (void)assetbridge::platform::windows::open_in_shell(snapshot.output_root);
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Batch Report")) {
            (void)assetbridge::platform::windows::open_in_shell(
                snapshot.output_root / "batch-report.json");
        }
    }
    ImGui::End();
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const bool smoke_test = command_line_has(L"--smoke-test");
    const bool force_scale_150 = command_line_has(L"--ui-scale-150");
    const auto acceptance_inputs = command_line_paths(L"--acceptance-input");
    const auto acceptance_output = command_line_path(L"--acceptance-output");
    const bool acceptance_test = !acceptance_inputs.empty() && acceptance_output.has_value();
    const bool acceptance_hold = command_line_has(L"--acceptance-hold");
    const bool acceptance_cancel_after_current =
        command_line_has(L"--acceptance-cancel-after-current");
    const auto startup_begin = std::chrono::steady_clock::now();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        CoUninitialize();
        return 10;
    }
    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
    SDL_SetEventEnabled(SDL_EVENT_DROP_BEGIN, true);
    SDL_SetEventEnabled(SDL_EVENT_DROP_COMPLETE, true);

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (smoke_test) flags |= SDL_WINDOW_HIDDEN;
    SDL_Window* window = SDL_CreateWindow("AssetBridge", 920, 680, flags);
    if (window == nullptr) {
        SDL_Quit();
        CoUninitialize();
        return 11;
    }
    const float ui_scale = force_scale_150
        ? 1.5F
        : std::max(1.0F, SDL_GetWindowDisplayScale(window));
    SDL_SetWindowSize(window, static_cast<int>(920.0F * ui_scale),
        static_cast<int>(680.0F * ui_scale));
    SDL_SetWindowMinimumSize(window, static_cast<int>(760.0F * ui_scale),
        static_cast<int>(560.0F * ui_scale));
    const auto properties = SDL_GetWindowProperties(window);
    HWND native_window = static_cast<HWND>(
        SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (native_window == nullptr) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        CoUninitialize();
        return 12;
    }

    Dx11Context graphics;
    if (!graphics.initialize(native_window)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        CoUninitialize();
        return 13;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    apply_style(ui_scale);
    const auto cjk_font = assetbridge::platform::windows::find_cjk_font();
    if (cjk_font) {
        const auto font_path = assetbridge::platform::windows::path_to_utf8(*cjk_font);
        io.Fonts->AddFontFromFileTTF(font_path.c_str(), 17.0F * ui_scale, nullptr,
            io.Fonts->GetGlyphRangesChineseFull());
    } else {
        io.Fonts->AddFontDefault();
    }
    if (!ImGui_ImplSDL3_InitForD3D(window)
        || !ImGui_ImplDX11_Init(graphics.device, graphics.context)) {
        ImGui::DestroyContext();
        graphics.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        CoUninitialize();
        return 14;
    }

    DesktopBatchState state(
        assetbridge::make_asset_batch_preflight_executor(),
        assetbridge::make_asset_batch_executor());
    DesktopBatchWorker worker;
    std::vector<std::filesystem::path> pending_drop;
    std::vector<std::string> acceptance_input_utf8;
    bool acceptance_conversion_started = false;
    bool acceptance_cancel_requested = false;
    int acceptance_exit_code = 0;
    if (acceptance_test) {
        (void)state.set_output_root(*acceptance_output);
        acceptance_input_utf8.reserve(acceptance_inputs.size());
        for (const auto& input : acceptance_inputs) {
            acceptance_input_utf8.push_back(
                assetbridge::platform::windows::path_to_utf8(input));
        }
        SDL_Event begin{};
        begin.type = SDL_EVENT_DROP_BEGIN;
        begin.drop.windowID = SDL_GetWindowID(window);
        SDL_PushEvent(&begin);
        for (const auto& input : acceptance_input_utf8) {
            SDL_Event file{};
            file.type = SDL_EVENT_DROP_FILE;
            file.drop.windowID = SDL_GetWindowID(window);
            file.drop.data = input.c_str();
            SDL_PushEvent(&file);
        }
        SDL_Event complete{};
        complete.type = SDL_EVENT_DROP_COMPLETE;
        complete.drop.windowID = SDL_GetWindowID(window);
        SDL_PushEvent(&complete);
    }

    bool running = true;
    int rendered_frames = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT
                || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
                    && event.window.windowID == SDL_GetWindowID(window))) {
                running = false;
            } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                graphics.resize(event.window.data1, event.window.data2);
            } else if (event.type == SDL_EVENT_DROP_BEGIN) {
                pending_drop.clear();
            } else if (event.type == SDL_EVENT_DROP_FILE) {
                const std::string copied_utf8 = event.drop.data == nullptr ? "" : event.drop.data;
                try {
                    pending_drop.push_back(
                        assetbridge::platform::windows::path_from_utf8(copied_utf8));
                } catch (const std::exception&) {
                    pending_drop.clear();
                }
            } else if (event.type == SDL_EVENT_DROP_COMPLETE) {
                if (!pending_drop.empty() && !worker.busy()) {
                    const auto added = state.add_files(pending_drop);
                    if (!added.accepted.empty()) (void)worker.start_preparation(state);
                }
                pending_drop.clear();
            }
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        if (auto completed = worker.take_completed()) {
            std::string report_error;
            if (!assetbridge::write_batch_report(*completed, report_error)) {
                worker.set_report_error(std::move(report_error));
            }
        }

        if (acceptance_test && !worker.busy() && !acceptance_conversion_started) {
            if (state.can_start_batch()) {
                acceptance_conversion_started = worker.start_batch(state);
            } else {
                const auto current = state.snapshot();
                const bool no_pending = !current.jobs.empty()
                    && std::none_of(current.jobs.begin(), current.jobs.end(), [](const BatchJob& job) {
                        return job.status == BatchJobStatus::queued
                            || job.status == BatchJobStatus::inspecting
                            || job.status == BatchJobStatus::preflighting;
                    });
                if (no_pending) {
                    acceptance_exit_code = 20;
                    if (!acceptance_hold) running = false;
                }
            }
        }
        if (acceptance_test && acceptance_cancel_after_current
            && acceptance_conversion_started && worker.busy()
            && !acceptance_cancel_requested) {
            acceptance_cancel_requested = state.cancel_after_current();
        }
        if (acceptance_test && acceptance_conversion_started && !worker.busy()) {
            const auto completed = state.snapshot();
            if (completed.status != BatchRunStatus::not_started
                && completed.status != BatchRunStatus::running) {
                const bool expected_result = acceptance_cancel_after_current
                    ? completed.status == BatchRunStatus::partial
                        && completed.summary.succeeded == 1
                        && completed.summary.canceled > 0
                        && completed.summary.failed == 0
                        && completed.summary.not_supported == 0
                    : completed.status == BatchRunStatus::success;
                acceptance_exit_code = expected_result && worker.error().empty() ? 0 : 20;
                if (!acceptance_hold) running = false;
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        draw_ui(state, worker, native_window, cjk_font.has_value(), ui_scale);
        ImGui::Render();
        constexpr float clear_color[4] = { 0.055F, 0.063F, 0.075F, 1.0F };
        graphics.context->OMSetRenderTargets(1, &graphics.render_target, nullptr);
        graphics.context->ClearRenderTargetView(graphics.render_target, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        graphics.swap_chain->Present(1, 0);
        ++rendered_frames;
        if (smoke_test && rendered_frames >= 1) running = false;
    }

    worker.wait();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    graphics.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    CoUninitialize();
    const auto startup_elapsed = std::chrono::steady_clock::now() - startup_begin;
    (void)startup_elapsed;
    if (smoke_test && rendered_frames < 1) return 15;
    return acceptance_test ? acceptance_exit_code : 0;
}
