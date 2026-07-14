#include "assetbridge/core/asset_inspector.hpp"
#include "assetbridge/core/report_serializer.hpp"
#include "assetbridge/core/runtime_capabilities.hpp"

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
    std::cerr
        << "Usage:\n"
        << "  assetbridge-cli inspect <file> [--json]\n"
        << "  assetbridge-cli capabilities [--json]\n";
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc >= 2 && std::wstring_view(argv[1]) == L"inspect") {
        const bool json_output = argc == 4 && std::wstring_view(argv[3]) == L"--json";
        if (argc != 3 && !json_output) {
            print_usage();
            return 2;
        }

        const std::filesystem::path file(argv[2]);
        const assetbridge::AssetInspector inspector;
        const auto result = inspector.inspect(file);
        if (json_output) {
            std::cout << assetbridge::inspection_report_to_json(file, result) << '\n';
        } else if (result) {
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
        } else {
            std::cerr
                << "Error [" << assetbridge::to_string(result.error_code) << "]: "
                << result.error_message << '\n';
        }

        return result ? 0 : 1;
    }

    if (argc >= 2 && std::wstring_view(argv[1]) == L"capabilities") {
        const bool json_output = argc == 3 && std::wstring_view(argv[2]) == L"--json";
        if (argc != 2 && !json_output) {
            print_usage();
            return 2;
        }

        const auto capabilities = assetbridge::query_runtime_capabilities();
        std::cout << (json_output
            ? assetbridge::capabilities_to_json(capabilities)
            : assetbridge::capabilities_to_text(capabilities));
        if (json_output) {
            std::cout << '\n';
        }
        return 0;
    }

    print_usage();
    return 2;
}
