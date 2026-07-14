#include "assetbridge/core/asset_inspector.hpp"

#include <filesystem>
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

} // namespace

int main() {
    const auto asset_directory = std::filesystem::current_path() / "tests" / "assets";
    const auto obj_path = asset_directory / "minimal_triangle.obj";

    const assetbridge::AssetInspector inspector;
    const auto result = inspector.inspect(obj_path);

    int failures = 0;
    failures += require(static_cast<bool>(result), "minimal OBJ should import successfully");
    if (result) {
        const auto& summary = *result.summary;
        failures += require(summary.mesh_count == 1, "mesh count should be 1");
        failures += require(summary.vertex_count == 3, "vertex count should be 3");
        failures += require(summary.face_count == 1, "face count should be 1");
        failures += require(summary.triangle_count == 1, "triangle count should be 1");
        failures += require(summary.material_count >= 1, "material count should be at least 1");
        failures += require(summary.uv_channel_count == 1, "UV channel count should be 1");
        failures += require(summary.has_normals, "mesh should have normals");
    } else {
        std::cerr << "Import error: " << result.error_message << '\n';
    }

    const auto missing_result = inspector.inspect(asset_directory / "does_not_exist.obj");
    failures += require(!missing_result, "missing file should fail inspection");

    if (failures == 0) {
        std::cout << "All AssetBridge Phase 1A tests passed.\n";
        return 0;
    }

    std::cerr << failures << " test assertion(s) failed.\n";
    return 1;
}
