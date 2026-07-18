#include "assetbridge/core/glb_container.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>

namespace assetbridge {
namespace {

using Json = nlohmann::json;

struct GlbDocument {
    Json json;
    std::vector<std::uint8_t> binary;
};

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not open generated GLB.");
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

std::uint32_t read_u32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("GLB integer field is out of range.");
    }
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

GlbDocument parse_glb(const std::filesystem::path& file) {
    const auto bytes = read_bytes(file);
    if (bytes.size() < 20) throw std::runtime_error("GLB is too small.");
    if (read_u32(bytes, 0) != 0x46546C67U) {
        throw std::runtime_error("GLB magic is invalid.");
    }
    if (read_u32(bytes, 4) != 2U) {
        throw std::runtime_error("GLB version is not 2.");
    }
    if (read_u32(bytes, 8) != bytes.size()) {
        throw std::runtime_error("GLB declared length does not match file size.");
    }

    std::size_t offset = 12;
    const auto json_length = read_u32(bytes, offset);
    if (read_u32(bytes, offset + 4) != 0x4E4F534AU) {
        throw std::runtime_error("First GLB chunk is not JSON.");
    }
    offset += 8;
    if (json_length > bytes.size() - offset) {
        throw std::runtime_error("GLB JSON chunk exceeds the file.");
    }
    std::string json_text(
        reinterpret_cast<const char*>(bytes.data() + offset),
        json_length);
    while (!json_text.empty()
        && (json_text.back() == ' ' || json_text.back() == '\0')) {
        json_text.pop_back();
    }
    offset += json_length;
    if (offset > bytes.size() || bytes.size() - offset < 8) {
        throw std::runtime_error("GLB does not contain a BIN chunk.");
    }
    const auto binary_length = read_u32(bytes, offset);
    if (read_u32(bytes, offset + 4) != 0x004E4942U) {
        throw std::runtime_error("Second GLB chunk is not BIN.");
    }
    offset += 8;
    if (binary_length > bytes.size() - offset || offset + binary_length != bytes.size()) {
        throw std::runtime_error("GLB BIN chunk length is invalid.");
    }

    GlbDocument result;
    result.json = Json::parse(json_text);
    result.binary.assign(bytes.begin() + offset, bytes.end());
    return result;
}

bool looks_like_private_absolute_path(std::string_view value) {
    const bool drive_path = value.size() >= 3
        && std::isalpha(static_cast<unsigned char>(value[0])) != 0
        && value[1] == ':' && (value[2] == '/' || value[2] == '\\');
    return drive_path || value.starts_with("\\\\") || value.starts_with("//")
        || value.starts_with("file://");
}

void validate_no_absolute_strings(const Json& value) {
    if (value.is_string()) {
        if (looks_like_private_absolute_path(value.get_ref<const std::string&>())) {
            throw std::runtime_error("GLB JSON contains a local absolute path or file URI.");
        }
        return;
    }
    if (value.is_array()) {
        for (const auto& child : value) validate_no_absolute_strings(child);
    } else if (value.is_object()) {
        for (const auto& [key, child] : value.items()) {
            static_cast<void>(key);
            validate_no_absolute_strings(child);
        }
    }
}

const Json& require_array(const Json& document, std::string_view name) {
    const auto key = std::string(name);
    if (!document.contains(key) || !document.at(key).is_array()) {
        throw std::runtime_error("GLB JSON is missing array: " + key);
    }
    return document.at(key);
}

std::vector<std::uint8_t> image_bytes(
    const GlbDocument& document,
    const Json& image) {
    if (image.contains("uri")) {
        throw std::runtime_error("GLB image uses an external URI instead of bufferView embedding.");
    }
    if (!image.contains("bufferView") || !image.at("bufferView").is_number_unsigned()) {
        throw std::runtime_error("GLB image does not reference a valid bufferView.");
    }
    const auto& views = require_array(document.json, "bufferViews");
    const auto view_index = image.at("bufferView").get<std::size_t>();
    if (view_index >= views.size()) {
        throw std::runtime_error("GLB image bufferView index is out of range.");
    }
    const auto& view = views.at(view_index);
    if (!view.is_object() || view.value("buffer", 0U) != 0U
        || !view.contains("byteLength") || !view.at("byteLength").is_number_unsigned()) {
        throw std::runtime_error("GLB image bufferView is malformed.");
    }
    const auto offset = view.value("byteOffset", std::size_t { 0 });
    const auto length = view.at("byteLength").get<std::size_t>();
    if (offset > document.binary.size() || length > document.binary.size() - offset) {
        throw std::runtime_error("GLB image bufferView exceeds the BIN chunk.");
    }
    return {
        document.binary.begin() + offset,
        document.binary.begin() + offset + length
    };
}

bool valid_image_signature(
    const std::vector<std::uint8_t>& bytes,
    std::string_view mime_type) {
    static constexpr std::array<std::uint8_t, 8> png_signature {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    };
    if (mime_type == "image/png") {
        return bytes.size() >= png_signature.size()
            && std::equal(png_signature.begin(), png_signature.end(), bytes.begin());
    }
    if (mime_type == "image/jpeg") {
        return bytes.size() >= 4 && bytes[0] == 0xFF && bytes[1] == 0xD8
            && bytes[bytes.size() - 2] == 0xFF && bytes.back() == 0xD9;
    }
    return false;
}

std::map<std::string, std::size_t> named_materials(const Json& materials) {
    std::map<std::string, std::size_t> result;
    for (std::size_t index = 0; index < materials.size(); ++index) {
        const auto& material = materials.at(index);
        if (!material.is_object() || !material.contains("name")
            || !material.at("name").is_string()) continue;
        const auto& name = material.at("name").get_ref<const std::string&>();
        if (!result.emplace(name, index).second) {
            throw std::runtime_error("GLB contains duplicate material names: " + name);
        }
    }
    return result;
}

std::optional<std::size_t> base_color_texture_index(const Json& material) {
    if (!material.contains("pbrMetallicRoughness")) return std::nullopt;
    const auto& pbr = material.at("pbrMetallicRoughness");
    if (!pbr.is_object() || !pbr.contains("baseColorTexture")) return std::nullopt;
    const auto& texture = pbr.at("baseColorTexture");
    if (!texture.is_object() || !texture.contains("index")
        || !texture.at("index").is_number_unsigned()) {
        throw std::runtime_error("GLB baseColorTexture index is malformed.");
    }
    return texture.at("index").get<std::size_t>();
}

} // namespace

GlbContainerValidation validate_glb_container(
    const std::filesystem::path& file,
    const CompanionResolution& expected) {
    GlbContainerValidation result;
    try {
        const auto document = parse_glb(file);
        validate_no_absolute_strings(document.json);
        result.checks.push_back({
            "glb_header_and_chunks", true,
            "GLB v2 header, JSON chunk, BIN chunk, and declared lengths are valid."
        });

        const auto& buffers = require_array(document.json, "buffers");
        if (buffers.empty() || buffers.front().contains("uri")
            || !buffers.front().contains("byteLength")
            || !buffers.front().at("byteLength").is_number_unsigned()
            || buffers.front().at("byteLength").get<std::size_t>() > document.binary.size()) {
            throw std::runtime_error("GLB buffer is missing, external, or exceeds the BIN chunk.");
        }
        const auto& views = require_array(document.json, "bufferViews");
        for (const auto& view : views) {
            if (!view.is_object() || view.value("buffer", 0U) != 0U
                || !view.contains("byteLength") || !view.at("byteLength").is_number_unsigned()) {
                throw std::runtime_error("GLB contains a malformed bufferView.");
            }
            const auto offset = view.value("byteOffset", std::size_t { 0 });
            const auto length = view.at("byteLength").get<std::size_t>();
            if (offset > document.binary.size() || length > document.binary.size() - offset) {
                throw std::runtime_error("GLB bufferView exceeds the BIN chunk.");
            }
        }
        const Json empty_array = Json::array();
        const auto& images = document.json.contains("images")
            ? require_array(document.json, "images")
            : empty_array;
        const auto& textures = document.json.contains("textures")
            ? require_array(document.json, "textures")
            : empty_array;
        const auto& materials = require_array(document.json, "materials");
        const auto& meshes = require_array(document.json, "meshes");
        result.image_count = images.size();
        result.texture_count = textures.size();
        result.material_count = materials.size();
        if (images.size() != expected.textures.size()) {
            throw std::runtime_error("GLB embedded image count does not match resolved unique textures.");
        }
        if (textures.size() != expected.textures.size()) {
            throw std::runtime_error("GLB texture count does not match resolved unique textures.");
        }

        std::vector<std::vector<std::uint8_t>> embedded_bytes;
        embedded_bytes.reserve(images.size());
        for (const auto& image : images) {
            if (!image.is_object() || !image.contains("mimeType")
                || !image.at("mimeType").is_string()) {
                throw std::runtime_error("GLB embedded image has no MIME type.");
            }
            const auto bytes = image_bytes(document, image);
            if (!valid_image_signature(
                    bytes,
                    image.at("mimeType").get_ref<const std::string&>())) {
                throw std::runtime_error("GLB embedded image signature does not match its MIME type.");
            }
            embedded_bytes.push_back(bytes);
        }
        result.checks.push_back({
            "embedded_images", true,
            "Every image uses a bounded BIN bufferView with a verified PNG/JPEG signature."
        });

        const auto material_indices = named_materials(materials);
        std::map<std::size_t, std::size_t> source_to_image;
        std::set<std::string> expected_material_names;
        for (const auto& binding : expected.material_bindings) {
            expected_material_names.insert(binding.material_name);
            const auto material = material_indices.find(binding.material_name);
            if (material == material_indices.end()) {
                throw std::runtime_error(
                    "GLB is missing referenced material: " + binding.material_name);
            }
            const auto texture_index = base_color_texture_index(materials.at(material->second));
            if (!binding.resolved_texture_index.has_value()) {
                if (texture_index.has_value()) {
                    throw std::runtime_error(
                        "GLB added a base-color texture to an untextured source material.");
                }
                continue;
            }
            if (!texture_index.has_value() || *texture_index >= textures.size()) {
                throw std::runtime_error("GLB material has an invalid base-color texture binding.");
            }
            const auto& texture = textures.at(*texture_index);
            if (!texture.is_object() || !texture.contains("source")
                || !texture.at("source").is_number_unsigned()) {
                throw std::runtime_error("GLB texture has no valid image source index.");
            }
            const auto image_index = texture.at("source").get<std::size_t>();
            const auto source_index = *binding.resolved_texture_index;
            if (image_index >= embedded_bytes.size() || source_index >= expected.textures.size()) {
                throw std::runtime_error("GLB material-to-image mapping is out of range.");
            }
            if (embedded_bytes[image_index] != expected.textures[source_index].bytes) {
                throw std::runtime_error(
                    "GLB embedded compressed bytes do not match the source texture binding.");
            }
            const auto [existing, inserted] = source_to_image.emplace(source_index, image_index);
            if (!inserted && existing->second != image_index) {
                throw std::runtime_error("A shared source texture maps to multiple GLB images.");
            }
        }
        if (source_to_image.size() != expected.textures.size()) {
            throw std::runtime_error("At least one resolved texture is not referenced by a GLB material.");
        }
        std::set<std::size_t> distinct_images;
        for (const auto& [source, image] : source_to_image) {
            static_cast<void>(source);
            distinct_images.insert(image);
        }
        if (distinct_images.size() != expected.textures.size()) {
            throw std::runtime_error("Distinct source textures were incorrectly merged in the GLB.");
        }
        result.checks.push_back({
            "material_texture_bindings", true,
            "Referenced material names map to the expected embedded source bytes; shared paths are deduplicated once."
        });

        std::set<std::string> used_material_names;
        for (const auto& mesh : meshes) {
            if (!mesh.is_object() || !mesh.contains("primitives")
                || !mesh.at("primitives").is_array()) {
                throw std::runtime_error("GLB mesh has no primitive array.");
            }
            for (const auto& primitive : mesh.at("primitives")) {
                if (!primitive.is_object() || !primitive.contains("material")
                    || !primitive.at("material").is_number_unsigned()) {
                    throw std::runtime_error("GLB primitive has no valid material index.");
                }
                const auto index = primitive.at("material").get<std::size_t>();
                if (index >= materials.size() || !materials.at(index).contains("name")
                    || !materials.at(index).at("name").is_string()) {
                    throw std::runtime_error("GLB primitive references an unnamed or missing material.");
                }
                used_material_names.insert(
                    materials.at(index).at("name").get<std::string>());
            }
        }
        if (used_material_names != expected_material_names) {
            throw std::runtime_error(
                "GLB primitive material set differs from the source mesh-referenced material set.");
        }
        result.checks.push_back({
            "primitive_material_bindings", true,
            "GLB primitives use exactly the source mesh-referenced material-name set."
        });

        result.valid = true;
        return result;
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.checks.push_back({ "glb_container_validation", false, result.error });
        return result;
    }
}

} // namespace assetbridge
