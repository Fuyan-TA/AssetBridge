#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace assetbridge {

inline constexpr std::uint64_t kMaxTextureFileBytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::size_t kMaxTextureCount = 64;
inline constexpr std::uint64_t kMaxTextureTotalBytes = 256ULL * 1024ULL * 1024ULL;

struct TextureLimits {
    std::uint64_t max_file_bytes = kMaxTextureFileBytes;
    std::size_t max_texture_count = kMaxTextureCount;
    std::uint64_t max_total_bytes = kMaxTextureTotalBytes;
};

enum class TextureFileFormat {
    png,
    jpeg
};

enum class CompanionErrorCode {
    none,
    material_library_missing,
    material_library_path_absolute,
    material_library_path_outside_asset_root,
    material_library_not_regular_file,
    material_reference_unresolved,
    texture_file_missing,
    texture_path_absolute,
    texture_path_outside_asset_root,
    texture_path_not_regular_file,
    texture_format_unsupported,
    texture_signature_mismatch,
    texture_semantic_unverified,
    texture_options_unverified,
    transparency_unverified,
    multiple_textures_per_material_unverified,
    texture_file_too_large,
    texture_count_limit_exceeded,
    texture_total_size_limit_exceeded,
    companion_parse_failed
};

[[nodiscard]] std::string_view to_string(CompanionErrorCode code) noexcept;
[[nodiscard]] std::string_view to_string(TextureFileFormat format) noexcept;

struct CompanionIssue {
    CompanionErrorCode code = CompanionErrorCode::none;
    std::string material_name;
    std::string reference;
    std::string message;
};

struct ResolvedTexture {
    std::filesystem::path path;
    // Stable asset-root-relative key. Never exposes a machine-absolute path.
    std::string asset_relative_key;
    TextureFileFormat format = TextureFileFormat::png;
    std::string mime_type;
    std::uint64_t byte_size = 0;
    std::vector<std::uint8_t> bytes;
};

struct MaterialTextureBinding {
    std::string material_name;
    std::optional<std::string> texture_reference;
    std::optional<std::size_t> resolved_texture_index;
};

struct CompanionResolution {
    std::filesystem::path asset_root;
    std::vector<std::filesystem::path> material_libraries;
    std::size_t referenced_material_count = 0;
    std::size_t external_texture_reference_count = 0;
    std::size_t shared_texture_deduplication_count = 0;
    std::uint64_t texture_bytes = 0;
    std::vector<ResolvedTexture> textures;
    std::vector<MaterialTextureBinding> material_bindings;
    std::vector<CompanionIssue> issues;

    [[nodiscard]] bool safe() const noexcept { return issues.empty(); }
};

class CompanionResolver {
public:
    [[nodiscard]] CompanionResolution resolve(
        const std::filesystem::path& obj_path,
        const std::vector<std::string>& referenced_material_names,
        const TextureLimits& limits = {}) const;
};

} // namespace assetbridge
