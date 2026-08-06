#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace assetbridge::platform::windows {

[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view utf8);
[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);
[[nodiscard]] std::optional<std::filesystem::path> choose_obj_file(void* owner_window);
[[nodiscard]] std::vector<std::filesystem::path> choose_obj_files(void* owner_window);
[[nodiscard]] std::optional<std::filesystem::path> choose_folder(void* owner_window);
[[nodiscard]] bool open_in_shell(const std::filesystem::path& path);
[[nodiscard]] std::optional<std::filesystem::path> find_cjk_font();

} // namespace assetbridge::platform::windows
