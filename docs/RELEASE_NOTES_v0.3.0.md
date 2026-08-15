# AssetBridge v0.3.0 Release Notes

Release date: 2026-08-07

AssetBridge v0.3.0 adds sequential batch orchestration around the existing
verified OBJ→GLB2 route. It does not add another source or target format, and it
does not broaden the verified per-asset conversion boundary introduced by
v0.2.0.

## Added

- Sequential multi-file OBJ→GLB2 conversion in the desktop application and CLI.
- Stable `assetbridge.batch.v1` JSON reports with per-job states, summary
  counts, diagnostics, output/report paths, timing, and structured errors.
- Multi-file SDL3 drag-and-drop and Windows native multi-select.
- Queue inspection, item removal, one shared output root, and per-item status.
- Failure isolation: failed and not-supported assets do not stop later jobs.
- `Cancel After Current`: the active transaction finishes, then remaining
  queued jobs are marked `canceled` without partial output.
- Canonical input deduplication, a 256-job queue limit, deterministic same-stem
  collision suffixes, and Windows-reserved output-name sanitization.

## Manual desktop acceptance

Manual acceptance used project-authored OBJ assets and was recorded separately
from automated tests:

- A queue of 100 OBJ files completed successfully.
- An invalid OBJ was marked `Failed` without preventing valid jobs from
  continuing; the batch correctly reported `Partial`.
- `Cancel After Current` completed the active job and marked later queued jobs
  `Canceled`.
- No incorrect final directory or transaction-temporary-directory residue was
  observed.

## Unchanged conversion boundary

- OBJ/MTL→GLB2 remains the only product-enabled and verified conversion route.
- Batch jobs use the same per-asset inspection, preflight, transactional export,
  reimport, geometry/material/texture validation, and commit boundary.
- Existing single-asset CLI and desktop behavior remains available.
- Batch processing does not enable new texture semantics, hierarchy or
  instancing behavior, character data, formats, or `--allow-lossy`.

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
- The accompanying SHA-256 verifies archive integrity only; it does not prove
  publisher identity and is not a substitute for code signing.

Published [v0.2.0](https://github.com/Fuyan-TA/AssetBridge/releases/tag/v0.2.0)
and [v0.1.0](https://github.com/Fuyan-TA/AssetBridge/releases/tag/v0.1.0)
releases remain unchanged. This preparation does not create or link to a
v0.3.0 Release before publication.
