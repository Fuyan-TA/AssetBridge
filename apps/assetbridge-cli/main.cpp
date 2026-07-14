#include "assetbridge/core/asset_inspector.hpp"
#include "assetbridge/core/asset_converter.hpp"
#include "assetbridge/core/conversion_serializer.hpp"
#include "assetbridge/core/preflight_report.hpp"
#include "assetbridge/core/preflight_serializer.hpp"
#include "assetbridge/core/report_serializer.hpp"
#include "assetbridge/core/runtime_capabilities.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Windows.h>

namespace {

std::string to_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

std::string utf16_to_utf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const int required_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required_size <= 0) {
        throw std::runtime_error("Could not convert a UTF-16 argument to UTF-8.");
    }

    std::string result(static_cast<std::size_t>(required_size), '\0');
    const int converted_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required_size,
        nullptr,
        nullptr);
    if (converted_size != required_size) {
        throw std::runtime_error("Could not convert a UTF-16 argument to UTF-8.");
    }
    return result;
}

void print_usage() {
    std::cerr
        << "Usage:\n"
        << "  assetbridge-cli inspect <file> [--json]\n"
        << "  assetbridge-cli capabilities [--json]\n"
        << "  assetbridge-cli preflight <file> --target <format> [--json]\n"
        << "  assetbridge-cli convert <input.obj> --to glb --output <directory> [--json]\n";
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

    if (argc >= 2 && std::wstring_view(argv[1]) == L"preflight") {
        const bool json_output = argc == 6 && std::wstring_view(argv[5]) == L"--json";
        if ((argc != 5 && !json_output) || std::wstring_view(argv[3]) != L"--target") {
            print_usage();
            return 2;
        }

        std::string requested_target;
        try {
            requested_target = utf16_to_utf8(argv[4]);
        } catch (const std::exception& exception) {
            if (json_output) {
                std::cout << assetbridge::unknown_target_to_json("invalid UTF-16 argument") << '\n';
            } else {
                std::cerr << "Error: " << exception.what() << '\n';
            }
            return 2;
        }

        const auto target = assetbridge::parse_format_id(requested_target);
        if (!target.has_value()) {
            if (json_output) {
                std::cout << assetbridge::unknown_target_to_json(requested_target) << '\n';
            } else {
                std::cerr << assetbridge::unknown_target_to_text(requested_target);
            }
            return 2;
        }

        const std::filesystem::path file(argv[2]);
        const auto report = assetbridge::create_preflight_report(file, *target);
        if (json_output) {
            std::cout << assetbridge::preflight_to_json(report) << '\n';
        } else if (report) {
            std::cout << assetbridge::preflight_to_text(report);
        } else {
            std::cerr << assetbridge::preflight_to_text(report);
        }
        return report ? 0 : 1;
    }

    if (argc >= 2 && std::wstring_view(argv[1]) == L"convert") {
        const bool json_output = argc == 8 && std::wstring_view(argv[7]) == L"--json";
        if ((argc != 7 && !json_output)
            || std::wstring_view(argv[3]) != L"--to"
            || std::wstring_view(argv[5]) != L"--output") {
            if (argc >= 3 && std::wstring_view(argv[argc - 1]) == L"--json") {
                std::cout << assetbridge::conversion_argument_error_to_json(
                    "invalid_arguments",
                    "Expected: convert <input.obj> --to glb --output <directory> [--json]")
                          << '\n';
            } else {
                print_usage();
            }
            return 2;
        }

        std::string requested_target;
        try {
            requested_target = utf16_to_utf8(argv[4]);
        } catch (const std::exception& exception) {
            if (json_output) {
                std::cout << assetbridge::conversion_argument_error_to_json(
                    "invalid_target_format",
                    exception.what()) << '\n';
            } else {
                std::cerr << "Error: " << exception.what() << '\n';
            }
            return 2;
        }

        const auto target = assetbridge::parse_format_id(requested_target);
        if (!target.has_value()) {
            const std::string message = "Unknown target format: " + requested_target;
            if (json_output) {
                std::cout << assetbridge::conversion_argument_error_to_json(
                    "unknown_target_format",
                    message) << '\n';
            } else {
                std::cerr << "Error [unknown_target_format]: " << message << '\n';
            }
            return 2;
        }

        const assetbridge::AssetConverter converter;
        const auto report = converter.convert(
            std::filesystem::path(argv[2]),
            *target,
            std::filesystem::path(argv[6]));
        if (json_output) {
            std::cout << assetbridge::conversion_report_to_json(report) << '\n';
        } else if (report) {
            std::cout << assetbridge::conversion_report_to_text(report);
        } else {
            std::cerr << assetbridge::conversion_report_to_text(report);
        }
        return report ? 0 : 1;
    }

    print_usage();
    return 2;
}
