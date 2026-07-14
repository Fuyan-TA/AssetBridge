#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {

using Json = nlohmann::json;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::string path_to_generic_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
}

std::string expected_report_path(const std::filesystem::path& input) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(input, error);
    require(!error, "Could not make the expected input path absolute");
    return path_to_generic_utf8(absolute.lexically_normal());
}

void require_exact_keys(
    const Json& value,
    std::initializer_list<std::string_view> expected_keys) {
    require(value.is_object(), "Expected a JSON object");
    require(value.size() == expected_keys.size(), "JSON object has an unstable field set");
    for (const auto key : expected_keys) {
        require(value.contains(std::string(key)), "JSON object is missing a required field");
    }
}

void validate_file(const Json& file, const std::filesystem::path& input) {
    require_exact_keys(file, { "path", "extension" });
    require(file.at("path").is_string(), "file.path must be a string");
    require(file.at("extension").is_string(), "file.extension must be a string");
    require(
        file.at("path").get<std::string>() == expected_report_path(input),
        "file.path does not match the requested input path");
    require(file.at("extension") == ".obj", "file.extension must be .obj");
}

void validate_inspection_success(const Json& report, const std::filesystem::path& input) {
    require_exact_keys(report, { "schema", "status", "file", "summary", "error" });
    require(report.at("schema").is_string(), "schema must be a string");
    require(report.at("schema") == "assetbridge.inspect.v1", "inspection schema mismatch");
    require(report.at("status").is_string(), "status must be a string");
    require(report.at("status") == "success", "successful inspection status mismatch");
    validate_file(report.at("file"), input);
    require(report.at("error").is_null(), "successful inspection error must be null");

    const auto& summary = report.at("summary");
    require_exact_keys(summary, {
        "mesh_count",
        "vertex_count",
        "face_count",
        "triangle_count",
        "material_count",
        "uv_channel_count",
        "has_normals"
    });
    for (const auto field : {
             "mesh_count",
             "vertex_count",
             "face_count",
             "triangle_count",
             "material_count",
             "uv_channel_count" }) {
        require(summary.at(field).is_number_unsigned(), "summary count must be unsigned JSON number");
    }
    require(summary.at("mesh_count") == 1, "mesh_count mismatch");
    require(summary.at("vertex_count") == 3, "vertex_count mismatch");
    require(summary.at("face_count") == 1, "face_count mismatch");
    require(summary.at("triangle_count") == 1, "triangle_count mismatch");
    require(summary.at("material_count") == 2, "material_count mismatch");
    require(summary.at("uv_channel_count") == 1, "uv_channel_count mismatch");
    require(summary.at("has_normals").is_boolean(), "has_normals must be a boolean");
    require(summary.at("has_normals") == true, "has_normals mismatch");
}

void validate_inspection_error(const Json& report, const std::filesystem::path& input) {
    require_exact_keys(report, { "schema", "status", "file", "summary", "error" });
    require(report.at("schema").is_string(), "schema must be a string");
    require(report.at("schema") == "assetbridge.inspect.v1", "inspection schema mismatch");
    require(report.at("status").is_string(), "status must be a string");
    require(report.at("status") == "error", "failed inspection status mismatch");
    validate_file(report.at("file"), input);
    require(report.at("summary").is_null(), "failed inspection summary must be null");

    const auto& error = report.at("error");
    require_exact_keys(error, { "code", "message" });
    require(error.at("code").is_string(), "error.code must be a string");
    require(error.at("code") == "file_not_found", "error.code mismatch");
    require(error.at("message").is_string(), "error.message must be a string");
    require(!error.at("message").get<std::string>().empty(), "error.message must not be empty");
}

void validate_capabilities(const Json& report) {
    require_exact_keys(report, { "schema", "assimp_version", "importers", "exporters" });
    require(report.at("schema").is_string(), "schema must be a string");
    require(report.at("schema") == "assetbridge.capabilities.v1", "capabilities schema mismatch");
    require(report.at("assimp_version").is_string(), "assimp_version must be a string");
    require(!report.at("assimp_version").get<std::string>().empty(), "assimp_version is empty");

    const auto& importers = report.at("importers");
    require(importers.is_array() && !importers.empty(), "importers must be a non-empty array");
    bool obj_verified = false;
    for (const auto& importer : importers) {
        require_exact_keys(importer, { "name", "formats" });
        require(importer.at("name").is_string(), "importer name must be a string");
        require(importer.at("formats").is_array(), "importer formats must be an array");
        for (const auto& format : importer.at("formats")) {
            require_exact_keys(format, {
                "extension",
                "runtime_available",
                "product_enabled",
                "verified"
            });
            require(format.at("extension").is_string(), "input extension must be a string");
            require(format.at("runtime_available").is_boolean(), "runtime_available must be boolean");
            require(format.at("product_enabled").is_boolean(), "product_enabled must be boolean");
            require(format.at("verified").is_boolean(), "verified must be boolean");
            require(format.at("runtime_available") == true, "enumerated input must be runtime available");

            const auto extension = format.at("extension").get<std::string>();
            const bool product_enabled = format.at("product_enabled").get<bool>();
            const bool verified = format.at("verified").get<bool>();
            if (extension == ".obj") {
                require(product_enabled && verified, "OBJ input must be enabled and verified");
                obj_verified = true;
            } else {
                require(!product_enabled && !verified, "non-OBJ input must remain unsupported");
            }
        }
    }
    require(obj_verified, "No enabled and verified OBJ input was reported");

    const auto& exporters = report.at("exporters");
    require(exporters.is_array() && !exporters.empty(), "exporters must be a non-empty array");
    for (const auto& exporter : exporters) {
        require_exact_keys(exporter, {
            "id",
            "description",
            "extension",
            "runtime_available",
            "product_enabled",
            "verified"
        });
        require(exporter.at("id").is_string(), "exporter id must be a string");
        require(exporter.at("description").is_string(), "exporter description must be a string");
        require(exporter.at("extension").is_string(), "exporter extension must be a string");
        require(exporter.at("runtime_available").is_boolean(), "runtime_available must be boolean");
        require(exporter.at("product_enabled").is_boolean(), "product_enabled must be boolean");
        require(exporter.at("verified").is_boolean(), "verified must be boolean");
        require(exporter.at("runtime_available") == true, "enumerated exporter must be runtime available");
        require(exporter.at("product_enabled") == false, "all outputs must remain disabled");
        require(exporter.at("verified") == false, "all outputs must remain unverified");
    }
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: assetbridge-json-validator <mode> <json-file> [input-file]\n";
        return 2;
    }

    try {
        const std::wstring_view mode(argv[1]);
        std::ifstream input(argv[2], std::ios::binary);
        require(static_cast<bool>(input), "Could not open captured JSON output");
        const Json report = Json::parse(input);

        if (mode == L"inspect_success") {
            require(argc == 4, "inspect_success requires an input file");
            validate_inspection_success(report, std::filesystem::path(argv[3]));
        } else if (mode == L"inspect_error") {
            require(argc == 4, "inspect_error requires an input file");
            validate_inspection_error(report, std::filesystem::path(argv[3]));
        } else if (mode == L"capabilities") {
            require(argc == 3, "capabilities does not accept an input file");
            validate_capabilities(report);
        } else {
            throw std::runtime_error("Unknown JSON validation mode");
        }

        std::cout << "JSON parsed and validated successfully.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "JSON validation failed: " << exception.what() << '\n';
        return 1;
    }
}
