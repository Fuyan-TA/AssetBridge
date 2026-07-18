#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace assetbridge {

enum class FormatId {
    obj,
    gltf2,
    glb2,
    stl,
    ply
};

enum class AssetFeature {
    mesh,
    multiple_meshes,
    node_hierarchy,
    normals,
    tangents,
    uv0,
    multiple_uv_channels,
    vertex_colors,
    material_slots,
    pbr_materials,
    external_textures,
    embedded_textures,
    bones,
    skin_weights,
    animations,
    morph_targets
};

enum class SupportLevel {
    supported,
    partial,
    unsupported,
    unverified
};

enum class LossSeverity {
    info,
    warning,
    blocking
};

struct AnimationFeature {
    std::string name;
    std::optional<double> duration_seconds;
    std::optional<double> duration_ticks;
    std::optional<double> ticks_per_second;
};

struct AssetFeatures {
    std::uint64_t mesh_count = 0;
    // Despite the legacy field name, this means meaningful hierarchy: nested
    // non-root nodes, non-identity transforms, or mesh instancing. Assimp's
    // flat identity wrapper nodes do not set this flag.
    bool has_node_hierarchy = false;
    std::uint64_t meshes_with_normals = 0;
    std::uint64_t meshes_with_tangents = 0;
    std::uint32_t max_uv_channel_count = 0;
    std::uint64_t meshes_with_vertex_colors = 0;
    std::uint64_t referenced_material_count = 0;
    // Names are collected only from materials referenced by non-null meshes.
    // They allow Core companion resolution without broadening support based on
    // unused MTL declarations.
    std::vector<std::string> referenced_material_names;
    bool has_pbr_materials = false;
    std::vector<std::string> external_texture_references;
    std::uint64_t embedded_texture_count = 0;
    std::uint64_t bone_count = 0;
    std::uint64_t skinned_mesh_count = 0;
    std::uint32_t max_weights_per_vertex = 0;
    std::vector<AnimationFeature> animations;
    std::vector<std::string> morph_target_names;
};

struct FeatureCapability {
    AssetFeature feature;
    SupportLevel support;
    LossSeverity loss_severity;
    bool overrideable;
    std::string_view reason;
};

struct FormatCapability {
    FormatId id;
    bool product_enabled;
    bool verified;
    std::vector<FeatureCapability> features;
};

[[nodiscard]] std::optional<FormatId> parse_format_id(std::string_view input);
[[nodiscard]] std::string_view to_string(FormatId id) noexcept;
[[nodiscard]] std::string_view to_string(AssetFeature feature) noexcept;
[[nodiscard]] std::string_view to_string(SupportLevel level) noexcept;
[[nodiscard]] std::string_view to_string(LossSeverity severity) noexcept;

[[nodiscard]] std::optional<double> animation_duration_seconds(
    double duration_ticks,
    double ticks_per_second) noexcept;

[[nodiscard]] const std::array<FormatCapability, 5>& capability_matrix();
[[nodiscard]] const FormatCapability& capability_for(FormatId id);
[[nodiscard]] const FeatureCapability& capability_for(
    FormatId format,
    AssetFeature feature);
[[nodiscard]] std::vector<AssetFeature> present_features(const AssetFeatures& features);

} // namespace assetbridge
