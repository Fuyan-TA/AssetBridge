# AssetBridge

[中文说明 / Chinese documentation](README.zh-CN.md)

AssetBridge is a lightweight native Windows tool for inspecting 3D assets and
performing a narrow, validated OBJ-to-GLB conversion workflow. It is designed
around explicit format capabilities, preflight diagnostics, transactional
output, and round-trip verification.

AssetBridge v0.3.0 is not a general-purpose all-format converter and makes no
readiness claim beyond its verified boundary. The product exposes only the
OBJ-to-GLB route that has automated and local acceptance evidence.

![AssetBridge v0.1.0 desktop conversion](docs/images/assetbridge-v0.1.0.png)

Local file-system paths are redacted in documentation screenshots.

## v0.3.0 batch workflow

AssetBridge v0.3.0 adds sequential batch orchestration without
expanding the verified conversion route. Desktop drag-and-drop and the native
file dialog accept multiple OBJ files, preflight each job independently, and
show a per-asset queue. `Convert All to GLB` processes one job at a time; a
failed or unsupported asset does not stop later jobs. `Cancel After Current`
finishes the active transaction and marks the remaining queued jobs canceled.

The CLI exposes the same coordinator and stable JSON schema:

```powershell
assetbridge-cli batch --input .\a.obj --input .\b.obj `
  --to glb --output .\converted --json
```

The root output contains `batch-report.json`; each successful job retains its
existing non-overwriting per-asset directory and `conversion-report.json`.
Batch support does not enable another source format, target format, texture
semantic, hierarchy mode, or `--allow-lossy` behavior.

## v0.3.0 verified scope

| Area | Verified v0.3.0 boundary |
|---|---|
| Platform | Native Windows x64 desktop and CLI applications. |
| Route | OBJ/MTL input to binary GLB2 output only. |
| Batch | Sequential processing of up to 256 unique canonical OBJ paths, with per-job isolation and `Cancel After Current`. |
| Geometry | One static mesh or multiple non-empty flat static meshes; controlled triangulation of non-triangle faces. |
| Attributes | Vertex positions, normals, and UV0 when present in the source. |
| Materials | Multiple mesh-referenced MTL materials with per-material Kd color. |
| Images | One Base Color `map_Kd` PNG/JPEG/JPG per referenced material; original compressed bytes embedded in GLB BIN `bufferView` entries. |
| Deduplication | A shared source image referenced by multiple materials is embedded once. |
| Paths | UTF-16 Windows input/output boundary and UTF-8 JSON, including Chinese and space-containing paths. |
| Safety | Asset-root-bounded companion resolution, transactional output, GLB structure checks, Assimp reimport, and validation before commit. |

The final GLB does not depend on external image files. Companion MTL and image
files must resolve inside the OBJ asset-root directory tree. Absolute paths,
UNC paths, URLs, Data URIs, normalized `../` escapes, reparse-point escapes,
missing/non-regular files, extension/signature mismatches, unsupported semantics,
and resource-limit violations block conversion with stable diagnostics.

The following remain outside the verified v0.3.0 boundary and are rejected when
detected:

- Alpha and transparency semantics: `d`, `Tr`, and `map_d`.
- Normal and bump maps.
- Metallic, roughness, specular, emissive, and opacity textures.
- `map_Kd` transform, option, and clamp semantics.
- Image transcoding or resampling.
- Meaningful node hierarchy, non-identity node transforms, or mesh instancing.
- Multiple UV channels, vertex colors, tangents, or unverified PBR data.
- Bones, skin weights, animation, or morph targets.
- Other input or output formats and `--allow-lossy`.

## Quick start

### Desktop

1. Extract the complete Windows x64 ZIP; keep the EXE and DLL files together.
2. Run `assetbridge-desktop.exe`.
3. Drop one or more OBJ files into the window or use **Choose OBJ Files**.
4. Review each queue item's statistics, preflight result, and diagnostics.
5. Remove unwanted jobs, choose one shared output root, and select
   **Convert All to GLB**.
6. Review the final success/not-supported/failed/canceled summary and the root
   batch report.

The queue accepts at most 256 unique canonical OBJ paths. It is immutable while
conversion is running. A single OBJ follows the same path as a one-item batch.

### CLI

```powershell
assetbridge-cli --version
assetbridge-cli inspect .\model.obj
assetbridge-cli inspect .\model.obj --json
assetbridge-cli preflight .\model.obj --target glb --json
assetbridge-cli convert .\model.obj --to glb --output .\converted --json
assetbridge-cli batch --input .\a.obj --input .\b.obj --to glb --output .\converted --json
assetbridge-cli capabilities --json
```

`capabilities` separates three facts: what the current Assimp runtime exposes,
what AssetBridge enables as a product, and what AssetBridge has verified with
tests. Runtime availability alone is not a support claim.

Successful conversion creates a non-overwriting per-asset directory:

```text
<output-root>/
└── <source-stem>/
    ├── <source-stem>.glb
    └── conversion-report.json
```

If the final directory exists, AssetBridge selects `<source-stem>_2`, `_3`, and
so on. Export and validation happen in a temporary sibling directory. The final
directory appears only after every validation check passes.

Batch output adds one root-level report while preserving the same transaction
boundary for every asset:

```text
<output-root>/
├── batch-report.json
├── a/
│   ├── a.glb
│   └── conversion-report.json
└── b/
    ├── b.glb
    └── conversion-report.json
```

CLI exit codes are stable:

| Code | Meaning |
|---:|---|
| 0 | Command completed successfully. |
| 1 | File, import, preflight, export, or validation error. |
| 2 | Invalid command, arguments, or target format. |

For `batch`, exit code `0` means every job succeeded, `1` means partial or
complete job failure/cancellation, and `2` means invalid arguments or no valid
OBJ input.

## Build from source

Prerequisites:

- Windows 11 or a compatible Windows x64 development environment.
- Visual Studio 2022 Build Tools with MSVC v143 and a Windows SDK.
- CMake 3.25 or later and Ninja.
- vcpkg, with `VCPKG_ROOT` set to its installation directory.

Run these commands from an x64 Visual Studio Developer PowerShell:

```powershell
$env:VCPKG_ROOT = "<vcpkg-root>"

cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug

cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release
```

Create the portable Release package and SHA-256 file with:

```powershell
cmake --build --preset msvc-release --target package
```

Outputs are written below `out/build/msvc-release/packages/`. CMake project
version `0.3.0` is the authoritative version source; a configured header feeds
the CLI and desktop UI.

## Validation model

The conversion path inspects the original asset, runs product preflight, builds
an export-ready triangulated scene, exports GLB, reimports it, and compares the
result before committing output. Mesh count, triangle count, index validity,
whole-scene and per-mesh AABBs, normal/UV0 presence, and the verified material
data are checked. For textured assets, AssetBridge also validates GLB JSON/BIN
structure, image `bufferView` bounds, material/texture bindings, image
signatures, absence of external image URIs, and exact preservation of the
source compressed image bytes. Vertex count is diagnostic because legal
triangulation and format representation can change vertex splitting.

See [Architecture](docs/ARCHITECTURE.md) and
[Validation Pipeline](docs/VALIDATION_PIPELINE.md) for the engineering details.

## Lightweight metrics

These are measurements, not cross-machine guarantees. No system cache was
cleared and no Windows security policy was changed.

| Metric | v0.2.0 recorded | v0.3.0 recorded | Change from v0.2.0 |
|---|---:|---:|---:|
| Desktop EXE | 865,792 bytes | 944,128 bytes | +78,336 bytes |
| CLI EXE | 496,640 bytes | 561,664 bytes | +65,024 bytes |
| DLL count | 11 | 11 | 0 |
| Portable directory | 10,603,313 bytes | 10,746,673 bytes | +143,360 bytes |
| ZIP | 4,461,429 bytes | 4,528,346 bytes | +66,917 bytes |
| First measured launch to responsive window | 247.930 ms | 198.407 ms | -49.523 ms |
| Cached launch median, 5 runs | 170.737 ms | 168.460 ms | -2.277 ms |
| Idle Working Set median, 5 runs | 71,917,568 bytes | 72,151,040 bytes (68.81 MiB) | +233,472 bytes |
| Idle Private Memory median, 5 runs | 79,364,096 bytes | 82,112,512 bytes (78.31 MiB) | +2,748,416 bytes |

Ten copied instances of the self-authored minimal OBJ completed sequentially in
235.050 ms. The one-job GUI run peaked at 73,928,704 bytes Working Set; the
ten-job run peaked at 87,666,688 bytes and returned to 74,014,720 bytes after
completion. All ten GLBs and reports were produced, and no transaction directory
remained. These numbers demonstrate the measured candidate only; they are not a
throughput guarantee for larger assets.

Manual GUI acceptance used project-authored assets. A 100-OBJ queue completed
successfully; an invalid OBJ was isolated as `Failed` without stopping valid
jobs; and `Cancel After Current` completed the active transaction before
marking later queued jobs `Canceled`. No incorrect final directory or
transaction-temporary-directory residue was observed. These are manual
acceptance observations and are separate from the automated CTest evidence.

Measurement machine: AMD Ryzen 5 9600X, 12 logical processors, Windows build
10.0.26200.8875. Launch timing is measured from process creation until a
responsive main window exists. Idle memory is sampled 1.5 seconds after that
point with no asset loaded. The v0.3.0 measurement uses the batch feature tree
merged into main at `f8724bd2999316f45f81520b8f119dbd02d528bb` and the
v0.3.0 release-preparation build produced from that base.

The complete DLL and validation evidence is summarized in the
[portfolio case study](docs/PORTFOLIO_CASE_STUDY.md).

## Unsigned Windows build

The v0.3.0 portable build is not Authenticode-signed. Some Windows security
policies may block unsigned executables. AssetBridge does not recommend turning
off or bypassing Smart App Control, antivirus software, or organizational code
integrity policy. If execution is blocked, inspect the source and build it
locally, or wait for a future signed build.

The accompanying SHA-256 file verifies archive integrity only. It is not an
identity signature and does not replace code signing.

## Documentation

- [v0.3.0 Release Notes](docs/RELEASE_NOTES_v0.3.0.md)
- [v0.2.0 Release Notes](docs/RELEASE_NOTES_v0.2.0.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Validation Pipeline](docs/VALIDATION_PIPELINE.md)
- [Portfolio Case Study](docs/PORTFOLIO_CASE_STUDY.md)
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md)

Published historical releases remain available at
[v0.2.0](https://github.com/Fuyan-TA/AssetBridge/releases/tag/v0.2.0) and
[v0.1.0](https://github.com/Fuyan-TA/AssetBridge/releases/tag/v0.1.0). This
document does not add a v0.3.0 Release URL before that Release exists.

## License

AssetBridge is licensed under the [MIT License](LICENSE). Third-party libraries
and redistributed runtime dependencies are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), with license texts in
`third_party/licenses/`.
