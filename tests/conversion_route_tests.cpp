#include "assetbridge/product/conversion_routes.hpp"
#include "assetbridge/product/loss_preflight.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace {

int require(bool condition, std::string_view message) {
    if (condition) {
        return 0;
    }
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

bool has_route_feature_block(
    const assetbridge::PreflightDecision& decision,
    assetbridge::AssetFeature feature) {
    return std::any_of(
        decision.losses.begin(),
        decision.losses.end(),
        [feature](const assetbridge::LossItem& loss) {
            return loss.code == "route_feature_unverified"
                && loss.feature == feature
                && loss.severity == assetbridge::LossSeverity::blocking
                && !loss.overrideable;
        });
}

} // namespace

int main() {
    using namespace assetbridge;
    int failures = 0;

    const auto& routes = conversion_route_registry();
    failures += require(routes.size() == 25, "5x5 route registry should define 25 routes");

    std::size_t enabled_count = 0;
    for (const auto& route : routes) {
        if (route.product_enabled || route.verified) {
            ++enabled_count;
            failures += require(
                route.source == FormatId::obj && route.target == FormatId::glb2,
                "only OBJ to GLB2 may be enabled and verified in Phase 4");
            failures += require(
                route.product_enabled && route.verified,
                "enabled OBJ to GLB2 route must also be verified");
        }
    }
    failures += require(enabled_count == 1, "exactly one conversion route should be enabled");

    const auto& obj_to_glb = conversion_route(FormatId::obj, FormatId::glb2);
    failures += require(obj_to_glb.product_enabled, "OBJ to GLB2 should be enabled");
    failures += require(obj_to_glb.verified, "OBJ to GLB2 should be verified");
    failures += require(
        route_feature_verified(obj_to_glb, AssetFeature::mesh)
            && route_feature_verified(obj_to_glb, AssetFeature::multiple_meshes)
            && route_feature_verified(obj_to_glb, AssetFeature::normals)
            && route_feature_verified(obj_to_glb, AssetFeature::uv0)
            && route_feature_verified(obj_to_glb, AssetFeature::material_slots),
        "OBJ to GLB2 verified feature set is incomplete");
    failures += require(
        !route_feature_verified(obj_to_glb, AssetFeature::external_textures)
            && !route_feature_verified(obj_to_glb, AssetFeature::node_hierarchy)
            && !route_feature_verified(obj_to_glb, AssetFeature::animations),
        "unverified route features must stay outside the verified feature set");

    AssetFeatures supported_features;
    supported_features.mesh_count = 1;
    supported_features.meshes_with_normals = 1;
    supported_features.max_uv_channel_count = 1;
    supported_features.referenced_material_count = 1;
    const auto supported = evaluate_route_preflight(
        FormatId::obj,
        FormatId::glb2,
        supported_features,
        true);
    failures += require(
        supported.product_enabled && supported.verified,
        "verified route preflight should expose route status");
    failures += require(
        supported.compatibility_result == CompatibilityResult::safe
            && supported.overall_result == OverallResult::safe,
        "verified supported OBJ features should produce safe route preflight");

    auto textured_features = supported_features;
    textured_features.external_texture_references.push_back("unverified.png");
    const auto textured = evaluate_route_preflight(
        FormatId::obj,
        FormatId::glb2,
        textured_features,
        true);
    failures += require(
        textured.overall_result == OverallResult::blocked,
        "external texture should remain blocked for the Phase 4 route");
    failures += require(
        has_route_feature_block(textured, AssetFeature::external_textures),
        "external texture block should use route_feature_unverified");

    auto multi_mesh_features = supported_features;
    multi_mesh_features.mesh_count = 2;
    const auto multi_mesh = evaluate_route_preflight(
        FormatId::obj,
        FormatId::glb2,
        multi_mesh_features,
        true);
    failures += require(
        multi_mesh.overall_result == OverallResult::safe
            && !has_route_feature_block(multi_mesh, AssetFeature::multiple_meshes),
        "flat identity sibling meshes should be safe on the verified route");

    auto hierarchy_features = multi_mesh_features;
    hierarchy_features.has_node_hierarchy = true;
    const auto hierarchy = evaluate_route_preflight(
        FormatId::obj,
        FormatId::glb2,
        hierarchy_features,
        true);
    failures += require(
        hierarchy.overall_result == OverallResult::blocked
            && has_route_feature_block(hierarchy, AssetFeature::node_hierarchy),
        "meaningful hierarchy must remain independently blocked");

    const auto& obj_to_stl = conversion_route(FormatId::obj, FormatId::stl);
    failures += require(
        !obj_to_stl.product_enabled && !obj_to_stl.verified,
        "OBJ to STL must remain disabled and unverified");

    if (failures == 0) {
        std::cout << "All AssetBridge conversion route tests passed.\n";
        return 0;
    }
    std::cerr << failures << " conversion route assertion(s) failed.\n";
    return 1;
}
