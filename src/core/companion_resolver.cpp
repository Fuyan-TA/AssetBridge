#include "assetbridge/core/companion_resolver.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <map>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>

namespace assetbridge {
namespace {

struct ParsedMaterial {
    std::string name;
    std::vector<std::string> base_color_references;
    bool texture_options = false;
    bool empty_base_color_reference = false;
    bool unsupported_texture_semantic = false;
    bool transparency = false;
    std::filesystem::path library_path;
};

struct ParsedObj {
    std::vector<std::string> material_libraries;
    std::set<std::string> used_materials;
};

std::string trim(std::string value) {
    const auto is_space = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    const auto begin = std::find_if_not(value.begin(), value.end(), is_space);
    const auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
    return begin < end ? std::string(begin, end) : std::string {};
}

std::string without_comment(const std::string& line) {
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        if (line[index] == '"') quoted = !quoted;
        if (line[index] == '#' && !quoted) return line.substr(0, index);
    }
    return line;
}

std::pair<std::string, std::string> directive_and_value(const std::string& line) {
    const auto clean = trim(without_comment(line));
    const auto split = clean.find_first_of(" \t");
    if (split == std::string::npos) return { clean, {} };
    return { clean.substr(0, split), trim(clean.substr(split + 1)) };
}

std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::optional<std::vector<std::string>> read_lines(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    return stream.eof() ? std::optional<std::vector<std::string>>(std::move(lines)) : std::nullopt;
}

ParsedObj parse_obj(const std::vector<std::string>& lines) {
    ParsedObj result;
    for (const auto& line : lines) {
        const auto [directive, value] = directive_and_value(line);
        if (directive == "mtllib" && !value.empty()) {
            result.material_libraries.push_back(unquote(value));
        } else if (directive == "usemtl" && !value.empty()) {
            result.used_materials.insert(value);
        }
    }
    return result;
}

std::map<std::string, ParsedMaterial> parse_mtl(
    const std::vector<std::string>& lines,
    const std::filesystem::path& library_path) {
    std::map<std::string, ParsedMaterial> materials;
    ParsedMaterial* current = nullptr;
    for (const auto& line : lines) {
        const auto [directive, value] = directive_and_value(line);
        if (directive == "newmtl" && !value.empty()) {
            auto [iterator, inserted] = materials.emplace(value, ParsedMaterial {});
            current = &iterator->second;
            current->name = value;
            current->library_path = library_path;
            continue;
        }
        if (current == nullptr) continue;
        if (directive == "map_Kd") {
            if (value.empty()) {
                current->empty_base_color_reference = true;
            } else if (value.front() == '-') {
                current->texture_options = true;
            } else {
                current->base_color_references.push_back(unquote(value));
            }
        } else if (directive == "d" || directive == "Tr" || directive == "map_d") {
            current->transparency = true;
        } else if (directive == "map_Bump" || directive == "map_bump"
            || directive == "bump" || directive == "norm"
            || directive == "map_Ka" || directive == "map_Ks"
            || directive == "map_Ns" || directive == "map_Pr"
            || directive == "map_Pm" || directive == "map_Ps"
            || directive == "map_Ke"
            || directive == "disp" || directive == "decal"
            || directive == "refl") {
            current->unsupported_texture_semantic = true;
        }
    }
    return materials;
}

bool is_url(std::string_view reference) {
    std::string lower(reference);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return lower.starts_with("http://") || lower.starts_with("https://")
        || lower.starts_with("file://") || lower.starts_with("data:");
}

std::wstring lower_native(const std::filesystem::path& path) {
    auto value = path.lexically_normal().native();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

bool path_is_within(
    const std::filesystem::path& candidate,
    const std::filesystem::path& root) {
    const auto candidate_value = lower_native(candidate);
    auto root_value = lower_native(root);
    if (!root_value.empty() && root_value.back() != L'\\' && root_value.back() != L'/') {
        root_value.push_back(std::filesystem::path::preferred_separator);
    }
    return candidate_value == lower_native(root)
        || (candidate_value.size() > root_value.size()
            && candidate_value.compare(0, root_value.size(), root_value) == 0);
}

std::filesystem::path normalized_absolute(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

std::filesystem::path canonical_if_existing(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    return error ? normalized_absolute(path) : canonical;
}

void add_issue(
    CompanionResolution& result,
    CompanionErrorCode code,
    std::string material,
    std::string reference,
    std::string message) {
    result.issues.push_back({ code, std::move(material), std::move(reference), std::move(message) });
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

std::string lower_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

std::optional<std::vector<std::uint8_t>> read_binary_exact(
    const std::filesystem::path& path,
    std::uint64_t expected_size) {
    if (expected_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(expected_size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) return std::nullopt;

    char extra = 0;
    if (stream.read(&extra, 1)) return std::nullopt;
    if (!stream.eof()) return std::nullopt;
    return bytes;
}

bool png_has_transparency(const std::vector<std::uint8_t>& bytes) {
    constexpr std::size_t signature_size = 8;
    std::size_t offset = signature_size;
    while (offset <= bytes.size() && bytes.size() - offset >= 12) {
        const auto chunk_size = (static_cast<std::uint32_t>(bytes[offset]) << 24U)
            | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U)
            | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U)
            | static_cast<std::uint32_t>(bytes[offset + 3]);
        const auto data_offset = offset + 8;
        if (chunk_size > bytes.size() - data_offset - 4) return false;
        const std::string_view chunk_type(
            reinterpret_cast<const char*>(bytes.data() + offset + 4), 4);
        if (chunk_type == "IHDR" && chunk_size >= 13) {
            const auto color_type = bytes[data_offset + 9];
            if (color_type == 4 || color_type == 6) return true;
        }
        if (chunk_type == "tRNS") return true;
        if (chunk_type == "IEND") return false;
        offset = data_offset + chunk_size + 4;
    }
    return false;
}

std::optional<TextureFileFormat> validate_signature(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
    static constexpr std::array<std::uint8_t, 8> png_signature {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    };
    const auto extension = lower_extension(path);
    if (extension == ".png") {
        return bytes.size() >= png_signature.size()
                && std::equal(png_signature.begin(), png_signature.end(), bytes.begin())
            ? std::optional<TextureFileFormat>(TextureFileFormat::png)
            : std::nullopt;
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        return bytes.size() >= 4 && bytes[0] == 0xFF && bytes[1] == 0xD8
                && bytes[bytes.size() - 2] == 0xFF && bytes.back() == 0xD9
            ? std::optional<TextureFileFormat>(TextureFileFormat::jpeg)
            : std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> resolve_inside_root(
    const std::filesystem::path& asset_root,
    const std::filesystem::path& base,
    const std::string& reference,
    bool material_library,
    CompanionResolution& result,
    const std::string& material_name) {
    const std::u8string utf8_reference(
        reinterpret_cast<const char8_t*>(reference.data()),
        reference.size());
    const std::filesystem::path relative(utf8_reference);
    if (relative.is_absolute() || relative.has_root_name() || is_url(reference)) {
        add_issue(
            result,
            material_library ? CompanionErrorCode::material_library_path_absolute
                             : CompanionErrorCode::texture_path_absolute,
            material_name,
            reference,
            "Absolute, UNC, URL, and data-URI companion references are not allowed.");
        return std::nullopt;
    }
    const auto lexical = normalized_absolute(base / relative);
    if (!path_is_within(lexical, asset_root)) {
        add_issue(
            result,
            material_library ? CompanionErrorCode::material_library_path_outside_asset_root
                             : CompanionErrorCode::texture_path_outside_asset_root,
            material_name,
            reference,
            "Companion path escapes the OBJ asset root after normalization.");
        return std::nullopt;
    }

    std::error_code error;
    if (!std::filesystem::exists(lexical, error) || error) {
        add_issue(
            result,
            material_library ? CompanionErrorCode::material_library_missing
                             : CompanionErrorCode::texture_file_missing,
            material_name,
            reference,
            material_library ? "Referenced MTL file does not exist."
                             : "Referenced texture file does not exist.");
        return std::nullopt;
    }
    const auto canonical = canonical_if_existing(lexical);
    if (!path_is_within(canonical, asset_root)) {
        add_issue(
            result,
            material_library ? CompanionErrorCode::material_library_path_outside_asset_root
                             : CompanionErrorCode::texture_path_outside_asset_root,
            material_name,
            reference,
            "Resolved companion target escapes the OBJ asset root.");
        return std::nullopt;
    }
    if (!std::filesystem::is_regular_file(canonical, error) || error) {
        add_issue(
            result,
            material_library ? CompanionErrorCode::material_library_not_regular_file
                             : CompanionErrorCode::texture_path_not_regular_file,
            material_name,
            reference,
            "Companion path is not a regular file.");
        return std::nullopt;
    }
    return canonical;
}

} // namespace

std::string_view to_string(CompanionErrorCode code) noexcept {
    switch (code) {
    case CompanionErrorCode::none: return "none";
    case CompanionErrorCode::material_library_missing: return "material_library_missing";
    case CompanionErrorCode::material_library_path_absolute: return "material_library_path_absolute";
    case CompanionErrorCode::material_library_path_outside_asset_root: return "material_library_path_outside_asset_root";
    case CompanionErrorCode::material_library_not_regular_file: return "material_library_not_regular_file";
    case CompanionErrorCode::material_reference_unresolved: return "material_reference_unresolved";
    case CompanionErrorCode::texture_file_missing: return "texture_file_missing";
    case CompanionErrorCode::texture_path_absolute: return "texture_path_absolute";
    case CompanionErrorCode::texture_path_outside_asset_root: return "texture_path_outside_asset_root";
    case CompanionErrorCode::texture_path_not_regular_file: return "texture_path_not_regular_file";
    case CompanionErrorCode::texture_format_unsupported: return "texture_format_unsupported";
    case CompanionErrorCode::texture_signature_mismatch: return "texture_signature_mismatch";
    case CompanionErrorCode::texture_semantic_unverified: return "texture_semantic_unverified";
    case CompanionErrorCode::texture_options_unverified: return "texture_options_unverified";
    case CompanionErrorCode::transparency_unverified: return "transparency_unverified";
    case CompanionErrorCode::multiple_textures_per_material_unverified: return "multiple_textures_per_material_unverified";
    case CompanionErrorCode::texture_file_too_large: return "texture_file_too_large";
    case CompanionErrorCode::texture_count_limit_exceeded: return "texture_count_limit_exceeded";
    case CompanionErrorCode::texture_total_size_limit_exceeded: return "texture_total_size_limit_exceeded";
    case CompanionErrorCode::companion_parse_failed: return "companion_parse_failed";
    }
    return "unknown";
}

std::string_view to_string(TextureFileFormat format) noexcept {
    switch (format) {
    case TextureFileFormat::png: return "png";
    case TextureFileFormat::jpeg: return "jpeg";
    }
    return "unknown";
}

CompanionResolution CompanionResolver::resolve(
    const std::filesystem::path& obj_path,
    const std::vector<std::string>& referenced_material_names,
    const TextureLimits& limits) const {
    CompanionResolution result;
    result.asset_root = canonical_if_existing(obj_path.parent_path());
    result.referenced_material_count = referenced_material_names.size();

    const auto obj_lines = read_lines(obj_path);
    if (!obj_lines.has_value()) {
        add_issue(result, CompanionErrorCode::companion_parse_failed, {}, {},
            "Could not read OBJ companion declarations.");
        return result;
    }
    const auto parsed_obj = parse_obj(*obj_lines);
    if (parsed_obj.material_libraries.empty()) {
        if (!parsed_obj.used_materials.empty()) {
            add_issue(result, CompanionErrorCode::material_library_missing, {}, {},
                "OBJ uses named materials but declares no MTL library.");
        }
        for (const auto& name : referenced_material_names) {
            result.material_bindings.push_back({ name, std::nullopt, std::nullopt });
        }
        return result;
    }

    std::map<std::string, ParsedMaterial> materials;
    for (const auto& library_reference : parsed_obj.material_libraries) {
        const auto resolved = resolve_inside_root(
            result.asset_root,
            result.asset_root,
            library_reference,
            true,
            result,
            {});
        if (!resolved.has_value()) continue;
        result.material_libraries.push_back(*resolved);
        const auto lines = read_lines(*resolved);
        if (!lines.has_value()) {
            add_issue(result, CompanionErrorCode::companion_parse_failed, {}, library_reference,
                "Could not read referenced MTL file.");
            continue;
        }
        const auto library_materials = parse_mtl(*lines, *resolved);
        materials.insert(library_materials.begin(), library_materials.end());
    }

    std::map<std::wstring, std::size_t> texture_indices;
    for (const auto& material_name : referenced_material_names) {
        MaterialTextureBinding binding { material_name, std::nullopt, std::nullopt };
        const auto iterator = materials.find(material_name);
        if (iterator == materials.end()) {
            add_issue(result, CompanionErrorCode::material_reference_unresolved,
                material_name, {}, "A mesh-referenced material was not found in the declared MTL files.");
            result.material_bindings.push_back(std::move(binding));
            continue;
        }
        const auto& material = iterator->second;
        if (material.texture_options) {
            add_issue(result, CompanionErrorCode::texture_options_unverified,
                material_name, {}, "map_Kd options are outside the verified v0.2 boundary.");
        }
        if (material.empty_base_color_reference) {
            add_issue(result, CompanionErrorCode::companion_parse_failed,
                material_name, {}, "map_Kd does not contain a texture path.");
        }
        if (material.unsupported_texture_semantic) {
            add_issue(result, CompanionErrorCode::texture_semantic_unverified,
                material_name, {}, "The material contains a non-base-color texture semantic.");
        }
        if (material.transparency) {
            add_issue(result, CompanionErrorCode::transparency_unverified,
                material_name, {}, "Material transparency semantics are not verified.");
        }
        if (material.base_color_references.size() > 1) {
            add_issue(result, CompanionErrorCode::multiple_textures_per_material_unverified,
                material_name, {}, "Each material may reference at most one map_Kd texture.");
        }
        if (material.base_color_references.empty()) {
            result.material_bindings.push_back(std::move(binding));
            continue;
        }

        const auto& reference = material.base_color_references.front();
        binding.texture_reference = reference;
        ++result.external_texture_reference_count;
        const auto resolved = resolve_inside_root(
            result.asset_root,
            material.library_path.parent_path(),
            reference,
            false,
            result,
            material_name);
        if (!resolved.has_value()) {
            result.material_bindings.push_back(std::move(binding));
            continue;
        }
        const auto extension = lower_extension(*resolved);
        if (extension != ".png" && extension != ".jpg" && extension != ".jpeg") {
            add_issue(result, CompanionErrorCode::texture_format_unsupported,
                material_name, reference, "Only PNG and JPEG/JPG base-color textures are verified.");
            result.material_bindings.push_back(std::move(binding));
            continue;
        }

        std::error_code error;
        const auto file_size = std::filesystem::file_size(*resolved, error);
        if (error || file_size == 0) {
            add_issue(result, CompanionErrorCode::texture_signature_mismatch,
                material_name, reference, "Texture is empty or its size could not be read.");
            result.material_bindings.push_back(std::move(binding));
            continue;
        }
        if (file_size > limits.max_file_bytes) {
            add_issue(result, CompanionErrorCode::texture_file_too_large,
                material_name, reference, "Texture exceeds the per-file compressed-byte limit.");
            result.material_bindings.push_back(std::move(binding));
            continue;
        }

        const auto key = lower_native(*resolved);
        if (const auto existing = texture_indices.find(key); existing != texture_indices.end()) {
            binding.resolved_texture_index = existing->second;
            ++result.shared_texture_deduplication_count;
            result.material_bindings.push_back(std::move(binding));
            continue;
        }
        if (result.textures.size() >= limits.max_texture_count) {
            add_issue(result, CompanionErrorCode::texture_count_limit_exceeded,
                material_name, reference, "Asset exceeds the unique texture count limit.");
            result.material_bindings.push_back(std::move(binding));
            continue;
        }
        if (result.texture_bytes > limits.max_total_bytes
            || file_size > limits.max_total_bytes - result.texture_bytes) {
            add_issue(result, CompanionErrorCode::texture_total_size_limit_exceeded,
                material_name, reference, "Asset exceeds the total compressed texture-byte limit.");
            result.material_bindings.push_back(std::move(binding));
            continue;
        }

        auto bytes = read_binary_exact(*resolved, file_size);
        if (!bytes.has_value()) {
            add_issue(result, CompanionErrorCode::texture_signature_mismatch,
                material_name, reference, "Texture bytes could not be read completely.");
            result.material_bindings.push_back(std::move(binding));
            continue;
        }
        const auto format = validate_signature(*resolved, *bytes);
        if (!format.has_value()) {
            add_issue(result, CompanionErrorCode::texture_signature_mismatch,
                material_name, reference, "Texture signature does not match its PNG/JPEG extension.");
            result.material_bindings.push_back(std::move(binding));
            continue;
        }
        if (*format == TextureFileFormat::png && png_has_transparency(*bytes)) {
            add_issue(result, CompanionErrorCode::transparency_unverified,
                material_name, reference, "PNG contains alpha or tRNS transparency semantics.");
            result.material_bindings.push_back(std::move(binding));
            continue;
        }

        const auto index = result.textures.size();
        texture_indices.emplace(key, index);
        binding.resolved_texture_index = index;
        result.texture_bytes += file_size;
        result.textures.push_back({
            *resolved,
            path_to_utf8(std::filesystem::relative(*resolved, result.asset_root)),
            *format,
            *format == TextureFileFormat::png ? "image/png" : "image/jpeg",
            file_size,
            std::move(*bytes)
        });
        result.material_bindings.push_back(std::move(binding));
    }
    return result;
}

} // namespace assetbridge
