#include "assetbridge/platform/windows/windows_platform.hpp"

#include <Windows.h>
#include <ShObjIdl.h>
#include <ShlObj.h>
#include <shellapi.h>

#include <array>
#include <stdexcept>
#include <system_error>

namespace assetbridge::platform::windows {
namespace {

std::wstring utf8_to_utf16(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        throw std::system_error(GetLastError(), std::system_category(), "Invalid UTF-8 path");
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            size) != size) {
        throw std::system_error(GetLastError(), std::system_category(), "UTF-8 conversion failed");
    }
    return result;
}

std::optional<std::filesystem::path> show_dialog(bool folder, void* owner_window) {
    IFileDialog* dialog = nullptr;
    const CLSID class_id = folder ? CLSID_FileOpenDialog : CLSID_FileOpenDialog;
    if (FAILED(CoCreateInstance(
            class_id, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (folder) {
        options |= FOS_PICKFOLDERS;
    } else {
        options |= FOS_FILEMUSTEXIST;
        const COMDLG_FILTERSPEC filters[] = {
            { L"Wavefront OBJ (*.obj)", L"*.obj" },
            { L"All files (*.*)", L"*.*" }
        };
        dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
        dialog->SetDefaultExtension(L"obj");
    }
    dialog->SetOptions(options);

    const HRESULT shown = dialog->Show(static_cast<HWND>(owner_window));
    if (FAILED(shown)) {
        dialog->Release();
        return std::nullopt;
    }
    IShellItem* item = nullptr;
    if (FAILED(dialog->GetResult(&item))) {
        dialog->Release();
        return std::nullopt;
    }
    PWSTR raw_path = nullptr;
    const HRESULT display_name = item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path);
    std::optional<std::filesystem::path> result;
    if (SUCCEEDED(display_name) && raw_path != nullptr) {
        result.emplace(raw_path);
    }
    CoTaskMemFree(raw_path);
    item->Release();
    dialog->Release();
    return result;
}

std::vector<std::filesystem::path> show_obj_files_dialog(void* owner_window) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return {};
    }

    DWORD options = 0;
    if (FAILED(dialog->GetOptions(&options))) {
        dialog->Release();
        return {};
    }
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST
        | FOS_ALLOWMULTISELECT;
    const COMDLG_FILTERSPEC filters[] = {
        { L"Wavefront OBJ (*.obj)", L"*.obj" },
        { L"All files (*.*)", L"*.*" }
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetDefaultExtension(L"obj");
    dialog->SetOptions(options);

    std::vector<std::filesystem::path> result;
    if (SUCCEEDED(dialog->Show(static_cast<HWND>(owner_window)))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dialog->GetResults(&items))) {
            DWORD count = 0;
            items->GetCount(&count);
            result.reserve(count);
            for (DWORD index = 0; index < count; ++index) {
                IShellItem* item = nullptr;
                if (FAILED(items->GetItemAt(index, &item))) continue;
                PWSTR raw_path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path))
                    && raw_path != nullptr) {
                    result.emplace_back(raw_path);
                }
                CoTaskMemFree(raw_path);
                item->Release();
            }
            items->Release();
        }
    }
    dialog->Release();
    return result;
}

} // namespace

std::filesystem::path path_from_utf8(std::string_view utf8) {
    return std::filesystem::path(utf8_to_utf16(utf8));
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return { reinterpret_cast<const char*>(value.data()), value.size() };
}

std::optional<std::filesystem::path> choose_obj_file(void* owner_window) {
    return show_dialog(false, owner_window);
}

std::vector<std::filesystem::path> choose_obj_files(void* owner_window) {
    return show_obj_files_dialog(owner_window);
}

std::optional<std::filesystem::path> choose_folder(void* owner_window) {
    return show_dialog(true, owner_window);
}

bool open_in_shell(const std::filesystem::path& path) {
    const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(
        nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
}

std::optional<std::filesystem::path> find_cjk_font() {
    PWSTR raw_fonts = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Fonts, KF_FLAG_DEFAULT, nullptr, &raw_fonts))) {
        return std::nullopt;
    }
    const std::filesystem::path fonts(raw_fonts);
    CoTaskMemFree(raw_fonts);
    constexpr std::array candidates = {
        L"msyh.ttc", L"msyhbd.ttc", L"simhei.ttf", L"simsun.ttc"
    };
    for (const auto* name : candidates) {
        const auto candidate = fonts / name;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace assetbridge::platform::windows
