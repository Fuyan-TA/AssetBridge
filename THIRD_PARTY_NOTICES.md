# Third-Party Notices

AssetBridge currently uses the following third-party libraries. Runtime
availability in Assimp does not imply that AssetBridge enables or verifies a
corresponding conversion path.

## Assimp 6.0.4

- Project: Open Asset Import Library
- License: BSD 3-Clause
- Usage: Model inspection, OBJ import, GLB export, runtime capability enumeration, and round-trip validation
- License text: [third_party/licenses/assimp.txt](third_party/licenses/assimp.txt)

## nlohmann/json 3.12.0

- Project: JSON for Modern C++
- License: MIT
- Usage: JSON inspection, capability, preflight, and conversion report serialization
- License text: [third_party/licenses/nlohmann-json.txt](third_party/licenses/nlohmann-json.txt)

## Dear ImGui 1.92.8

- Project: Dear ImGui
- License: MIT
- Usage: Desktop immediate-mode user interface and the official SDL3 and DirectX 11 backends
- License text: [third_party/licenses/imgui.txt](third_party/licenses/imgui.txt)

## SDL 3.4.12

- Project: Simple DirectMedia Layer
- License: Zlib, with bundled third-party notices in the upstream license file
- Usage: Desktop window, events, drag-and-drop, input, and DPI integration
- License text: [third_party/licenses/sdl3.txt](third_party/licenses/sdl3.txt)

## Runtime transitive dependencies

The Windows portable directory also carries the following Assimp runtime
dependencies. They are listed explicitly because their DLLs are redistributed
with AssetBridge.

| Package | Version | License | License text |
|---|---:|---|---|
| jhasse/poly2tri | 2023-12-27 | BSD 3-Clause | [jhasse-poly2tri.txt](third_party/licenses/jhasse-poly2tri.txt) |
| kuba--/zip | 0.3.14 | MIT | [kubazip.txt](third_party/licenses/kubazip.txt) |
| minizip | 1.3.2 | Zlib | [minizip.txt](third_party/licenses/minizip.txt) |
| pugixml | 1.16 | MIT | [pugixml.txt](third_party/licenses/pugixml.txt) |
| zlib | 1.3.2 | Zlib | [zlib.txt](third_party/licenses/zlib.txt) |
