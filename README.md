# AssetBridge

[中文说明 / Chinese documentation](README.zh-CN.md)

AssetBridge is a lightweight native Windows tool for inspecting 3D assets and
performing a narrow, validated OBJ-to-GLB conversion workflow. It is designed
around explicit format capabilities, preflight diagnostics, transactional
output, and round-trip verification.

AssetBridge v0.2.0 is not a general-purpose all-format converter and makes no
readiness claim beyond its verified boundary. The product exposes only the
OBJ-to-GLB route that has automated and local acceptance evidence.

![AssetBridge v0.1.0 desktop conversion](docs/images/assetbridge-v0.1.0.png)

Local file-system paths are redacted in documentation screenshots.

## v0.2.0 verified scope

| Area | Verified v0.2.0 boundary |
|---|---|
| Platform | Native Windows x64 desktop and CLI applications. |
| Route | OBJ/MTL input to binary GLB2 output only. |
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

The following remain outside the verified v0.2.0 boundary and are rejected when
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
3. Drop one OBJ file into the window or use **Choose OBJ**.
4. Review the asset summary and preflight result.
5. Choose an output directory and select **Convert to GLB**.
6. After successful reimport validation, open the output folder or JSON report.

AssetBridge accepts one OBJ at a time. Dropping multiple files or a non-OBJ file is
rejected before conversion.

### CLI

```powershell
assetbridge-cli --version
assetbridge-cli inspect .\model.obj
assetbridge-cli inspect .\model.obj --json
assetbridge-cli preflight .\model.obj --target glb --json
assetbridge-cli convert .\model.obj --to glb --output .\converted --json
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

CLI exit codes are stable:

| Code | Meaning |
|---:|---|
| 0 | Command completed successfully. |
| 1 | File, import, preflight, export, or validation error. |
| 2 | Invalid command, arguments, or target format. |

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
version `0.2.0` is the authoritative version source; a configured header feeds
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

| Metric | v0.1.0 recorded | v0.2.0 candidate | Change |
|---|---:|---:|---:|
| Desktop EXE | 745,984 bytes | 865,792 bytes | +119,808 bytes |
| CLI EXE | 376,320 bytes | 496,640 bytes | +120,320 bytes |
| DLL count | 11 | 11 | 0 |
| Portable directory | 10,363,188 bytes | 10,603,313 bytes | +240,125 bytes |
| ZIP | 4,347,759 bytes | 4,461,429 bytes | +113,670 bytes |
| First measured launch to responsive window | 215.983 ms | 247.930 ms | +31.947 ms |
| Cached launch median, 5 runs | 183.421 ms | 170.737 ms | -12.684 ms |
| Idle Working Set median, 5 runs | 71,897,088 bytes | 71,917,568 bytes (68.59 MiB) | +20,480 bytes |
| Idle Private Memory median, 5 runs | Not recorded | 79,364,096 bytes (75.69 MiB) | Not comparable |

Measurement machine: AMD Ryzen 5 9600X, 12 logical processors, Windows build
10.0.26200.8875. Launch timing is measured from process creation until a
responsive main window exists. Idle memory is sampled 1.5 seconds after that
point with no asset loaded. The v0.2.0 measurement state is the
`release/v0.2.0` preparation tree based on merge commit
`5e638a19638f8fdb5bfc9d1d0527d36e77ad410d`. The v0.1.0 ZIP size is the
published Release asset size; the other v0.1.0 values are retained release
evidence.

The complete DLL and validation evidence is summarized in the
[portfolio case study](docs/PORTFOLIO_CASE_STUDY.md).

## Unsigned Windows build

The v0.2.0 portable build is not Authenticode-signed. Some Windows security
policies may block unsigned executables. AssetBridge does not recommend turning
off or bypassing Smart App Control, antivirus software, or organizational code
integrity policy. If execution is blocked, inspect the source and build it
locally, or wait for a future signed build.

The accompanying SHA-256 file verifies archive integrity only. It is not an
identity signature and does not replace code signing.

## Documentation

- [v0.2.0 Release Notes](docs/RELEASE_NOTES_v0.2.0.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Validation Pipeline](docs/VALIDATION_PIPELINE.md)
- [Portfolio Case Study](docs/PORTFOLIO_CASE_STUDY.md)
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md)

The published [v0.1.0 release](https://github.com/Fuyan-TA/AssetBridge/releases/tag/v0.1.0)
is retained as the previous public version. This preparation does not add a
v0.2.0 Release URL before such a Release exists.

## License

AssetBridge is licensed under the [MIT License](LICENSE). Third-party libraries
and redistributed runtime dependencies are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), with license texts in
`third_party/licenses/`.
