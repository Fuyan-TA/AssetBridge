# AssetBridge v0.2.0 Release Notes

Release date: 2026-08-06

AssetBridge v0.2.0 expands only the verified OBJ→GLB2 route. It does not add
another input/output format or broaden support to character data or general PBR
material conversion.

## Added

- Verified multi-material OBJ→GLB2 conversion for the documented flat static
  single- and multi-mesh boundary.
- One Base Color `map_Kd` image per mesh-referenced material.
- PNG, JPEG, and JPG companion images with extension/signature validation.
- Original compressed image bytes embedded into GLB BIN `bufferView` entries;
  the resulting GLB does not depend on external image files.
- Shared-image deduplication when multiple materials use the same source image.
- Chinese and space-containing OBJ, MTL, texture, output, and report paths.
- Stable companion-resolution diagnostics in preflight, conversion JSON, CLI,
  and the desktop interface.
- GLB JSON/BIN structure checks, embedded-image binding validation, and exact
  source-to-embedded compressed-byte comparison.
- Desktop conversion stages for Resolving, Embedding, and Validating.

## Security and safety boundary

- Companion files must resolve within the OBJ asset-root directory tree.
- Absolute paths, UNC paths, URLs, Data URIs, and normalized `../` escapes are
  rejected.
- Canonical paths and Windows reparse points are checked before a companion is
  accepted.
- PNG/JPEG signatures must match the declared extension.
- Per-image, unique-image-count, and aggregate byte limits are enforced.
- Missing, non-regular, mismatched, unsafe, or unsupported companions block the
  conversion before a final output directory is committed.

## Still unsupported

- Alpha or transparency semantics: `d`, `Tr`, and `map_d`.
- Normal and bump maps.
- Metallic, roughness, specular, emissive, and opacity textures.
- `map_Kd` transform, option, and clamp semantics.
- Image transcoding or resampling.
- Meaningful hierarchy, mesh instancing, multiple UV channels, vertex colors,
  and unverified tangent/PBR data.
- Bones, skin weights, animation, and morph targets.
- Other input/output routes and `--allow-lossy`.

## Platform notice

- Distribution target: Windows x64 portable ZIP.
- The Windows executables are not Authenticode-signed. Smart App Control or
  Microsoft Defender may warn about or block newly generated unsigned files.
- The accompanying SHA-256 verifies download integrity only; it does not prove
  publisher identity and is not a substitute for code signing.

The previously published [v0.1.0 release](https://github.com/Fuyan-TA/AssetBridge/releases/tag/v0.1.0)
remains available as historical evidence and is not modified by this release
preparation.
