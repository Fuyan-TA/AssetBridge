#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "Could not open captured text output");
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
}

void validate_preflight_features(const Json& features) {
    require_exact_keys(features, {
        "mesh_count",
        "has_node_hierarchy",
        "meshes_with_normals",
        "meshes_with_tangents",
        "max_uv_channel_count",
        "meshes_with_vertex_colors",
        "referenced_material_count",
        "has_pbr_materials",
        "external_texture_references",
        "embedded_texture_count",
        "bone_count",
        "skinned_mesh_count",
        "max_weights_per_vertex",
        "animation_count",
        "animations",
        "morph_target_count",
        "morph_target_names"
    });
    for (const auto field : {
             "mesh_count",
             "meshes_with_normals",
             "meshes_with_tangents",
             "max_uv_channel_count",
             "meshes_with_vertex_colors",
             "referenced_material_count",
             "embedded_texture_count",
             "bone_count",
             "skinned_mesh_count",
             "max_weights_per_vertex",
             "animation_count",
             "morph_target_count" }) {
        require(features.at(field).is_number_unsigned(), "feature count must be unsigned");
    }
    require(features.at("has_node_hierarchy").is_boolean(), "hierarchy flag must be boolean");
    require(
        features.at("has_node_hierarchy") == false,
        "minimal OBJ must not report Assimp's flat wrapper as meaningful hierarchy");
    require(features.at("has_pbr_materials").is_boolean(), "PBR flag must be boolean");
    require(features.at("external_texture_references").is_array(), "texture references must be an array");
    require(features.at("animations").is_array(), "animations must be an array");
    require(features.at("morph_target_names").is_array(), "morph names must be an array");
    require(features.at("mesh_count") == 1, "preflight mesh_count mismatch");
    require(features.at("meshes_with_normals") == 1, "preflight normal count mismatch");
    require(features.at("max_uv_channel_count") == 1, "preflight UV count mismatch");
    require(features.at("referenced_material_count") == 1, "referenced material count mismatch");
    require(features.at("animation_count") == 0, "OBJ must not invent animations");
    require(features.at("bone_count") == 0, "OBJ must not invent bones");
    require(features.at("morph_target_count") == 0, "OBJ must not invent morph targets");
}

void validate_assessments(const Json& assessments) {
    require(assessments.is_array() && !assessments.empty(), "assessments must be non-empty");
    for (const auto& assessment : assessments) {
        require_exact_keys(assessment, { "feature", "support", "reason" });
        require(assessment.at("feature").is_string(), "assessment feature must be a string");
        require(assessment.at("support").is_string(), "assessment support must be a string");
        require(assessment.at("reason").is_string(), "assessment reason must be a string");
        const auto feature = assessment.at("feature").get<std::string>();
        require(feature != "animations", "absent animation must not be assessed");
        require(feature != "bones", "absent bones must not be assessed");
        require(feature != "morph_targets", "absent morph targets must not be assessed");
    }
}

std::vector<std::string> validate_losses(const Json& losses) {
    require(losses.is_array(), "losses must be an array");
    std::vector<std::string> codes;
    for (const auto& loss : losses) {
        require_exact_keys(loss, { "code", "feature", "severity", "overrideable", "reason" });
        require(loss.at("code").is_string(), "loss code must be a string");
        require(
            loss.at("feature").is_string() || loss.at("feature").is_null(),
            "loss feature must be string or null");
        require(loss.at("severity").is_string(), "loss severity must be a string");
        require(loss.at("overrideable").is_boolean(), "loss overrideable must be boolean");
        require(loss.at("reason").is_string(), "loss reason must be a string");
        codes.push_back(loss.at("code").get<std::string>());
    }
    return codes;
}

void validate_preflight_success(
    const Json& report,
    const std::filesystem::path& input,
    std::string_view expected_target,
    std::string_view expected_compatibility,
    std::string_view expected_overall,
    bool expected_product_enabled,
    bool expected_verified,
    const std::filesystem::path& text_path) {
    require_exact_keys(report, {
        "schema",
        "status",
        "source",
        "target",
        "features",
        "assessments",
        "losses",
        "compatibility_result",
        "product_enabled",
        "verified",
        "overall_result",
        "error"
    });
    require(report.at("schema") == "assetbridge.preflight.v1", "preflight schema mismatch");
    require(report.at("status") == "success", "preflight status mismatch");
    require(report.at("error").is_null(), "successful preflight error must be null");
    require(report.at("product_enabled").is_boolean(), "product_enabled must be boolean");
    require(report.at("verified").is_boolean(), "verified must be boolean");
    require(
        report.at("product_enabled") == expected_product_enabled,
        "preflight route product_enabled mismatch");
    require(
        report.at("verified") == expected_verified,
        "preflight route verified mismatch");
    require(
        report.at("compatibility_result").get<std::string>() == expected_compatibility,
        "compatibility mismatch");
    require(
        report.at("overall_result").get<std::string>() == expected_overall,
        "overall result mismatch");

    const auto& source = report.at("source");
    require_exact_keys(source, { "file", "format_id", "product_enabled", "verified", "valid" });
    validate_file(source.at("file"), input);
    require(source.at("format_id") == "obj", "source canonical format must be obj");
    require(source.at("product_enabled") == true, "OBJ input must be enabled");
    require(source.at("verified") == true, "OBJ input must be verified");
    require(source.at("valid") == true, "source must be valid");

    const auto& target = report.at("target");
    require_exact_keys(target, {
        "format_id",
        "runtime_exporter_available",
        "product_enabled",
        "verified"
    });
    require(
        target.at("format_id").get<std::string>() == expected_target,
        "target canonical ID mismatch");
    require(target.at("runtime_exporter_available") == true, "expected runtime exporter is absent");
    require(
        target.at("product_enabled") == expected_product_enabled,
        "target route product_enabled mismatch");
    require(target.at("verified") == expected_verified, "target route verified mismatch");

    validate_preflight_features(report.at("features"));
    validate_assessments(report.at("assessments"));
    const auto loss_codes = validate_losses(report.at("losses"));

    if (expected_target == "stl") {
        require(!loss_codes.empty(), "OBJ to STL must report losses");
        require(
            std::find(loss_codes.begin(), loss_codes.end(), "normals_partial") != loss_codes.end(),
            "OBJ to STL must report partial normal preservation");
        require(
            std::find(loss_codes.begin(), loss_codes.end(), "uv0_unsupported") != loss_codes.end(),
            "OBJ to STL must report UV loss");
        require(
            std::find(loss_codes.begin(), loss_codes.end(), "material_slots_unsupported") != loss_codes.end(),
            "OBJ to STL must report material loss");
        require(
            std::find(
                loss_codes.begin(),
                loss_codes.end(),
                "node_hierarchy_unsupported") == loss_codes.end(),
            "minimal OBJ must not report a hierarchy loss");
    } else if (expected_target == "glb2") {
        require(loss_codes.empty(), "OBJ to GLB must not invent a known loss");
    }

    const auto text = read_text_file(text_path);
    require(
        text.find("Target Format: " + std::string(expected_target)) != std::string::npos,
        "text and JSON target disagree");
    require(
        text.find("Compatibility Result: " + std::string(expected_compatibility)) != std::string::npos,
        "text and JSON compatibility disagree");
    require(
        text.find("Overall Result: " + std::string(expected_overall)) != std::string::npos,
        "text and JSON overall result disagree");
    for (const auto& code : loss_codes) {
        require(text.find(code) != std::string::npos, "text output is missing a JSON loss code");
    }
    require(
        text.find("node_hierarchy_unsupported") == std::string::npos,
        "text output must not report a hierarchy loss for minimal OBJ");
}

void validate_unknown_target(const Json& report) {
    require_exact_keys(report, {
        "schema",
        "status",
        "source",
        "target",
        "features",
        "assessments",
        "losses",
        "compatibility_result",
        "product_enabled",
        "verified",
        "overall_result",
        "error"
    });
    require(report.at("schema") == "assetbridge.preflight.v1", "preflight schema mismatch");
    require(report.at("status") == "error", "unknown target status must be error");
    require(report.at("source").is_null(), "unknown target source must be null");
    require(report.at("target").is_null(), "unknown target must not have a canonical ID");
    require(report.at("features").is_null(), "unknown target features must be null");
    require(report.at("assessments").is_array() && report.at("assessments").empty(), "unknown target assessments must be empty");
    const auto codes = validate_losses(report.at("losses"));
    require(codes.size() == 1 && codes.front() == "unknown_target_format", "unknown target loss mismatch");
    const auto& loss = report.at("losses").front();
    require(loss.at("severity") == "blocking", "unknown target must block");
    require(loss.at("overrideable") == false, "unknown target must not be overrideable");
    require(report.at("compatibility_result") == "blocked", "unknown target compatibility mismatch");
    require(report.at("overall_result") == "blocked", "unknown target overall mismatch");
    require(report.at("product_enabled") == false, "unknown target must be disabled");
    require(report.at("verified") == false, "unknown target must be unverified");
    require_exact_keys(report.at("error"), { "code", "message" });
    require(report.at("error").at("code") == "unknown_target_format", "unknown target error mismatch");
}

std::filesystem::path path_from_utf8(std::string_view value) {
    const auto* begin = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(begin, begin + value.size()));
}

void validate_conversion_analysis(const Json& analysis) {
    require_exact_keys(analysis, {
        "mesh_count",
        "vertex_count",
        "triangle_count",
        "referenced_material_count",
        "has_normals",
        "has_uv0",
        "bounds",
        "diffuse_color"
    });
    for (const auto field : {
             "mesh_count", "vertex_count", "triangle_count", "referenced_material_count" }) {
        require(analysis.at(field).is_number_unsigned(), "conversion analysis count must be unsigned");
    }
    require(analysis.at("mesh_count") == 1, "converted mesh count mismatch");
    require(analysis.at("triangle_count") == 1, "converted triangle count mismatch");
    require(analysis.at("referenced_material_count") == 1, "referenced material count mismatch");
    require(analysis.at("has_normals") == true, "converted scene must contain normals");
    require(analysis.at("has_uv0") == true, "converted scene must contain UV0");

    const auto& bounds = analysis.at("bounds");
    require_exact_keys(bounds, { "valid", "minimum", "maximum", "center", "size" });
    require(bounds.at("valid") == true, "conversion bounds must be valid");
    for (const auto vector_name : { "minimum", "maximum", "center", "size" }) {
        const auto& vector = bounds.at(vector_name);
        require_exact_keys(vector, { "x", "y", "z" });
        require(vector.at("x").is_number(), "bounds x must be numeric");
        require(vector.at("y").is_number(), "bounds y must be numeric");
        require(vector.at("z").is_number(), "bounds z must be numeric");
    }

    const auto& color = analysis.at("diffuse_color");
    require_exact_keys(color, { "red", "green", "blue", "alpha" });
    for (const auto channel : { "red", "green", "blue", "alpha" }) {
        require(color.at(channel).is_number(), "diffuse color channel must be numeric");
    }
}

void validate_conversion_success(const Json& report, const std::filesystem::path& input) {
    require_exact_keys(report, {
        "schema",
        "status",
        "source",
        "route",
        "output",
        "source_analysis",
        "preflight",
        "processing_steps",
        "output_analysis",
        "validation_checks",
        "warnings",
        "error",
        "assimp_version",
        "timings_ms"
    });
    require(report.at("schema") == "assetbridge.conversion.v1", "conversion schema mismatch");
    require(report.at("status") == "success", "conversion success status mismatch");
    require(report.at("error").is_null(), "successful conversion error must be null");

    const auto& source = report.at("source");
    require_exact_keys(source, { "path", "format_id" });
    require(source.at("path") == expected_report_path(input), "conversion source path mismatch");
    require(source.at("format_id") == "obj", "conversion source canonical ID mismatch");

    const auto& route = report.at("route");
    require_exact_keys(route, {
        "source_format_id", "target_format_id", "product_enabled", "verified" });
    require(route.at("source_format_id") == "obj", "route source canonical ID mismatch");
    require(route.at("target_format_id") == "glb2", "route target canonical ID mismatch");
    require(route.at("product_enabled") == true, "OBJ to GLB2 route must be enabled");
    require(route.at("verified") == true, "OBJ to GLB2 route must be verified");

    const auto& output = report.at("output");
    require_exact_keys(output, { "directory", "files" });
    require(output.at("directory").is_string(), "successful output directory must be a string");
    require(output.at("files").is_array() && output.at("files").size() == 2,
        "successful conversion must report GLB and JSON files");
    const std::filesystem::path output_directory =
        path_from_utf8(output.at("directory").get_ref<const std::string&>());
    require(std::filesystem::is_directory(output_directory), "reported output directory does not exist");

    std::filesystem::path glb_path;
    std::filesystem::path report_path;
    for (const auto& file : output.at("files")) {
        require(file.is_string(), "output file path must be a string");
        const auto path = path_from_utf8(file.get_ref<const std::string&>());
        require(std::filesystem::is_regular_file(path), "reported output file does not exist");
        if (path.extension() == ".glb") {
            glb_path = path;
        } else if (path.filename() == "conversion-report.json") {
            report_path = path;
        }
    }
    require(!glb_path.empty() && std::filesystem::file_size(glb_path) > 0,
        "generated GLB must exist and be non-empty");
    require(!report_path.empty(), "conversion-report.json was not reported");

    validate_conversion_analysis(report.at("source_analysis"));
    validate_conversion_analysis(report.at("output_analysis"));
    require(
        report.at("source_analysis").at("triangle_count")
            == report.at("output_analysis").at("triangle_count"),
        "source and output triangle counts must match");
    const auto& source_color = report.at("source_analysis").at("diffuse_color");
    const auto& output_color = report.at("output_analysis").at("diffuse_color");
    for (const auto channel : { "red", "green", "blue" }) {
        require(
            std::abs(source_color.at(channel).get<double>()
                - output_color.at(channel).get<double>()) < 1.0e-4,
            "MTL diffuse color did not survive conversion");
    }
    const bool unicode_asset = input.filename() == L"中文三角形.obj";
    const double expected_red = unicode_asset ? 0.125 : 0.8;
    const double expected_green = unicode_asset ? 0.5 : 0.8;
    const double expected_blue = unicode_asset ? 0.875 : 0.8;
    require(std::abs(source_color.at("red").get<double>() - expected_red) < 1.0e-4,
        "source MTL red channel was not resolved");
    require(std::abs(source_color.at("green").get<double>() - expected_green) < 1.0e-4,
        "source MTL green channel was not resolved");
    require(std::abs(source_color.at("blue").get<double>() - expected_blue) < 1.0e-4,
        "source MTL blue channel was not resolved");

    const auto& preflight = report.at("preflight");
    require(preflight.at("schema") == "assetbridge.preflight.v1", "embedded preflight schema mismatch");
    require(preflight.at("overall_result") == "safe", "verified route preflight must be safe");
    require(preflight.at("product_enabled") == true, "embedded preflight route must be enabled");
    require(preflight.at("verified") == true, "embedded preflight route must be verified");

    const auto& steps = report.at("processing_steps");
    require(steps.is_array() && steps.size() == 2, "processing step list must be exact");
    require(std::find(steps.begin(), steps.end(), "aiProcess_Triangulate") != steps.end(),
        "triangulation step must be recorded");
    require(std::find(steps.begin(), steps.end(), "aiProcess_ValidateDataStructure") != steps.end(),
        "data validation step must be recorded");
    for (const auto forbidden : {
             "GenerateNormals", "CalcTangentSpace", "JoinIdenticalVertices",
             "OptimizeMeshes", "MakeLeftHanded", "FlipUVs" }) {
        require(std::find(steps.begin(), steps.end(), forbidden) == steps.end(),
            "forbidden processing step was reported");
    }

    const auto& checks = report.at("validation_checks");
    require(checks.is_array() && checks.size() >= 10, "round-trip checks are incomplete");
    std::vector<std::string> check_names;
    for (const auto& check : checks) {
        require_exact_keys(check, { "name", "passed", "details" });
        require(check.at("name").is_string(), "validation check name must be a string");
        require(check.at("passed") == true, "all committed validation checks must pass");
        require(check.at("details").is_string(), "validation details must be a string");
        check_names.push_back(check.at("name").get<std::string>());
    }
    for (const auto required : {
             "output_file_nonempty", "reimport_scene_valid", "mesh_indices_valid",
             "triangle_count", "aabb_center", "aabb_size", "normals_preserved",
             "uv0_preserved", "referenced_material_present", "diffuse_color" }) {
        require(std::find(check_names.begin(), check_names.end(), required) != check_names.end(),
            "required validation check is missing");
    }

    require(report.at("warnings").is_array(), "warnings must be an array");
    require(report.at("assimp_version").is_string()
        && !report.at("assimp_version").get<std::string>().empty(),
        "Assimp version must be reported");
    const auto& timings = report.at("timings_ms");
    require_exact_keys(timings, {
        "preflight", "import", "export", "reimport", "validation", "total" });
    for (const auto field : { "preflight", "import", "export", "reimport", "validation", "total" }) {
        require(timings.at(field).is_number() && timings.at(field).get<double>() >= 0.0,
            "conversion timing must be a non-negative number");
    }

    std::ifstream committed_input(report_path, std::ios::binary);
    require(static_cast<bool>(committed_input), "could not reopen committed conversion report");
    const Json committed_report = Json::parse(committed_input);
    require(committed_report == report, "stdout JSON and committed report must match");

    for (const auto& entry : std::filesystem::directory_iterator(output_directory.parent_path())) {
        require(!entry.path().filename().string().starts_with(".assetbridge-tmp-"),
            "successful conversion left a temporary directory");
    }
}

void validate_conversion_error(const Json& report, std::string_view expected_code) {
    require(report.at("schema") == "assetbridge.conversion.v1", "conversion error schema mismatch");
    require(report.at("status") == "error", "conversion error status mismatch");
    const auto& output = report.at("output");
    require(output.at("directory").is_null(), "failed conversion must not report a final directory");
    require(output.at("files").is_array() && output.at("files").empty(),
        "failed conversion must not report output files");
    require(report.at("error").is_object(), "failed conversion must report an error object");
    require(
        report.at("error").at("code").get<std::string>() == expected_code,
        "conversion error code mismatch");
    require(report.at("error").at("message").is_string()
        && !report.at("error").at("message").get<std::string>().empty(),
        "conversion error message must be non-empty");
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 3 || argc > 5) {
        std::cerr << "Usage: assetbridge-json-validator <mode> <json-file> [input-file] [text-file]\n";
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
        } else if (mode == L"preflight_stl") {
            require(argc == 5, "preflight_stl requires input and text files");
            validate_preflight_success(
                report,
                std::filesystem::path(argv[3]),
                "stl",
                "lossy",
                "lossy",
                false,
                false,
                std::filesystem::path(argv[4]));
        } else if (mode == L"preflight_glb") {
            require(argc == 5, "preflight_glb requires input and text files");
            validate_preflight_success(
                report,
                std::filesystem::path(argv[3]),
                "glb2",
                "safe",
                "safe",
                true,
                true,
                std::filesystem::path(argv[4]));
        } else if (mode == L"preflight_unknown") {
            require(argc == 4, "preflight_unknown requires the requested input file");
            validate_unknown_target(report);
        } else if (mode == L"conversion_success") {
            require(argc == 4, "conversion_success requires an input file");
            validate_conversion_success(report, std::filesystem::path(argv[3]));
        } else if (mode == L"conversion_import_error") {
            require(argc == 4, "conversion_import_error requires an input file");
            validate_conversion_error(report, "import_failed");
        } else if (mode == L"conversion_unverified") {
            require(argc == 4, "conversion_unverified requires an input file");
            validate_conversion_error(report, "route_feature_unverified");
        } else if (mode == L"conversion_unknown") {
            require(argc == 4, "conversion_unknown requires an input file");
            validate_conversion_error(report, "unknown_target_format");
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
