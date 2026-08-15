# Changelog

All notable changes to AssetBridge are documented here.

## [Unreleased]

No unreleased changes are currently documented.

## [0.3.0] - 2026-08-07

### Added

- Sequential multi-file OBJ→GLB2 batch conversion in the desktop application
  and CLI, using the same verified per-asset conversion route.
- Stable `assetbridge.batch.v1` JSON with per-job states, summary counts,
  diagnostics, output/report paths, timing, and structured errors.
- Multi-file SDL3 drag-and-drop and Windows native multi-select, queue
  inspection, item removal, shared output-root selection, and per-item status.
- Failure isolation: failed and not-supported assets do not block later jobs.
- `Cancel After Current`, which completes the active transaction and cancels
  the remaining queued jobs without partial output.
- Canonical input deduplication, a 256-job queue limit, deterministic same-stem
  collision suffixes, and Windows-reserved output-name sanitization.

### Unchanged product boundary

- OBJ→GLB2 remains the only product-enabled and verified conversion route.
- No new texture semantics, hierarchy/instancing behavior, character data,
  formats, or `--allow-lossy` behavior are enabled.
- Existing single-asset behavior and the verified conversion boundary remain
  unchanged.

## [0.2.0] - 2026-08-06

### Added

- Verified multi-material OBJ→GLB2 conversion within the documented static-mesh
  route boundary.
- One Base Color `map_Kd` image per mesh-referenced material, accepting PNG,
  JPEG, and JPG file extensions when the file signature matches.
- Explicit embedding of original compressed image bytes in GLB BIN
  `bufferView` entries, with shared-image deduplication across materials.
- Unicode and space-containing OBJ, MTL, texture, and output paths.
- Asset-root-bounded companion-file resolution with stable diagnostics.
- GLB v2 JSON/BIN validation for embedded images, material/texture bindings,
  `bufferView` bounds, signatures, and exact source-byte preservation.
- Texture diagnostics and Resolving, Embedding, and Validating stages in the
  desktop conversion flow.

### Security / Safety

- Reject absolute paths, UNC paths, URLs, and Data URIs.
- Reject normalized `../` paths that escape the OBJ asset root.
- Check canonical paths and Windows reparse points before accepting companions.
- Validate image file signatures against PNG/JPEG extensions.
- Enforce per-image, unique-image-count, and aggregate image-byte limits.

### Still Unsupported

- Alpha and transparency semantics, including `d`, `Tr`, and `map_d`.
- Normal and bump maps.
- Metallic, roughness, specular, emissive, and opacity textures.
- `map_Kd` transform, option, and clamp semantics.
- Image transcoding or resampling.
- Meaningful hierarchy, instancing, multiple UV channels, vertex colors, and
  unverified tangent/PBR data.
- Bones, skin weights, animation, and morph targets.
- Other conversion formats and `--allow-lossy`.

### Platform Notice

- Windows x64 portable ZIP only.
- Binaries are not Authenticode-signed. Smart App Control or Microsoft Defender
  may warn about or block newly generated unsigned executables.
- SHA-256 verifies archive integrity; it is not a code-signing identity proof.

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
