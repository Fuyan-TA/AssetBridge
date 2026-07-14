#include "assetbridge/product/format_capabilities.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace assetbridge {
namespace {

using AF = AssetFeature;
using LS = LossSeverity;
using SL = SupportLevel;

FeatureCapability rule(
    AF feature,
    SL support,
    LS severity,
    bool overrideable,
    std::string_view reason) {
    return { feature, support, severity, overrideable, reason };
}

std::vector<FeatureCapability> obj_rules() {
    return {
        rule(AF::mesh, SL::supported, LS::info, false, "OBJ represents polygon mesh geometry."),
        rule(AF::multiple_meshes, SL::partial, LS::warning, false, "OBJ groups do not preserve a general scene graph."),
        rule(AF::node_hierarchy, SL::unsupported, LS::warning, false, "OBJ cannot preserve node hierarchy and transforms."),
        rule(AF::normals, SL::supported, LS::info, false, "OBJ supports vertex normals."),
        rule(AF::tangents, SL::unsupported, LS::warning, false, "OBJ has no standard tangent channel."),
        rule(AF::uv0, SL::supported, LS::info, false, "OBJ supports one texture coordinate set."),
        rule(AF::multiple_uv_channels, SL::unsupported, LS::warning, false, "OBJ cannot preserve multiple UV channels."),
        rule(AF::vertex_colors, SL::partial, LS::warning, false, "Vertex colors are non-standard OBJ extensions."),
        rule(AF::material_slots, SL::partial, LS::warning, false, "OBJ material assignments depend on an external MTL representation."),
        rule(AF::pbr_materials, SL::partial, LS::warning, false, "MTL cannot represent the full source PBR material model."),
        rule(AF::external_textures, SL::partial, LS::warning, false, "Texture references depend on MTL paths and supported slots."),
        rule(AF::embedded_textures, SL::unsupported, LS::warning, false, "OBJ cannot embed texture payloads."),
        rule(AF::bones, SL::unsupported, LS::blocking, true, "OBJ cannot represent skeleton bones."),
        rule(AF::skin_weights, SL::unsupported, LS::blocking, true, "OBJ cannot represent skin weights."),
        rule(AF::animations, SL::unsupported, LS::blocking, true, "OBJ cannot represent animation clips."),
        rule(AF::morph_targets, SL::unsupported, LS::blocking, true, "OBJ cannot represent morph targets.")
    };
}

std::vector<FeatureCapability> stl_rules() {
    return {
        rule(AF::mesh, SL::supported, LS::info, false, "STL represents triangulated surface geometry."),
        rule(AF::multiple_meshes, SL::partial, LS::warning, false, "STL does not preserve independent mesh objects."),
        rule(AF::node_hierarchy, SL::unsupported, LS::warning, false, "STL cannot preserve node hierarchy or transforms."),
        rule(AF::normals, SL::partial, LS::warning, false, "STL facet normals do not preserve arbitrary vertex normals."),
        rule(AF::tangents, SL::unsupported, LS::warning, false, "STL has no tangent channel."),
        rule(AF::uv0, SL::unsupported, LS::warning, false, "STL has no UV channel."),
        rule(AF::multiple_uv_channels, SL::unsupported, LS::warning, false, "STL has no UV channels."),
        rule(AF::vertex_colors, SL::unsupported, LS::warning, false, "Standard STL does not preserve vertex colors."),
        rule(AF::material_slots, SL::unsupported, LS::warning, false, "STL does not preserve material slots."),
        rule(AF::pbr_materials, SL::unsupported, LS::warning, false, "STL does not represent PBR materials."),
        rule(AF::external_textures, SL::unsupported, LS::warning, false, "STL does not reference textures."),
        rule(AF::embedded_textures, SL::unsupported, LS::warning, false, "STL cannot embed textures."),
        rule(AF::bones, SL::unsupported, LS::blocking, true, "STL cannot represent skeleton bones."),
        rule(AF::skin_weights, SL::unsupported, LS::blocking, true, "STL cannot represent skin weights."),
        rule(AF::animations, SL::unsupported, LS::blocking, true, "STL cannot represent animation clips."),
        rule(AF::morph_targets, SL::unsupported, LS::blocking, true, "STL cannot represent morph targets.")
    };
}

std::vector<FeatureCapability> ply_rules() {
    return {
        rule(AF::mesh, SL::supported, LS::info, false, "PLY represents polygon mesh geometry."),
        rule(AF::multiple_meshes, SL::partial, LS::warning, false, "PLY does not preserve a general multi-object scene."),
        rule(AF::node_hierarchy, SL::unsupported, LS::warning, false, "PLY cannot preserve node hierarchy or transforms."),
        rule(AF::normals, SL::unverified, LS::info, false, "Normal preservation has not been verified by AssetBridge."),
        rule(AF::tangents, SL::unsupported, LS::warning, false, "PLY has no portable tangent convention."),
        rule(AF::uv0, SL::unverified, LS::info, false, "UV preservation has not been verified by AssetBridge."),
        rule(AF::multiple_uv_channels, SL::unsupported, LS::warning, false, "PLY has no portable multiple-UV convention."),
        rule(AF::vertex_colors, SL::supported, LS::info, false, "PLY supports per-vertex color properties."),
        rule(AF::material_slots, SL::unsupported, LS::warning, false, "PLY does not preserve material slots."),
        rule(AF::pbr_materials, SL::unsupported, LS::warning, false, "PLY does not represent PBR materials."),
        rule(AF::external_textures, SL::unsupported, LS::warning, false, "PLY does not provide portable texture references."),
        rule(AF::embedded_textures, SL::unsupported, LS::warning, false, "PLY cannot embed textures."),
        rule(AF::bones, SL::unsupported, LS::blocking, true, "PLY cannot represent skeleton bones."),
        rule(AF::skin_weights, SL::unsupported, LS::blocking, true, "PLY cannot represent skin weights."),
        rule(AF::animations, SL::unsupported, LS::blocking, true, "PLY cannot represent animation clips."),
        rule(AF::morph_targets, SL::unsupported, LS::blocking, true, "PLY cannot represent morph targets.")
    };
}

std::vector<FeatureCapability> unverified_gltf_rules() {
    return {
        rule(AF::mesh, SL::unverified, LS::info, false, "Format semantics support meshes, but the AssetBridge path is unverified."),
        rule(AF::multiple_meshes, SL::unverified, LS::info, false, "Format semantics support multiple meshes, but the path is unverified."),
        rule(AF::node_hierarchy, SL::unverified, LS::info, false, "Format semantics support hierarchy, but the path is unverified."),
        rule(AF::normals, SL::unverified, LS::info, false, "Format semantics support normals, but the path is unverified."),
        rule(AF::tangents, SL::unverified, LS::info, false, "Format semantics support tangents, but the path is unverified."),
        rule(AF::uv0, SL::unverified, LS::info, false, "Format semantics support UVs, but the path is unverified."),
        rule(AF::multiple_uv_channels, SL::unverified, LS::info, false, "Format semantics support multiple UV sets, but the path is unverified."),
        rule(AF::vertex_colors, SL::unverified, LS::info, false, "Format semantics support vertex colors, but the path is unverified."),
        rule(AF::material_slots, SL::unverified, LS::info, false, "Format semantics support material assignments, but the path is unverified."),
        rule(AF::pbr_materials, SL::unverified, LS::info, false, "Format semantics support PBR materials, but the path is unverified."),
        rule(AF::external_textures, SL::unverified, LS::info, false, "Format semantics support external images, but the path is unverified."),
        rule(AF::embedded_textures, SL::unverified, LS::info, false, "Format semantics support embedded image data, but the path is unverified."),
        rule(AF::bones, SL::unverified, LS::info, false, "Format semantics support skeletons, but the path is unverified."),
        rule(AF::skin_weights, SL::unverified, LS::info, false, "Format semantics support skin weights, but the path is unverified."),
        rule(AF::animations, SL::unverified, LS::info, false, "Format semantics support animation, but the path is unverified."),
        rule(AF::morph_targets, SL::unverified, LS::info, false, "Format semantics support morph targets, but the path is unverified.")
    };
}

} // namespace

std::optional<FormatId> parse_format_id(std::string_view input) {
    std::string normalized(input);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    if (!normalized.empty() && normalized.front() == '.') {
        normalized.erase(normalized.begin());
    }

    if (normalized == "obj") {
        return FormatId::obj;
    }
    if (normalized == "gltf" || normalized == "gltf2") {
        return FormatId::gltf2;
    }
    if (normalized == "glb" || normalized == "glb2") {
        return FormatId::glb2;
    }
    if (normalized == "stl") {
        return FormatId::stl;
    }
    if (normalized == "ply") {
        return FormatId::ply;
    }
    return std::nullopt;
}

std::string_view to_string(FormatId id) noexcept {
    switch (id) {
    case FormatId::obj: return "obj";
    case FormatId::gltf2: return "gltf2";
    case FormatId::glb2: return "glb2";
    case FormatId::stl: return "stl";
    case FormatId::ply: return "ply";
    }
    return "unknown";
}

std::string_view to_string(AssetFeature feature) noexcept {
    switch (feature) {
    case AssetFeature::mesh: return "mesh";
    case AssetFeature::multiple_meshes: return "multiple_meshes";
    case AssetFeature::node_hierarchy: return "node_hierarchy";
    case AssetFeature::normals: return "normals";
    case AssetFeature::tangents: return "tangents";
    case AssetFeature::uv0: return "uv0";
    case AssetFeature::multiple_uv_channels: return "multiple_uv_channels";
    case AssetFeature::vertex_colors: return "vertex_colors";
    case AssetFeature::material_slots: return "material_slots";
    case AssetFeature::pbr_materials: return "pbr_materials";
    case AssetFeature::external_textures: return "external_textures";
    case AssetFeature::embedded_textures: return "embedded_textures";
    case AssetFeature::bones: return "bones";
    case AssetFeature::skin_weights: return "skin_weights";
    case AssetFeature::animations: return "animations";
    case AssetFeature::morph_targets: return "morph_targets";
    }
    return "unknown";
}

std::string_view to_string(SupportLevel level) noexcept {
    switch (level) {
    case SupportLevel::supported: return "supported";
    case SupportLevel::partial: return "partial";
    case SupportLevel::unsupported: return "unsupported";
    case SupportLevel::unverified: return "unverified";
    }
    return "unknown";
}

std::string_view to_string(LossSeverity severity) noexcept {
    switch (severity) {
    case LossSeverity::info: return "info";
    case LossSeverity::warning: return "warning";
    case LossSeverity::blocking: return "blocking";
    }
    return "unknown";
}

const std::array<FormatCapability, 5>& capability_matrix() {
    static const std::array<FormatCapability, 5> matrix = {
        FormatCapability { FormatId::obj, false, false, obj_rules() },
        FormatCapability { FormatId::gltf2, false, false, unverified_gltf_rules() },
        FormatCapability { FormatId::glb2, false, false, unverified_gltf_rules() },
        FormatCapability { FormatId::stl, false, false, stl_rules() },
        FormatCapability { FormatId::ply, false, false, ply_rules() }
    };
    return matrix;
}

const FormatCapability& capability_for(FormatId id) {
    const auto& matrix = capability_matrix();
    const auto iterator = std::find_if(
        matrix.begin(),
        matrix.end(),
        [id](const FormatCapability& capability) {
            return capability.id == id;
        });
    if (iterator == matrix.end()) {
        throw std::logic_error("Format capability is missing from the matrix.");
    }
    return *iterator;
}

const FeatureCapability& capability_for(FormatId format, AssetFeature feature) {
    const auto& format_capability = capability_for(format);
    const auto iterator = std::find_if(
        format_capability.features.begin(),
        format_capability.features.end(),
        [feature](const FeatureCapability& capability) {
            return capability.feature == feature;
        });
    if (iterator == format_capability.features.end()) {
        throw std::logic_error("Asset feature is missing from the format capability.");
    }
    return *iterator;
}

std::vector<AssetFeature> present_features(const AssetFeatures& features) {
    std::vector<AssetFeature> result;
    const auto add_if = [&result](bool present, AssetFeature feature) {
        if (present) {
            result.push_back(feature);
        }
    };

    add_if(features.mesh_count > 0, AssetFeature::mesh);
    add_if(features.mesh_count > 1, AssetFeature::multiple_meshes);
    add_if(features.has_node_hierarchy, AssetFeature::node_hierarchy);
    add_if(features.meshes_with_normals > 0, AssetFeature::normals);
    add_if(features.meshes_with_tangents > 0, AssetFeature::tangents);
    add_if(features.max_uv_channel_count > 0, AssetFeature::uv0);
    add_if(features.max_uv_channel_count > 1, AssetFeature::multiple_uv_channels);
    add_if(features.meshes_with_vertex_colors > 0, AssetFeature::vertex_colors);
    add_if(features.referenced_material_count > 0, AssetFeature::material_slots);
    add_if(features.has_pbr_materials, AssetFeature::pbr_materials);
    add_if(!features.external_texture_references.empty(), AssetFeature::external_textures);
    add_if(features.embedded_texture_count > 0, AssetFeature::embedded_textures);
    add_if(features.bone_count > 0, AssetFeature::bones);
    add_if(features.skinned_mesh_count > 0, AssetFeature::skin_weights);
    add_if(!features.animations.empty(), AssetFeature::animations);
    add_if(!features.morph_target_names.empty(), AssetFeature::morph_targets);
    return result;
}

} // namespace assetbridge
