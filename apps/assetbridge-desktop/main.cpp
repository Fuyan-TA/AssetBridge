#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

#include <imgui_impl_dx11.h>
#include <imgui_impl_sdl3.h>
#include <imgui.h>

#include "assetbridge/core/asset_converter.hpp"
#include "assetbridge/core/asset_inspector.hpp"
#include "assetbridge/core/preflight_report.hpp"
#include "assetbridge/desktop/desktop_app_state.hpp"
#include "assetbridge/platform/windows/windows_platform.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <shellapi.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

using assetbridge::desktop::AppStatus;
using assetbridge::desktop::DesktopAppState;

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
        if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) {
            return false;
        }
        const HRESULT result = device->CreateRenderTargetView(back_buffer, nullptr, &render_target);
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
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL created{};
        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            requested,
            static_cast<UINT>(std::size(requested)),
            D3D11_SDK_VERSION,
            &description,
            &swap_chain,
            &device,
            &created,
            &context);
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
        if (swap_chain == nullptr || width <= 0 || height <= 0) {
            return;
        }
        release_render_target();
        if (SUCCEEDED(swap_chain->ResizeBuffers(0, static_cast<UINT>(width),
                static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0))) {
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

struct InspectionWorkResult {
    assetbridge::InspectionResult inspection;
    assetbridge::PreflightReport preflight;
};

struct ConversionWorkResult {
    assetbridge::ConversionReport report;
};

using WorkResult = std::variant<InspectionWorkResult, ConversionWorkResult>;

class DesktopWorker {
public:
    ~DesktopWorker() { wait(); }

    bool start_inspection(std::filesystem::path input) {
        if (busy_.exchange(true)) return false;
        wait_finished_thread();
        stage_.store(assetbridge::ConversionStage::preflight);
        thread_ = std::jthread([this, input = std::move(input)] {
            assetbridge::AssetInspector inspector;
            InspectionWorkResult completed{
                inspector.inspect(input),
                assetbridge::create_preflight_report(input, assetbridge::FormatId::glb2)
            };
            {
                std::scoped_lock lock(mutex_);
                result_ = std::move(completed);
            }
            busy_.store(false);
        });
        return true;
    }

    bool start_conversion(std::filesystem::path input, std::filesystem::path output_root) {
        if (busy_.exchange(true)) return false;
        wait_finished_thread();
        stage_.store(assetbridge::ConversionStage::importing);
        thread_ = std::jthread([this, input = std::move(input), output_root = std::move(output_root)] {
            const assetbridge::AssetConverter converter;
            auto report = converter.convert(
                input,
                assetbridge::FormatId::glb2,
                output_root,
                [this](assetbridge::ConversionStage stage) { stage_.store(stage); });
            {
                std::scoped_lock lock(mutex_);
                result_ = ConversionWorkResult{ std::move(report) };
            }
            busy_.store(false);
        });
        return true;
    }

    [[nodiscard]] bool busy() const noexcept { return busy_.load(); }
    [[nodiscard]] assetbridge::ConversionStage stage() const noexcept { return stage_.load(); }

    std::optional<WorkResult> take_result() {
        std::scoped_lock lock(mutex_);
        auto result = std::move(result_);
        result_.reset();
        return result;
    }

    void wait() {
        if (thread_.joinable()) thread_.join();
    }

private:
    void wait_finished_thread() {
        if (thread_.joinable()) thread_.join();
    }

    std::jthread thread_;
    std::atomic_bool busy_ = false;
    std::atomic<assetbridge::ConversionStage> stage_ = assetbridge::ConversionStage::preflight;
    std::mutex mutex_;
    std::optional<WorkResult> result_;
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

std::optional<std::filesystem::path> command_line_path(std::wstring_view option) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == nullptr) return std::nullopt;
    std::optional<std::filesystem::path> result;
    for (int index = 1; index + 1 < count; ++index) {
        if (option == arguments[index]) {
            result.emplace(arguments[index + 1]);
            break;
        }
    }
    LocalFree(arguments);
    return result;
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

ImVec4 status_color(AppStatus status) {
    switch (status) {
    case AppStatus::success: return { 0.25F, 0.82F, 0.52F, 1.0F };
    case AppStatus::failed: return { 0.96F, 0.38F, 0.38F, 1.0F };
    case AppStatus::inspecting:
    case AppStatus::converting:
    case AppStatus::validating: return { 0.98F, 0.70F, 0.25F, 1.0F };
    default: return { 0.38F, 0.66F, 1.0F, 1.0F };
    }
}

void disabled_begin(bool disabled) {
    if (disabled) ImGui::BeginDisabled();
}

void disabled_end(bool disabled) {
    if (disabled) ImGui::EndDisabled();
}

std::optional<std::filesystem::path> glb_path(const assetbridge::ConversionReport& report) {
    for (const auto& path : report.output_files) {
        if (path.extension() == L".glb") return path;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> report_path(const assetbridge::ConversionReport& report) {
    for (const auto& path : report.output_files) {
        if (path.filename() == L"conversion-report.json") return path;
    }
    return std::nullopt;
}

void draw_ui(
    DesktopAppState& state,
    DesktopWorker& worker,
    HWND native_window,
    bool cjk_font_available,
    float ui_scale) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(
        "AssetBridgeRoot",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetWindowFontScale(1.22F);
    ImGui::TextUnformatted("AssetBridge");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextColored({ 0.62F, 0.68F, 0.76F, 1.0F }, "Lightweight 3D Asset Converter");
    ImGui::SameLine();
    ImGui::TextDisabled("  v0.1 / Phase 3 MVP");
    if (!cjk_font_available) {
        ImGui::TextColored({ 0.98F, 0.70F, 0.25F, 1.0F },
            "CJK display font was not found; Unicode file operations remain enabled.");
    }
    ImGui::Separator();

    const bool busy = state.is_busy();
    ImGui::BeginChild(
        "DropZone",
        { 0.0F, 100.0F * ui_scale },
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::Dummy({ 0.0F, 7.0F * ui_scale });
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - 190.0F * ui_scale) * 0.5F);
    ImGui::TextUnformatted("Drop an OBJ file here");
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - 122.0F * ui_scale) * 0.5F);
    ImGui::TextDisabled("拖入OBJ模型");
    ImGui::SameLine();
    disabled_begin(busy);
    if (ImGui::SmallButton("Choose OBJ")) {
        if (auto path = assetbridge::platform::windows::choose_obj_file(native_window)) {
            if (state.select_files({ *path }) == assetbridge::desktop::FileSelectionResult::accepted) {
                worker.start_inspection(*path);
            }
        }
    }
    disabled_end(busy);
    ImGui::EndChild();

    if (state.input_path()) {
        const std::string name = assetbridge::platform::windows::path_to_utf8(
            state.input_path()->filename());
        const std::string full = assetbridge::platform::windows::path_to_utf8(*state.input_path());
        ImGui::Text("File: %s", name.c_str());
        ImGui::TextWrapped("Path: %s", full.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy Path")) ImGui::SetClipboardText(full.c_str());
        if (state.summary()) {
            const auto& value = *state.summary();
            ImGui::Text("Meshes: %llu    Triangles: %llu    UV: %u    Normals: %s    Materials: %llu",
                static_cast<unsigned long long>(value.mesh_count),
                static_cast<unsigned long long>(value.triangle_count),
                value.uv_channel_count,
                value.has_normals ? "yes" : "no",
                static_cast<unsigned long long>(value.material_count));
        }
        if (state.preflight()) {
            ImGui::Text("Preflight: %s",
                std::string(assetbridge::to_string(state.preflight()->decision.overall_result)).c_str());
        }
    } else {
        ImGui::TextDisabled("No asset selected.");
    }

    ImGui::SeparatorText("Conversion Settings");
    ImGui::TextUnformatted("Input: OBJ (verified)     Output: GLB (verified)");
    const std::string output = state.output_root().empty()
        ? "Not selected"
        : assetbridge::platform::windows::path_to_utf8(state.output_root());
    ImGui::TextWrapped("Output directory: %s", output.c_str());
    ImGui::SameLine();
    disabled_begin(busy);
    if (ImGui::SmallButton("Choose Folder")) {
        if (auto path = assetbridge::platform::windows::choose_folder(native_window)) {
            state.set_output_root(*path);
        }
    }
    disabled_end(busy);

    ImGui::Spacing();
    const bool convert_disabled = !state.can_convert() || busy;
    disabled_begin(convert_disabled);
    if (ImGui::Button("Convert to GLB", { 178.0F * ui_scale, 38.0F * ui_scale })) {
        if (state.begin_conversion()) {
            worker.start_conversion(*state.input_path(), state.output_root());
        }
    }
    disabled_end(convert_disabled);

    ImGui::SameLine();
    ImGui::TextColored(status_color(state.status()), "%s",
        std::string(assetbridge::desktop::to_string(state.status())).c_str());
    ImGui::TextWrapped("%s", state.message().c_str());

    if (state.status() == AppStatus::success && state.conversion()) {
        const auto output_glb = glb_path(*state.conversion());
        if (output_glb) {
            const auto path = assetbridge::platform::windows::path_to_utf8(*output_glb);
            ImGui::TextWrapped("GLB: %s", path.c_str());
        }
        if (state.conversion()->output_directory
            && ImGui::Button("Open Output Folder")) {
            (void)assetbridge::platform::windows::open_in_shell(
                *state.conversion()->output_directory);
        }
        ImGui::SameLine();
        if (const auto json = report_path(*state.conversion()); json && ImGui::Button("Open Report")) {
            (void)assetbridge::platform::windows::open_in_shell(*json);
        }
        ImGui::SameLine();
        if (ImGui::Button("Convert Another")) state.convert_another();
    }

    ImGui::End();
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const bool smoke_test = command_line_has(L"--smoke-test");
    const bool force_scale_150 = command_line_has(L"--ui-scale-150");
    const auto acceptance_input = command_line_path(L"--acceptance-input");
    const auto acceptance_output = command_line_path(L"--acceptance-output");
    const bool acceptance_test = acceptance_input.has_value() && acceptance_output.has_value();
    const bool acceptance_hold = command_line_has(L"--acceptance-hold");
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
    SDL_Window* window = SDL_CreateWindow("AssetBridge", 760, 520, flags);
    if (window == nullptr) {
        SDL_Quit();
        CoUninitialize();
        return 11;
    }
    const float ui_scale = force_scale_150
        ? 1.5F
        : std::max(1.0F, SDL_GetWindowDisplayScale(window));
    SDL_SetWindowSize(
        window,
        static_cast<int>(760.0F * ui_scale),
        static_cast<int>(520.0F * ui_scale));
    SDL_SetWindowMinimumSize(
        window,
        static_cast<int>(640.0F * ui_scale),
        static_cast<int>(440.0F * ui_scale));
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

    DesktopAppState state;
    DesktopWorker worker;
    std::vector<std::filesystem::path> pending_drop;
    std::string acceptance_input_utf8;
    bool acceptance_conversion_started = false;
    int acceptance_exit_code = 0;
    if (acceptance_test) {
        state.set_output_root(*acceptance_output);
        acceptance_input_utf8 = assetbridge::platform::windows::path_to_utf8(*acceptance_input);
        SDL_Event begin{};
        begin.type = SDL_EVENT_DROP_BEGIN;
        begin.drop.windowID = SDL_GetWindowID(window);
        SDL_PushEvent(&begin);
        SDL_Event file{};
        file.type = SDL_EVENT_DROP_FILE;
        file.drop.windowID = SDL_GetWindowID(window);
        file.drop.data = acceptance_input_utf8.c_str();
        SDL_PushEvent(&file);
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
                    const auto copied_path = assetbridge::platform::windows::path_from_utf8(copied_utf8);
                    pending_drop.push_back(copied_path);
                } catch (const std::exception&) {
                    pending_drop.clear();
                    state.report_failure("SDL supplied an invalid UTF-8 drop path.");
                }
            } else if (event.type == SDL_EVENT_DROP_COMPLETE) {
                if (!pending_drop.empty()
                    && state.select_files(pending_drop)
                        == assetbridge::desktop::FileSelectionResult::accepted) {
                    worker.start_inspection(pending_drop.front());
                }
                pending_drop.clear();
            }
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        if (state.status() == AppStatus::converting
            && (worker.stage() == assetbridge::ConversionStage::reimporting
                || worker.stage() == assetbridge::ConversionStage::validating
                || worker.stage() == assetbridge::ConversionStage::committing)) {
            state.mark_validating();
        }
        if (auto result = worker.take_result()) {
            if (auto* inspection = std::get_if<InspectionWorkResult>(&*result)) {
                state.complete_inspection(
                    std::move(inspection->inspection), std::move(inspection->preflight));
            } else if (auto* conversion = std::get_if<ConversionWorkResult>(&*result)) {
                state.complete_conversion(std::move(conversion->report));
            }
        }
        if (acceptance_test
            && !acceptance_conversion_started
            && state.can_convert()
            && state.begin_conversion()) {
            acceptance_conversion_started = worker.start_conversion(
                *state.input_path(), state.output_root());
        }
        if (acceptance_test
            && (state.status() == AppStatus::success || state.status() == AppStatus::failed)) {
            acceptance_exit_code = state.status() == AppStatus::success ? 0 : 20;
            if (!acceptance_hold) running = false;
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
