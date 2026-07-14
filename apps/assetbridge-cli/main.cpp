#include "assetbridge/core/asset_inspector.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

std::string to_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

void print_usage() {
    std::cerr << "Usage: assetbridge-cli inspect <file>\n";
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 3 || std::wstring_view(argv[1]) != L"inspect") {
        print_usage();
        return 2;
    }

    const assetbridge::AssetInspector inspector;
    // wchar_t arguments from wmain are UTF-16 on Windows. Constructing the
    // filesystem path directly preserves them without an ANSI code-page hop.
    const auto result = inspector.inspect(std::filesystem::path(argv[2]));
    if (!result) {
        std::cerr
            << "Error [" << assetbridge::to_string(result.error_code) << "]: "
            << result.error_message << '\n';
        return 1;
    }

    const auto& summary = *result.summary;
    std::cout
        << "File: " << to_utf8(summary.file_path) << '\n'
        << "Meshes: " << summary.mesh_count << '\n'
        << "Vertices: " << summary.vertex_count << '\n'
        << "Faces: " << summary.face_count << '\n'
        << "Triangles: " << summary.triangle_count << '\n'
        << "Materials: " << summary.material_count << '\n'
        << "UV Channels: " << summary.uv_channel_count << '\n'
        << "Has Normals: " << (summary.has_normals ? "yes" : "no") << '\n';

    return 0;
}
