# Changelog

All notable changes to AssetBridge are documented here.

## [Unreleased]

No product capability is currently scheduled for this section.

## [0.1.0] - 2026-07-15

### Added

- Native Windows x64 desktop and CLI applications.
- OBJ inspection with text and stable JSON output.
- Runtime importer/exporter enumeration separated from product enablement and
  verification status.
- Capability matrix, route registry, and loss preflight diagnostics.
- Verified OBJ→GLB2 conversion for one static mesh and multiple flat static
  meshes within the documented attribute/material boundary.
- Controlled non-triangle-face triangulation and export-ready diagnostics.
- Transactional output, GLB reimport, geometry/attribute validation, and JSON
  conversion reports.
- UTF-16 Windows command-line handling and Chinese OBJ/MTL/output-path tests.
- SDL3, Dear ImGui, and DirectX 11 desktop UI with drag-and-drop.
- Reproducible CMake/CPack Windows ZIP, SHA-256 output, package-content tests,
  and Windows CI configuration.

### Known limitations

- No external or embedded textures.
- No expanded multi-material semantics, meaningful hierarchy, or instancing.
- No multi-UV, vertex-color, tangent, unverified PBR, or character-data support.
- No other conversion route is product-enabled.
- Portable binaries are not Authenticode-signed.
