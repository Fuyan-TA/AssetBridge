#include "assetbridge/product/format_capabilities.hpp"
#include "assetbridge/product/loss_preflight.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string_view>

namespace {

int require(bool condition, std::string_view message) {
    if (condition) {
        return 0;
    }
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

const assetbridge::LossItem* find_loss(
    const assetbridge::PreflightDecision& decision,
    assetbridge::AssetFeature feature) {
    for (const auto& loss : decision.losses) {
        if (loss.feature == feature) {
            return &loss;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    using namespace assetbridge;
    int failures = 0;

    failures += require(parse_format_id("obj") == FormatId::obj, "obj alias should parse");
    failures += require(parse_format_id("GLTF") == FormatId::gltf2, "GLTF should canonicalize to gltf2");
    failures += require(parse_format_id("gltf2") == FormatId::gltf2, "gltf2 should parse");
    failures += require(parse_format_id("GlB") == FormatId::glb2, "GLB should canonicalize to glb2");
    failures += require(parse_format_id("glb2") == FormatId::glb2, "glb2 should parse");
    failures += require(!parse_format_id("fbx").has_value(), "unknown format should not parse");

    const auto known_duration = animation_duration_seconds(120.0, 24.0);
    failures += require(
        known_duration.has_value() && std::fabs(*known_duration - 5.0) < 0.000001,
        "finite positive tick rate should produce duration seconds");
    const auto zero_tick_rate = animation_duration_seconds(120.0, 0.0);
    failures += require(
        !zero_tick_rate.has_value(),
        "zero tick rate must produce unknown duration seconds");
    AnimationFeature zero_tick_animation {
        "ZeroTickRate",
        zero_tick_rate,
        120.0,
        0.0
    };
    failures += require(
        !zero_tick_animation.duration_seconds.has_value()
            && zero_tick_animation.duration_ticks == 120.0
            && zero_tick_animation.ticks_per_second == 0.0,
        "zero tick rate should retain finite tick data without inventing seconds");
    failures += require(
        !animation_duration_seconds(
            std::numeric_limits<double>::infinity(),
            24.0).has_value()
        && !animation_duration_seconds(
            120.0,
            std::numeric_limits<double>::quiet_NaN()).has_value(),
        "non-finite animation inputs must not produce NaN or infinity seconds");

    constexpr std::array all_features {
        AssetFeature::mesh,
        AssetFeature::multiple_meshes,
        AssetFeature::node_hierarchy,
        AssetFeature::normals,
        AssetFeature::tangents,
        AssetFeature::uv0,
        AssetFeature::multiple_uv_channels,
        AssetFeature::vertex_colors,
        AssetFeature::material_slots,
        AssetFeature::pbr_materials,
        AssetFeature::external_textures,
        AssetFeature::embedded_textures,
        AssetFeature::bones,
        AssetFeature::skin_weights,
        AssetFeature::animations,
        AssetFeature::morph_targets
    };

    const auto& matrix = capability_matrix();
    failures += require(matrix.size() == 5, "every canonical FormatId should have a matrix entry");
    std::set<FormatId> format_ids;
    for (const auto& format : matrix) {
        format_ids.insert(format.id);
        failures += require(!format.product_enabled, "Phase 1C outputs must remain disabled");
        failures += require(!format.verified, "Phase 1C outputs must remain unverified");
        failures += require(
            format.features.size() == all_features.size(),
            "each format should define every AssetFeature");
        std::set<AssetFeature> feature_ids;
        for (const auto& capability : format.features) {
            feature_ids.insert(capability.feature);
        }
        failures += require(
            feature_ids.size() == all_features.size(),
            "each AssetFeature should appear exactly once per format");
    }
    failures += require(format_ids.size() == matrix.size(), "FormatId definitions should be unique");

    AssetFeatures animation_features;
    animation_features.mesh_count = 1;
    animation_features.animations.push_back({ "SyntheticAnimation", 1.0, 24.0, 24.0 });
    const auto animation_to_obj = evaluate_preflight(
        FormatId::obj,
        animation_features,
        true);
    const auto* animation_loss = find_loss(animation_to_obj, AssetFeature::animations);
    failures += require(animation_loss != nullptr, "animation to OBJ should produce a loss");
    failures += require(
        animation_loss != nullptr && animation_loss->severity == LossSeverity::blocking,
        "animation to OBJ should be blocking");
    failures += require(
        animation_loss != nullptr && animation_loss->overrideable,
        "animation to OBJ should be overrideable in a future convert policy");
    failures += require(
        animation_to_obj.overall_result == OverallResult::blocked,
        "blocking must outrank every other overall result");

    AssetFeatures bone_features;
    bone_features.mesh_count = 1;
    bone_features.bone_count = 1;
    const auto bones_to_stl = evaluate_preflight(FormatId::stl, bone_features, true);
    const auto* bone_loss = find_loss(bones_to_stl, AssetFeature::bones);
    failures += require(
        bone_loss != nullptr && bone_loss->severity == LossSeverity::blocking,
        "bones to STL should be blocking");
    failures += require(
        bone_loss != nullptr && bone_loss->overrideable,
        "bones to STL should be overrideable in a future convert policy");

    AssetFeatures morph_features;
    morph_features.mesh_count = 1;
    morph_features.morph_target_names.push_back("SyntheticMorph");
    const auto morph_to_obj = evaluate_preflight(FormatId::obj, morph_features, true);
    const auto* morph_loss = find_loss(morph_to_obj, AssetFeature::morph_targets);
    failures += require(
        morph_loss != nullptr && morph_loss->severity == LossSeverity::blocking,
        "morph targets to OBJ should be blocking");
    failures += require(
        morph_loss != nullptr && morph_loss->overrideable,
        "morph targets to OBJ should be overrideable in a future convert policy");

    AssetFeatures static_obj_features;
    static_obj_features.mesh_count = 1;
    static_obj_features.max_uv_channel_count = 1;
    static_obj_features.referenced_material_count = 1;
    const auto obj_to_stl = evaluate_preflight(FormatId::stl, static_obj_features, true);
    failures += require(
        find_loss(obj_to_stl, AssetFeature::uv0) != nullptr,
        "OBJ to STL should report UV loss when UVs exist");
    failures += require(
        find_loss(obj_to_stl, AssetFeature::material_slots) != nullptr,
        "OBJ to STL should report material loss when materials exist");
    failures += require(
        find_loss(obj_to_stl, AssetFeature::animations) == nullptr,
        "absent animation must not produce a loss");
    failures += require(
        obj_to_stl.compatibility_result == CompatibilityResult::lossy
            && obj_to_stl.overall_result == OverallResult::lossy,
        "lossy must outrank unverified");

    AssetFeatures meaningful_hierarchy_features;
    meaningful_hierarchy_features.mesh_count = 1;
    meaningful_hierarchy_features.has_node_hierarchy = true;
    const auto hierarchy_to_stl = evaluate_preflight(
        FormatId::stl,
        meaningful_hierarchy_features,
        true);
    failures += require(
        find_loss(hierarchy_to_stl, AssetFeature::node_hierarchy) != nullptr,
        "meaningful hierarchy to STL should report node hierarchy loss");

    const auto obj_to_glb = evaluate_preflight(FormatId::glb2, static_obj_features, true);
    failures += require(
        obj_to_glb.compatibility_result == CompatibilityResult::safe,
        "unverified glb2 semantics should not invent a known loss");
    failures += require(
        obj_to_glb.overall_result == OverallResult::unverified,
        "OBJ to GLB must remain unverified in Phase 1C");
    failures += require(!obj_to_glb.verified, "OBJ to GLB must not be verified");

    const auto missing_exporter = evaluate_preflight(FormatId::glb2, static_obj_features, false);
    failures += require(
        missing_exporter.overall_result == OverallResult::blocked,
        "missing runtime exporter should block preflight");
    failures += require(
        !missing_exporter.losses.empty()
            && missing_exporter.losses.front().severity == LossSeverity::blocking
            && !missing_exporter.losses.front().overrideable,
        "missing exporter block must not be overrideable");

    const auto invalid_asset = evaluate_preflight(FormatId::obj, {}, true, false);
    failures += require(
        invalid_asset.overall_result == OverallResult::blocked,
        "invalid asset should block preflight");
    failures += require(
        !invalid_asset.losses.empty() && !invalid_asset.losses.front().overrideable,
        "invalid asset block must not be overrideable");

    if (failures == 0) {
        std::cout << "All AssetBridge capability matrix tests passed.\n";
        return 0;
    }
    std::cerr << failures << " capability matrix assertion(s) failed.\n";
    return 1;
}
